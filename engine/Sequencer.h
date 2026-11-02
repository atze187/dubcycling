#pragma once
#include <functional>
#include <random>
#include "engine/RuleSet.h"

namespace dc {

// Fade length (ms) the Engine uses when a new entry cancels a running Loop-2 voice.
constexpr int kCancelFadeMs = 5;

// State machine for the single Loop-2 voice. Holds state only; the Engine renders audio.
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

    // Replaces the random source (returns uniform values in [0,1)). Default: std::mt19937.
    void setRandom(std::function<float()> uniform01);

    // Call on every Loop-1 region entry (also wrap / transport jump); draws anew.
    // Start = (re)start target, cancelling any running voice. Stop = silence draw (a rule exists,
    // the draw lands in the remainder; a no-op for the Engine if the voice is idle).
    // None = the region has no rule: not a draw, state unchanged (check shouldGateStop).
    Command onRegion1(int region1, const RuleSet& rules);

    // Gate mode only: true when the voice runs in Gate mode and Loop 1 left the triggering region.
    bool shouldGateStop(int region1Now) const;

    // The Engine reports that the voice ended by itself (one-shot finished) or was stopped.
    void onVoiceEnded();
    void reset(); // transport stop
    bool active() const { return isActive; }

private:
    std::function<float()> random;
    std::mt19937 rng;
    bool isActive = false;
    int activeRegion2 = -1;
    PlayMode activeMode = PlayMode::Gate;
    int triggerRegion1 = -1;
};

} // namespace dc
