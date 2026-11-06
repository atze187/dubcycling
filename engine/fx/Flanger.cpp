#include "engine/fx/Flanger.h"
#include <algorithm>
#include <cmath>

namespace dc::fx {

namespace {
constexpr double kTwoPi = 6.283185307179586;
constexpr float kMinDelayMs = 0.1f;
constexpr float kDepthMs = 6.9f; // modulation span at depth 1: delay sweeps 0.1 .. 7 ms
constexpr float kTailFloor = 1e-6f;

inline void approach(float& v, float target, float coeff)
{
    if (v == target) return;
    v += (target - v) * coeff;
    if (std::abs(target - v) < 1e-5f) v = target;
}
}

void Flanger::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    const float sr = static_cast<float>(sampleRate_);
    smoothCoeff_ = 1.0f - std::exp(-1.0f / (0.010f * sr));
    tailRelease_ = std::exp(-1.0f / (0.100f * sr));
    size_ = static_cast<int>(std::ceil((kMinDelayMs + kDepthMs) * 0.001 * sampleRate_)) + 4;
    for (auto& b : buf_) b.assign(static_cast<size_t>(size_), 0.0f);
    phase_ = 0.0;
    reset();
}

void Flanger::reset()
{
    first_ = true;
    for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
    write_ = 0;
    tail_ = 0.0f;
    // phase_ is deliberately kept: the LFO keeps running across silence.
}

void Flanger::setParams(const FxParams& p)
{
    rateTarget_ = std::clamp(p.get(FxParam::FlangerRate), 0.05f, 5.0f);
    depthTarget_ = std::clamp(p.get(FxParam::FlangerDepth), 0.0f, 1.0f);
    fbTarget_ = std::clamp(p.get(FxParam::FlangerFeedback), -0.9f, 0.9f);
    mixTarget_ = std::clamp(p.get(FxParam::FlangerMix), 0.0f, 1.0f);
    if (first_)
    {
        rate_ = rateTarget_;
        depth_ = depthTarget_;
        fb_ = fbTarget_;
        mix_ = mixTarget_;
        first_ = false;
    }
}

void Flanger::process(float* l, float* r, int n)
{
    if (size_ == 0) return;
    const float sr = static_cast<float>(sampleRate_);
    float* ch[2] = {l, r};
    for (int i = 0; i < n; ++i)
    {
        approach(rate_, rateTarget_, smoothCoeff_);
        approach(depth_, depthTarget_, smoothCoeff_);
        approach(fb_, fbTarget_, smoothCoeff_);
        approach(mix_, mixTarget_, smoothCoeff_);
        phase_ += rate_ / sampleRate_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        // Level compensation for the feedback resonance: (1 - |fb|)^0.5, from the smoothed feedback
        // so a feedback change does not zipper. Mix 0 still gives exact silence.
        const float out = mix_ * std::sqrt(1.0f - std::abs(fb_));

        float peak = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            // Right channel runs a quarter cycle ahead for stereo width.
            const double ph = phase_ + (c == 1 ? 0.25 : 0.0);
            const float lfo = 0.5f + 0.5f * static_cast<float>(std::sin(kTwoPi * ph));
            const float delay = (kMinDelayMs + depth_ * kDepthMs * lfo) * 0.001f * sr;
            float rp = static_cast<float>(write_) - delay;
            if (rp < 0.0f) rp += static_cast<float>(size_);
            const int i0 = std::min(static_cast<int>(rp), size_ - 1);
            const float frac = rp - static_cast<float>(i0);
            const int i1 = i0 + 1 >= size_ ? 0 : i0 + 1;
            auto& b = buf_[c];
            const float delayed = b[static_cast<size_t>(i0)] * (1.0f - frac) + b[static_cast<size_t>(i1)] * frac;

            const float in = std::isfinite(ch[c][i]) ? ch[c][i] : 0.0f;
            float fed = in + fb_ * delayed;
            if (!std::isfinite(fed) || std::abs(fed) < 1e-20f) fed = 0.0f; // flush denormals / blown state
            b[static_cast<size_t>(write_)] = fed;

            peak = std::max(peak, std::abs(delayed));
            ch[c][i] = delayed * out;
        }
        tail_ = std::max(peak, tail_ * tailRelease_);
        if (tail_ < kTailFloor) tail_ = 0.0f;
        if (++write_ >= size_) write_ = 0;
    }
}

} // namespace dc::fx
