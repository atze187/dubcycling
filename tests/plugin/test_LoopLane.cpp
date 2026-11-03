#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"
#include "plugin/ui/Editor.h"
#include "plugin/ui/LoopLane.h"

using namespace dc;
using dc::ui::LoopLane;
using Catch::Matchers::WithinAbs;

namespace {

std::shared_ptr<const StereoData> someData(size_t frames = 4800)
{
    auto d = std::make_shared<StereoData>();
    d->left.resize(frames);
    d->right.resize(frames);
    for (size_t i = 0; i < frames; ++i)
    {
        d->left[i] = (i % 100) < 50 ? 0.5f : -0.5f;
        d->right[i] = d->left[i];
    }
    return d;
}

RegionMap regions(std::vector<double> b)
{
    RegionMap m;
    m.boundaries = std::move(b);
    return m;
}

// A 1000 px wide lane with data, so x / 1000 is the loop phase.
struct LaneFixture
{
    juce::ScopedJuceInitialiser_GUI gui;
    LoopLane lane;
    int changes = 0;
    RegionMap last;
    std::vector<int> selected;

    LaneFixture()
    {
        lane.setSize(1000, 100);
        lane.setData(someData());
        lane.onRegionsChanged = [this](const RegionMap& m) { ++changes; last = m; };
        lane.onRegionSelected = [this](int r) { selected.push_back(r); };
    }
};

} // namespace

TEST_CASE("LoopLane: double click adds a boundary at the clicked x", "[LoopLane]")
{
    LaneFixture f;
    f.lane.doubleClickAt(250.0f);
    REQUIRE(f.changes == 1);
    REQUIRE(f.last.boundaries.size() == 1);
    CHECK_THAT(f.last.boundaries[0], WithinAbs(0.25, 1e-9));
    REQUIRE(f.lane.regions().boundaries.size() == 1);
}

TEST_CASE("LoopLane: drag moves the boundary and is clamped by kMinRegion", "[LoopLane]")
{
    LaneFixture f;
    f.lane.setRegions(regions({ 0.3, 0.6 }));
    f.lane.pressAt(303.0f); // within the hit tolerance of the boundary at 300 px (grab offset -3 px)
    f.lane.dragTo(403.0f);
    CHECK_THAT(f.lane.regions().boundaries[0], WithinAbs(0.4, 1e-9));
    f.lane.dragTo(900.0f); // past the neighbour
    CHECK_THAT(f.lane.regions().boundaries[0], WithinAbs(0.6 - kMinRegion, 1e-9));
    f.lane.dragTo(-50.0f); // past the left edge
    CHECK_THAT(f.lane.regions().boundaries[0], WithinAbs(kMinRegion, 1e-9));
    f.lane.release();
    REQUIRE(f.changes == 1);
    CHECK_THAT(f.last.boundaries[0], WithinAbs(kMinRegion, 1e-9));
    CHECK_THAT(f.last.boundaries[1], WithinAbs(0.6, 1e-9));

    // after release, dragging does nothing
    f.lane.dragTo(500.0f);
    f.lane.release();
    CHECK(f.changes == 1);
}

TEST_CASE("LoopLane: a drag publishes nothing until release, then once with the final value", "[LoopLane]")
{
    LaneFixture f;
    f.lane.setRegions(regions({ 0.3, 0.6 }));
    f.lane.pressAt(300.0f);
    for (float x : { 320.0f, 350.0f, 410.0f, 450.0f })
        f.lane.dragTo(x);
    CHECK(f.changes == 0);
    CHECK_THAT(f.lane.regions().boundaries[0], WithinAbs(0.45, 1e-9)); // local preview
    f.lane.release();
    REQUIRE(f.changes == 1);
    CHECK_THAT(f.last.boundaries[0], WithinAbs(0.45, 1e-9));
}

TEST_CASE("LoopLane: grabbing a boundary off-centre does not make it jump to the cursor", "[LoopLane]")
{
    LaneFixture f;
    f.lane.setRegions(regions({ 0.3 }));
    f.lane.pressAt(304.0f);
    f.lane.dragTo(305.0f); // 1 px of movement moves the boundary by 1 px, not by 5
    CHECK_THAT(f.lane.regions().boundaries[0], WithinAbs(0.301, 1e-9));
}

