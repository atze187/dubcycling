#pragma once
#include <cstddef>
#include <vector>

namespace dc {

// Decoded stereo loop. Both channels have the same length.
struct StereoData
{
    std::vector<float> left, right;
    double sampleRate = 44100.0;

    size_t frames() const { return left.size(); }
};

} // namespace dc
