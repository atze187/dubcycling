#include "engine/fx/FxChain.h"
#include <algorithm>

namespace dc::fx {

namespace {
constexpr float kGateSeconds = 0.005f;
constexpr float kFlushSeconds = 0.050f;
// Tail level (pre-Mix, pre wet scale) below which a closed Flanger/Reverb is put to sleep:
// -100 dB. With the reverb's wet gain (at most about 2.8) the output at that point is below 3e-5
// (-90 dB) before the reset.
constexpr float kSleepThreshold = 1e-5f;

constexpr int idx(FxInsert i) { return static_cast<int>(i); }
}

void FxChain::prepare(double sampleRate, int maxBlock)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock_ = std::max(1, maxBlock);
    const float sr = static_cast<float>(sampleRate_);
    gateStep_ = 1.0f / std::max(1.0f, kGateSeconds * sr);
    fadeStep_ = 1.0f / std::max(1.0f, kFlushSeconds * sr);
    for (auto* v : {&tmpL_, &tmpR_, &fadeBuf_, &gateBuf_}) v->assign(static_cast<size_t>(maxBlock_), 0.0f);
    bitcrush_.prepare(sampleRate_);
    filter_.prepare(sampleRate_);
    flanger_.prepare(sampleRate_);
    reverb_.prepare(sampleRate_);
    reset();
}

void FxChain::reset()
{
    for (int i = 0; i < kFxInserts; ++i)
    {
        sleep(i);
        gate_[i] = target_[i];
    }
    flushing_ = false;
    fade_ = 1.0f;
}

void FxChain::setParams(const FxParams& p)
{
    params_ = p;
    bitcrush_.setParams(p);
    filter_.setParams(p);
    flanger_.setParams(p);
    reverb_.setParams(p);
}

void FxChain::setMask(std::uint8_t mask)
{
    for (int i = 0; i < kFxInserts; ++i) target_[i] = (mask >> i) & 1u ? 1.0f : 0.0f;
}

void FxChain::flush()
{
    if (!anyActive()) return; // nothing sounds, nothing to fade
    flushing_ = true;
}

bool FxChain::isActive(FxInsert i) const
{
    const int k = idx(i);
    return k >= 0 && k < kFxInserts && (awake_[k] || target_[k] > 0.0f);
}

bool FxChain::anyActive() const
{
    for (int i = 0; i < kFxInserts; ++i)
        if (isActive(static_cast<FxInsert>(i))) return true;
    return false;
}

void FxChain::sleep(int insert)
{
    switch (insert)
    {
        case 0: bitcrush_.reset(); bitcrush_.setParams(params_); break;
        case 1: filter_.reset(); filter_.setParams(params_); break;
        case 2: flanger_.reset(); flanger_.setParams(params_); break;
        default: reverb_.reset(); reverb_.setParams(params_); break;
    }
    awake_[insert] = false;
}

void FxChain::process(float* l, float* r, int n)
{
    if (maxBlock_ == 0) return; // not prepared
    for (int pos = 0; pos < n; pos += maxBlock_)
        processChunk(l + pos, r + pos, std::min(maxBlock_, n - pos));
}

void FxChain::fillGate(int insert, int n)
{
    float g = gate_[insert];
    const float t = target_[insert];
    for (int i = 0; i < n; ++i)
    {
        if (g < t) g = std::min(t, g + gateStep_);
        else if (g > t) g = std::max(t, g - gateStep_);
        gateBuf_[static_cast<size_t>(i)] = g;
    }
    gate_[insert] = g;
}

void FxChain::processChunk(float* l, float* r, int n)
{
    if (n <= 0) return;

    // Flush fade for this chunk (applies to the effect parts only).
    for (int i = 0; i < n; ++i)
    {
        if (flushing_) fade_ = std::max(0.0f, fade_ - fadeStep_);
        fadeBuf_[static_cast<size_t>(i)] = fade_;
    }

    float* tl = tmpL_.data();
    float* tr = tmpR_.data();
    const float* g = gateBuf_.data();
    const float* f = fadeBuf_.data();

    // Bitcrush and Filter: blend between the signal and the processed signal.
    for (int k = idx(FxInsert::Bitcrush); k <= idx(FxInsert::Filter); ++k)
    {
        if (gate_[k] == 0.0f && target_[k] == 0.0f)
        {
            if (awake_[k]) sleep(k);
            continue;
        }
        awake_[k] = true;
        fillGate(k, n);
        std::copy(l, l + n, tl);
        std::copy(r, r + n, tr);
        if (k == idx(FxInsert::Bitcrush)) bitcrush_.process(tl, tr, n);
        else filter_.process(tl, tr, n);
        for (int i = 0; i < n; ++i)
        {
            const float w = g[i] * f[i];
            l[i] += w * (tl[i] - l[i]);
            r[i] += w * (tr[i] - r[i]);
        }
    }

    // Flanger and Reverb: the gated signal feeds the unit, its wet output is added.
    for (int k = idx(FxInsert::Flanger); k <= idx(FxInsert::Reverb); ++k)
    {
        const bool flanger = k == idx(FxInsert::Flanger);
        if (!awake_[k] && gate_[k] == 0.0f && target_[k] == 0.0f) continue;
        awake_[k] = true;
        fillGate(k, n);
        for (int i = 0; i < n; ++i)
        {
            tl[i] = g[i] * l[i];
            tr[i] = g[i] * r[i];
        }
        if (flanger) flanger_.process(tl, tr, n);
        else reverb_.process(tl, tr, n);
        for (int i = 0; i < n; ++i)
        {
            l[i] += f[i] * tl[i];
            r[i] += f[i] * tr[i];
        }
        const float tail = flanger ? flanger_.tailLevel() : reverb_.tailLevel();
        if (gate_[k] == 0.0f && target_[k] == 0.0f && tail <= kSleepThreshold) sleep(k);
    }

    if (flushing_ && fade_ == 0.0f)
    {
        // Faded out: clear every state; gates restart from closed so open inserts ramp back in.
        for (int k = 0; k < kFxInserts; ++k)
        {
            sleep(k);
            gate_[k] = 0.0f;
        }
        flushing_ = false;
        fade_ = 1.0f;
    }
}

} // namespace dc::fx
