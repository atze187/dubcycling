#include "engine/fx/Delay.h"
#include <algorithm>
#include <cmath>

namespace dc::fx {

namespace {
constexpr double kMaxSeconds = 30.0;
constexpr double kTapFadeSeconds = 0.030;  // delay time change crossfade
constexpr double kRevFadeSeconds = 0.010;  // crossfade between consecutive reversed blocks
constexpr double kModeSeconds = 0.020;     // Reverse on/off crossfade
constexpr float kTailFloor = 1e-6f;
constexpr float kFlush = 1e-15f;           // denormal guard for the recording

inline void approach(float& v, float target, float coeff)
{
    if (v == target) return;
    v += (target - v) * coeff;
    if (std::abs(target - v) < 1e-5f) v = target;
}

inline float smoothstep(float x) { return x * x * (3.0f - 2.0f * x); }
}

void Delay::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
    const float sr = static_cast<float>(sampleRate_);
    smoothCoeff_ = 1.0f - std::exp(-1.0f / (0.010f * sr));
    tailRelease_ = std::exp(-1.0f / (0.100f * sr));
    modeStep_ = 1.0f / std::max(1.0f, static_cast<float>(kModeSeconds * sampleRate_));
    tapFadeLen_ = std::max(1, static_cast<int>(kTapFadeSeconds * sampleRate_));
    revFadeMax_ = std::max(1, static_cast<int>(kRevFadeSeconds * sampleRate_));
    maxTap_ = static_cast<int>(std::ceil(kMaxSeconds * sampleRate_));
    size_ = maxTap_ + 2 * revFadeMax_ + 8;
    // A reversed block of length L with the next block's fade-out reads ages up to 2 (L + fade) - 1.
    maxRevLen_ = (size_ - 2 * revFadeMax_ - 4) / 2;
    for (auto& b : buf_) b.assign(static_cast<size_t>(size_), 0.0f);
    write_ = 0;
    reset();
}

void Delay::reset()
{
    fresh_ = true;
    written_ = 0; // older content is read as silence, no clearing needed
    cur_ = {};
    old_ = {};
    tapFading_ = false;
    tapPos_ = 0;
    tapA_ = tapB_ = tapTarget_;
    tail_ = held_ = 0.0f;
    loudAge_ = 0;
}

void Delay::setParams(const FxParams& p)
{
    step_ = static_cast<int>(std::lround(p.get(FxParam::DelayTime)));
    fbTarget_ = std::clamp(p.get(FxParam::DelayFeedback), 0.0f, 0.9f);
    mixTarget_ = std::clamp(p.get(FxParam::DelayMix), 0.0f, 1.0f);
    revTarget_ = p.get(FxParam::DelayReverse) >= 0.5f ? 1.0f : 0.0f;
    updateTarget();
}

void Delay::setTempo(double bpm, double beatsPerBar)
{
    if (std::isfinite(bpm) && bpm > 0.0) bpm_ = bpm;
    if (std::isfinite(beatsPerBar) && beatsPerBar > 0.0) beatsPerBar_ = beatsPerBar;
    updateTarget();
}

void Delay::updateTarget()
{
    const double samples = delayTimeBeats(step_, beatsPerBar_) * 60.0 / bpm_ * sampleRate_;
    tapTarget_ = static_cast<int>(std::clamp(std::llround(samples), 1LL, static_cast<long long>(std::max(1, maxTap_))));
    if (fresh_)
    {
        tapA_ = tapB_ = tapTarget_;
        tapFading_ = false;
        fb_ = fbTarget_;
        mix_ = mixTarget_;
        rev_ = revTarget_;
    }
}

float Delay::read(const std::vector<float>& b, int age) const
{
    if (age > written_) return 0.0f; // before the last reset
    int i = write_ - age;
    if (i < 0) i += size_;
    return b[static_cast<size_t>(i)];
}

void Delay::startBlock()
{
    old_ = cur_;
    cur_.j = 0;
    cur_.len = std::clamp(tapTarget_, 1, maxRevLen_);
    cur_.active = true;
    curFade_ = std::clamp(cur_.len / 4, 1, revFadeMax_);
}

void Delay::process(float* l, float* r, int n)
{
    if (size_ == 0) return;
    float* ch[2] = {l, r};
    for (int i = 0; i < n; ++i)
    {
        fresh_ = false;
        approach(fb_, fbTarget_, smoothCoeff_);
        approach(mix_, mixTarget_, smoothCoeff_);
        if (rev_ < revTarget_) rev_ = std::min(revTarget_, rev_ + modeStep_);
        else if (rev_ > revTarget_) rev_ = std::max(revTarget_, rev_ - modeStep_);

        // Forward tap: crossfade to a new delay time, one change at a time.
        if (!tapFading_ && tapTarget_ != tapA_)
        {
            tapB_ = tapTarget_;
            tapPos_ = 0;
            tapFading_ = true;
        }
        const float tapG = tapFading_ ? smoothstep(static_cast<float>(tapPos_) / static_cast<float>(tapFadeLen_)) : 0.0f;

        // Reverse readers: a new block starts when the current one has played its length.
        if (!cur_.active || cur_.j >= cur_.len) startBlock();
        const bool fadingIn = cur_.j < curFade_;
        const float revG = fadingIn ? smoothstep(static_cast<float>(cur_.j) / static_cast<float>(curFade_)) : 1.0f;
        const bool useOld = fadingIn && old_.active;

        float peak = 0.0f, fedPeak = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            const auto& b = buf_[c];
            float fwd = read(b, tapA_);
            if (tapFading_) fwd += tapG * (read(b, tapB_) - fwd);
            float rev = read(b, 2 * cur_.j + 1) * revG;
            if (useOld) rev += (1.0f - revG) * read(b, 2 * old_.j + 1);
            const float wet = fwd + rev_ * (rev - fwd);

            const float in = std::isfinite(ch[c][i]) ? ch[c][i] : 0.0f;
            float fed = in + fb_ * fwd; // the forward block is re-fed in both modes
            if (!std::isfinite(fed) || std::abs(fed) < kFlush) fed = 0.0f;
            buf_[c][static_cast<size_t>(write_)] = fed;

            peak = std::max(peak, std::abs(wet));
            fedPeak = std::max(fedPeak, std::abs(fed));
            ch[c][i] = wet * mix_;
        }

        // Advance.
        if (tapFading_ && ++tapPos_ >= tapFadeLen_)
        {
            tapA_ = tapB_;
            tapFading_ = false;
        }
        ++cur_.j;
        if (old_.active && ++old_.j >= old_.len + curFade_) old_.active = false;
        if (++write_ >= size_) write_ = 0;
        if (written_ < size_) ++written_;

        // Tail: the output peak with release, plus what is still in the buffer within reach of a tap
        // (so a pending echo keeps the delay awake while the output is still silent).
        tail_ = std::max(peak, tail_ * tailRelease_);
        if (tail_ < kTailFloor) tail_ = 0.0f;
        const int reach = std::max({tapA_, tapB_, tapTarget_, 2 * (cur_.len + revFadeMax_) + 2});
        if (fedPeak > kTailFloor)
        {
            held_ = std::max(held_, fedPeak);
            loudAge_ = 0;
        }
        else if (held_ > 0.0f && ++loudAge_ > reach)
            held_ = 0.0f;
    }
}

} // namespace dc::fx
