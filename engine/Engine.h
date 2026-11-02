#pragma once
#include <atomic>
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
// (JUCE guarantees this for prepareToPlay). A model change is a hard restart of both loops
// (the next frame is a fresh entry draw).
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
    struct Runtime
    {
        std::shared_ptr<const Model> model;
        LoopReader reader1;
    };

    // The single Loop-2 voice: plays [lo, hi) of loop 2 with its own grain stretch, cycling inside
    // the region (Gate repeats seamlessly) and a linear gain ramp of kCancelFadeMs for start/stop.
    struct Voice
    {
        enum class State { Idle, Playing, FadingOut } state = State::Idle;
        PlayMode mode = PlayMode::Gate;
        double lo = 0.0, hi = 0.0; // source frames
        double elapsed = 0.0;      // source frames since region start (Gate: wrapped)
        float gain = 0.0f;
        int fadeLeft = 0;        // frames left in the current fade (in or out); integer so fades end exactly
        float fadeDelta = 0.0f;  // fade-out gain decrement per frame
        bool endedByItself = false; // one-shot end fade (report onVoiceEnded when it completes)
        bool hasPending = false;    // start queued until the fade-out completes
        int pendingRegion = 0;
        PlayMode pendingMode = PlayMode::Gate;
        GrainStretch gl, gr;
    };

    void resetPlayback();
    void startVoice(const Model& m, int region2, PlayMode mode);
    void requestStart(const Model& m, int region2, PlayMode mode);
    void requestStop();
    void beginFadeOut();
    void finishFade(const Model& m);

    void releaseRetiredLocked();

    // Message-side state (guarded by mutex_, never touched by process()). owned_ holds every
    // Runtime that may still be published or in use; the audio thread never frees anything.
    std::mutex mutex_;
    double sampleRate_ = 44100.0;
    int maxBlock_ = 0;
    std::vector<std::unique_ptr<Runtime>> owned_;

    // Handoff (plain atomic pointers, lock-free): next_ is the latest published Runtime; inUse_ is
    // the one the audio thread is using (hazard pointer, set and re-validated at block start).
    // A Runtime is freed only when it is neither next_ nor inUse_.
    std::atomic<Runtime*> next_ { nullptr };
    std::atomic<Runtime*> inUse_ { nullptr };

    // Audio-thread state.
    Runtime* current_ = nullptr;
    Sequencer sequencer_;
    Voice voice_;
    double outRate_ = 44100.0;
    int grainLen_ = 1764;
    int fadeLen_ = 220;
    bool running_ = false; // false: next valid frame is a fresh start (entry draw)
    double expectedPpq_ = 0.0;
    double lastPpq_ = 0.0;
    int lastRegion1_ = -1;
    long long lastCycle1_ = 0;
};

} // namespace dc
