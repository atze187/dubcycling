// RulePanel: one row per Loop-2 region, capped weights, remainder as silence, stale entries kept.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <iterator>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"
#include "plugin/ui/Editor.h"
#include "plugin/ui/PanelLook.h"
#include "plugin/ui/RulePanel.h"
#include "tests/plugin/TestLoops.h"

using namespace dc;
using dc::testing::LoopPair;
using dc::ui::RulePanel;

namespace {

struct Published
{
    int calls = 0;
    int region1 = -1;
    std::vector<RuleTarget> targets;
};

void record(RulePanel& p, Published& out)
{
    p.onChanged = [&out](int r1, std::vector<RuleTarget> t) {
        ++out.calls;
        out.region1 = r1;
        out.targets = std::move(t);
    };
}

float pct(float p) { return p / 100.0f; }

// Children are found by component ID: "row<i>" in the panel, "name", "weight", "percent", "mode",
// "mix", "mixValue" inside a row.
juce::Component* findById(juce::Component& root, const juce::String& id)
{
    for (auto* c : root.getChildren())
    {
        if (c->getComponentID() == id)
            return c;
        if (auto* found = findById(*c, id))
            return found;
    }
    return nullptr;
}

juce::Component& control(RulePanel& p, int row, const char* id)
{
    auto* r = findById(p, "row" + juce::String(row));
    REQUIRE(r != nullptr);
    auto* c = findById(*r, id);
    REQUIRE(c != nullptr);
    return *c;
}

} // namespace

TEST_CASE("RulePanel: rows match the region 2 count", "[RulePanel]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    p.setContext(0, 3, {});
    CHECK(p.rowCount() == 3);
    CHECK(p.rowLabel(0) == "A");
    CHECK(p.rowLabel(2) == "C");
    p.setContext(0, 28, {});
    CHECK(p.rowCount() == 28);
    CHECK(p.rowLabel(25) == "Z");
    CHECK(p.rowLabel(26) == "AA");
    CHECK(p.rowLabel(27) == "AB");
    p.setContext(0, 2, { RuleTarget { 1, 0.4f, PlayMode::OneShot } });
    CHECK(p.rowCount() == 2);
    CHECK(p.weightPercent(0) == 0.0f);
    CHECK(p.weightPercent(1) == 40.0f);
    CHECK(p.mode(1) == PlayMode::OneShot);
    CHECK(p.mode(0) == PlayMode::Gate);
}

TEST_CASE("RulePanel: setWeight caps at the remaining percent", "[RulePanel]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(0, 3, { RuleTarget { 0, 0.5f }, RuleTarget { 1, 0.3f } });
    p.setWeight(2, 60.0f); // only 20 % left
    CHECK(p.weightPercent(2) == 20.0f);
    CHECK(p.weightPercent(0) == 50.0f);
    CHECK(p.weightPercent(1) == 30.0f);
    p.setWeight(0, 100.0f); // 100 - 30 - 20 = 50
    CHECK(p.weightPercent(0) == 50.0f);
    p.setWeight(0, -5.0f);
    CHECK(p.weightPercent(0) == 0.0f);
    p.setWeight(0, std::nanf(""));
    CHECK(p.weightPercent(0) == 0.0f);
    CHECK(p.weightPercent(1) == 30.0f);
    CHECK(p.weightPercent(2) == 20.0f);
}

TEST_CASE("RulePanel: the silence label shows the rounded remainder", "[RulePanel]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    p.setContext(0, 3, {});
    CHECK(p.silenceText() == "Silence: 100 %");
    p.setWeight(0, 70.0f);
    CHECK(p.silenceText() == "Silence: 30 %");
    CHECK(p.percentText(0) == "70 %");
    p.setWeight(1, 33.3f);
    p.setWeight(2, 33.3f);
    CHECK(p.silenceText() == "Silence: 0 %"); // 70 + 30 (capped) leaves nothing
    p.setContext(0, 3, { RuleTarget { 0, 0.7f } });
    CHECK(p.silenceText() == "Silence: 30 %");
    p.setContext(0, 3, { RuleTarget { 0, 0.333f }, RuleTarget { 1, 0.333f } });
    CHECK(p.silenceText() == "Silence: 33 %");
}

