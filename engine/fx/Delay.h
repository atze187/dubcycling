#pragma once
#include <vector>
#include "engine/FxSettings.h"

namespace dc::fx {

// Stereo tempo-synced feedback delay with a reverse mode. process() replaces the input by the
// pure wet signal (scaled by Mix, no dry part).
//
// Both modes share one recording per channel: w[t] = in[t] + feedback * w[t - D] (D = delay time),
// so the feedback always re-feeds the forward block. Only the output tap differs:
//  - Reverse off: out = w[t - D], a normal delay.
//  - Reverse on: the timeline is cut into blocks of D samples (the length is latched at each block
//    start). During a block the previous block is read backwards: at sample j of the block the
//    reader returns w[start - 1 - j]. Consecutive reversed blocks overlap by a short crossfade
//    (smoothstep, at most 10 ms and a quarter block): the old reader keeps running backwards for the
//    crossfade length while the new one fades in. Since the feedback recirculates the forward
//    recording, every repeat plays reversed; the first echo starts one delay time after its block.
// Delay time changes (step or tempo) crossfade between the old and the new tap (30 ms); toggling
// Reverse crossfades the two outputs (20 ms). All gains sum to 1, so |out| <= max |w|.
// Buffers are sized in prepare() (30 s per channel plus the reverse crossfade margin); a reversed
// block needs twice its length of history, so the reverse block length is clamped to half the buffer.
// reset() is O(1): samples older than the last reset are read as silence.
class Delay
{
public:
    void prepare(double sampleRate);
    void reset();
    void setParams(const FxParams& p);
    void setTempo(double bpm, double beatsPerBar); // non-finite or <= 0 values keep the last (initial 120, 4)
    void process(float* l, float* r, int n);
    float tailLevel() const { return tail_ > held_ ? tail_ : held_; }

    const float* bufferData(int channel) const { return buf_[channel & 1].data(); } // for tests

private:
    struct Reader
    {
        int j = 0, len = 0;
        bool active = false;
    };

    void updateTarget();
    float read(const std::vector<float>& b, int age) const;
    void startBlock();

    double sampleRate_ = 48000.0;
    float smoothCoeff_ = 0.0f, tailRelease_ = 0.0f, modeStep_ = 0.0f;
    int tapFadeLen_ = 1, revFadeMax_ = 1, maxTap_ = 1, maxRevLen_ = 1;
    bool fresh_ = true; // nothing processed since prepare/reset: parameter changes jump

    double bpm_ = 120.0, beatsPerBar_ = 4.0;
    int step_ = 5;
    float fbTarget_ = 0.4f, mixTarget_ = 0.3f, revTarget_ = 0.0f;
    float fb_ = 0.4f, mix_ = 0.3f, rev_ = 0.0f; // smoothed

    int tapTarget_ = 1, tapA_ = 1, tapB_ = 1, tapPos_ = 0; // forward tap ages in samples
    bool tapFading_ = false;                                // crossfading tapA_ -> tapB_
    Reader cur_, old_;
    int curFade_ = 1;

    std::vector<float> buf_[2];
    int size_ = 0, write_ = 0, written_ = 0; // written_: samples since reset (capped at size_)
    float tail_ = 0.0f, held_ = 0.0f;        // held_: peak still in the buffer within reach of a tap
    int loudAge_ = 0;
};

} // namespace dc::fx
