#pragma once
#include <cstddef>

namespace dc {

constexpr double kMinStep = 0.25;
constexpr double kMaxStep = 4.0;

// Beats per bar from a time signature; invalid (<= 0) gives 4.0.
double beatsPerBar(int tsNum, int tsDen);

// Rounded number of bars for a loop of `seconds`, at least 1; invalid input gives 1.
int suggestBars(double seconds, double bpm, double beatsPerBar);

// Combined cycle length of two loops in bars; invalid (<= 0) inputs are treated as 1.
long long lcmBars(int a, int b);

// Source frames per output frame, clamped to [kMinStep, kMaxStep].
// Invalid input (bpm <= 0, NaN/inf, outRate <= 0, no frames, loopBeats <= 0) gives 0.0.
double sourceStep(size_t sourceFrames, double loopBeats, double bpm, double outRate);

} // namespace dc
