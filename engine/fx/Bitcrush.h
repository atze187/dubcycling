#pragma once
#include "engine/FxSettings.h"

namespace dc::fx {

// Bit depth reduction plus sample-and-hold decimation, mixed against the dry signal.
class Bitcrush
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
    float mixTarget_ = 1.0f, mix_ = 1.0f;
    float half_ = 128.0f; // levels / 2
    int hold_ = 1;
    int counter_ = 0;
    float heldL_ = 0.0f, heldR_ = 0.0f;
};

} // namespace dc::fx