TEST_CASE("RulePanel: setMode only changes that row", "[RulePanel]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(0, 2, { RuleTarget { 0, 0.5f }, RuleTarget { 1, 0.25f } });
    p.setMode(1, PlayMode::OneShot);
    CHECK(p.mode(0) == PlayMode::Gate);
    CHECK(p.mode(1) == PlayMode::OneShot);
    CHECK(p.weightPercent(1) == 25.0f);
    CHECK(out.calls == 1);
    REQUIRE(out.targets.size() == 2);
    CHECK(out.targets[0].mode == PlayMode::Gate);
    CHECK(out.targets[1].mode == PlayMode::OneShot);
    CHECK(out.targets[1].weight == pct(25.0f));
}

TEST_CASE("RulePanel: onChanged fires with the updated targets", "[RulePanel]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(2, 3, {});
    p.setWeight(1, 70.0f);
    CHECK(out.calls == 1);
    CHECK(out.region1 == 2);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].region2 == 1);
    CHECK(out.targets[0].weight == pct(70.0f));
    CHECK(out.targets[0].mode == PlayMode::Gate);
    p.setWeight(0, 20.0f);
    p.setMode(0, PlayMode::OneShot);
    CHECK(out.calls == 3);
    REQUIRE(out.targets.size() == 2);
    CHECK(out.targets[0].region2 == 1);
    CHECK(out.targets[1].region2 == 0);
    CHECK(out.targets[1].weight == pct(20.0f));
    CHECK(out.targets[1].mode == PlayMode::OneShot);
}

TEST_CASE("RulePanel: weight 0 keeps its target (and mode) stored with weight 0", "[RulePanel]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(0, 2, {});
    p.setMode(0, PlayMode::OneShot); // untouched row: stored with weight 0
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].weight == 0.0f);
    CHECK(out.targets[0].mode == PlayMode::OneShot);
    p.setWeight(0, 40.0f);
    CHECK(out.targets[0].mode == PlayMode::OneShot);
    p.setWeight(0, 0.0f);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].weight == 0.0f);
    CHECK(out.targets[0].mode == PlayMode::OneShot);
}

TEST_CASE("RulePanel: stale entries are preserved when other rows are edited", "[RulePanel]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    // Loop 2 has 2 regions now; the rule still points at region 4 (boundaries were removed).
    p.setContext(0, 2, { RuleTarget { 4, 0.2f, PlayMode::OneShot }, RuleTarget { 1, 0.3f } });
    CHECK(p.rowCount() == 2);
    CHECK(p.silenceText() == "Silence: 50 %"); // the stale 20 % still counts
    p.setWeight(0, 90.0f);                     // capped at 100 - 20 - 30
    CHECK(p.weightPercent(0) == 50.0f);
    REQUIRE(out.targets.size() == 3);
    CHECK(out.targets[0].region2 == 4);
    CHECK(out.targets[0].weight == 0.2f);
    CHECK(out.targets[0].mode == PlayMode::OneShot);
    CHECK(out.targets[1].region2 == 1);
    CHECK(out.targets[2].region2 == 0);
}

TEST_CASE("RulePanel: a slider drag publishes only at the drag end", "[RulePanel]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(0, 2, { RuleTarget { 1, 0.3f } });
    p.beginDrag(0);
    p.dragWeight(0, 10.0f);
    p.dragWeight(0, 40.0f);
    p.dragWeight(0, 90.0f); // capped at 70
    CHECK(out.calls == 0);
    CHECK(p.weightPercent(0) == 70.0f);
    CHECK(p.percentText(0) == "70 %");
    CHECK(p.silenceText() == "Silence: 0 %");
    p.endDrag();
    CHECK(out.calls == 1);
    REQUIRE(out.targets.size() == 2);
    CHECK(out.targets[1].region2 == 0);
    CHECK(out.targets[1].weight == pct(70.0f));
    p.beginDrag(0);
    p.endDrag(); // nothing moved: nothing published
    CHECK(out.calls == 1);
}

