#pragma once
#include "engine/FxSettings.h"
#include "engine/fx/Lfo.h"

namespace dc::fx {

// Stereo state variable filter (TPT). Types: 0 low pass, 1 high pass, 2 band pass, 3 notch
// (low + high), 4 peak (low - high), 5 LP 24 (the low pass followed by a second, Butterworth stage).
// A type change while running crossfades the outputs over about 5 ms. A tempo-synced LFO moves the cutoff in the log
// domain; at depth 0 it is skipped entirely (output identical to the filter without LFO).
class Filter
{
public:
    static constexpr int kNumTypes = 6;

    void prepare(double sampleRate);
    void reset();
    void setParams(const FxParams& p);
    // Non-finite or <= 0 values keep the last valid ones (initially 120 BPM, 4 beats per bar).
    void setTempo(double bpm, double beatsPerBar);
    // Host position in beats (quarter notes) of the next sample to render; non-finite is ignored. Without calls the
    // position advances by itself at the last tempo, but only while samples pass through (process/advance).
    void setPosition(double ppq);
    // Moves the beat position on by n samples at the current tempo without rendering (the chain skips a
    // sleeping Filter, its LFO clock still runs).
    void advance(int n);
    void process(float* l, float* r, int n);

private:
    double sampleRate_ = 48000.0;
    float smoothCoeff_ = 0.0f;
    // LFO on the cutoff, in octaves around it (depth 1 = +/-2 octaves), synced to the beat position.
    float slewCoeff_ = 0.0f;                          // one-pole slew of the LFO value (1 ms)
    double bpm_ = 120.0, beatsPerBar_ = 4.0;
    double beatPos_ = 0.0;                            // beat position of the next sample
    int lfoRate_ = 8;                                 // delay time step: cycle length
    LfoShape lfoShape_ = LfoShape::Sine;
    float depthTarget_ = 0.0f, depth_ = 0.0f;         // depth_ smoothed
    float lfoSlewed_ = 0.0f;
    bool modulated_ = false;                          // the coefficients were last computed with the LFO
    float fadeStep_ = 0.0f; // per-sample weight change of the type crossfade
    bool first_ = true;
    int type_ = 0;
    float cutoffTarget_ = 2000.0f, resTarget_ = 0.2f; // cutoff in Hz
    float logCutoff_ = 0.0f, res_ = 0.0f;             // smoothed: ln(Hz), 0..1
    float g_ = 0.0f, k_ = 1.0f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float peakGain_ = 1.0f;                           // peak mode output scale
    float b1_ = 0.0f, b2_ = 0.0f, b3_ = 0.0f;         // LP 24 second stage (shares g_, fixed Q)
    float ic1_[2] = {0.0f, 0.0f}, ic2_[2] = {0.0f, 0.0f};
    float jc1_[2] = {0.0f, 0.0f}, jc2_[2] = {0.0f, 0.0f}; // LP 24 second stage state
    // Type crossfade: one weight per type, each ramps towards 1 (current type) or 0.
    bool fading_ = false;
    float mix_[kNumTypes] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace dc::fx
