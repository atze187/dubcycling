// FxPanel: four insert modules, per-region switches, parameter display, one publish per change.
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/ui/FxPanel.h"

using namespace dc;
using dc::ui::FxPanel;

namespace {

struct PanelFixture
{
    juce::ScopedJuceInitialiser_GUI gui;
    FxPanel panel;
    int paramCalls = 0;
    FxParams last;
    struct Sw { int region; FxInsert insert; bool on; };
    std::vector<Sw> switches;

    PanelFixture()
    {
        panel.setSize(440, 190);
        panel.onParamsChanged = [this](const FxParams& p) { ++paramCalls; last = p; };
        panel.onSwitchChanged = [this](int r, FxInsert i, bool on) { switches.push_back({ r, i, on }); };
    }
};

const juce::String kTimes = juce::String(juce::CharPointer_UTF8("\xc3\x97"));

} // namespace

TEST_CASE("FxPanel: setContext shows the switch of the selected region and the knob values", "[FxPanel]")
{
    PanelFixture f;
    FxSettings fx;
    fx.params.set(FxParam::ReverbMix, 0.6);
    fx.params.set(FxParam::FilterType, 2);
    fx.setSwitch(1, FxInsert::Reverb, true);
    fx.setSwitch(2, FxInsert::Bitcrush, true);
    f.panel.setContext(1, 3, { "Intro" }, fx);
    CHECK(f.panel.param(FxParam::ReverbMix) == 0.6f);
    CHECK(f.panel.param(FxParam::FilterType) == 2.0);
    CHECK(f.panel.switchOn(FxInsert::Reverb));
    CHECK_FALSE(f.panel.switchOn(FxInsert::Bitcrush));
    f.panel.setContext(2, 3, { "Intro" }, fx);
    CHECK(f.panel.switchOn(FxInsert::Bitcrush));
    CHECK_FALSE(f.panel.switchOn(FxInsert::Reverb));
    CHECK(f.panel.switchText(FxInsert::Reverb).contains("C"));
    f.panel.setContext(0, 3, { "Intro" }, fx);
    CHECK(f.panel.switchText(FxInsert::Reverb).contains("Intro"));
    CHECK(f.paramCalls == 0);
    CHECK(f.switches.empty());
}

TEST_CASE("FxPanel: setParam publishes the whole parameter set once", "[FxPanel]")
{
    PanelFixture f;
    FxSettings fx;
    fx.params.set(FxParam::FlangerMix, 0.7);
    f.panel.setContext(0, 1, {}, fx);
    f.panel.setParam(FxParam::FilterCutoff, 800.0);
    CHECK(f.paramCalls == 1);
    CHECK(f.last.get(FxParam::FilterCutoff) == 800.0f);
    CHECK(f.last.get(FxParam::FlangerMix) == 0.7f); // the rest rides along
    CHECK(f.panel.param(FxParam::FilterCutoff) == 800.0);
}

TEST_CASE("FxPanel: a switch change reports region, insert and state", "[FxPanel]")
{
    PanelFixture f;
    f.panel.setContext(2, 4, {}, FxSettings {});
    f.panel.setSwitch(FxInsert::Flanger, true);
    REQUIRE(f.switches.size() == 1);
    CHECK(f.switches[0].region == 2);
    CHECK(f.switches[0].insert == FxInsert::Flanger);
    CHECK(f.switches[0].on);
    CHECK(f.panel.switchOn(FxInsert::Flanger));
    f.panel.setSwitch(FxInsert::Flanger, false);
    REQUIRE(f.switches.size() == 2);
    CHECK_FALSE(f.switches[1].on);
    CHECK_FALSE(f.panel.switchOn(FxInsert::Flanger));
    CHECK(f.paramCalls == 0);
}

TEST_CASE("FxPanel: reset restores the default and publishes", "[FxPanel]")
{
    PanelFixture f;
    f.panel.setContext(0, 1, {}, FxSettings {});
    f.panel.setParam(FxParam::BitcrushBits, 3.0);
    f.panel.resetParam(FxParam::BitcrushBits);
    CHECK(f.paramCalls == 2);
    CHECK(f.panel.param(FxParam::BitcrushBits) == fxParamInfo(FxParam::BitcrushBits).def);
    CHECK(f.last.get(FxParam::BitcrushBits) == 8.0f);
}

