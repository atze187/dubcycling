#include "engine/LoopTiming.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace dc {

namespace {
bool validPositive(double v) { return std::isfinite(v) && v > 0.0; }
}

double beatsPerBar(int tsNum, int tsDen)
{
    if (tsNum <= 0 || tsDen <= 0)
        return 4.0;
    return tsNum * 4.0 / tsDen;
}

int suggestBars(double seconds, double bpm, double beatsPerBarValue)
{
    if (!validPositive(seconds) || !validPositive(bpm) || !validPositive(beatsPerBarValue))
        return 1;
    const double bars = std::round(seconds * bpm / 60.0 / beatsPerBarValue);
    if (!(bars < 1.0e6))
        return 1000000;
    return std::max(1, static_cast<int>(bars));
}

long long lcmBars(int a, int b)
{
    const long long x = a > 0 ? a : 1;
    const long long y = b > 0 ? b : 1;
    return std::lcm(x, y);
}

double sourceStep(size_t sourceFrames, double loopBeats, double bpm, double outRate)
{
    if (sourceFrames == 0 || !validPositive(loopBeats) || !validPositive(bpm) || !validPositive(outRate))
        return 0.0;
    const double outFrames = loopBeats * 60.0 / bpm * outRate;
    if (!validPositive(outFrames))
        return 0.0;
    const double step = static_cast<double>(sourceFrames) / outFrames;
    if (!std::isfinite(step))
        return 0.0;
    return std::clamp(step, kMinStep, kMaxStep);
}

} // namespace dc
