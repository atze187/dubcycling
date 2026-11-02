#pragma once
#include <functional>
#include <random>
#include "engine/RuleSet.h"

namespace dc {

// Fade length (ms) the Engine uses when a new entry cancels a running Loop-2 voice.
constexpr int kCancelFadeMs = 5;

// State machine for the single Loop-2 voice. Holds state only; the Engine renders audio.
// Audio thread only (onRegion1/shouldGateStop/onVoiceEnded/reset/retargetGate do not allocate or
// lock); setRandom only while the audio thread is not using it. Not copyable or movable.
class Sequencer
{
public:
    struct Command
    {
        enum class Type { None, Stop, Start } type = Type::None;
        int region2 = -1;
        PlayMode mode = PlayMode::Gate;
    };

    Sequencer();
    Sequencer(const Sequencer&) = delete;
    Sequencer& operator=(const Sequencer&) = delete;
    Sequencer(Sequencer&&) = delete;
    Sequencer& operator=(Sequencer&&) = delete;

    // Replaces the random source (returns uniform values in [0,1)). Default (or an empty function):
    // the member std::mt19937. A value of 1 or more is clamped just below 1 before drawing.
    void setRandom(std::function<float()> uniform01);

    // Call on every Loop-1 region entry (also wrap / transport jump); draws anew.
    // Start = (re)start target, cancelling any running voice. Stop = silence draw (a rule exists,
    // the draw lands in the remainder; a no-op for the Engine if the voice is idle).
    // None = the region has no rule: not a draw, state unchanged (check shouldGateStop).
    Command onRegion1(int region1, const RuleSet& rules);

    // Gate mode only: true when the voice runs in Gate mode and Loop 1 left the triggering region.
    bool shouldGateStop(int region1Now) const;

    // The Engine reports that the voice ended by itself (one-shot finished) or was stopped. After a
    // Gate stop (shouldGateStop true, or a Stop the Engine issued itself) the Engine must call
    // onVoiceEnded() or reset(), otherwise shouldGateStop stays true.
    void onVoiceEnded();
    void reset(); // transport stop
    bool active() const { return isActive; }

    // Model swap (boundary edit): the Loop-1 region that triggered the running Gate voice, or -1.
    int gateTrigger() const { return isActive && activeMode == PlayMode::Gate ? triggerRegion1 : -1; }
    // Model swap: re-maps the Gate trigger to region1 (its new index). -1 makes shouldGateStop true
    // on the next check (the Gate ends through the normal path). No-op unless a Gate voice runs.
    void retargetGate(int region1)
    {
        if (isActive && activeMode == PlayMode::Gate)
            triggerRegion1 = region1;
    }

private:
    float nextUniform(); // override or member RNG, clamped below 1

    std::function<float()> random; // empty = member RNG
    std::mt19937 rng;
    std::uniform_real_distribution<float> dist { 0.0f, 1.0f };
    bool isActive = false;
    int activeRegion2 = -1;
    PlayMode activeMode = PlayMode::Gate;
    int triggerRegion1 = -1;
};

} // namespace dc