TEST_CASE("RulePanel: a context refresh does not clobber a drag in progress", "[RulePanel]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(0, 2, {});
    p.beginDrag(0);
    p.dragWeight(0, 55.0f);
    p.setContext(0, 2, {}); // the editor's 10 Hz poll
    CHECK(p.weightPercent(0) == 55.0f);
    p.endDrag();
    CHECK(out.targets.size() == 1);
}

TEST_CASE("Editor: selecting a Loop 1 region shows its rule and edits reach the state", "[RulePanel][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    DubcyclingProcessor proc;
    LoopPair loops(proc);
    auto s = proc.getPluginState();
    s.loop1.boundaries = { 0.25, 0.5, 0.75 };
    s.loop2.boundaries = { 0.5 };
    s.rules = { RuleEntry { 2, { RuleTarget { 1, 0.6f, PlayMode::OneShot } } },
                RuleEntry { 7, { RuleTarget { 0, 0.1f } } } }; // stale region1
    proc.applyEdit(s);

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto* editor = dynamic_cast<dc::ui::Editor*>(ed.get());
    REQUIRE(editor != nullptr);
    editor->refresh();
    CHECK(editor->rulePanel().rowCount() == 2);
    CHECK(editor->rulePanel().region1() == 0);
    CHECK(editor->rulePanel().weightPercent(1) == 0.0f);

    editor->lane(0).setSelectedRegion(2);
    editor->refresh();
    CHECK(editor->selectedRegion1() == 2);
    CHECK(editor->rulePanel().region1() == 2);
    CHECK(editor->rulePanel().weightPercent(1) == 60.0f);
    CHECK(editor->rulePanel().mode(1) == PlayMode::OneShot);

    editor->rulePanel().setWeight(0, 30.0f);
    const auto after = proc.getPluginState();
    REQUIRE(after.rules.size() == 2);
    CHECK(after.rules[0].region1 == 2);
    REQUIRE(after.rules[0].targets.size() == 2);
    CHECK(after.rules[0].targets[0].region2 == 1);
    CHECK(after.rules[0].targets[1].region2 == 0);
    CHECK(after.rules[0].targets[1].weight == pct(30.0f));
    CHECK(after.rules[1].region1 == 7); // the stale entry survives
    CHECK(after.rules[1].targets.size() == 1);

    // an entry for a region without rules is appended
    editor->lane(0).setSelectedRegion(3);
    editor->refresh();
    editor->rulePanel().setWeight(1, 100.0f);
    const auto added = proc.getPluginState();
    REQUIRE(added.rules.size() == 3);
    CHECK(added.rules[2].region1 == 3);
    CHECK(added.rules[2].targets[0].region2 == 1);
}

TEST_CASE("Editor: a selected region that disappears falls back to the last valid one", "[RulePanel][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    DubcyclingProcessor proc;
    LoopPair loops(proc);
    auto s = proc.getPluginState();
    s.loop1.boundaries = { 0.25, 0.5, 0.75 };
    s.loop2.boundaries = { 0.5 };
    proc.applyEdit(s);
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto* editor = dynamic_cast<dc::ui::Editor*>(ed.get());
    REQUIRE(editor != nullptr);
    editor->lane(0).setSelectedRegion(3);
    editor->refresh();
    CHECK(editor->rulePanel().region1() == 3);

    s.loop1.boundaries = { 0.25 };
    proc.applyEdit(s);
    editor->refresh();
    CHECK(editor->selectedRegion1() == 1);
    CHECK(editor->rulePanel().region1() == 1);

    s.loop2.boundaries = {};
    proc.applyEdit(s);
    editor->refresh();
    CHECK(editor->rulePanel().rowCount() == 1);
}

