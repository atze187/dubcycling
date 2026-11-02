#include "engine/RegionMap.h"
#include <algorithm>
#include <cmath>

namespace dc {

namespace {
constexpr double kEps = 1e-12; // tolerance for the minimum-distance comparison
}

int RegionMap::regionAt(double phase01) const
{
    if (!std::isfinite(phase01))
        return 0;
    double p = phase01 - std::floor(phase01);
    if (p >= 1.0) // tiny negative input can round to exactly 1.0
        p = 0.0;
    return static_cast<int>(std::upper_bound(boundaries.begin(), boundaries.end(), p) - boundaries.begin());
}

std::pair<double, double> RegionMap::bounds(int region) const
{
    if (region < 0 || region >= regionCount())
        return { 0.0, 0.0 };
    const double lo = region == 0 ? 0.0 : boundaries[static_cast<size_t>(region) - 1];
    const double hi = region == regionCount() - 1 ? 1.0 : boundaries[static_cast<size_t>(region)];
    return { lo, hi };
}

bool RegionMap::addBoundary(double x)
{
    if (std::isnan(x) || x - 0.0 < kMinRegion - kEps || 1.0 - x < kMinRegion - kEps)
        return false;
    auto it = std::lower_bound(boundaries.begin(), boundaries.end(), x);
    if (it != boundaries.end() && *it - x < kMinRegion - kEps)
        return false;
    if (it != boundaries.begin() && x - *(it - 1) < kMinRegion - kEps)
        return false;
    boundaries.insert(it, x);
    return true;
}

bool RegionMap::moveBoundary(int index, double x)
{
    if (index < 0 || index >= static_cast<int>(boundaries.size()) || std::isnan(x))
        return false;
    const size_t i = static_cast<size_t>(index);
    const double lo = (i > 0 ? boundaries[i - 1] : 0.0) + kMinRegion;
    const double hi = (i + 1 < boundaries.size() ? boundaries[i + 1] : 1.0) - kMinRegion;
    if (lo > hi)
        return false;
    boundaries[i] = std::clamp(x, lo, hi);
    return true;
}

bool RegionMap::removeBoundary(int index)
{
    if (index < 0 || index >= static_cast<int>(boundaries.size()))
        return false;
    boundaries.erase(boundaries.begin() + index);
    return true;
}

} // namespace dc
