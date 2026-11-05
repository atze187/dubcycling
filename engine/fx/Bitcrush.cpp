#include "engine/fx/Bitcrush.h"
#include <algorithm>
#include <cmath>

namespace dc::fx {

void Bitcrush::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    smoothCoeff_ = 1.0f - std::exp(-1.0f / (0.010f * static_cast<float>(sampleRate_)));
    reset();
}

void Bitcrush::reset()
{
    first_ = true;
    counter_ = 0;
    heldL_ = heldR_ = 0.0f;
}

void Bitcrush::setParams(const FxParams& p)
{
    const double bits = p.get(FxParam::BitcrushBits);
    const double levels = std::max(2.0, std::round(std::exp2(bits)));
    half_ = static_cast<float>(levels * 0.5);
    hold_ = std::clamp(static_cast<int>(std::lround(p.get(FxParam::BitcrushRate))), 1, 64);
    mixTarget_ = std::clamp(p.get(FxParam::BitcrushMix), 0.0f, 1.0f);
    if (first_)
    {
        mix_ = mixTarget_;
        first_ = false;
    }
}

void Bitcrush::process(float* l, float* r, int n)
{
    auto quantize = [this](float x) {
        const float idx = std::clamp(std::floor(x * half_), -half_, half_ - 1.0f);
        return (idx + 0.5f) / half_;
    };
    for (int i = 0; i < n; ++i)
    {
        if (counter_ >= hold_) counter_ = 0;
        if (counter_ == 0)
        {
            heldL_ = quantize(l[i]);
            heldR_ = quantize(r[i]);
        }
        ++counter_;
        mix_ += (mixTarget_ - mix_) * smoothCoeff_;
        if (std::abs(mixTarget_ - mix_) < 1e-6f) mix_ = mixTarget_;
        l[i] = l[i] * (1.0f - mix_) + heldL_ * mix_;
        r[i] = r[i] * (1.0f - mix_) + heldR_ * mix_;
    }
}

} // namespace dc::fx
