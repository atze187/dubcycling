#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "engine/Engine.h"

namespace dc {

// Maps the host position to the Engine's TransportInfo. A missing position or BPM gives
// playing = false, so the Engine renders silence. A missing time signature defaults to 4/4.
// A missing PPQ position is derived from timeInSeconds * bpm / 60 (assumes a constant tempo, so
// a tempo-automated project drifts); with neither PPQ nor seconds the result stays invalid.
TransportInfo toTransportInfo(const juce::Optional<juce::AudioPlayHead::PositionInfo>& position);

} // namespace dc
