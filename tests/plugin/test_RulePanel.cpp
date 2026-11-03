// RulePanel: one row per Loop-2 region, capped weights, remainder as silence, stale entries kept.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"
#include "plugin/ui/Editor.h"
#include "plugin/ui/RulePanel.h"

using namespace dc;
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

// Loads a short WAV into both slots: the rule panel only shows weights once both loops exist.
// Must be created before the state is edited (replaceLoop resets regions and rules).
struct LoopPair
{
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dcpair", "", false);

    explicit LoopPair(DubcyclingProcessor& proc)
    {
        dir.createDirectory();
        const auto file = dir.getChildFile("loop.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream>(file);
        auto writer = wav.createWriterFor(out, juce::AudioFormatWriterOptions {}
                                                   .withSampleRate(8000)
                                                   .withNumChannels(2)
                                                   .withBitsPerSample(16));
        REQUIRE(writer != nullptr);
        juce::AudioBuffer<float> buf(2, 16000);
        buf.clear();
        REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, 16000));
        writer.reset();
        REQUIRE(proc.replaceLoop(0, file, 2));
        REQUIRE(proc.replaceLoop(1, file, 2));
        proc.waitForLoads();
    }
    ~LoopPair() { dir.deleteRecursively(); }
};

float pct(float p) { return p / 100.0f; }

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
    CHECK(p.silenceText() == "Stille: 100 %");
    p.setWeight(0, 70.0f);
    CHECK(p.silenceText() == "Stille: 30 %");
    CHECK(p.percentText(0) == "70 %");
    p.setWeight(1, 33.3f);
    p.setWeight(2, 33.3f);
    CHECK(p.silenceText() == "Stille: 0 %"); // 70 + 30 (capped) leaves nothing
    p.setContext(0, 3, { RuleTarget { 0, 0.7f } });
    CHECK(p.silenceText() == "Stille: 30 %");
    p.setContext(0, 3, { RuleTarget { 0, 0.333f }, RuleTarget { 1, 0.333f } });
    CHECK(p.silenceText() == "Stille: 33 %");
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
    CHECK(p.silenceText() == "Stille: 50 %"); // the stale 20 % still counts
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
    CHECK(p.silenceText() == "Stille: 0 %");
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
    CHECK(editor->rulePanel().silenceText() == "Stille: 30 %");

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
    CHECK(p.staleText() == "davon 35 % auf entfernte Regionen");
    CHECK(p.silenceText() == "Stille: 15 %");
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
    CHECK(p.silenceText() == "Stille: 0 %");
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
    CHECK(p.silenceText() == "Stille: 20 %");
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
    CHECK(editor->rulePanel().staleText() == "davon 20 % auf entfernte Regionen");
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
    CHECK(editor->rulePanel().silenceText() == "Stille: 60 %");
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
