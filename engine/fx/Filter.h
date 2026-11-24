#pragma once
#include "engine/FxSettings.h"

namespace dc::fx {

// Stereo state variable filter (TPT). Types: 0 low pass, 1 high pass, 2 band pass, 3 notch
// (low + high), 4 peak (low - high), 5 LP 24 (the low pass followed by a second, Butterworth stage).
// A type change while running crossfades the outputs over about 5 ms.
class Filter
{
public:
    static constexpr int kNumTypes = 6;

    void prepare(double sampleRate);
    void reset();
    void setParams(const FxParams& p);
    void process(float* l, float* r, int n);

private:
    double sampleRate_ = 48000.0;
    float smoothCoeff_ = 0.0f;
    float fadeStep_ = 0.0f; // per-sample weight change of the type crossfade
    bool first_ = true;
    int type_ = 0;
    float cutoffTarget_ = 2000.0f, resTarget_ = 0.2f; // cutoff in Hz
    float logCutoff_ = 0.0f, res_ = 0.0f;             // smoothed: ln(Hz), 0..1
    float g_ = 0.0f, k_ = 1.0f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float peakGain_ = 1.0f;                           // peak mode output scale
    float b1_ = 0.0f, b2_ = 0.0f, b3_ = 0.0f;         // LP 24 second stage (shares g_, fixed Q)
    float ic1_[2] = {0.0f, 0.0f}, ic2_[2] = {0.0f, 0.0f};
    float jc1_[2] = {0.0f, 0.0f}, jc2_[2] = {0.0f, 0.0f}; // LP 24 second stage state
    // Type crossfade: one weight per type, each ramps towards 1 (current type) or 0.
    bool fading_ = false;
    float mix_[kNumTypes] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace dc::fx