TEST_CASE("LoopLane: a click on a boundary without movement publishes nothing", "[LoopLane]")
{
    LaneFixture f;
    f.lane.setRegions(regions({ 0.3 }));
    f.lane.pressAt(302.0f);
    f.lane.release();
    f.lane.pressAt(302.0f);
    f.lane.dragTo(302.0f); // jitter that does not move the boundary
    f.lane.release();
    f.lane.pressAt(302.0f);
    f.lane.dragTo(340.0f);
    f.lane.dragTo(302.0f); // moved and back to the start
    f.lane.release();
    CHECK(f.changes == 0);
}

TEST_CASE("LoopLane: new data during a drag cancels it without publishing", "[LoopLane]")
{
    LaneFixture f;
    f.lane.setRegions(regions({ 0.3 }));
    f.lane.pressAt(300.0f);
    f.lane.dragTo(500.0f);
    f.lane.setData(someData(9600));
    f.lane.release();
    CHECK(f.changes == 0);
    CHECK_THAT(f.lane.regions().boundaries[0], WithinAbs(0.3, 1e-9));
}

TEST_CASE("LoopLane: the nearest boundary wins the drag", "[LoopLane]")
{
    LaneFixture f;
    f.lane.setRegions(regions({ 0.300, 0.306 }));
    f.lane.pressAt(305.0f); // 5 px from the first, 1 px from the second (grab offset +1 px)
    f.lane.dragTo(499.0f);
    f.lane.release();
    REQUIRE(f.changes == 1);
    CHECK_THAT(f.last.boundaries[0], WithinAbs(0.3, 1e-9));
    CHECK_THAT(f.last.boundaries[1], WithinAbs(0.5, 1e-9));
}

TEST_CASE("LoopLane: double click on a boundary removes it", "[LoopLane]")
{
    LaneFixture f;
    f.lane.setRegions(regions({ 0.3, 0.6 }));
    f.lane.doubleClickAt(598.0f);
    REQUIRE(f.changes == 1);
    REQUIRE(f.last.boundaries.size() == 1);
    CHECK_THAT(f.last.boundaries[0], WithinAbs(0.3, 1e-9));
}

TEST_CASE("LoopLane: click selects the region under x and fires onRegionSelected", "[LoopLane]")
{
    LaneFixture f;
    f.lane.setRegions(regions({ 0.3, 0.6 }));
    f.lane.pressAt(450.0f);
    f.lane.release();
    REQUIRE(f.selected == std::vector<int> { 1 });
    CHECK(f.lane.selectedRegion() == 1);
    f.lane.pressAt(900.0f);
    f.lane.release();
    CHECK(f.selected == std::vector<int> { 1, 2 });
    CHECK(f.changes == 0); // selecting does not change regions
}

TEST_CASE("LoopLane: the playhead takes phases in 0..1 and hides on anything else", "[LoopLane][playhead]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LoopLane lane;
    lane.setSize(1000, 100);
    CHECK(lane.playhead() == -1.0);
    lane.setPlayhead(0.25);
    CHECK(lane.playhead() == 0.25);
    lane.setPlayhead(1.0);
    CHECK(lane.playhead() == 1.0);
    for (double bad : { -1.0, -0.001, 1.001, std::numeric_limits<double>::quiet_NaN(),
                        std::numeric_limits<double>::infinity() })
    {
        lane.setPlayhead(0.5);
        lane.setPlayhead(bad);
        CHECK(lane.playhead() == -1.0);
    }
}

TEST_CASE("LoopLane: empty data shows hint text and ignores clicks", "[LoopLane]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LoopLane lane;
    lane.setSize(1000, 100);
    int changes = 0, selections = 0;
    lane.onRegionsChanged = [&](const RegionMap&) { ++changes; };
    lane.onRegionSelected = [&](int) { ++selections; };

    CHECK(lane.hintText() == "Drop an audio file here");
    lane.setStatus(LoopLane::Status::Loading);
    CHECK(lane.hintText() == juce::String(juce::CharPointer_UTF8("L\xc3\xa4" "dt\xe2\x80\xa6")));
    lane.setStatus(LoopLane::Status::Missing);
    CHECK(lane.hintText() == "Datei fehlt");

    lane.setRegions(regions({ 0.5 }));
    lane.doubleClickAt(250.0f);
    lane.doubleClickAt(500.0f);
    lane.pressAt(500.0f);
    lane.dragTo(700.0f);
    lane.release();
    lane.pressAt(800.0f);
    lane.release();
    CHECK(changes == 0);
    CHECK(selections == 0);

    lane.setData(someData());
    CHECK(lane.hintText().isEmpty());
}

