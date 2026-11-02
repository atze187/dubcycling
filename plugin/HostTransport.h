#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "engine/Engine.h"

namespace dc {

// Maps the host position to the Engine's TransportInfo. A missing position or a missing field
// (PPQ position, BPM, time signature) gives playing = false, so the Engine renders silence.
TransportInfo toTransportInfo(const juce::Optional<juce::AudioPlayHead::PositionInfo>& position);

} // namespace dc
