#include "plugin/ui/PadLook.h"
#include <algorithm>
#include <array>
#include <cmath>
#include "plugin/ui/PanelLook.h"

namespace dc::ui::pads {

namespace {
// Distinct hues around the wheel, bright enough for dark text on a playing pad (contrast >= 4.5).
const std::array<juce::Colour, kPassSlots> kPalette {
    juce::Colour { 0xffef5b5b }, // red
    juce::Colour { 0xffffc94a }, // amber
    juce::Colour { 0xff7fe3a0 }, // green (panel::kGreen)
    juce::Colour { 0xff4fd8d0 }, // aqua
    juce::Colour { 0xff5aa9e6 }, // blue (panel::kAccent)
    juce::Colour { 0xffa98bf0 }, // violet
    juce::Colour { 0xffe36fc0 }, // magenta
    juce::Colour { 0xffff8a3d }, // orange
};
constexpr float kRestingAmount = 0.30f; // share of the pad colour in the resting fill

int wrap(int slot) { return ((slot % kPassSlots) + kPassSlots) % kPassSlots; }

float linear(juce::uint8 channel)
{
    const float c = static_cast<float>(channel) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
} // namespace

juce::Colour padColour(int slot) { return kPalette[static_cast<std::size_t>(wrap(slot))]; }

juce::Colour padPlayingText(int) { return panel::kLcd; }

juce::Colour padRestingFill(int slot) { return panel::kLcd.interpolatedWith(padColour(slot), kRestingAmount); }

float relativeLuminance(juce::Colour c)
{
    return 0.2126f * linear(c.getRed()) + 0.7152f * linear(c.getGreen()) + 0.0722f * linear(c.getBlue());
}

float contrastRatio(juce::Colour a, juce::Colour b)
{
    const float la = relativeLuminance(a);
    const float lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

std::vector<StripSegment> passStripSegments(const Pass& pass)
{
    const Pass p = sanitizePass(pass);
    std::vector<StripSegment> out;
    for (std::size_t i = 0; i < p.events.size(); ++i)
    {
        const auto& e = p.events[i];
        if (e.stop)
            continue;
        const double end = i + 1 < p.events.size() ? p.events[i + 1].phase : 1.0;
        if (end > e.phase)
            out.push_back({ static_cast<float>(e.phase), static_cast<float>(end), e.region2 });
    }
    return out;
}

} // namespace dc::ui::pads