TEST_CASE("LoopLane: setRegions keeps the selection inside the region count", "[LoopLane]")
{
    LaneFixture f;
    f.lane.setRegions(regions({ 0.3, 0.6 }));
    f.lane.setSelectedRegion(2);
    f.lane.setRegions(regions({ 0.3 }));
    CHECK(f.lane.selectedRegion() == 1);
}

TEST_CASE("LoopLane accepts only audio files for drops", "[LoopLane]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LoopLane lane;
    CHECK(lane.isInterestedInFileDrag({ "C:/a/loop.wav" }));
    CHECK(lane.isInterestedInFileDrag({ "C:/a/loop.AIFF" }));
    CHECK(lane.isInterestedInFileDrag({ "C:/a/loop.mp3", "C:/a/b.flac" }));
    CHECK_FALSE(lane.isInterestedInFileDrag({ "C:/a/notes.txt" }));

    juce::File dropped;
    lane.onFileDropped = [&](const juce::File& file) { dropped = file; };
    lane.filesDropped({ "C:/a/notes.txt", "C:/a/loop.aif" }, 10, 10);
    CHECK(dropped.getFileName() == "loop.aif");
}

TEST_CASE("computePeaks: min/max per column of the channel mean", "[LoopLane][peaks]")
{
    using dc::ui::computePeaks;
    CHECK(computePeaks({}, {}, 10).empty());
    CHECK(computePeaks({ 0.5f }, { 0.5f }, 0).empty());

    const std::vector<float> l { 1.0f, 0.0f, -1.0f, 0.5f };
    const std::vector<float> r { 0.0f, 0.0f, -1.0f, 0.5f };
    auto p = computePeaks(l, r, 2);
    REQUIRE(p.size() == 2);
    CHECK_THAT(p[0].min, WithinAbs(0.0, 1e-6));
    CHECK_THAT(p[0].max, WithinAbs(0.5, 1e-6));
    CHECK_THAT(p[1].min, WithinAbs(-1.0, 1e-6));
    CHECK_THAT(p[1].max, WithinAbs(0.5, 1e-6));

    // more columns than samples: every column still covers one sample
    auto wide = computePeaks(l, r, 8);
    REQUIRE(wide.size() == 8);
    CHECK_THAT(wide[7].max, WithinAbs(0.5, 1e-6));

    // values are clamped to -1..1
    auto loud = computePeaks({ 4.0f }, { 4.0f }, 1);
    CHECK_THAT(loud[0].max, WithinAbs(1.0, 1e-6));
}

TEST_CASE("Editor: opens, paints and exposes the Loop-1 selection", "[LoopLane][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    DubcyclingProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    REQUIRE(ed != nullptr);
    auto* editor = dynamic_cast<dc::ui::Editor*>(ed.get());
    REQUIRE(editor != nullptr);
    ed->setSize(900, 600);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds());
    CHECK(img.isValid());
    CHECK(editor->selectedRegion1() == 0);
    CHECK(editor->cycleText() == "Zyklus: 1 Takte");

    const auto dir = juce::SystemStats::getEnvironmentVariable("DC_SNAPSHOT_DIR", {});
    if (dir.isNotEmpty())
    {
        auto file = juce::File(dir).getChildFile("editor.png");
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        if (auto out = file.createOutputStream())
            juce::PNGImageFormat().writeImageToStream(img, *out);
    }
}

TEST_CASE("Editor: cycle length follows the bars of both loops", "[LoopLane][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    DubcyclingProcessor proc;
    auto s = proc.getPluginState();
    s.loop1.bars = 4;
    s.loop2.bars = 6;
    proc.applyEdit(s);
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto* editor = dynamic_cast<dc::ui::Editor*>(ed.get());
    REQUIRE(editor != nullptr);
    editor->refresh();
    CHECK(editor->cycleText() == "Zyklus: 12 Takte");
}