TEST_CASE("Editor: renders the rule panel with a loaded loop pair", "[RulePanel][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dcrule", "", false);
    dir.createDirectory();
    auto makeWav = [&dir](const juce::String& name, float freq) {
        const auto file = dir.getChildFile(name);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream>(file);
        auto writer = wav.createWriterFor(out, juce::AudioFormatWriterOptions {}
                                                   .withSampleRate(8000)
                                                   .withNumChannels(2)
                                                   .withBitsPerSample(16));
        REQUIRE(writer != nullptr);
        juce::AudioBuffer<float> buf(2, 16000);
        for (int i = 0; i < 16000; ++i)
        {
            const float env = 1.0f - static_cast<float>(i % 4000) / 4000.0f;
            const float v = 0.8f * env * std::sin(static_cast<float>(i) * freq);
            buf.setSample(0, i, v);
            buf.setSample(1, i, v);
        }
        REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, 16000));
        return file;
    };
    DubcyclingProcessor proc;
    proc.replaceLoop(0, makeWav("a.wav", 0.05f), 1);
    proc.replaceLoop(1, makeWav("b.wav", 0.11f), 1);
    proc.waitForLoads();
    auto s = proc.getPluginState();
    s.loop1.boundaries = { 0.25, 0.5, 0.75 };
    s.loop2.boundaries = { 0.25, 0.5, 0.75 };
    s.rules = { RuleEntry { 1, { RuleTarget { 0, 0.5f, PlayMode::Gate }, RuleTarget { 2, 0.2f, PlayMode::OneShot } } } };
    proc.applyEdit(s);

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto* editor = dynamic_cast<dc::ui::Editor*>(ed.get());
    REQUIRE(editor != nullptr);
    editor->lane(0).setSelectedRegion(1);
    editor->refresh();
    CHECK(editor->rulePanel().rowCount() == 4);
    CHECK(editor->rulePanel().silenceText() == "Silence: 30 %");

    auto img = ed->createComponentSnapshot(ed->getLocalBounds());
    CHECK(img.isValid());
    const auto snapDir = juce::SystemStats::getEnvironmentVariable("DC_SNAPSHOT_DIR", {});
    if (snapDir.isNotEmpty())
    {
        auto png = juce::File(snapDir).getChildFile("editor_rules.png");
        png.deleteFile();
        if (auto out = png.createOutputStream())
            juce::PNGImageFormat().writeImageToStream(img, *out);
    }
    ed.reset();
    dir.deleteRecursively();
}

TEST_CASE("RulePanel: the stale hint shows the exact percent and is hidden otherwise", "[RulePanel]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    p.setContext(0, 2, { RuleTarget { 0, 0.5f } });
    CHECK(p.staleText().isEmpty());
    p.setContext(0, 2, { RuleTarget { 0, 0.5f }, RuleTarget { 4, 0.2f }, RuleTarget { 5, 0.15f } });
    CHECK(p.staleText() == "35 % of it on removed regions");
    CHECK(p.silenceText() == "Silence: 15 %");
    p.setContext(0, 6, { RuleTarget { 0, 0.5f }, RuleTarget { 4, 0.2f }, RuleTarget { 5, 0.15f } });
    CHECK(p.staleText().isEmpty()); // the regions exist again
}

TEST_CASE("RulePanel: reset drops only the stale targets and publishes once", "[RulePanel]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(3, 2, { RuleTarget { 4, 0.2f, PlayMode::OneShot }, RuleTarget { 1, 0.3f, PlayMode::OneShot },
                         RuleTarget { 0, 0.5f } });
    CHECK(p.silenceText() == "Silence: 0 %");
    p.resetStale();
    CHECK(out.calls == 1);
    CHECK(out.region1 == 3);
    REQUIRE(out.targets.size() == 2);
    CHECK(out.targets[0].region2 == 1);
    CHECK(out.targets[0].weight == 0.3f);
    CHECK(out.targets[0].mode == PlayMode::OneShot);
    CHECK(out.targets[1].region2 == 0);
    CHECK(out.targets[1].weight == 0.5f);
    CHECK(p.staleText().isEmpty());
    CHECK(p.silenceText() == "Silence: 20 %");
    p.resetStale(); // nothing stale left
    CHECK(out.calls == 1);
}

TEST_CASE("RulePanel: reset during a drag does nothing, setContext leaves the drag alone", "[RulePanel]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(0, 2, { RuleTarget { 4, 0.2f } });
    p.beginDrag(0);
    p.dragWeight(0, 30.0f);
    p.setContext(0, 2, { RuleTarget { 4, 0.2f } });
    p.resetStale();
    CHECK(out.calls == 0);
    CHECK(p.weightPercent(0) == 30.0f);
    p.endDrag();
    REQUIRE(out.targets.size() == 2);
    CHECK(out.targets[0].region2 == 4); // still preserved
}

