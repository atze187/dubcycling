#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace dc {

// Catmull-Rom-Interpolation; Werte außerhalb des Puffers gelten als 0.
inline float cubicAt(const std::vector<float>& x, double pos)
{
    const auto n = static_cast<std::int64_t>(x.size());
    const double fl = std::floor(pos); // auch für negative Positionen (Rückwärtslauf) abrunden
    const auto i = static_cast<std::int64_t>(fl);
    const float t = static_cast<float>(pos - fl);
    const auto at = [&](std::int64_t k) { return (k < 0 || k >= n) ? 0.0f : x[static_cast<std::size_t>(k)]; };
    const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
}

} // namespace dc