TEST_CASE("Editor: a loaded loop shows its waveform, regions and file name", "[LoopLane][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dcedit", "", false);
    dir.createDirectory();
    const auto file = dir.getChildFile("sine.wav");
    {
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
            const float v = 0.8f * env * std::sin(static_cast<float>(i) * 0.05f);
            buf.setSample(0, i, v);
            buf.setSample(1, i, v);
        }
        REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, 16000));
    }

    DubcyclingProcessor proc;
    proc.replaceLoop(0, file, dc::ui::suggestedBarsFor(file, 120.0)); // 2 s at 120 bpm = 1 bar
    proc.waitForLoads();
    REQUIRE(proc.getSlotStatus(0) == DubcyclingProcessor::SlotStatus::Loaded);
    auto s = proc.getPluginState();
    s.loop1.boundaries = { 0.25, 0.5, 0.75 };
    s.loop2.bars = 3;
    proc.applyEdit(s);

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto* editor = dynamic_cast<dc::ui::Editor*>(ed.get());
    REQUIRE(editor != nullptr);
    editor->refresh();
    CHECK(editor->lane(0).hasData());
    CHECK(editor->lane(0).hintText().isEmpty());
    CHECK(editor->lane(0).regions().regionCount() == 4);
    CHECK_FALSE(editor->lane(1).hasData());
    CHECK(editor->cycleText() == "Zyklus: 3 Takte");

    auto img = ed->createComponentSnapshot(ed->getLocalBounds());
    CHECK(img.isValid());
    const auto snapDir = juce::SystemStats::getEnvironmentVariable("DC_SNAPSHOT_DIR", {});
    if (snapDir.isNotEmpty())
    {
        auto png = juce::File(snapDir).getChildFile("editor_loaded.png");
        png.deleteFile();
        if (auto out = png.createOutputStream())
            juce::PNGImageFormat().writeImageToStream(img, *out);
    }
    ed.reset();
    dir.deleteRecursively();
}

TEST_CASE("suggestedBarsFor uses the file length and the tempo", "[LoopLane][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dclane", "", false);
    dir.createDirectory();
    const auto file = dir.getChildFile("four_bars.wav");
    {
        // 8 s at 120 bpm, 4/4 = 4 bars
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream>(file);
        auto writer = wav.createWriterFor(out, juce::AudioFormatWriterOptions {}
                                                   .withSampleRate(8000)
                                                   .withNumChannels(1)
                                                   .withBitsPerSample(16));
        REQUIRE(writer != nullptr);
        juce::AudioBuffer<float> buf(1, 64000);
        buf.clear();
        REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, 64000));
    }
    CHECK(dc::ui::suggestedBarsFor(file, 120.0) == 4);
    CHECK(dc::ui::suggestedBarsFor(file, 0.0) == 4); // no host tempo: 120
    CHECK(dc::ui::suggestedBarsFor(file, 60.0) == 2);
    CHECK(dc::ui::suggestedBarsFor(dir.getChildFile("nope.wav"), 120.0) == 1);
    dir.deleteRecursively();
}

TEST_CASE("LoopLane: double-click on a region name edits it; Enter commits, Escape cancels", "[LoopLane][labels]")
{
    LaneFixture f;
    f.lane.setRegions(regions({ 0.5 }));
    std::vector<std::pair<int, juce::String>> edits;
    f.lane.onLabelEdited = [&](int r, juce::String t) { edits.emplace_back(r, t); };

    const float y = static_cast<float>(f.lane.getHeight()) - 8.0f;
    CHECK(f.lane.labelAt(10.0f, y) == 0);
    CHECK(f.lane.labelAt(510.0f, y) == 1);
    CHECK(f.lane.labelAt(300.0f, y) == -1);          // between the names
    CHECK(f.lane.labelAt(10.0f, 5.0f) == -1);        // too high

    f.lane.doubleClickAt(10.0f, y);
    REQUIRE(f.lane.isEditingLabel());
    f.lane.labelEditor()->setText("  Kick  ", false);
    f.lane.endLabelEdit(true);
    CHECK_FALSE(f.lane.isEditingLabel());
    REQUIRE(edits.size() == 1);
    CHECK(edits[0] == std::make_pair(0, juce::String("Kick")));

    REQUIRE(f.lane.beginLabelEdit(1));
    f.lane.labelEditor()->setText("Snare", false);
    f.lane.endLabelEdit(false);
    CHECK(edits.size() == 1); // cancelled

    REQUIRE(f.lane.beginLabelEdit(1));
    f.lane.labelEditor()->setText("B", false); // the default letter of region 1
    f.lane.endLabelEdit(true);
    REQUIRE(edits.size() == 2);
    CHECK(edits[1] == std::make_pair(1, juce::String()));
}

TEST_CASE("LoopLane: a double-click on empty lane still adds a boundary and names can be set", "[LoopLane][labels]")
{
    LaneFixture f;
    int changes = 0;
    f.lane.onRegionsChanged = [&](const RegionMap&) { ++changes; };
    f.lane.setLabels({ "Kick" });
    f.lane.doubleClickAt(300.0f, 30.0f); // away from every name
    CHECK(changes == 1);
    CHECK_FALSE(f.lane.isEditingLabel());

    LoopLane empty; // no data: no editor
    empty.setSize(1000, 100);
    CHECK_FALSE(empty.beginLabelEdit(0));
}
