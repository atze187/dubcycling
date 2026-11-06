#pragma once
#include <array>
#include <vector>

namespace dc {

// Read range and speed of the grains for one call of GrainStretch::process.
struct GrainFrame
{
    double step = 1.0;  // source samples per output sample (including tune and pitch)
    double dir = 1.0;   // +1 forward, -1 backward
    bool cycle = false; // true: wrap read positions into [lo, hi); false: hold at [lo, hi - 1]
    double lo = 0.0;
    double hi = 0.0;
};

// Cyclic stretch in the style of the Akai S950: every grainLen output samples a
// new grain starts at the source position; the old one runs on for a quarter of the grain duration and is crossfaded linearly.
// The pitch comes solely from GrainFrame::step; the caller supplies the source position.
// With each new grain all older ones run out within the new crossfade time; so at most
// two grains live, and the weights stay continuous even when the grain size changes.
class GrainStretch
{
public:
    void reset();
    // Starts the first grain immediately at srcPos, without a fade-in (the caller crossfades itself).
    void start(double srcPos, double dir, int grainLen);
    // Brings the next grain forward (source position jump): it starts as soon as no grain is
    // fading out any more, and then crossfades over the normal crossfade time. Keeps "at most two".
    void retrigger();
    float process(const std::vector<float>& x, double srcPos, int grainLen, const GrainFrame& f);
    // Number of currently audible grains (for tests of the "at most two" invariant).
    int activeGrains() const;
    // Starts where both slots were occupied (invariant violation; must stay 0).
    int slotCollisions() const { return collisions_; }

private:
    struct Grain
    {
        bool active = false;
        bool fadingOut = false;
        double pos = 0.0;
        double dir = 1.0;
        float gain = 1.0f;
        float delta = 0.0f; // weight change per output sample
        int remaining = 0;  // samples until the fade-out begins
        int fade = 1;       // length of the fade-out in samples
        int fadeLeft = 0;   // remaining fade-out samples; at 0 the grain ends (integer, exact)
    };

    void launch(double srcPos, double dir, int grainLen, bool fadeIn);

    std::array<Grain, 2> grains_ {};
    int untilNext_ = 0;
    int collisions_ = 0;
};

} // namespace dc