TEST_CASE("Editor: resetting stale targets leaves other region1 entries alone", "[RulePanel][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    DubcyclingProcessor proc;
    LoopPair loops(proc);
    auto s = proc.getPluginState();
    s.loop1.boundaries = { 0.5 };
    s.loop2.boundaries = { 0.5 };
    s.rules = { RuleEntry { 0, { RuleTarget { 5, 0.2f }, RuleTarget { 1, 0.4f } } },
                RuleEntry { 1, { RuleTarget { 5, 0.1f } } } };
    proc.applyEdit(s);
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto* editor = dynamic_cast<dc::ui::Editor*>(ed.get());
    REQUIRE(editor != nullptr);
    editor->refresh();
    CHECK(editor->rulePanel().staleText() == "20 % of it on removed regions");
    editor->rulePanel().resetStale();
    const auto after = proc.getPluginState();
    REQUIRE(after.rules.size() == 2);
    REQUIRE(after.rules[0].targets.size() == 1);
    CHECK(after.rules[0].targets[0].region2 == 1);
    CHECK(after.rules[0].targets[0].weight == 0.4f);
    CHECK(after.rules[1].targets.size() == 1);
    CHECK(after.rules[1].targets[0].region2 == 5);
    editor->refresh();
    CHECK(editor->rulePanel().staleText().isEmpty());
    CHECK(editor->rulePanel().silenceText() == "Silence: 60 %");
}

TEST_CASE("Editor: renders the rule panel with a stale target", "[RulePanel][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    DubcyclingProcessor proc;
    auto s = proc.getPluginState();
    s.loop1.boundaries = { 0.5 };
    s.loop2.boundaries = { 0.5 };
    s.rules = { RuleEntry { 0, { RuleTarget { 0, 0.5f }, RuleTarget { 4, 0.2f, PlayMode::OneShot } } } };
    proc.applyEdit(s);
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto* editor = dynamic_cast<dc::ui::Editor*>(ed.get());
    REQUIRE(editor != nullptr);
    editor->refresh();
    auto img = ed->createComponentSnapshot(ed->getLocalBounds());
    CHECK(img.isValid());
    const auto snapDir = juce::SystemStats::getEnvironmentVariable("DC_SNAPSHOT_DIR", {});
    if (snapDir.isNotEmpty())
    {
        auto png = juce::File(snapDir).getChildFile("editor_rules_stale.png");
        png.deleteFile();
        if (auto out = png.createOutputStream())
            juce::PNGImageFormat().writeImageToStream(img, *out);
    }
}

TEST_CASE("Editor: no weight sliders until both loops are loaded", "[RulePanel][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    DubcyclingProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto* editor = dynamic_cast<dc::ui::Editor*>(ed.get());
    REQUIRE(editor != nullptr);
    editor->refresh();
    CHECK(editor->rulePanel().rowCount() == 0);

    LoopPair loops(proc);
    editor->refresh();
    CHECK(editor->rulePanel().rowCount() == 1);
}

TEST_CASE("Editor: a freshly loaded loop is one region over the whole loop", "[RulePanel][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    DubcyclingProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto* editor = dynamic_cast<dc::ui::Editor*>(ed.get());
    REQUIRE(editor != nullptr);
    LoopPair loops(proc);
    editor->refresh();
    for (int slot = 0; slot < 2; ++slot)
    {
        CHECK(editor->lane(slot).hasData());
        CHECK(editor->lane(slot).regions().regionCount() == 1);
    }
    CHECK(editor->rulePanel().rowCount() == 1);

    auto img = ed->createComponentSnapshot(ed->getLocalBounds());
    CHECK(img.isValid());
    const auto snapDir = juce::SystemStats::getEnvironmentVariable("DC_SNAPSHOT_DIR", {});
    if (snapDir.isNotEmpty())
    {
        auto png = juce::File(snapDir).getChildFile("editor_unsplit.png");
        png.deleteFile();
        if (auto out = png.createOutputStream())
            juce::PNGImageFormat().writeImageToStream(img, *out);
    }
}

// --- Mix per rule row -----------------------------------------------------------------------------

