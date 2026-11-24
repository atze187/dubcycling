#include "engine/fx/Filter.h"
#include <algorithm>
#include <cmath>

namespace dc::fx {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kQMin = 0.7071f; // resonance 0
constexpr float kQMax = 4.0f;    // resonance 1: clear peak (at most +12 dB), no self-oscillation
constexpr float kStage2K = 1.41421356f; // LP 24 second stage: Butterworth (Q 0.7071), never resonant
constexpr float kFadeSeconds = 0.005f;  // type change crossfade
}

void Filter::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    smoothCoeff_ = 1.0f - std::exp(-1.0f / (0.010f * static_cast<float>(sampleRate_)));
    fadeStep_ = 1.0f / (kFadeSeconds * static_cast<float>(sampleRate_));
    reset();
}

void Filter::reset()
{
    first_ = true;
    ic1_[0] = ic1_[1] = ic2_[0] = ic2_[1] = 0.0f;
    jc1_[0] = jc1_[1] = jc2_[0] = jc2_[1] = 0.0f;
    logCutoff_ = std::log(cutoffTarget_);
    res_ = resTarget_;
    g_ = -1.0f; // forces a coefficient update
    fading_ = false;
    for (int t = 0; t < kNumTypes; ++t) mix_[t] = t == type_ ? 1.0f : 0.0f;
}

void Filter::setParams(const FxParams& p)
{
    const int type = std::clamp(static_cast<int>(std::lround(p.get(FxParam::FilterType))), 0, kNumTypes - 1);
    const float maxHz = 0.45f * static_cast<float>(sampleRate_);
    cutoffTarget_ = std::clamp(p.get(FxParam::FilterCutoff), 20.0f, maxHz);
    resTarget_ = std::clamp(p.get(FxParam::FilterResonance), 0.0f, 1.0f);
    if (first_)
    {
        type_ = type;
        fading_ = false;
        for (int t = 0; t < kNumTypes; ++t) mix_[t] = t == type_ ? 1.0f : 0.0f;
        logCutoff_ = std::log(cutoffTarget_);
        res_ = resTarget_;
        g_ = -1.0f;
        first_ = false;
    }
    else if (type != type_)
    {
        type_ = type;
        fading_ = true; // the weights ramp in process()
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
            // Peak = (low - high) has 2q at the cutoff and 1 far away; scaled by q^-0.6 it is unity far away
            // at resonance 0.2 (q = 1) and at most 2 * 4^0.4 = 3.5 (+10.8 dB) at the cutoff at resonance 1.
            peakGain_ = std::pow(k_, 0.6f);
            b1_ = 1.0f / (1.0f + g_ * (g_ + kStage2K));
            b2_ = g_ * b1_;
            b3_ = g_ * b2_;
        }
        float wsum = 1.0f;
        if (fading_)
        {
            bool done = true;
            wsum = 0.0f;
            for (int t = 0; t < kNumTypes; ++t)
            {
                if (t == type_) mix_[t] = std::min(1.0f, mix_[t] + fadeStep_);
                else mix_[t] = std::max(0.0f, mix_[t] - fadeStep_);
                if (mix_[t] != (t == type_ ? 1.0f : 0.0f)) done = false;
                wsum += mix_[t];
            }
            if (done) fading_ = false;
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
            // LP 24 second stage, fed by the low pass; always running so a switch to LP 24 finds it settled.
            const float w3 = v2 - jc2_[c];
            const float w1 = b1_ * jc1_[c] + b2_ * w3;
            const float w2 = jc2_[c] + b2_ * jc1_[c] + b3_ * w3;
            jc1_[c] = 2.0f * w1 - jc1_[c];
            jc2_[c] = 2.0f * w2 - jc2_[c];
            if (std::abs(jc1_[c]) < 1e-20f) jc1_[c] = 0.0f;
            if (std::abs(jc2_[c]) < 1e-20f) jc2_[c] = 0.0f;

            auto typeOut = [&](int t) {
                switch (t)
                {
                    case 0: return v2;
                    case 1: return v0 - k_ * v1 - v2;
                    case 2: return k_ * v1; // unity gain at the cutoff
                    case 3: return v0 - k_ * v1; // notch: low + high
                    case 4: return (v2 - (v0 - k_ * v1 - v2)) * peakGain_; // peak: low - high
                    default: return w2; // LP 24
                }
            };
            float out;
            if (!fading_)
                out = typeOut(type_);
            else
            {
                // Convex mix of the types in flight (wsum >= fadeStep_ > 0: the current type never falls).
                out = 0.0f;
                for (int t = 0; t < kNumTypes; ++t)
                    if (mix_[t] > 0.0f) out += mix_[t] * typeOut(t);
                out /= wsum;
            }
            if (!std::isfinite(out) || !std::isfinite(w2))
            {
                ic1_[c] = ic2_[c] = 0.0f; // clear a blown-up state
                jc1_[c] = jc2_[c] = 0.0f;
                if (!std::isfinite(out)) out = 0.0f;
            }
            *ch[c] = out;
        }
    }
}

} // namespace dc::fx
