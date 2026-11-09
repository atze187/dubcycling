#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include "engine/FxSettings.h"
#include "engine/GrainStretch.h"
#include "engine/LoopReader.h"
#include "engine/Pass.h"
#include "engine/RegionMap.h"
#include "engine/RuleSet.h"
#include "engine/Sequencer.h"
#include "engine/StereoData.h"
#include "engine/fx/FxChain.h"

namespace dc {

// Host transport for one block. ppq is the position at the first frame of the block.
struct TransportInfo
{
    bool playing = false;
    double ppq = 0.0;
    double bpm = 0.0;
    int tsNum = 4;
    int tsDen = 4;
    // MIDI trigger mode only (see MidiTrigger): the stretch right after a gate-off. Loop 1 and the
    // Loop-2 voice fade out over kCancelFadeMs, no new region entries are drawn; effect tails ring
    // out. Hosts never set it, so Autoplay is unaffected.
    bool release = false;
};

struct LoopSlot
{
    std::shared_ptr<const StereoData> data; // nullptr = silent loop (its timing still runs)
    int bars = 1;
    RegionMap regions;
};

// Immutable snapshot of everything the audio thread plays.
struct Model
{
    LoopSlot loop1, loop2;
    RuleSet rules;
    FxSettings fx; // insert parameters and switches per Loop-2 region
    std::array<Pass, kPassSlots> passes; // saved passes (empty = free slot), events sorted by phase (sanitizePass)
};

// Audio core: Loop 1 phase-locked to the host, Loop 2 as one region voice driven by the Sequencer.
//
// Threading: process() runs on the audio thread and never locks or allocates. setModel() may be
// called from any non-audio thread; prepare() and setRandom() only while process() is not running
// (JUCE guarantees this for prepareToPlay).
//
// Model swaps: if both loops keep their data pointer and bars, a new model is adopted seamlessly
// (edits while playing): Loop 1 and the running Loop-2 voice continue sample-continuously, the
// swap is not a region entry, new rules apply from the next entry. A Gate voice keeps running if
// the Loop-1 region at the play position still has its trigger region's start or end edge;
// a voice whose Loop-2 region index no longer exists fades out like a Stop. "Same data" means the
// same data id (see Runtime): a buffer re-published after other data was published in between counts
// as new. Any other model change
// is a hard restart of Loop 2 and the Sequencer (the next frame is a fresh entry draw); Loop 1
// restarts too unless its data pointer and bars are unchanged.
//
// Effects: Loop 2 (voice, cancelled voice's tail, Loop-2 audition) is rendered into a bus, sent
// through the insert chain and added to the output; Loop 1 and its audition stay dry. The switch
// mask follows what started last: a Loop-2 voice (its region's switches), a Loop-2 audition (the
// auditioned region's switches, for as long as it plays) or a stop (0). A seamless swap re-reads
// the switches of what sounds. Transport stop, jumps and invalid transport let the tails ring out;
// a hard restart with other loop data or bars fades the chain out (flush).
//
// Mix: a Loop-2 voice plays at mixGains(mix).loop2 of the target that started it (before the bus,
// so before the inserts; a cancelled voice's tail keeps its level), and Loop 1 (playback only, not
// its audition) is scaled by mixGains(mix).loop1 of voice_ while it plays, else 1. Both levels ramp
// over kMixRampMs. Auditions ignore the mix. A seamless swap re-reads the mix of the sounding voice
// if its Loop-2 region and the Loop-1 region that started it kept their bounds.
//
// Passes: the Loop-2 commands of one Loop-1 cycle (entry starts and stops, Gate ends, the jump stop)
// are recorded at their Loop-1 phase. A pass begins at a cycle start reached continuously, or by a
// fresh start or jump that lands within kJumpToleranceBeats of it, and is published (lastPass) at
// the next continuous cycle start, or at a jump from a cycle end to a cycle start (a host loop split
// at its end). A stop, invalid transport, any other jump, a MIDI release or a hard restart abandons
// it; a seamless swap does not.
//
// Replay (setReplay): at a pass boundary (a cycle start reached continuously, or a fresh start or
// jump landing on it) the request is read. While a slot replays nothing is drawn (no early draw, no
// teaser), nothing is recorded, and the slot's events are issued as voice commands when Loop 1
// reaches their phase; each replayed pass starts with Loop 2 silent (a carried-over voice stops). A
// Start takes the mix of the current rule target (Loop-1 region at its phase -> its Loop-2 region),
// else 0.5; a Start of a Loop-2 region that no longer exists is skipped. The Sequencer is reset on
// entering and leaving replay. A jump, stop or fresh start mid-loop stops the voice and holds the
// events back until the next boundary; a MIDI release drops the events it passes over. A seamless
// swap (e.g. a slot cleared or saved while it replays) goes on with the new slot's first event after
// the current Loop-1 phase.
class Engine
{
public:
    Engine() = default;
    ~Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void prepare(double sampleRate, int maxBlock);
    // Thread-safe publication (lock-free for the audio thread). nullptr = silence. Allocates.
    void setModel(std::shared_ptr<const Model> model);
    void process(const TransportInfo& t, float* l, float* r, int n);

