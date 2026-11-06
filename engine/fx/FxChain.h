#pragma once
#include <cstdint>
#include <vector>
#include "engine/FxSettings.h"
#include "engine/fx/Bitcrush.h"
#include "engine/fx/Filter.h"
#include "engine/fx/Flanger.h"
#include "engine/fx/Reverb.h"

namespace dc::fx {

// The insert chain Bitcrush -> Filter -> Flanger -> Reverb, processed in place on the bus.
// Each insert has a gate gain g (0..1, ramped over about 5 ms) that follows its mask bit.
// Bitcrush and Filter blend: out = in + g * (wet - in). Flanger and Reverb get g * in and their
// wet output is added to the signal, so their tails ring out after the gate closes. Inserts
// with a closed gate and no tail are skipped and reset. Mask 0 with empty state: out == in.
class FxChain
{
public:
    void prepare(double sampleRate, int maxBlock);
    void reset();
    void setParams(const FxParams& p);
    void setMask(std::uint8_t mask);
    void flush();
    void process(float* l, float* r, int n);
    bool isActive(FxInsert i) const;
    bool anyActive() const;

private:
    void processChunk(float* l, float* r, int n);
    void sleep(int insert);
    // Advances the gate of an insert over n samples, writing the per-sample gain into gateBuf_.
    void fillGate(int insert, int n);

    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
    FxParams params_;
    Bitcrush bitcrush_;
    Filter filter_;
    Flanger flanger_;
    Reverb reverb_;

    float gate_[kFxInserts] = {};
    float target_[kFxInserts] = {};
    bool awake_[kFxInserts] = {};
    float gateStep_ = 1.0f;

    bool flushing_ = false;
    float fade_ = 1.0f;
    float fadeStep_ = 1.0f;

    std::vector<float> tmpL_, tmpR_, fadeBuf_, gateBuf_;
};

} // namespace dc::fx
