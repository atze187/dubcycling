#pragma once
#include <vector>
#include "engine/StereoData.h"

namespace dc {

// Suggests region boundaries at the onsets (transients) of a loop, tuned for drum loops.
// Returns ascending fractions in (0,1): at most maxBoundaries of the strongest onsets, never
// within the first/last 1 % and at least max(kMinRegion, ~40 ms) apart. Silence, loops too short
// to analyse, maxBoundaries <= 0 and non-finite samples (treated as 0) give a valid (maybe empty)
// result. Deterministic; allocates, so call it from the message thread only.
std::vector<double> suggestBoundaries(const StereoData& data, int maxBoundaries);

} // namespace dc