    // Tests: deterministic draws. Not while process() runs.
    void setRandom(std::function<float()> uniform01) { sequencer_.setRandom(std::move(uniform01)); }

    // Audition: plays region `region` of loop `slot` (0 = Loop 1, 1 = Loop 2) once, mixed on top of
    // whatever else plays and independent of the transport (also while the host is stopped). Tempo
    // is the host's last reported one (120 bpm and 4/4 before the first block). A new request cancels
    // a running one with the usual short crossfade. Any thread, lock-free; the request is taken at the
    // next block (several requests within one block: the last wins). Ignored for a loop without data or
    // a region that does not exist. A model swap that changes that loop's data ends the audition.
    void requestPreview(int slot, int region)
    {
        if (slot >= 0 && slot <= 1 && region >= 0 && region < 0x10000)
            previewReq_.store((slot << 16) | region, std::memory_order_relaxed);
    }

    // Where the loops were at the end of the last processed block, for the UI. Any thread; the three
    // fields are read separately (relaxed), so a read during a block may mix two blocks, which is
    // fine for a display. phase1/phase2 are 0..1 over the whole loop (Loop 2: its source position),
    // -1 when there is nothing to show: transport stopped or invalid for phase1, no running Loop-2
    // voice for phase2/region2.
    //
    // Teaser: with a lookahead set (below), the draw for the next Loop-1 region entry is made that
    // long before the entry and shown here: `teaser` is the Loop-2 region that will start (>= 0),
    // kTeaserSilence for a silence draw, kNoTeaser when nothing is pending; `teaserEta` the seconds
    // from the end of the last block to that entry, `teaserWindow` the lookahead it counts down from.
    static constexpr int kNoTeaser = -1;
    static constexpr int kTeaserSilence = -2;
    struct Playhead
    {
        float phase1 = -1.0f;
        float phase2 = -1.0f;
        int region2 = -1;
        int teaser = kNoTeaser;
        float teaserEta = 0.0f;
        float teaserWindow = 0.0f;
    };
    Playhead playhead() const
    {
        return { head1_.load(std::memory_order_relaxed),    head2_.load(std::memory_order_relaxed),
                 headRegion2_.load(std::memory_order_relaxed), headTeaser_.load(std::memory_order_relaxed),
                 headEta_.load(std::memory_order_relaxed),     static_cast<float>(teaserSec_.load(std::memory_order_relaxed)) };
    }

    // Seconds before a Loop-1 region entry at which its draw is made (and shown as teaser); 0 (the
    // default) draws at the entry itself. Any thread. The decision is the same either way: the
    // result of the early draw is what the entry applies. Edits to the model, transport jumps and
    // a stop discard an early draw (the entry then draws again).
    void setTeaserLookahead(double seconds)
    {
        teaserSec_.store(std::isfinite(seconds) && seconds > 0.0 ? std::min(seconds, 2.0) : 0.0,
                         std::memory_order_relaxed);
    }

    // Frees models the audio thread no longer uses. Called by setModel(); may also be called
    // periodically from a non-audio thread to release memory sooner.
    void releaseRetired();

    // True while an insert of the chain is switched on or still rings out (as of the end of the last
    // processed block). Any thread.
    bool fxActive() const { return fxActive_.load(std::memory_order_relaxed); }

