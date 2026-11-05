#pragma once
#include <vector>
#include "engine/FxSettings.h"

namespace dc::fx {

// Stereo Freeverb-style reverb (8 damped combs + 4 allpasses per channel). process() replaces
// the input by the pure wet signal (scaled by Mix, no dry part).
class Reverb
{
public:
    void prepare(double sampleRate);
    void reset();
    void setParams(const FxParams& p);
    void process(float* l, float* r, int n);
    float tailLevel() const { return tail_; }

private:
    static constexpr int kCombs = 8, kAllpasses = 4;
    struct Line
    {
        std::vector<float> buf;
        int pos = 0;
        float store = 0.0f; // comb damping filter state
    };

    double sampleRate_ = 48000.0;
    float smoothCoeff_ = 0.0f, tailRelease_ = 0.0f;
    bool first_ = true;
    float mixTarget_ = 0.3f, sizeTarget_ = 0.5f, dampTarget_ = 0.5f;
    float mix_ = 0.3f, size_ = 0.5f, damp_ = 0.5f; // smoothed
    Line combs_[2][kCombs];
    Line allpasses_[2][kAllpasses];
    float tail_ = 0.0f;
};

} // namespace dc::fx
