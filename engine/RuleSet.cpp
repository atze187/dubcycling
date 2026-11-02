#include "engine/RuleSet.h"
#include <algorithm>
#include <cmath>

namespace dc {

namespace {
constexpr int kMaxRegions = 4096;
const std::vector<RuleTarget> kNone;
}

void RuleSet::setTargets(int region1, std::vector<RuleTarget> targets)
{
    if (region1 < 0 || region1 >= kMaxRegions)
        return;

    double sum = 0.0;
    for (auto& t : targets)
    {
        if (!(t.weight > 0.0f)) // also catches NaN
            t.weight = 0.0f;
        t.weight = std::min(t.weight, 1.0f);
        sum += t.weight;
    }
    if (sum > 1.0)
        for (auto& t : targets)
            t.weight = static_cast<float>(t.weight / sum);

    const size_t i = static_cast<size_t>(region1);
    if (byRegion.size() <= i)
        byRegion.resize(i + 1);
    byRegion[i] = std::move(targets);
}

const std::vector<RuleTarget>& RuleSet::targets(int region1) const
{
    if (region1 < 0 || static_cast<size_t>(region1) >= byRegion.size())
        return kNone;
    return byRegion[static_cast<size_t>(region1)];
}

std::optional<RuleTarget> RuleSet::draw(int region1, float u) const
{
    if (!(u >= 0.0f && u < 1.0f)) // also catches NaN
        return std::nullopt;
    double cum = 0.0;
    for (const auto& t : targets(region1))
    {
        cum += t.weight;
        if (static_cast<double>(u) < cum)
            return t;
    }
    return std::nullopt;
}

} // namespace dc