    // The last finished pass: the Loop-2 commands (starts and stops at their Loop-1 phase) of one
    // complete, uninterrupted Loop-1 cycle. id counts finished passes (1, 2, ...). False if none has
    // finished yet. Any thread, but not the audio thread (out.events is assigned: allocates).
    bool lastPass(Pass& out, std::uint32_t& id) const { return recorder_.read(out, id); }

    // Replay of saved passes (Model::passes). request: 0 off, 1 chain, 2 + i slot i (0..7); any
    // other value means off. Any thread (relaxed); taken at the next pass boundary.
    void setReplay(int request)
    {
        replayReq_.store(request >= 0 && request < 2 + kPassSlots ? request : 0, std::memory_order_relaxed);
    }
    int replayRequest() const { return replayReq_.load(std::memory_order_relaxed); }
    // The slot replayed at the end of the last processed block (also while it waits for the next
    // boundary after a jump), -1 while random plays. Any thread.
    int replaySlot() const { return replaySlotOut_.load(std::memory_order_relaxed); }

private:
    // A published model plus the Loop-1 reader bound to its data. Built off the audio thread
    // (LoopReader::setData is not called on the audio thread); the audio thread only swaps pointers.
    // reader1 is never null. setModel shares the reader of the latest published Runtime when the
    // Loop-1 data pointer is the same, so the audio thread keeps rendering the same reader object
    // (grain state intact) across a swap. The shared_ptr's count only changes on the message thread
    // (Runtimes are created and freed there); the audio thread uses the raw pointer.
    // data1Id/data2Id identify the loop data buffers: setModel keeps the previous Runtime's id when
    // the data pointer is the same as in the latest published Runtime (which is alive, so that
    // comparison is safe), else it assigns a new one. The audio thread compares ids, never pointers
    // of data that may have been freed meanwhile (an address can be reused: ABA).
    struct Runtime
    {
        std::shared_ptr<const Model> model;
        std::shared_ptr<LoopReader> reader1;
        std::uint64_t data1Id = 0, data2Id = 0;
    };

    // What decides whether a swap can be seamless.
    struct SwapKey
    {
        bool valid = false; // a model is present
        std::uint64_t data1Id = 0, data2Id = 0;
        int bars1 = 0, bars2 = 0;
    };
    static SwapKey keyOf(const Runtime* rt);

    // A Loop-2 region player: plays [lo, hi) of loop 2 with its own grain stretch (Gate grains cycle
    // inside the region so it repeats seamlessly; One-Shot grains read the whole loop so an overshoot
    // past hi plays the continuation) and a linear gain ramp of kCancelFadeMs for start/stop.
    // voice_ is the one logical Loop-2 voice; tail_ only carries the fade-out of the voice a new
    // draw cancelled, so the new region starts on the exact entry frame (crossfade, no allocation).
    struct Voice
    {
        enum class State { Idle, Playing, FadingOut } state = State::Idle;
        PlayMode mode = PlayMode::Gate;
        int region2 = -1;          // Loop-2 region index it was started with
        double lo = 0.0, hi = 0.0; // source frames
        double elapsed = 0.0;      // source frames since region start (Gate: wrapped)
        float gain = 0.0f;
        int fadeLeft = 0;        // frames left in the current fade (in or out); integer so fades end exactly
        float fadeDelta = 0.0f;  // fade-out gain decrement per frame
        bool endedByItself = false; // one-shot end fade (report onVoiceEnded when it completes)
        // Mix of the target that started it (audition: 0.5). level2 is the Loop-2 level it renders
        // with, ramped towards mixGains(mix).loop2 (exactly 1 at the centre). mixRegion1 and its
        // 0..1 bounds name the Loop-1 region that started it (for the re-read on a seamless swap).
        float mix = 0.5f;
        float level2 = 1.0f;
        int mixRegion1 = -1;
        double mixLo1 = 0.0, mixHi1 = 0.0;
        GrainStretch gl, gr;
    };

    // Linear per-sample ramp of a gain towards a target, kMixRampMs for a full 0..1 swing. It lands
    // exactly on the target, so a centre mix stays at exactly 1 (bit-identical output).
    static constexpr double kMixRampMs = 5.0;
    static float rampTowards(float value, float target, float step)
    {
        const float d = target - value;
        return d > step ? value + step : (d < -step ? value - step : target);
    }

