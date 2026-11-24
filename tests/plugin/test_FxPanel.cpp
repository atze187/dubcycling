// FxPanel: effect list (chain order, per-region switches) and the edit view of the selected effect;
// one publish per change.
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/ui/FxList.h"
#include "plugin/ui/FxPanel.h"
#include "plugin/ui/FxStrip.h"
#include "plugin/ui/PanelLook.h"

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
    std::vector<FxOrder> orders;

    explicit PanelFixture(int w = 340, int h = 190)
    {
        panel.setSize(w, h);
        panel.onParamsChanged = [this](const FxParams& p) { ++paramCalls; last = p; };
        panel.onSwitchChanged = [this](int r, FxInsert i, bool on) { switches.push_back({ r, i, on }); };
        panel.onOrderChanged = [this](const FxOrder& o) { orders.push_back(o); };
    }
};

const juce::String kTimes = juce::String(juce::CharPointer_UTF8("\xc3\x97"));

std::vector<FxParam> paramsOf(FxInsert i)
{
    std::vector<FxParam> out;
    for (int p = 0; p < kFxParamCount; ++p)
        if (fxParamInfo(static_cast<FxParam>(p)).insert == i)
            out.push_back(static_cast<FxParam>(p));
    return out;
}

juce::Point<float> centre(juce::Rectangle<int> r) { return r.toFloat().getCentre(); }

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
    CHECK(f.panel.switchText(FxInsert::Reverb) == "Region C");
    f.panel.setContext(0, 3, { "Intro" }, fx);
    CHECK(f.panel.switchText(FxInsert::Reverb) == "Region Intro");
    f.panel.setContext(0, 0, { "Intro" }, fx);
    CHECK(f.panel.switchText(FxInsert::Reverb) == "no region");
    CHECK(f.paramCalls == 0);
    CHECK(f.switches.empty());
    CHECK(f.orders.empty());
}