TEST_CASE("RulePanel: every row shows a mix control that defaults to the centre", "[RulePanel][mix]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    p.setSize(440, 200);
    p.setContext(0, 3, { RuleTarget { 1, 0.4f, PlayMode::Gate, 0.2f } });
    for (int row = 0; row < 3; ++row)
    {
        CAPTURE(row);
        CHECK(control(p, row, "mix").isVisible());
        CHECK(control(p, row, "mixValue").isVisible());
    }
    CHECK(p.mix(0) == 0.5f); // weight 0, no target: still a mix control at the centre
    CHECK(p.mix(1) == 0.2f);
    CHECK(p.mix(2) == 0.5f);
    CHECK(p.mixText(0) == "L1 | L2");
    auto* slider = dynamic_cast<juce::Slider*>(&control(p, 1, "mix"));
    REQUIRE(slider != nullptr);
    CHECK(std::abs(slider->getValue() - 0.2) < 1e-6);
}

TEST_CASE("RulePanel: setMix publishes the whole target list once and keeps weight and mode", "[RulePanel][mix]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(1, 3, { RuleTarget { 0, 0.5f, PlayMode::OneShot }, RuleTarget { 2, 0.25f } });
    p.setMix(0, 0.3f);
    CHECK(out.calls == 1);
    CHECK(out.region1 == 1);
    REQUIRE(out.targets.size() == 2);
    CHECK(out.targets[0].region2 == 0);
    CHECK(out.targets[0].weight == 0.5f);
    CHECK(out.targets[0].mode == PlayMode::OneShot);
    CHECK(out.targets[0].mix == 0.3f);
    CHECK(out.targets[1].region2 == 2);
    CHECK(out.targets[1].mix == 0.5f);
    CHECK(p.mix(0) == 0.3f);
    p.setMix(0, 0.3f); // unchanged: nothing published
    CHECK(out.calls == 1);
}

TEST_CASE("RulePanel: changing weight or mode keeps the mix", "[RulePanel][mix]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(0, 2, { RuleTarget { 1, 0.4f, PlayMode::Gate, 0.8f } });
    p.setWeight(1, 60.0f);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].weight == pct(60.0f));
    CHECK(out.targets[0].mix == 0.8f);
    p.setMode(1, PlayMode::OneShot);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].mode == PlayMode::OneShot);
    CHECK(out.targets[0].mix == 0.8f);
    p.setMix(0, 0.1f);
    p.setWeight(0, 20.0f);
    REQUIRE(out.targets.size() == 2);
    CHECK(out.targets[1].region2 == 0);
    CHECK(out.targets[1].mix == 0.1f);
    CHECK(out.targets[1].weight == pct(20.0f));
    CHECK(p.mix(1) == 0.8f);
}

TEST_CASE("RulePanel: a touched mix is stored for a row with weight 0", "[RulePanel][mix]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(0, 2, {});
    p.setMix(1, 0.9f);
    CHECK(out.calls == 1);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].region2 == 1);
    CHECK(out.targets[0].weight == 0.0f);
    CHECK(out.targets[0].mode == PlayMode::Gate);
    CHECK(out.targets[0].mix == 0.9f);
}

TEST_CASE("RulePanel: an untouched row stores no target", "[RulePanel][mix]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(0, 3, {});
    p.setMix(1, 0.7f);
    p.setWeight(2, 10.0f);
    REQUIRE(out.targets.size() == 2);
    CHECK(out.targets[0].region2 == 1);
    CHECK(out.targets[1].region2 == 2);
    CHECK(out.targets[1].mix == 0.5f);
}

TEST_CASE("RulePanel: a mix drag publishes once at its end", "[RulePanel][mix]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(0, 2, { RuleTarget { 0, 0.5f } });
    p.beginMixDrag(0);
    p.dragMix(0, 0.4f);
    p.dragMix(0, 0.2f);
    CHECK(out.calls == 0);
    CHECK(p.mix(0) == 0.2f);
    CHECK(p.mixText(0) == "L2 40 %");
    p.endMixDrag();
    CHECK(out.calls == 1);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].mix == 0.2f);
    CHECK(out.targets[0].weight == 0.5f);
    p.beginMixDrag(0);
    p.endMixDrag(); // nothing moved: nothing published
    CHECK(out.calls == 1);
}

