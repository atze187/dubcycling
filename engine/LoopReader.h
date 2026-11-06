#pragma once
#include <memory>
#include "engine/GrainStretch.h"
#include "engine/StereoData.h"

namespace dc {

// Plays a stereo loop phase-locked: the position in the loop comes from the caller (host phase),
// the pitch is preserved by the grain stretch.
class LoopReader
{
public:
    static constexpr double kGrainMs = 40.0;

    // Output sample rate (default 44100); determines grain length and read ratio. Resets.
    void prepare(double outSampleRate);
    // New loop; resets. Do not call concurrently with render.
    void setData(std::shared_ptr<const StereoData> data);
    void reset();
    // phase01: position in the loop at the first frame (wrapped to [0,1)), phaseStep01: increment per frame,
    // step: source frames per output frame from sourceStep. Invalid values or no data give zeros.
    // Real-time safe (no allocation).
    void render(float* l, float* r, int n, double phase01, double phaseStep01, double step);

    int grainLength() const { return grainLen_; }

private:
    std::shared_ptr<const StereoData> data_;
    GrainStretch left_, right_;
    double outRate_ = 44100.0;
    int grainLen_ = 1764;
    bool started_ = false;
    double expectedPhase_ = 0.0;
    double lastPhaseStep_ = 0.0;
};

} // namespace dc
