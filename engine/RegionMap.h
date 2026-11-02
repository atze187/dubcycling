#pragma once
#include <utility>
#include <vector>

namespace dc {

constexpr double kMinRegion = 0.005; // minimum region width as a fraction of the loop

// Splits the loop (phase 0..1) into regions by ascending boundaries in (0,1).
class RegionMap
{
public:
    std::vector<double> boundaries;

    int regionCount() const { return static_cast<int>(boundaries.size()) + 1; }

    // phase01 is wrapped into [0,1); NaN/inf gives region 0. A boundary belongs to the region after it.
    int regionAt(double phase01) const;

    // {lo, hi} of a region (first lo = 0.0, last hi = 1.0); invalid index gives {0, 0}.
    std::pair<double, double> bounds(int region) const;

    // False (and unchanged) for NaN, outside (0,1), or closer than kMinRegion to an edge or neighbour.
    bool addBoundary(double x);

    // Clamps x between the neighbours (kMinRegion apart); false for a bad index or NaN.
    bool moveBoundary(int index, double x);

    // False for a bad index.
    bool removeBoundary(int index);
};

} // namespace dc
