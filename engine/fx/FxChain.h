#pragma once
#include <cstdint>
#include <vector>
#include "engine/FxSettings.h"
#include "engine/fx/Bitcrush.h"
#include "engine/fx/Delay.h"
#include "engine/fx/Filter.h"
#include "engine/fx/Flanger.h"
#include "engine/fx/Reverb.h"

namespace dc::fx {

// The insert chain of five effects in a user-chosen order (default Bitcrush -> Filter -> Flanger ->
// Delay -> Reverb), processed in place on the bus. Each insert has a gate gain g (0..1, ramped over
// about 5 ms) that follows its mask bit. Bitcrush and Filter blend: out = in + g * (wet - in).
// Flanger, Delay and Reverb get g * in and their wet output is added to the signal, so their tails
// ring out after the gate closes. Inserts with a closed gate and no tail are skipped and reset.
// Mask 0 with empty state: out == in.
//
// A new order while something sounds is not switched over at once (the inserts' states belong to
// their old positions): the chain runs its flush fade (50 ms), and the order takes effect when the
// fade has completed and every state is cleared; open inserts then ramp back in over the gate time.
class FxChain
{
public:
    void prepare(double sampleRate, int maxBlock);
    void reset();
    void setParams(const FxParams& p);
    void setMask(std::uint8_t mask);
    // Sanitized; idle chain: applied at once; otherwise applied after a flush fade (see above).
    void setOrder(const FxOrder& order);
    const FxOrder& order() const { return order_; } // the order in effect
    void setTempo(double bpm, double beatsPerBar);  // for the Delay and the Filter LFO
    // Host position in beats of the first frame of the next process() call, for the Filter LFO. The Filter
    // counts on from there by itself while the chain runs (also while it is skipped), so one call per process()
    // slice is enough. An idle chain passes nothing through the Filter, so its clock pauses.
    void setPosition(double ppq);
    void flush();
    void process(float* l, float* r, int n);
    bool isActive(FxInsert i) const;
    bool anyActive() const;

private:
    void processChunk(float* l, float* r, int n);
    void processUnit(int insert, float* l, float* r, int n);
    float tailOf(int insert) const;
    void sleep(int insert);
    void endFlush();
    // Advances the gate of an insert over n samples, writing the per-sample gain into gateBuf_.
    void fillGate(int insert, int n);

    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
    FxParams params_;
    Bitcrush bitcrush_;
    Filter filter_;
    Flanger flanger_;
    Reverb reverb_;
    Delay delay_;

    FxOrder order_ = defaultFxOrder();
    FxOrder pendingOrder_ = defaultFxOrder();
    bool hasPendingOrder_ = false;

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