TEST_CASE("RulePanel: reset restores the centre and publishes", "[RulePanel][mix]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setSize(440, 200);
    p.setContext(0, 2, { RuleTarget { 1, 0.3f, PlayMode::Gate, 0.9f } });
    auto* slider = dynamic_cast<juce::Slider*>(&control(p, 1, "mix"));
    REQUIRE(slider != nullptr);
    const auto at = slider->getLocalBounds().getCentre().toFloat();
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const juce::MouseEvent dbl(source, at, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f,
                               0.0f, 0.0f, 0.0f, slider, slider, juce::Time::getCurrentTime(), at,
                               juce::Time::getCurrentTime(), 2, false);
    slider->mouseDoubleClick(dbl); // double-click = centre
    CHECK(p.mix(1) == 0.5f);
    CHECK(p.mixText(1) == "L1 | L2");
    CHECK(out.calls == 1);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].mix == 0.5f);
    CHECK(out.targets[0].weight == 0.3f);
}

TEST_CASE("RulePanel: mixText reads centre, ends and sides", "[RulePanel][mix]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    p.setContext(0, 1, {});
    CHECK(p.mixText(0) == "L1 | L2");
    p.setMix(0, 0.0f);
    CHECK(p.mixText(0) == "L1 only");
    p.setMix(0, 1.0f);
    CHECK(p.mixText(0) == "L2 only");
    p.setMix(0, 0.25f); // left of centre: Loop 2 at half level
    CHECK(p.mixText(0) == "L2 50 %");
    p.setMix(0, 0.75f); // right of centre: Loop 1 at half level
    CHECK(p.mixText(0) == "L1 50 %");
    p.setMix(0, 0.5f);
    CHECK(p.mixText(0) == "L1 | L2");
    CHECK(p.mixText(5).isEmpty());
}

TEST_CASE("RulePanel: out-of-range mix values are clamped by setMix", "[RulePanel][mix]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(0, 1, { RuleTarget { 0, 0.5f, PlayMode::Gate, 1.7f } }); // stored junk is clamped for display
    CHECK(p.mix(0) == 1.0f);
    p.setMix(0, -0.3f);
    CHECK(p.mix(0) == 0.0f);
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].mix == 0.0f);
    p.setMix(0, 2.0f);
    CHECK(p.mix(0) == 1.0f);
    p.setMix(0, std::nanf(""));
    CHECK(p.mix(0) == 0.5f);
    CHECK(out.targets[0].mix == 0.5f);
    CHECK(p.mix(3) == 0.5f); // no such row
}

TEST_CASE("RulePanel: setContext does not overwrite a mix drag in progress", "[RulePanel][mix]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    Published out;
    record(p, out);
    p.setContext(0, 2, { RuleTarget { 0, 0.5f, PlayMode::Gate, 0.5f } });
    p.beginMixDrag(0);
    p.dragMix(0, 0.9f);
    p.setContext(0, 2, { RuleTarget { 0, 0.5f, PlayMode::Gate, 0.5f } }); // the editor's poll
    CHECK(p.mix(0) == 0.9f);
    p.endMixDrag();
    REQUIRE(out.targets.size() == 1);
    CHECK(out.targets[0].mix == 0.9f);

    // A stored list that differs only in the mix is a change the panel shows.
    p.setContext(0, 2, { RuleTarget { 0, 0.5f, PlayMode::Gate, 0.1f } });
    CHECK(p.mix(0) == 0.1f);
}

