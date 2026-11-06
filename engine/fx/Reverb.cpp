#include "engine/fx/Reverb.h"
#include <algorithm>
#include <cmath>

namespace dc::fx {

namespace {
// Freeverb tunings at 44.1 kHz (scaled to the sample rate); right channel is offset by kSpread.
constexpr int kCombTuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int kAllpassTuning[4] = {556, 441, 341, 225};
constexpr int kSpread = 23;
constexpr float kInputGain = 0.015f;
constexpr float kAllpassFeedback = 0.5f;
constexpr float kTailFloor = 1e-6f;

inline void approach(float& v, float target, float coeff)
{
    if (v == target) return;
    v += (target - v) * coeff;
    if (std::abs(target - v) < 1e-5f) v = target;
}

inline float flush(float x) { return std::abs(x) < 1e-20f ? 0.0f : x; }

// Wet gain that brings the white-noise RMS of the wet signal to about the input RMS.
// A comb with feedback fb and the one-pole damping d in its loop has, averaged over frequency,
// the power gain 1 + c / sqrt((1 + d^2 - c)^2 - 4 d^2) with c = (fb (1 - d))^2 (1 / (1 - fb^2)
// for d = 0). The rest of the structure is constant: 8 parallel combs fed with (L + R) * kInputGain,
// 4 Freeverb allpasses with feedback 0.5 in series (power gain 7/3 each).
inline float wetGain(float fb, float d)
{
    const double c = double(fb) * fb * (1.0 - d) * (1.0 - d);
    const double a = 1.0 + double(d) * d - c;
    const double comb = 1.0 + c / std::sqrt(std::max(1e-12, a * a - 4.0 * double(d) * d));
    const double ap = 7.0 / 3.0;
    const double structure = 8.0 * 2.0 * double(kInputGain) * kInputGain * ap * ap * ap * ap;
    return static_cast<float>(1.0 / std::sqrt(structure * comb));
}
}

void Reverb::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    const float sr = static_cast<float>(sampleRate_);
    smoothCoeff_ = 1.0f - std::exp(-1.0f / (0.010f * sr));
    tailRelease_ = std::exp(-1.0f / (0.100f * sr));
    const double scale = sampleRate_ / 44100.0;
    auto len = [scale](int tuning) { return std::max(1, static_cast<int>(std::lround(tuning * scale))); };
    for (int c = 0; c < 2; ++c)
    {
        for (int k = 0; k < kCombs; ++k)
            combs_[c][k].buf.assign(static_cast<size_t>(len(kCombTuning[k] + c * kSpread)), 0.0f);
        for (int k = 0; k < kAllpasses; ++k)
            allpasses_[c][k].buf.assign(static_cast<size_t>(len(kAllpassTuning[k] + c * kSpread)), 0.0f);
    }
    reset();
}

void Reverb::reset()
{
    first_ = true;
    tail_ = 0.0f;
    for (auto& ch : combs_)
        for (auto& line : ch) { std::fill(line.buf.begin(), line.buf.end(), 0.0f); line.pos = 0; line.store = 0.0f; }
    for (auto& ch : allpasses_)
        for (auto& line : ch) { std::fill(line.buf.begin(), line.buf.end(), 0.0f); line.pos = 0; line.store = 0.0f; }
}

void Reverb::setParams(const FxParams& p)
{
    mixTarget_ = std::clamp(p.get(FxParam::ReverbMix), 0.0f, 1.0f);
    sizeTarget_ = std::clamp(p.get(FxParam::ReverbSize), 0.0f, 1.0f);
    dampTarget_ = std::clamp(p.get(FxParam::ReverbDamping), 0.0f, 1.0f);
    if (first_)
    {
        mix_ = mixTarget_;
        size_ = sizeTarget_;
        damp_ = dampTarget_;
        first_ = false;
    }
}

void Reverb::process(float* l, float* r, int n)
{
    if (combs_[0][0].buf.empty()) return;
    float* ch[2] = {l, r};
    for (int i = 0; i < n; ++i)
    {
        approach(mix_, mixTarget_, smoothCoeff_);
        approach(size_, sizeTarget_, smoothCoeff_);
        approach(damp_, dampTarget_, smoothCoeff_);
        const float feedback = 0.7f + 0.28f * size_; // size 1: RT60 around 8 s
        const float d = damp_ * 0.5f;
        if (size_ != gainSize_ || damp_ != gainDamp_)
        {
            wetGain_ = wetGain(feedback, d);
            gainSize_ = size_;
            gainDamp_ = damp_;
        }

        const float inL = std::isfinite(l[i]) ? l[i] : 0.0f;
        const float inR = std::isfinite(r[i]) ? r[i] : 0.0f;
        const float in = (inL + inR) * kInputGain;

        float peak = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            float sum = 0.0f;
            for (auto& line : combs_[c])
            {
                float& slot = line.buf[static_cast<size_t>(line.pos)];
                const float out = slot;
                line.store = flush(out * (1.0f - d) + line.store * d);
                slot = flush(in + line.store * feedback);
                if (++line.pos >= static_cast<int>(line.buf.size())) line.pos = 0;
                sum += out;
            }
            for (auto& line : allpasses_[c])
            {
                float& slot = line.buf[static_cast<size_t>(line.pos)];
                const float bufOut = slot;
                const float out = bufOut - sum;
                slot = flush(sum + bufOut * kAllpassFeedback);
                if (++line.pos >= static_cast<int>(line.buf.size())) line.pos = 0;
                sum = out;
            }
            if (!std::isfinite(sum)) sum = 0.0f;
            peak = std::max(peak, std::abs(sum));
            ch[c][i] = sum * wetGain_ * mix_;
        }
        tail_ = std::max(peak, tail_ * tailRelease_);
        if (tail_ < kTailFloor) tail_ = 0.0f;
    }
}

} // namespace dc::fx
