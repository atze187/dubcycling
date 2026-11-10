#pragma once
#include <vector>
#include <juce_graphics/juce_graphics.h>
#include "engine/Pass.h"

namespace dc::ui {

// Colours of the eight pass pads (PassPanel) and the Loop-2 region strip drawn on a filled pad.
namespace pads {

// The pad's own colour: index 0..7 (slot 1..8) is red, amber, green, aqua, blue, violet, magenta,
// orange. Out-of-range indices wrap.
juce::Colour padColour(int slot);
// Text (number, note, clear x and strip) on a playing pad, which is filled with padColour.
juce::Colour padPlayingText(int slot);
// Fill of a filled pad at rest: a dim version of padColour on the LCD black.
juce::Colour padRestingFill(int slot);

// WCAG 2.x relative luminance (0..1) and contrast ratio (1..21).
float relativeLuminance(juce::Colour c);
float contrastRatio(juce::Colour a, juce::Colour b);

// One Loop-2 region that sounds within a pass: from `start` to `end` (Loop-1 phase, 0..1).
struct StripSegment
{
    float start = 0.0f;
    float end = 0.0f;
    int region2 = -1;
    bool operator==(const StripSegment&) const = default;
};

// The pass's Loop-2 sequence as segments in phase order: each Start sounds from its phase to the
// next event (a Start or a Stop), the last one to the pass end (1.0). Before the first Start, after
// a Stop and between a Stop and the next Start nothing sounds (gaps). Events are sanitized first
// (sanitizePass: non-finite phases dropped, phases clamped to 0..1, sorted); a Start followed by an
// event at the same phase has no width and is left out.
std::vector<StripSegment> passStripSegments(const Pass& pass);

} // namespace pads
} // namespace dc::ui