TEST_CASE("RulePanel: controls do not overlap at the editor's panel width", "[RulePanel][mix][layout]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    DubcyclingProcessor proc;
    LoopPair loops(proc);
    auto s = proc.getPluginState();
    s.loop2.boundaries = { 0.5 };
    s.loop2.labels = { "Intro" };
    s.rules = { RuleEntry { 0, { RuleTarget { 0, 1.0f, PlayMode::OneShot, 0.2f } } } };
    proc.applyEdit(s);
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto* editor = dynamic_cast<dc::ui::Editor*>(ed.get());
    REQUIRE(editor != nullptr);
    editor->refresh();
    const char* ids[] = { "name", "weight", "percent", "mode", "mix", "mixValue" };
    // Fits at the label's smallest horizontal scale (the label squeezes the text instead of cutting it).
    const auto fits = [](const juce::Label& l, const juce::String& text) {
        const float w = juce::GlyphArrangement::getStringWidth(l.getFont(), text) * l.getMinimumHorizontalScale();
        return w <= static_cast<float>(l.getBorderSize().subtractedFrom(l.getLocalBounds()).getWidth()) + 0.5f;
    };
    for (const auto size : { juce::Point<int> { 800, 540 }, juce::Point<int> { 900, 600 }, juce::Point<int> { 1400, 900 } })
    {
        editor->setSize(size.x, size.y);
        auto& p = editor->rulePanel();
        REQUIRE(p.rowCount() == 2);
        CAPTURE(size.x, p.getWidth());
        for (int row = 0; row < p.rowCount(); ++row)
        {
            auto* r = findById(p, "row" + juce::String(row));
            REQUIRE(r != nullptr);
            CHECK(r->getHeight() == 20);
            CHECK(r->getRight() <= p.getWidth());
            for (std::size_t i = 0; i < std::size(ids); ++i)
            {
                const auto a = control(p, row, ids[i]).getBounds();
                CAPTURE(row, ids[i], a.toString());
                CHECK(!a.isEmpty());
                CHECK(r->getLocalBounds().contains(a));
                for (std::size_t j = i + 1; j < std::size(ids); ++j)
                {
                    CAPTURE(ids[j]);
                    CHECK(!a.intersects(control(p, row, ids[j]).getBounds()));
                }
            }
            CHECK(control(p, row, "weight").getWidth() >= 36);
            CHECK(control(p, row, "mix").getWidth() >= 36);
            const auto* percent = dynamic_cast<juce::Label*>(&control(p, row, "percent"));
            const auto* mixValue = dynamic_cast<juce::Label*>(&control(p, row, "mixValue"));
            REQUIRE(percent != nullptr);
            REQUIRE(mixValue != nullptr);
            CHECK(fits(*percent, "100 %"));
            for (const auto* text : { "L1 | L2", "L2 98 %", "L1 98 %", "L1 only", "L2 only" })
            {
                CAPTURE(text);
                CHECK(fits(*mixValue, text));
            }
            const auto modeHalf = static_cast<float>(control(p, row, "mode").getWidth()) * 0.5f - 2.0f;
            CHECK(juce::GlyphArrangement::getStringWidth(dc::ui::panel::mono(10.0f), "1-SHOT") * 0.8f <= modeHalf);
        }
    }
    // Look at it: DC_SNAPSHOT_DIR=<dir> writes editor_mix_<w>x<h>.png.
    if (const auto dir = juce::SystemStats::getEnvironmentVariable("DC_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        for (const auto size : { juce::Point<int> { 800, 540 }, juce::Point<int> { 900, 600 } })
        {
            editor->setSize(size.x, size.y);
            const auto img = editor->createComponentSnapshot(editor->getLocalBounds());
            auto png = juce::File(dir).getChildFile("editor_mix_" + juce::String(size.x) + "x" + juce::String(size.y)
                                                    + ".png");
            png.deleteFile();
            if (auto o = png.createOutputStream())
                juce::PNGImageFormat().writeImageToStream(img, *o);
        }
}

TEST_CASE("RulePanel: with many rows the scrollbar does not hide the rightmost column", "[RulePanel][mix][layout]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    RulePanel p;
    p.setSize(440, 200); // ten 20 px rows do not fit: the vertical scrollbar appears
    p.setContext(0, 10, {});
    juce::Viewport* vp = nullptr;
    for (auto* c : p.getChildren())
        if ((vp = dynamic_cast<juce::Viewport*>(c)) != nullptr)
            break;
    REQUIRE(vp != nullptr);
    REQUIRE(vp->isVerticalScrollBarShown());
    for (int row = 0; row < 10; ++row)
    {
        CAPTURE(row);
        auto* r = findById(p, "row" + juce::String(row));
        REQUIRE(r != nullptr);
        const int right = r->getX() + control(p, row, "mixValue").getRight();
        CHECK(right <= vp->getViewWidth());
        CHECK(r->getRight() <= vp->getViewWidth());
    }
}
