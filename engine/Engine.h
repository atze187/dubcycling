#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include "engine/GrainStretch.h"
#include "engine/LoopReader.h"
#include "engine/RegionMap.h"
#include "engine/RuleSet.h"
#include "engine/Sequencer.h"
#include "engine/StereoData.h"

namespace dc {

// Host transport for one block. ppq is the position at the first frame of the block.
struct TransportInfo
{
    bool playing = false;
    double ppq = 0.0;
    double bpm = 0.0;
    int tsNum = 4;
    int tsDen = 4;
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

    // Frees models the audio thread no longer uses. Called by setModel(); may also be called
    // periodically from a non-audio thread to release memory sooner.
    void releaseRetired();

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
        GrainStretch gl, gr;
    };

    // keepLoop1: Loop 1 is unchanged by a swap, its reader keeps running.
    void resetPlayback(bool keepLoop1 = false);
    // Seamless swap to m (same data and bars), on the first valid block after the swap.
    void adoptSwap(const Model& m, double beats1);
    void startVoice(const Model& m, int region2, PlayMode mode);
    void requestStart(const Model& m, int region2, PlayMode mode);
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

    // Audio-thread state.
    Runtime* current_ = nullptr;
    Sequencer sequencer_;
    Voice voice_;
    Voice tail_;
    double outRate_ = 44100.0;
    int grainLen_ = 1764;
    int fadeLen_ = 220;
    bool running_ = false; // false: next valid frame is a fresh start (entry draw)
    double expectedPpq_ = 0.0;
    double lastPpq_ = 0.0;
    int lastRegion1_ = -1;
    double lastLo1_ = 0.0, lastHi1_ = 0.0; // phase bounds of lastRegion1_ (= Gate trigger while a Gate runs)
    long long lastCycle1_ = 0;
    bool swapPending_ = false; // a seamless swap waits for adoptSwap (needs the block's time signature)
};

} // namespace dc
