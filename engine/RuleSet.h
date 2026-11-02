#pragma once
#include <optional>
#include <vector>

namespace dc {

enum class PlayMode { Gate, OneShot };

struct RuleTarget
{
    int region2 = 0;
    float weight = 0.0f; // 0..1
    PlayMode mode = PlayMode::Gate;
};

// Per source region (region1) a list of weighted targets. Copyable; draw/targets do not allocate.
class RuleSet
{
public:
    // Sanitizes weights (NaN/negative -> 0, clamp to 1, scale proportionally if the sum exceeds 1).
    // A negative or absurdly large region1 is ignored.
    void setTargets(int region1, std::vector<RuleTarget> targets);

    // Empty for unknown regions.
    const std::vector<RuleTarget>& targets(int region1) const;

    // u in [0,1): walks the cumulative weights; beyond the sum (silence), or u invalid, gives nullopt.
    // If the weights sum to at least 1 - 1e-6 there is no silence: the last non-zero target takes
    // the rounding remainder.
    std::optional<RuleTarget> draw(int region1, float u) const;

private:
    std::vector<std::vector<RuleTarget>> byRegion;
};

} // namespace dc
