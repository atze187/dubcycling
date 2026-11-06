#include "plugin/ui/PanelLook.h"
#include <algorithm>
#include <cmath>

namespace dc::ui {

namespace panel {

juce::FontOptions mono(float height, bool bold)
{
    return juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height,
                             bold ? juce::Font::bold : juce::Font::plain);
}

void styleLcd(juce::Label& label, juce::Colour text, juce::Justification justification)
{
    label.setFont(mono(11.0f));
    label.setJustificationType(justification);
    label.setBorderSize(juce::BorderSize<int>(0, 6, 0, 6));
    label.setColour(juce::Label::textColourId, text);
    label.setColour(juce::Label::backgroundColourId, kLcd);
    label.setColour(juce::Label::outlineColourId, kEdge);
}

} // namespace panel

PanelLook::PanelLook()
{
    using namespace panel;
    setColour(juce::TextButton::buttonColourId, kButton);
    setColour(juce::TextButton::textColourOffId, kText);
    setColour(juce::TextButton::textColourOnId, kText);
    setColour(juce::Slider::textBoxTextColourId, kAmber);
    setColour(juce::Slider::textBoxBackgroundColourId, kLcd);
    setColour(juce::Slider::textBoxOutlineColourId, kEdge);
    setColour(juce::Slider::textBoxHighlightColourId, kAccent);
}

void PanelLook::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& background,
                                     bool highlighted, bool down)
{
    auto fill = background;
    if (!b.isEnabled())
        fill = fill.withAlpha(0.5f);
    else if (down)
        fill = fill.darker(0.3f);
    else if (highlighted)
        fill = fill.brighter(0.15f);
    const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 2.0f);
    g.setColour(panel::kEdgeLight);
    g.drawRoundedRectangle(r, 2.0f, 1.0f);
}

juce::Font PanelLook::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return juce::Font(panel::mono(std::min(11.0f, static_cast<float>(buttonHeight) * 0.6f)));
}

void PanelLook::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                 float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                 juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }
    constexpr float kSeg = 5.0f;
    constexpr float kGap = 1.0f;
    constexpr float kBar = 8.0f;
    const float left = static_cast<float>(x);
    const float right = left + static_cast<float>(width);
    const float top = static_cast<float>(y) + (static_cast<float>(height) - kBar) * 0.5f;
    const float filled = left + static_cast<float>(slider.valueToProportionOfLength(slider.getValue())) * (right - left);
    const auto on = slider.findColour(juce::Slider::trackColourId);
    const auto off = slider.findColour(juce::Slider::backgroundColourId);
    const bool bipolar = static_cast<bool>(slider.getProperties()[kBipolar]);
    const float centre = left + 0.5f * (right - left);
    const float litFrom = bipolar ? std::min(centre, filled) : left;
    const float litTo = bipolar ? std::max(centre, filled) : filled;
    for (float sx = left; sx + kSeg <= right + 0.01f; sx += kSeg + kGap)
    {
        const float mid = sx + kSeg * 0.5f;
        g.setColour(mid >= litFrom && mid <= litTo ? on : off);
        g.fillRect(sx, top, kSeg, kBar);
    }
    if (bipolar)
    {
        g.setColour(slider.findColour(juce::Slider::thumbColourId).withAlpha(0.6f));
        g.fillRect(std::round(centre) - 0.5f, top - 2.0f, 1.0f, kBar + 4.0f);
        g.setColour(slider.findColour(juce::Slider::thumbColourId));
        g.fillRect(std::clamp(filled, left + 1.0f, right - 1.0f) - 1.0f, top - 2.0f, 2.0f, kBar + 4.0f);
    }
}

} // namespace dc::ui
