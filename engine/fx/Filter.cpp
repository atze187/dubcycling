#include "engine/fx/Filter.h"
#include <algorithm>
#include <cmath>

namespace dc::fx {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kQMin = 0.7071f; // resonance 0
constexpr float kQMax = 8.0f;    // resonance 1: strong peak, no endless self-oscillation
}

void Filter::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    smoothCoeff_ = 1.0f - std::exp(-1.0f / (0.010f * static_cast<float>(sampleRate_)));
    reset();
}

void Filter::reset()
{
    first_ = true;
    ic1_[0] = ic1_[1] = ic2_[0] = ic2_[1] = 0.0f;
    logCutoff_ = std::log(cutoffTarget_);
    res_ = resTarget_;
    g_ = -1.0f; // forces a coefficient update
}

void Filter::setParams(const FxParams& p)
{
    type_ = std::clamp(static_cast<int>(std::lround(p.get(FxParam::FilterType))), 0, 2);
    const float maxHz = 0.45f * static_cast<float>(sampleRate_);
    cutoffTarget_ = std::clamp(p.get(FxParam::FilterCutoff), 20.0f, maxHz);
    resTarget_ = std::clamp(p.get(FxParam::FilterResonance), 0.0f, 1.0f);
    if (first_)
    {
        logCutoff_ = std::log(cutoffTarget_);
        res_ = resTarget_;
        g_ = -1.0f;
        first_ = false;
    }
}

void Filter::process(float* l, float* r, int n)
{
    const float logTarget = std::log(cutoffTarget_);
    for (int i = 0; i < n; ++i)
    {
        bool changed = g_ < 0.0f;
        if (logCutoff_ != logTarget)
        {
            logCutoff_ += (logTarget - logCutoff_) * smoothCoeff_;
            if (std::abs(logTarget - logCutoff_) < 1e-5f) logCutoff_ = logTarget;
            changed = true;
        }
        if (res_ != resTarget_)
        {
            res_ += (resTarget_ - res_) * smoothCoeff_;
            if (std::abs(resTarget_ - res_) < 1e-5f) res_ = resTarget_;
            changed = true;
        }
        if (changed)
        {
            const float hz = std::min(std::exp(logCutoff_), 0.45f * static_cast<float>(sampleRate_));
            g_ = std::tan(kPi * hz / static_cast<float>(sampleRate_));
            const float q = kQMin * std::pow(kQMax / kQMin, res_);
            k_ = 1.0f / q;
            a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
            a2_ = g_ * a1_;
            a3_ = g_ * a2_;
        }
        float* ch[2] = {l + i, r + i};
        for (int c = 0; c < 2; ++c)
        {
            const float in = *ch[c];
            const float v0 = std::isfinite(in) ? in : 0.0f;
            const float v3 = v0 - ic2_[c];
            const float v1 = a1_ * ic1_[c] + a2_ * v3;
            const float v2 = ic2_[c] + a2_ * ic1_[c] + a3_ * v3;
            ic1_[c] = 2.0f * v1 - ic1_[c];
            ic2_[c] = 2.0f * v2 - ic2_[c];
            // Flush denormals (resonance decaying after silence).
            if (std::abs(ic1_[c]) < 1e-20f) ic1_[c] = 0.0f;
            if (std::abs(ic2_[c]) < 1e-20f) ic2_[c] = 0.0f;
            float out;
            switch (type_)
            {
                case 0: out = v2; break;
                case 1: out = v0 - k_ * v1 - v2; break;
                default: out = k_ * v1; break; // unity gain at the cutoff
            }
            if (!std::isfinite(out))
            {
                ic1_[c] = ic2_[c] = 0.0f; // clear a blown-up state
                out = 0.0f;
            }
            *ch[c] = out;
        }
    }
}

} // namespace dc::fx
