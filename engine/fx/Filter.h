#pragma once
#include "engine/FxSettings.h"

namespace dc::fx {

// Stereo state variable filter (TPT), low / high / band pass.
class Filter
{
public:
    void prepare(double sampleRate);
    void reset();
    void setParams(const FxParams& p);
    void process(float* l, float* r, int n);

private:
    double sampleRate_ = 48000.0;
    float smoothCoeff_ = 0.0f;
    bool first_ = true;
    int type_ = 0;
    float cutoffTarget_ = 2000.0f, resTarget_ = 0.2f; // cutoff in Hz
    float logCutoff_ = 0.0f, res_ = 0.0f;             // smoothed: ln(Hz), 0..1
    float g_ = 0.0f, k_ = 1.0f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float ic1_[2] = {0.0f, 0.0f}, ic2_[2] = {0.0f, 0.0f};
};

} // namespace dc::fx