TEST_CASE("FxPanel: setContext shows the switch state of the selected region for every effect in the list", "[FxPanel]")
{
    PanelFixture f;
    FxSettings fx;
    fx.order = { FxInsert::Delay, FxInsert::Reverb, FxInsert::Filter, FxInsert::Bitcrush, FxInsert::Flanger };
    for (const auto i : { FxInsert::Bitcrush, FxInsert::Delay, FxInsert::Reverb })
        fx.setSwitch(1, i, true);
    fx.setSwitch(0, FxInsert::Filter, true);
    f.panel.setContext(1, 2, {}, fx);
    CHECK(f.panel.list().order() == fx.order);
    for (int k = 0; k < kFxInserts; ++k)
    {
        const auto ins = static_cast<FxInsert>(k);
        CAPTURE(k);
        CHECK(f.panel.list().switchOn(ins) == fx.switchOn(1, ins));
        CHECK(f.panel.switchOn(ins) == fx.switchOn(1, ins));
        CHECK(f.panel.switchEnabled(ins));
    }
    f.panel.setContext(0, 2, {}, fx);
    CHECK(f.panel.list().switchOn(FxInsert::Filter));
    CHECK_FALSE(f.panel.list().switchOn(FxInsert::Delay));
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

TEST_CASE("FxPanel: a quick switch in the list reports region, insert and state", "[FxPanel]")
{
    PanelFixture f;
    f.panel.setContext(1, 3, {}, FxSettings {});
    auto& list = f.panel.list();
    list.pressAt(centre(list.switchBounds(FxInsert::Delay)));
    list.release();
    REQUIRE(f.switches.size() == 1);
    CHECK(f.switches[0].region == 1);
    CHECK(f.switches[0].insert == FxInsert::Delay);
    CHECK(f.switches[0].on);
    CHECK(f.panel.switchOn(FxInsert::Delay));
    // switchBounds of the panel is the list switch in panel coordinates
    CHECK(f.panel.switchBounds(FxInsert::Delay) == list.switchBounds(FxInsert::Delay) + list.getPosition());
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
    f.panel.setParam(FxParam::FlangerManual, 0.25);
    CHECK(f.panel.paramText(FxParam::FlangerManual) == "25 %");
    f.panel.setParam(FxParam::DelayFeedback, 0.5);
    CHECK(f.panel.paramText(FxParam::DelayFeedback) == "50 %");
    f.panel.setParam(FxParam::DelayReverse, 1.0);
    CHECK(f.panel.paramText(FxParam::DelayReverse) == "On");
    f.panel.setParam(FxParam::DelayReverse, 0.0);
    CHECK(f.panel.paramText(FxParam::DelayReverse) == "Off");
}

TEST_CASE("FxPanel: Delay Time shows note names (\"1/8\") and spans all steps", "[FxPanel]")
{
    PanelFixture f;
    f.panel.setContext(0, 1, {}, FxSettings {});
    CHECK(f.panel.paramText(FxParam::DelayTime) == "1/8"); // default step 5
    for (int k = 0; k < kDelayTimeSteps; ++k)
    {
        CAPTURE(k);
        f.panel.setParam(FxParam::DelayTime, k);
        CHECK(f.panel.param(FxParam::DelayTime) == static_cast<double>(k));
        CHECK(f.panel.paramText(FxParam::DelayTime) == juce::String(delayTimeName(k)));
    }
    f.panel.setParam(FxParam::DelayTime, 99.0);
    CHECK(f.panel.paramText(FxParam::DelayTime) == "2 bars");
    f.panel.setParam(FxParam::DelayTime, -3.0);
    CHECK(f.panel.paramText(FxParam::DelayTime) == "1/32");
    // A drag between steps lands on a step.
    f.panel.beginDrag(FxParam::DelayTime);
    f.panel.dragParam(FxParam::DelayTime, 4.6);
    f.panel.endDrag();
    CHECK(f.panel.param(FxParam::DelayTime) == 5.0);
    CHECK(f.last.get(FxParam::DelayTime) == 5.0f);
}

TEST_CASE("FxPanel: Reverse toggle publishes params once", "[FxPanel]")
{
    PanelFixture f;
    f.panel.setContext(0, 1, {}, FxSettings {});
    f.panel.selectInsert(FxInsert::Delay);
    auto& reverse = f.panel.reverseButton();
    CHECK_FALSE(reverse.getToggleState());
    CHECK(f.panel.getLocalBounds().contains(reverse.getBounds()));
    CHECK(reverse.isVisible());
    reverse.setToggleState(true, juce::sendNotification); // like a click
    CHECK(f.paramCalls == 1);
    CHECK(f.last.get(FxParam::DelayReverse) == 1.0f);
    CHECK(f.panel.param(FxParam::DelayReverse) == 1.0);
    reverse.setToggleState(false, juce::sendNotification);
    CHECK(f.paramCalls == 2);
    CHECK(f.last.get(FxParam::DelayReverse) == 0.0f);
    // The poll shows the state without publishing.
    FxSettings fx;
    fx.params.set(FxParam::DelayReverse, 1.0);
    f.panel.setContext(0, 1, {}, fx);
    CHECK(reverse.getToggleState());
    CHECK(f.paramCalls == 2);
}

TEST_CASE("FxPanel: Flanger Manual is editable and publishes", "[FxPanel]")
{
    PanelFixture f;
    f.panel.setContext(0, 1, {}, FxSettings {}); // Rate 0.3 Hz
    f.panel.selectInsert(FxInsert::Flanger);
    CHECK_FALSE(f.panel.paramBounds(FxParam::FlangerManual).isEmpty());
    CHECK(f.panel.paramDimmed(FxParam::FlangerManual)); // Rate > 0: no effect, dimmed
    f.panel.setParam(FxParam::FlangerManual, 0.2);
    CHECK(f.paramCalls == 1);
    CHECK(f.last.get(FxParam::FlangerManual) == 0.2f);
    f.panel.setParam(FxParam::FlangerRate, 0.0);
    CHECK_FALSE(f.panel.paramDimmed(FxParam::FlangerManual));
    CHECK_FALSE(f.panel.paramDimmed(FxParam::FlangerRate));
    f.panel.beginDrag(FxParam::FlangerManual);
    f.panel.dragParam(FxParam::FlangerManual, 0.9);
    CHECK(f.paramCalls == 2);
    f.panel.endDrag();
    CHECK(f.paramCalls == 3);
    CHECK(f.last.get(FxParam::FlangerManual) == 0.9f);
    CHECK(f.last.get(FxParam::FlangerRate) == 0.0f);
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

TEST_CASE("FxPanel: a slider drag still publishes once at its end", "[FxPanel]")
{
    PanelFixture f;
    f.panel.setContext(0, 1, {}, FxSettings {});
    f.panel.selectInsert(FxInsert::Delay);
    f.panel.beginDrag(FxParam::DelayMix);
    for (double v = 0.31; v < 0.9; v += 0.05)
        f.panel.dragParam(FxParam::DelayMix, v);
    CHECK(f.paramCalls == 0);
    f.panel.endDrag();
    CHECK(f.paramCalls == 1);
    CHECK(f.last.get(FxParam::DelayMix) == static_cast<float>(f.panel.param(FxParam::DelayMix)));
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

TEST_CASE("FxPanel: edit view shows only the selected effect's params", "[FxPanel]")
{
    PanelFixture f;
    FxSettings fx;
    fx.order = { FxInsert::Reverb, FxInsert::Bitcrush, FxInsert::Filter, FxInsert::Flanger, FxInsert::Delay };
    f.panel.setContext(0, 1, {}, fx);
    CHECK(f.panel.selectedInsert() == FxInsert::Reverb); // first effect in the order
    CHECK(f.panel.list().selected() == FxInsert::Reverb);
    CHECK(f.panel.paramBounds(FxParam::DelayTime).isEmpty());
    CHECK_FALSE(f.panel.paramBounds(FxParam::ReverbMix).isEmpty());
    CHECK_FALSE(f.panel.reverseButton().isVisible());

    f.panel.selectInsert(FxInsert::Delay);
    CHECK(f.panel.selectedInsert() == FxInsert::Delay);
    CHECK(f.panel.list().selected() == FxInsert::Delay);
    for (int p = 0; p < kFxParamCount; ++p)
    {
        const auto param = static_cast<FxParam>(p);
        CAPTURE(p);
        CHECK(f.panel.paramBounds(param).isEmpty() == (fxParamInfo(param).insert != FxInsert::Delay));
    }
    CHECK(f.panel.reverseButton().isVisible());

    // A later poll with another order keeps the selection; a click in the list changes it.
    fx.order = defaultFxOrder();
    f.panel.setContext(0, 1, {}, fx);
    CHECK(f.panel.selectedInsert() == FxInsert::Delay);
    auto& list = f.panel.list();
    const auto row = list.rowBounds(1); // Filter
    list.pressAt({ static_cast<float>(row.getCentreX()), static_cast<float>(row.getCentreY()) });
    list.release();
    CHECK(f.panel.selectedInsert() == FxInsert::Filter);
    CHECK_FALSE(f.panel.paramBounds(FxParam::FilterCutoff).isEmpty());
    CHECK(f.panel.paramBounds(FxParam::DelayTime).isEmpty());
    CHECK(f.paramCalls == 0);
}

TEST_CASE("FxPanel: order change from the list publishes onOrderChanged and the strip order input stays consistent", "[FxPanel]")
{
    PanelFixture f;
    FxSettings fx;
    f.panel.setContext(0, 2, {}, fx);
    dc::ui::FxStrip strip;
    strip.setSize(1000, 16);
    RegionMap regions;
    regions.boundaries = { 0.5 };
    strip.setRegions(regions);
    // What the editor will do: store the order and feed it to strip and panel.
    f.panel.onOrderChanged = [&](const FxOrder& o) {
        f.orders.push_back(o);
        fx.order = o;
        strip.setOrder(fx.order);
        f.panel.setContext(0, 2, {}, fx);
    };
    f.panel.list().dragRow(4, 1); // Reverb behind Bitcrush
    REQUIRE(f.orders.size() == 1);
    const FxOrder expected { FxInsert::Bitcrush, FxInsert::Reverb, FxInsert::Filter, FxInsert::Flanger, FxInsert::Delay };
    CHECK(f.orders[0] == expected);
    CHECK(f.panel.list().order() == expected);
    for (int k = 1; k < kFxInserts; ++k)
        CHECK(strip.markerBounds(0, f.panel.list().insertAt(k - 1)).getX()
              < strip.markerBounds(0, f.panel.list().insertAt(k)).getX());
    CHECK(f.panel.selectedInsert() == FxInsert::Bitcrush); // reordering keeps the selection
    CHECK(f.paramCalls == 0);
    CHECK(f.switches.empty());
}

TEST_CASE("FxPanel: controls do not overlap at the minimum size", "[FxPanel]")
{
    for (const auto size : { juce::Point<int> { 300, 190 }, juce::Point<int> { 360, 190 }, juce::Point<int> { 340, 260 } })
    {
        PanelFixture f(size.x, size.y);
        f.panel.setContext(0, 1, {}, FxSettings {});
        for (int i = 0; i < kFxInserts; ++i)
        {
            const auto ins = static_cast<FxInsert>(i);
            f.panel.selectInsert(ins);
            CAPTURE(size.x, size.y, i);
            std::vector<juce::Rectangle<int>> areas;
            for (int k = 0; k < kFxInserts; ++k)
                areas.push_back(f.panel.switchBounds(static_cast<FxInsert>(k)));
            for (const auto p : paramsOf(ins))
                areas.push_back(f.panel.paramBounds(p));
            if (ins == FxInsert::Delay)
                CHECK(f.panel.paramBounds(FxParam::DelayReverse).contains(f.panel.reverseButton().getBounds()));
            const auto listArea = f.panel.list().getBounds();
            CHECK(listArea.getWidth() >= 120);
            CHECK(listArea.getWidth() <= 150);
            for (std::size_t a = 0; a < areas.size(); ++a)
            {
                CHECK_FALSE(areas[a].isEmpty());
                CHECK(f.panel.getLocalBounds().contains(areas[a]));
                if (a >= static_cast<std::size_t>(kFxInserts))
                    CHECK_FALSE(areas[a].intersects(listArea));
                for (std::size_t b = a + 1; b < areas.size(); ++b)
                    CHECK_FALSE(areas[a].intersects(areas[b]));
            }
            // Look at it: DC_SNAPSHOT_DIR=<dir> writes fxpanel_<insert>_<w>x<h>.png.
            if (const auto dir = juce::SystemStats::getEnvironmentVariable("DC_SNAPSHOT_DIR", {}); dir.isNotEmpty())
            {
                dc::ui::PanelLook look;
                f.panel.setLookAndFeel(&look);
                auto shoot = [&](const juce::String& name) {
                    const auto img = f.panel.createComponentSnapshot(f.panel.getLocalBounds(), true, 2.0f);
                    auto png = juce::File(dir).getChildFile(name + "_" + juce::String(size.x) + "x"
                                                            + juce::String(size.y) + ".png");
                    png.deleteFile();
                    if (auto out = png.createOutputStream())
                        juce::PNGImageFormat().writeImageToStream(img, *out);
                };
                shoot("fxpanel_" + juce::String(i));
                if (i == 0)
                {
                    auto& list = f.panel.list();
                    list.pressAt(centre(list.handleBounds(1)));
                    list.dragTo(centre(list.rowBounds(3)).translated(0.0f, 4.0f));
                    shoot("fxpanel_drag");
                    list.release();
                    f.orders.clear();
                }
                f.panel.setLookAndFeel(nullptr);
            }
        }
    }
}
