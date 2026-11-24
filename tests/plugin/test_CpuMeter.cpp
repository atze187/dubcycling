#include <catch2/catch_test_macros.hpp>
#include <limits>
#include "plugin/ui/CpuMeter.h"
#include "plugin/ui/PanelLook.h"

using namespace dc::ui;

namespace {
juce::Colour textColour(const CpuMeter& m) { return m.findColour(juce::Label::textColourId); }
} // namespace

TEST_CASE("cpu meter shows whole percent and warns by colour", "[editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    CpuMeter m;
    CHECK(m.getText() == "CPU 0 %");

    m.setLoad(0.042);
    CHECK(m.getText() == "CPU 4 %");
    CHECK(textColour(m) == panel::kDim);

    m.setLoad(0.5);
    CHECK(m.getText() == "CPU 50 %");
    CHECK(textColour(m) == panel::kAmber);

    m.setLoad(0.8);
    CHECK(textColour(m) == panel::kWarn);

    m.setLoad(1.7);
    CHECK(m.getText() == "CPU 170 %");
    CHECK(textColour(m) == panel::kWarn);
}

TEST_CASE("cpu meter treats invalid loads as zero", "[editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    CpuMeter m;
    m.setLoad(0.3);
    m.setLoad(-0.5);
    CHECK(m.getText() == "CPU 0 %");
    m.setLoad(0.3);
    m.setLoad(std::numeric_limits<double>::quiet_NaN());
    CHECK(m.getText() == "CPU 0 %");
    CHECK(textColour(m) == panel::kDim);
}

TEST_CASE("cpu meter looks like an LCD readout", "[editor][look]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    CpuMeter m;
    CHECK(m.findColour(juce::Label::backgroundColourId) == panel::kLcd);
    CHECK(m.findColour(juce::Label::outlineColourId) == panel::kEdge);
    CHECK(m.getJustificationType() == juce::Justification::centred);
}