    // keepLoop1: Loop 1 is unchanged by a swap, its reader keeps running.
    void resetPlayback(bool keepLoop1 = false);
    // Seamless swap to m (same data and bars), on the first valid block after the swap.
    void adoptSwap(const Model& m, double beats1);
    void startVoice(const Model& m, const Sequencer::Command& c);
    void requestStart(const Model& m, const Sequencer::Command& c);
    // True if region2 of m still names the stretch of Loop 2 that v plays (same bounds).
    static bool sameRegion2(const Model& m, const Voice& v);
    // After a seamless model change: the mix of the sounding target, read from the new model.
    void refreshMix(const Model& m);
    void requestStop();
    void beginFadeOut(Voice& v) const;
    // Renders one frame of v (added to outL/outR) and advances it. True if it ended by itself.
    bool stepVoice(Voice& v, const StereoData& d, GrainFrame gf, double adv, float& outL, float& outR) const;

    void releaseRetiredLocked();

    // Message-side state (guarded by mutex_, never touched by process()). owned_ holds every
    // Runtime that may still be published or in use; the audio thread never frees anything.
    std::mutex mutex_;
    double sampleRate_ = 44100.0;
    int maxBlock_ = 0;
    std::vector<std::unique_ptr<Runtime>> owned_;
    std::uint64_t nextDataId_ = 1;

    // Handoff (plain atomic pointers, lock-free): next_ is the latest published Runtime; inUse_ is
    // the one the audio thread is using (hazard pointer, set and re-validated at block start).
    // A Runtime is freed only when it is neither next_ nor inUse_.
    std::atomic<Runtime*> next_ { nullptr };
    std::atomic<Runtime*> inUse_ { nullptr };

    // Audition voices: a One-Shot Voice per request plus the tail of the one it cancelled. slot and
    // dataId say which loop buffer the voice reads; a swap to other data idles it.
    struct PreviewVoice
    {
        Voice voice;
        int slot = -1;
        std::uint64_t dataId = 0;
    };
    void startPreview(int slot, int region);
    void resetPreview();
    void dropStalePreviews();
    // Loop-1 audition into l/r, Loop-2 audition into busL/busR.
    void mixPreview(float* l, float* r, float* busL, float* busR, int n);
    void mixPreviewVoice(PreviewVoice& pv, const Model& m, float* l, float* r, int n);

    std::atomic<int> previewReq_ { -1 }; // (slot << 16) | region, or -1
    PreviewVoice preview_, previewTail_;
    double lastBpm_ = 120.0; // last tempo / bar length the host reported (any transport state)
    double lastBeatsPerBar_ = 4.0;

    // Written by process(), read by playhead().
    std::atomic<float> head1_ { -1.0f };
    std::atomic<float> head2_ { -1.0f };
    std::atomic<int> headRegion2_ { -1 };
    std::atomic<int> headTeaser_ { kNoTeaser };
    std::atomic<float> headEta_ { 0.0f };
    std::atomic<double> teaserSec_ { 0.0 };
    void publishPlayhead(float phase1, float phase2, int region2, int teaser = kNoTeaser, float eta = 0.0f)
    {
        head1_.store(phase1, std::memory_order_relaxed);
        head2_.store(phase2, std::memory_order_relaxed);
        headRegion2_.store(region2, std::memory_order_relaxed);
        headTeaser_.store(teaser, std::memory_order_relaxed);
        headEta_.store(eta, std::memory_order_relaxed);
    }

    // The early draw for the next Loop-1 entry: valid until that entry applies it or something
    // (a model swap, a jump, a stop) makes it stale. rt/cycle/region identify the entry.
    struct PendingDraw
    {
        bool valid = false;
        const Runtime* rt = nullptr;
        long long cycle = 0;
        int region = -1;
        double entryPpq = 0.0;
        Sequencer::Draw draw;
    };
    PendingDraw pending_;
    // drawWrap false: no early draw for an entry at the loop wrap (replay will take over there).
    void planEarlyDraw(const Model& m, double ppq, double bpm, double beats1, bool drawWrap);
    long long frameCounter_ = 0;      // frames processed since prepare (valid transport or not)
    long long lastEntryFrame_ = -1;   // frame of the latest Loop-1 entry handled, -1 = none