TEST_CASE("FxPanel: paramText shows percent, Hz, bits and factor", "[FxPanel]")
{
    PanelFixture f;
    f.panel.setContext(0, 1, {}, FxSettings {});
    f.panel.setParam(FxParam::ReverbMix, 0.3);
    CHECK(f.panel.paramText(FxParam::ReverbMix) == "30 %");
    f.panel.setParam(FxParam::FilterCutoff, 2000.0);
    CHECK(f.panel.paramText(FxParam::FilterCutoff) == "2.0 kHz");
    f.panel.setParam(FxParam::FilterCutoff, 200.0);
    CHECK(f.panel.paramText(FxParam::FilterCutoff) == "200 Hz");
    f.panel.setParam(FxParam::BitcrushBits, 8.0);
    CHECK(f.panel.paramText(FxParam::BitcrushBits) == "8");
    f.panel.setParam(FxParam::BitcrushRate, 4.0);
    CHECK(f.panel.paramText(FxParam::BitcrushRate) == kTimes + "4");
    f.panel.setParam(FxParam::FlangerRate, 0.3);
    CHECK(f.panel.paramText(FxParam::FlangerRate) == "0.30 Hz");
    f.panel.setParam(FxParam::FlangerFeedback, 0.4);
    CHECK(f.panel.paramText(FxParam::FlangerFeedback) == "40 %");
    f.panel.setParam(FxParam::FlangerFeedback, -0.4);
    CHECK(f.panel.paramText(FxParam::FlangerFeedback) == "-40 %");
    f.panel.setParam(FxParam::FilterType, 1.0);
    CHECK(f.panel.paramText(FxParam::FilterType) == "HP");
}

TEST_CASE("FxPanel: switches are disabled without regions", "[FxPanel]")
{
    PanelFixture f;
    f.panel.setContext(0, 0, {}, FxSettings {});
    for (int i = 0; i < kFxInserts; ++i)
        CHECK_FALSE(f.panel.switchEnabled(static_cast<FxInsert>(i)));
    f.panel.setSwitch(FxInsert::Reverb, true);
    CHECK(f.switches.empty());
    f.panel.setContext(0, 2, {}, FxSettings {});
    CHECK(f.panel.switchEnabled(FxInsert::Reverb));
}

TEST_CASE("FxPanel: setContext during a drag keeps the dragged value", "[FxPanel]")
{
    PanelFixture f;
    f.panel.setContext(0, 1, {}, FxSettings {});
    f.panel.beginDrag(FxParam::ReverbSize);
    f.panel.dragParam(FxParam::ReverbSize, 0.9);
    f.panel.dragParam(FxParam::ReverbSize, 0.8);
    CHECK(f.paramCalls == 0);
    CHECK(f.panel.paramText(FxParam::ReverbSize) == "80 %");
    f.panel.setContext(0, 1, {}, FxSettings {}); // the editor's poll
    CHECK(f.panel.param(FxParam::ReverbSize) == 0.8f);
    f.panel.endDrag();
    CHECK(f.paramCalls == 1);
    CHECK(f.last.get(FxParam::ReverbSize) == 0.8f);
    f.panel.beginDrag(FxParam::ReverbSize);
    f.panel.endDrag(); // nothing moved
    CHECK(f.paramCalls == 1);
}

TEST_CASE("FxPanel: out-of-range values are clamped by setParam", "[FxPanel]")
{
    PanelFixture f;
    f.panel.setContext(0, 1, {}, FxSettings {});
    f.panel.setParam(FxParam::ReverbMix, 7.0);
    CHECK(f.panel.param(FxParam::ReverbMix) == 1.0);
    f.panel.setParam(FxParam::FilterCutoff, 1.0);
    CHECK(f.panel.param(FxParam::FilterCutoff) == 20.0);
    f.panel.setParam(FxParam::FlangerFeedback, -5.0);
    CHECK(f.panel.param(FxParam::FlangerFeedback) == -0.9f);
    CHECK(f.last.get(FxParam::FlangerFeedback) == -0.9f);
}

TEST_CASE("FxPanel: controls do not overlap at the minimum size", "[FxPanel]")
{
    PanelFixture f;
    f.panel.setContext(0, 1, {}, FxSettings {});
    std::vector<juce::Rectangle<int>> areas;
    for (int i = 0; i < kFxInserts; ++i)
        areas.push_back(f.panel.switchBounds(static_cast<FxInsert>(i)));
    for (int p = 0; p < kFxParamCount; ++p)
        areas.push_back(f.panel.paramBounds(static_cast<FxParam>(p)));
    for (std::size_t a = 0; a < areas.size(); ++a)
    {
        CHECK_FALSE(areas[a].isEmpty());
        CHECK(f.panel.getLocalBounds().contains(areas[a]));
        for (std::size_t b = a + 1; b < areas.size(); ++b)
            CHECK_FALSE(areas[a].intersects(areas[b]));
    }
}
