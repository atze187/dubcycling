#pragma once
#include <cstddef>
#include <vector>

namespace dc {

// Dekodierter Stereo-Loop. Beide Kanaele sind gleich lang.
struct StereoData
{
    std::vector<float> left, right;
    double sampleRate = 44100.0;

    size_t frames() const { return left.size(); }
};

} // namespace dc
