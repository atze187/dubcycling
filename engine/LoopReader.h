#pragma once
#include <memory>
#include "engine/GrainStretch.h"
#include "engine/StereoData.h"

namespace dc {

// Spielt einen Stereo-Loop phasengekoppelt ab: Die Position im Loop kommt vom Aufrufer (Host-Phase),
// die Tonhöhe bleibt durch den Grain-Stretch erhalten.
class LoopReader
{
public:
    static constexpr double kGrainMs = 40.0;

    // Ausgabe-Samplerate (Standard 44100); bestimmt Grain-Länge und Leseverhältnis. Setzt zurück.
    void prepare(double outSampleRate);
    // Neuer Loop; setzt zurück. Nicht gleichzeitig mit render aufrufen.
    void setData(std::shared_ptr<const StereoData> data);
    void reset();
    // phase01: Position im Loop am ersten Frame (wird nach [0,1) gewrappt), phaseStep01: Zuwachs pro Frame,
    // step: Quell-Frames pro Ausgabe-Frame aus sourceStep. Ungültige Werte oder keine Daten ergeben Nullen.
    // Echtzeitsicher (keine Allokation).
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
