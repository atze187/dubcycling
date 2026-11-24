#include "engine/fx/Lfo.h"
#include <algorithm>
#include <cmath>

namespace dc::fx {

float lfoValue(LfoShape shape, double phase)
{
    double p = std::isfinite(phase) ? phase - std::floor(phase) : 0.0;
    if (!(p >= 0.0 && p < 1.0)) p = 0.0;
    switch (shape)
    {
        case LfoShape::Triangle: return static_cast<float>(1.0 - 4.0 * std::min(p, 1.0 - p));
        case LfoShape::SawDown: return static_cast<float>(1.0 - 2.0 * p);
        case LfoShape::Square: return p < 0.5 ? 1.0f : -1.0f;
        case LfoShape::Sine:
        default: return static_cast<float>(std::cos(2.0 * 3.14159265358979323846 * p));
    }
}

double lfoPhase(double beats, double cycleBeats)
{
    if (!std::isfinite(beats) || !std::isfinite(cycleBeats) || cycleBeats <= 0.0) return 0.0;
    const double x = beats / cycleBeats;
    if (!std::isfinite(x)) return 0.0;
    const double f = x - std::floor(x);
    return f >= 0.0 && f < 1.0 ? f : 0.0; // a tiny negative x rounds f up to 1.0
}

} // namespace dc::fx
