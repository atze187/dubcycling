#pragma once
#include <vector>
#include "engine/FxSettings.h"

namespace dc::fx {

// Stereo flanger: LFO-modulated delay with feedback. process() replaces the input by the
// pure wet signal (scaled by Mix, no dry part).
class Flanger
{
public:
    void prepare(double sampleRate);
    void reset();
    void setParams(const FxParams& p);
    void process(float* l, float* r, int n);
    float tailLevel() const { return tail_; }

private:
    double sampleRate_ = 48000.0;
    float smoothCoeff_ = 0.0f, tailRelease_ = 0.0f;
    bool first_ = true;
    float rateTarget_ = 0.3f, depthTarget_ = 0.5f, fbTarget_ = 0.4f, mixTarget_ = 0.5f;
    float rate_ = 0.3f, depth_ = 0.5f, fb_ = 0.4f, mix_ = 0.5f; // smoothed
    double phase_ = 0.0;                                         // LFO, 0..1, survives silence
    std::vector<float> buf_[2];
    int size_ = 0, write_ = 0;
    float tail_ = 0.0f;
};

} // namespace dc::fx