    // Audio-thread state.
    Runtime* current_ = nullptr;
    Sequencer sequencer_;
    Voice voice_;
    Voice tail_;
    double outRate_ = 44100.0;
    int grainLen_ = 1764;
    int fadeLen_ = 220;
    float mixStep_ = static_cast<float>(1.0 / (kMixRampMs * 0.001 * 44100.0)); // gain change per frame
    float gain1_ = 1.0f; // Loop-1 level from the mix of the playing voice (ramped)
    // Fade-out after a MIDI gate-off (TransportInfo::release): Loop 1 goes to 0 over fadeLen_ frames
    // (counted, so it ends exactly), the voice takes its cancel fade, no entries are drawn. Always 1
    // outside a release, so Autoplay output is unchanged.
    bool releasing_ = false;
    int releaseLeft_ = 0;
    float releaseGain_ = 1.0f;
    bool running_ = false; // false: next valid frame is a fresh start (entry draw)
    double expectedPpq_ = 0.0;
    double lastPpq_ = 0.0;
    int lastRegion1_ = -1;
    double lastLo1_ = 0.0, lastHi1_ = 0.0; // phase bounds of lastRegion1_ (= Gate trigger while a Gate runs)
    long long lastCycle1_ = 0;
    bool swapPending_ = false; // a seamless swap waits for adoptSwap (needs the block's time signature)
    PassRecorder recorder_;    // the pass being played (written here only, read by lastPass)

    // Replay (audio thread unless noted). replaySlot_ is the slot replayed (-1: random plays);
    // replayNext_ the index of its next event; replayWaiting_ holds the events back until the next
    // pass boundary (after a jump, a stop or a fresh start mid-loop).
    std::atomic<int> replayReq_ { 0 };      // any thread
    std::atomic<int> replaySlotOut_ { -1 }; // written by process(), read by replaySlot()
    int replaySlot_ = -1;
    int replayNext_ = 0;
    bool replayWaiting_ = false;
    // The slot the request asks for at a boundary now: the requested slot if filled; for the chain
    // the next filled slot after replaySlot_ (wrapping); else -1 (random).
    int pickReplaySlot(const Model& m) const;
    // At a pass boundary (phase ~0): enters, switches or leaves replay as requested.
    void replayBoundary(const Model& m);
    // Issues one recorded event as a voice command.
    void fireReplay(const Model& m, const PassEvent& ev);

    // Effects (audio thread). One slice is at most busL_.size() frames (process() splits larger
    // blocks); without prepare() the bus is empty and Loop 2 goes straight to the output, dry.
    void processSlice(const TransportInfo& t, float* l, float* r, int n);
    // Runs the chain over bus [0, n) in segments between the recorded mask changes (skipped while
    // nothing is switched on or rings out) and adds the bus to l/r.
    void runChain(float* l, float* r, int n);
    // End of a slice: chain (with a bus), fxActive, non-finite guard on the sum.
    void finishSlice(float* l, float* r, int n, bool useBus);
    // Sets the target switch mask from frame busFrame_ of the current slice on.
    void setTargetMask(std::uint8_t mask);
    // After a seamless model change: the switches of what sounds, read from the new model.
    void refreshMask(const Model& m);
    // The audition owned the switches and no Loop-2 audition plays any more: back to the voice.
    void releasePreviewMask();

    enum class MaskOwner { None, Voice, Preview };
    struct MaskChange
    {
        int frame = 0;
        std::uint8_t mask = 0;
    };
    static constexpr int kMaxMaskChanges = 32; // per slice; on overflow the last change keeps the latest mask
    fx::FxChain chain_;
    std::vector<float> busL_, busR_;
    std::uint8_t mask_ = 0;      // current target mask
    std::uint8_t sliceMask_ = 0; // target mask at the start of the current slice
    MaskOwner maskOwner_ = MaskOwner::None;
    std::array<MaskChange, kMaxMaskChanges> maskChanges_ {};
    int maskChangeCount_ = 0;
    int busFrame_ = 0; // frame of the current slice that mask changes are recorded at
    std::atomic<bool> fxActive_ { false };
};

} // namespace dc
