#pragma once
#include <algorithm>
#include <cmath>

namespace dc {

struct MixGains
{
    float loop1 = 1.0f;
    float loop2 = 1.0f;
};

// NaN and infinity give the centre (0.5), everything else is clamped to 0..1.
inline float sanitizeMix(float mix)
{
    if (!std::isfinite(mix))
        return 0.5f;
    return std::clamp(mix, 0.0f, 1.0f);
}

// 0.5 keeps both loops at full level; left of centre only loop 2 gets quieter, right of centre
// only loop 1.
inline MixGains mixGains(float mix)
{
    const float m = sanitizeMix(mix);
    MixGains g;
    g.loop2 = std::min(1.0f, 2.0f * m);
    g.loop1 = std::min(1.0f, 2.0f * (1.0f - m));
    return g;
}

} // namespace dc
