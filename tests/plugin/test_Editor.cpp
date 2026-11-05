// Editor: relink vs replace, data-loss protection, bars validation.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"
#include "plugin/ui/Editor.h"

using namespace dc;
using Status = DubcyclingProcessor::SlotStatus;
using dc::ui::LoopFileAction;

namespace {

struct EditorFixture
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dcfix", "", false);
    DubcyclingProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed;
    dc::ui::Editor* editor = nullptr;
    int asked = 0;
    std::function<void(bool)> pendingAnswer;

    EditorFixture()
    {
        dir.createDirectory();
        ed.reset(proc.createEditor());
        editor = dynamic_cast<dc::ui::Editor*>(ed.get());
        REQUIRE(editor != nullptr);
        editor->confirmReplace = [this](int, std::function<void(bool)> answer) {
            ++asked;
            pendingAnswer = std::move(answer);
        };
    }
    ~EditorFixture()
    {
        ed.reset();
        dir.deleteRecursively();
    }

    juce::File wav(const juce::String& name, int frames = 16000)
    {
        const auto file = dir.getChildFile(name);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream>(file);
        auto writer = format.createWriterFor(out, juce::AudioFormatWriterOptions {}
                                                      .withSampleRate(8000)
                                                      .withNumChannels(2)
                                                      .withBitsPerSample(16));
        REQUIRE(writer != nullptr);
        juce::AudioBuffer<float> buf(2, frames);
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < 2; ++c)
                buf.setSample(c, i, 0.5f * std::sin(static_cast<float>(i) * 0.01f));
        REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, frames));
        return file;
    }

    juce::File broken(const juce::String& name)
    {
        const auto file = dir.getChildFile(name);
        file.replaceWithText("this is not audio");
        return file;
    }

    // Slot 0 loaded from `file`, with boundaries and one rule.
    void loadedWithRegionsAndRules(const juce::File& file)
    {
        proc.replaceLoop(0, file, 2);
        proc.waitForLoads();
        REQUIRE(proc.getSlotStatus(0) == Status::Loaded);
        auto s = proc.getPluginState();
        s.loop1.boundaries = { 0.25, 0.5 };
        s.rules = { RuleEntry { 1, { RuleTarget { 0, 1.0f } } } };
        proc.applyEdit(s);
    }
};

} // namespace

TEST_CASE("decideLoopFileAction covers every combination", "[LoopLane][editor]")
{
    using dc::ui::decideLoopFileAction;
    for (auto status : { Status::Empty, Status::Loading, Status::Loaded, Status::Missing, Status::Failed })
        for (bool exists : { false, true })
            for (bool readable : { false, true })
                for (bool has : { false, true })
                {
                    CAPTURE(static_cast<int>(status), exists, readable, has);
                    const auto a = decideLoopFileAction(status, exists, readable, has);
                    if (!readable)
                        CHECK(a == LoopFileAction::Reject);
                    else if (status == Status::Missing && !exists)
                        CHECK(a == LoopFileAction::Relink);
                    else if (has)
                        CHECK(a == LoopFileAction::ReplaceAfterConfirm);
                    else
                        CHECK(a == LoopFileAction::ReplaceDirect);
                }
    // the cases that matter, spelled out
    CHECK(decideLoopFileAction(Status::Missing, false, true, true) == LoopFileAction::Relink);
    CHECK(decideLoopFileAction(Status::Missing, true, true, true) == LoopFileAction::ReplaceAfterConfirm);
    CHECK(decideLoopFileAction(Status::Loaded, true, false, true) == LoopFileAction::Reject);
    CHECK(decideLoopFileAction(Status::Empty, false, true, false) == LoopFileAction::ReplaceDirect);
    CHECK(decideLoopFileAction(Status::Failed, true, true, true) == LoopFileAction::ReplaceAfterConfirm);
}

TEST_CASE("Editor: a new file on a Missing slot relinks and keeps regions, rules and bars", "[LoopLane][editor]")
{
    EditorFixture f;
    PluginState s;
    s.loop1.path = f.dir.getChildFile("gone.wav").getFullPathName();
    s.loop1.fileName = "gone.wav";
    s.loop1.hash = "00";
    s.loop1.bars = 2;
    s.loop1.boundaries = { 0.5 };
    s.rules = { RuleEntry { 0, { RuleTarget { 1, 1.0f } } } };
    const auto block = serialize(s);
    f.proc.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    REQUIRE(f.proc.getSlotStatus(0) == Status::Missing);

    f.editor->loadFile(0, f.wav("found.wav"));
    f.proc.waitForLoads();

    CHECK(f.asked == 0);
    CHECK(f.proc.getSlotStatus(0) == Status::Loaded);
    const auto after = f.proc.getPluginState();
    CHECK(after.loop1.fileName == "found.wav");
    CHECK(after.loop1.bars == 2);
    CHECK(after.loop1.boundaries == std::vector<double> { 0.5 });
    REQUIRE(after.rules.size() == 1);
    CHECK(after.rules[0].targets.size() == 1);
}

TEST_CASE("Editor: an undecodable, too short or absent file on a Loaded slot changes nothing", "[LoopLane][editor]")
{
    EditorFixture f;
    f.loadedWithRegionsAndRules(f.wav("good.wav"));
    const auto before = serialize(f.proc.getPluginState());

    for (const auto& bad : { f.broken("broken.wav"), f.wav("short.wav", 500), f.dir.getChildFile("nothere.wav") })
    {
        CAPTURE(bad.getFileName());
        f.editor->loadFile(0, bad);
        f.proc.waitForLoads();
        CHECK(serialize(f.proc.getPluginState()) == before);
        CHECK(f.proc.getSlotStatus(0) == Status::Loaded);
        CHECK(f.editor->lane(0).hasData());
        CHECK(f.editor->statusText(0).startsWith("Nicht geladen: " + bad.getFileName()));
    }
    CHECK(f.asked == 0);
}

TEST_CASE("Editor: replacing a loop with regions or rules asks first", "[LoopLane][editor]")
{
    EditorFixture f;
    f.loadedWithRegionsAndRules(f.wav("good.wav"));
    const auto before = serialize(f.proc.getPluginState());
    const auto other = f.wav("other.wav", 32000);

    f.editor->loadFile(0, other);
    REQUIRE(f.asked == 1);
    CHECK(serialize(f.proc.getPluginState()) == before); // nothing until answered
    f.pendingAnswer(false);
    CHECK(serialize(f.proc.getPluginState()) == before);

    f.editor->loadFile(0, other);
    REQUIRE(f.asked == 2);
    f.pendingAnswer(true);
    f.proc.waitForLoads();
    const auto after = f.proc.getPluginState();
    CHECK(after.loop1.fileName == "other.wav");
    CHECK(after.loop1.boundaries.empty());
    CHECK(after.rules.empty());
    CHECK(after.loop1.bars == 2); // 4 s at 120 bpm
}

TEST_CASE("Editor: replacing a loop without regions or rules does not ask", "[LoopLane][editor]")
{
    EditorFixture f;
    f.editor->loadFile(1, f.wav("first.wav"));
    f.proc.waitForLoads();
    CHECK(f.proc.getSlotStatus(1) == Status::Loaded);
    f.editor->loadFile(1, f.wav("second.wav"));
    f.proc.waitForLoads();
    CHECK(f.asked == 0);
    CHECK(f.proc.getPluginState().loop2.fileName == "second.wav");
}

TEST_CASE("Editor: a confirmation answered after the editor is gone does nothing", "[LoopLane][editor]")
{
    EditorFixture f;
    f.loadedWithRegionsAndRules(f.wav("good.wav"));
    const auto before = serialize(f.proc.getPluginState());
    f.editor->loadFile(0, f.wav("other.wav"));
    REQUIRE(f.asked == 1);
    f.ed.reset();
    f.pendingAnswer(true);
    CHECK(serialize(f.proc.getPluginState()) == before);
}

TEST_CASE("parseBars clamps numbers to 1..256 and ignores non-numbers", "[LoopLane][editor]")
{
    using dc::ui::parseBars;
    CHECK(parseBars("12", 5) == 12);
    CHECK(parseBars(" 7 ", 5) == 7);
    CHECK(parseBars("0", 5) == 1);
    CHECK(parseBars("-3", 5) == 1);
    CHECK(parseBars("257", 5) == 256);
    CHECK(parseBars("100000", 5) == 256);
    CHECK(parseBars("abc", 5) == 5);
    CHECK(parseBars("", 5) == 5);
    CHECK(parseBars("3x", 5) == 5);
}

TEST_CASE("Editor: the bars field clamps typed values and writes them to the state", "[LoopLane][editor]")
{
    EditorFixture f;
    auto& field = f.editor->barsField(0);
    auto type = [&](const juce::String& text) {
        field.setValue(field.getValueFromText(text), juce::sendNotificationSync);
    };

    type("8");
    CHECK(f.proc.getPluginState().loop1.bars == 8);
    type("abc");
    CHECK(f.proc.getPluginState().loop1.bars == 8);
    type("257");
    CHECK(f.proc.getPluginState().loop1.bars == 256);
    type("0");
    CHECK(f.proc.getPluginState().loop1.bars == 1);
    type("-4");
    CHECK(f.proc.getPluginState().loop1.bars == 1);
    CHECK(f.editor->cycleText() == "Zyklus: 1 Takte");
}

// ---- Review round 2: re-validation on confirm, one pending confirmation per slot ----

TEST_CASE("Editor: the confirmation names the slot and says all rules are deleted", "[LoopLane][editor]")
{
    CHECK(dc::ui::replaceConfirmTitle(0) == "Loop 1 ersetzen?");
    CHECK(dc::ui::replaceConfirmTitle(1) == "Loop 2 ersetzen?");
    CHECK(dc::ui::replaceConfirmMessage()
          == juce::String(juce::CharPointer_UTF8(
              "Dessen Regionen und alle Regeln (auch die zwischen Loop 1 und Loop 2) werden gel\xc3\xb6scht.")));
}

TEST_CASE("Editor: a file that becomes unreadable before OK is rejected and changes nothing", "[LoopLane][editor]")
{
    EditorFixture f;
    f.loadedWithRegionsAndRules(f.wav("good.wav"));
    const auto before = serialize(f.proc.getPluginState());
    const auto other = f.wav("other.wav");

    f.editor->loadFile(0, other);
    REQUIRE(f.asked == 1);
    other.replaceWithText("now garbage");
    f.pendingAnswer(true);
    f.proc.waitForLoads();

    CHECK(serialize(f.proc.getPluginState()) == before);
    CHECK(f.proc.getSlotStatus(0) == Status::Loaded);
    CHECK(f.editor->statusText(0).startsWith("Nicht geladen: other.wav"));
    CHECK(f.asked == 1); // never asked again
    CHECK_FALSE(f.editor->isConfirmationPending(0));
}

TEST_CASE("Editor: a second drop while a confirmation is open is ignored", "[LoopLane][editor]")
{
    EditorFixture f;
    f.loadedWithRegionsAndRules(f.wav("good.wav"));
    f.editor->loadFile(0, f.wav("first.wav"));
    REQUIRE(f.asked == 1);
    CHECK(f.editor->isConfirmationPending(0));
    f.editor->loadFile(0, f.wav("second.wav")); // ignored: one open confirmation per slot
    f.editor->loadFile(0, f.broken("broken.wav"));
    CHECK(f.asked == 1);

    f.pendingAnswer(true);
    f.proc.waitForLoads();
    const auto after = f.proc.getPluginState();
    CHECK(after.loop1.fileName == "first.wav"); // the first drop, replaced once
    CHECK(after.loop1.boundaries.empty());
    CHECK_FALSE(f.editor->isConfirmationPending(0));

    // answering the same callback again does nothing
    const auto block = serialize(after);
    f.pendingAnswer(true);
    CHECK(serialize(f.proc.getPluginState()) == block);
}

TEST_CASE("Editor: OK after the slot was edited in the meantime does not wipe it", "[LoopLane][editor]")
{
    EditorFixture f;
    f.loadedWithRegionsAndRules(f.wav("good.wav"));
    f.editor->loadFile(0, f.wav("other.wav"));
    REQUIRE(f.asked == 1);

    auto s = f.proc.getPluginState();
    s.loop1.boundaries = { 0.25, 0.5, 0.75 }; // e.g. edited or restored by the host meanwhile
    f.proc.applyEdit(s);
    const auto before = serialize(f.proc.getPluginState());

    f.pendingAnswer(true);
    f.proc.waitForLoads();
    CHECK(serialize(f.proc.getPluginState()) == before);
    CHECK(f.editor->statusText(0).startsWith("Nicht ersetzt"));
    CHECK(f.asked == 1);
}

TEST_CASE("Editor: OK after the slot became Missing relinks instead of wiping", "[LoopLane][editor]")
{
    EditorFixture f;
    f.loadedWithRegionsAndRules(f.wav("good.wav"));
    const auto other = f.wav("other.wav");
    f.editor->loadFile(0, other);
    REQUIRE(f.asked == 1);

    // The host restores a session whose Loop 1 file is gone.
    PluginState s;
    s.loop1.path = f.dir.getChildFile("gone.wav").getFullPathName();
    s.loop1.fileName = "gone.wav";
    s.loop1.bars = 2;
    s.loop1.boundaries = { 0.5 };
    s.rules = { RuleEntry { 0, { RuleTarget { 1, 1.0f } } } };
    const auto block = serialize(s);
    f.proc.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    REQUIRE(f.proc.getSlotStatus(0) == Status::Missing);

    f.pendingAnswer(true);
    f.proc.waitForLoads();
    const auto after = f.proc.getPluginState();
    CHECK(after.loop1.fileName == "other.wav");
    CHECK(after.loop1.boundaries == std::vector<double> { 0.5 });
    CHECK(after.rules.size() == 1);
    CHECK(f.asked == 1);
}

namespace {
// 2 s stereo wav at 44.1 kHz with short noise clicks at 0, 25, 50 and 75 % (3 usable boundaries).
juce::File clickWav(const juce::File& dir, const juce::String& name, int clicks = 4)
{
    const auto file = dir.getChildFile(name);
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream>(file);
    auto writer = format.createWriterFor(out, juce::AudioFormatWriterOptions {}
                                                  .withSampleRate(44100)
                                                  .withNumChannels(2)
                                                  .withBitsPerSample(16));
    REQUIRE(writer != nullptr);
    const int frames = 88200;
    juce::AudioBuffer<float> buf(2, frames);
    buf.clear();
    juce::Random rnd(7);
    for (int click = 0; click < clicks; ++click)
        for (int i = 0; i < 130; ++i)
        {
            const float v = 0.8f * (rnd.nextFloat() * 2.0f - 1.0f);
            buf.setSample(0, click * frames / 4 + i, v);
            buf.setSample(1, click * frames / 4 + i, v);
        }
    REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, frames));
    return file;
}
} // namespace

TEST_CASE("Editor: Vorschlag on a lane without boundaries sets them without asking", "[editor][suggest]")
{
    EditorFixture f;
    f.proc.replaceLoop(0, clickWav(f.dir, "clicks.wav"), 2);
    f.proc.waitForLoads();
    f.editor->refresh();
    REQUIRE(f.editor->suggestButton(0).isEnabled());

    f.editor->suggestButton(0).onClick();
    CHECK(f.asked == 0);
    const auto st = f.proc.getPluginState();
    REQUIRE(st.loop1.boundaries.size() == 3);
    CHECK(st.loop1.boundaries[0] == Catch::Approx(0.25).margin(0.002));
    CHECK(st.loop1.boundaries[1] == Catch::Approx(0.5).margin(0.002));
    CHECK(st.loop1.boundaries[2] == Catch::Approx(0.75).margin(0.002));
    CHECK(f.editor->lane(0).regions().regionCount() == 4);
    CHECK(st.loop2.boundaries.empty());
}

TEST_CASE("Editor: Vorschlag on a lane with boundaries asks; cancel keeps, OK replaces, rules stay", "[editor][suggest]")
{
    EditorFixture f;
    f.loadedWithRegionsAndRules(clickWav(f.dir, "clicks.wav")); // boundaries {0.25, 0.5}, one rule
    f.editor->refresh();
    const auto before = serialize(f.proc.getPluginState());

    f.editor->suggestRegions(0);
    REQUIRE(f.asked == 1);
    CHECK(f.editor->isConfirmationPending(0));
    CHECK(serialize(f.proc.getPluginState()) == before); // nothing until answered
    f.pendingAnswer(false);
    CHECK(serialize(f.proc.getPluginState()) == before);
    CHECK_FALSE(f.editor->isConfirmationPending(0));

    f.editor->suggestRegions(0);
    REQUIRE(f.asked == 2);
    f.pendingAnswer(true);
    CHECK(f.asked == 2); // never asked twice
    const auto after = f.proc.getPluginState();
    REQUIRE(after.loop1.boundaries.size() == 3);
    CHECK(after.loop1.boundaries[2] == Catch::Approx(0.75).margin(0.002));
    REQUIRE(after.rules.size() == 1); // rules are kept
    CHECK(after.rules[0].region1 == 1);
    REQUIRE(after.rules[0].targets.size() == 1);
    CHECK(after.rules[0].targets[0].region2 == 0);
}

TEST_CASE("Editor: Vorschlag without loaded data does nothing", "[editor][suggest]")
{
    EditorFixture f;
    f.editor->refresh();
    CHECK_FALSE(f.editor->suggestButton(0).isEnabled());
    CHECK_FALSE(f.editor->suggestButton(1).isEnabled());
    const auto before = serialize(f.proc.getPluginState());
    f.editor->suggestRegions(0);
    f.editor->suggestRegions(1);
    CHECK(f.asked == 0);
    CHECK(serialize(f.proc.getPluginState()) == before);
}

TEST_CASE("Editor: an answer after the slot changed meanwhile does not overwrite it", "[editor][suggest]")
{
    EditorFixture f;
    f.loadedWithRegionsAndRules(clickWav(f.dir, "clicks.wav"));
    f.editor->refresh();
    f.editor->suggestRegions(0);
    REQUIRE(f.asked == 1);
    auto s = f.proc.getPluginState();
    s.loop1.boundaries = { 0.1 };
    f.proc.applyEdit(s);
    f.pendingAnswer(true);
    CHECK(f.proc.getPluginState().loop1.boundaries == std::vector<double> { 0.1 });
}

TEST_CASE("Editor: Vorschlag without any transient changes nothing and says so", "[editor][suggest]")
{
    EditorFixture f;
    f.proc.replaceLoop(0, clickWav(f.dir, "silent.wav", 0), 2);
    f.proc.waitForLoads();
    f.editor->refresh();
    const auto before = serialize(f.proc.getPluginState());
    f.editor->suggestRegions(0);
    CHECK(f.asked == 0);
    CHECK(serialize(f.proc.getPluginState()) == before);
    CHECK(f.editor->statusText(0) == "Keine Transienten gefunden");
}

// ---- Hardening: Failed vs Missing, replace failures ----

TEST_CASE("Editor: a Failed slot shows the loader's error, a Missing slot says the file is gone", "[editor][hardening]")
{
    EditorFixture f;
    PluginState s;
    const auto bad = f.broken("damaged.wav");
    s.loop1.path = bad.getFullPathName();
    s.loop1.fileName = "damaged.wav";
    s.loop2.path = f.dir.getChildFile("gone.wav").getFullPathName();
    s.loop2.fileName = "gone.wav";
    const auto block = serialize(s);
    f.proc.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    f.proc.waitForLoads();
    f.editor->refresh();
    REQUIRE(f.proc.getSlotStatus(0) == Status::Failed);
    REQUIRE(f.proc.getSlotStatus(1) == Status::Missing);
    CHECK(f.editor->statusText(0) == "Nicht lesbar: unsupported or damaged file");
    CHECK(f.editor->statusText(1) == "Datei fehlt: gone.wav");
    CHECK(f.editor->lane(0).hintText() == "Nicht lesbar");
    CHECK(f.editor->lane(1).hintText() == "Datei fehlt");
}

TEST_CASE("Editor: a new file on a Failed slot goes through the replace flow", "[editor][hardening]")
{
    EditorFixture f;
    PluginState s;
    const auto bad = f.broken("damaged.wav");
    s.loop1.path = bad.getFullPathName();
    s.loop1.fileName = "damaged.wav";
    s.loop1.boundaries = { 0.5 };
    const auto block = serialize(s);
    f.proc.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    f.proc.waitForLoads();
    REQUIRE(f.proc.getSlotStatus(0) == Status::Failed);

    f.editor->loadFile(0, f.wav("fixed.wav"));
    CHECK(f.asked == 1); // regions would be dropped: ask, no relink
    f.pendingAnswer(true);
    f.proc.waitForLoads();
    CHECK(f.proc.getSlotStatus(0) == Status::Loaded);
    CHECK(f.proc.getPluginState().loop1.fileName == "fixed.wav");
    CHECK(f.proc.getPluginState().loop1.boundaries.empty());
}

TEST_CASE("Editor: a replace that fails to decode keeps the slot and shows a notice", "[editor][hardening]")
{
    EditorFixture f;
    f.loadedWithRegionsAndRules(f.wav("good.wav"));
    f.editor->refresh();
    const auto before = serialize(f.proc.getPluginState());

    REQUIRE(f.proc.replaceLoop(0, f.broken("broken.wav"), 2));
    f.editor->refresh();
    CHECK(f.editor->statusText(0).contains("broken.wav")); // loading the staged file
    f.proc.waitForLoads();
    f.editor->refresh();

    CHECK(serialize(f.proc.getPluginState()) == before);
    CHECK(f.proc.getSlotStatus(0) == Status::Loaded);
    CHECK(f.editor->statusText(0) == "Nicht geladen: broken.wav (unsupported or damaged file)");
}

TEST_CASE("Editor: Vorschlag is only enabled for a Loaded slot", "[editor][suggest][hardening]")
{
    EditorFixture f;
    f.proc.replaceLoop(0, clickWav(f.dir, "clicks.wav"), 2);
    f.proc.waitForLoads();
    f.editor->refresh();
    REQUIRE(f.editor->suggestButton(0).isEnabled());

    // Loading: a staged replace, the old loop's data is still there
    REQUIRE(f.proc.replaceLoop(0, clickWav(f.dir, "clicks2.wav", 2), 2));
    f.editor->refresh();
    REQUIRE(f.proc.getSlotStatus(0) == Status::Loading);
    CHECK(f.editor->lane(0).hasData());
    CHECK_FALSE(f.editor->suggestButton(0).isEnabled());
    auto before = serialize(f.proc.getPluginState());
    f.editor->suggestRegions(0);
    CHECK(serialize(f.proc.getPluginState()) == before);
    f.proc.waitForLoads();
    f.editor->refresh();
    CHECK(f.editor->suggestButton(0).isEnabled());

    // Failed and Missing
    PluginState s;
    s.loop1.path = f.broken("damaged.wav").getFullPathName();
    s.loop1.fileName = "damaged.wav";
    s.loop2.path = f.dir.getChildFile("gone.wav").getFullPathName();
    s.loop2.fileName = "gone.wav";
    const auto block = serialize(s);
    f.proc.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    f.proc.waitForLoads();
    f.editor->refresh();
    REQUIRE(f.proc.getSlotStatus(0) == Status::Failed);
    REQUIRE(f.proc.getSlotStatus(1) == Status::Missing);
    CHECK_FALSE(f.editor->suggestButton(0).isEnabled());
    CHECK_FALSE(f.editor->suggestButton(1).isEnabled());
    before = serialize(f.proc.getPluginState());
    f.editor->suggestRegions(0);
    f.editor->suggestRegions(1);
    CHECK(serialize(f.proc.getPluginState()) == before);
    CHECK(f.asked == 0);
}

// --- F1: rules follow their regions when a boundary is added or removed ---------------------------
namespace {
using Shape = std::map<int, std::vector<int>>; // region1 -> region2 of its targets, in order

Shape shapeOf(const std::vector<RuleEntry>& rules)
{
    Shape s;
    for (const auto& e : rules)
    {
        auto& v = s[e.region1];
        for (const auto& t : e.targets)
            v.push_back(t.region2);
    }
    return s;
}

// Loop-1 rules for regions 0..3 of {0.25, 0.5, 0.75} plus a stale entry for region 5; entry k
// targets Loop-2 region 10 + k so it can be told apart after a shift.
std::vector<RuleEntry> loop1Rules()
{
    std::vector<RuleEntry> r;
    for (int k : { 0, 1, 2, 3, 5 })
        r.push_back(RuleEntry { k, { RuleTarget { 10 + k, 0.1f } } });
    return r;
}

// One Loop-1 entry with targets for Loop-2 regions 0..3 of {0.25, 0.5, 0.75} plus a stale 5.
std::vector<RuleEntry> loop2Rules()
{
    std::vector<RuleEntry> r { RuleEntry { 7, {} } };
    for (int k : { 0, 1, 2, 3, 5 })
        r[0].targets.push_back(RuleTarget { k, 0.1f });
    return r;
}

const std::vector<double> kFour { 0.25, 0.5, 0.75 };
} // namespace

TEST_CASE("remapRules: inserting a Loop-1 boundary shifts later entries, the new half has no rule", "[editor][remap]")
{
    using dc::ui::remapRulesForBoundaryEdit;
    // first: 0.1 splits region 0
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop1Rules(), 0, kFour, { 0.1, 0.25, 0.5, 0.75 }))
          == Shape { { 0, { 10 } }, { 2, { 11 } }, { 3, { 12 } }, { 4, { 13 } }, { 6, { 15 } } });
    // middle: 0.6 splits region 2
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop1Rules(), 0, kFour, { 0.25, 0.5, 0.6, 0.75 }))
          == Shape { { 0, { 10 } }, { 1, { 11 } }, { 2, { 12 } }, { 4, { 13 } }, { 6, { 15 } } });
    // last: 0.9 splits region 3
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop1Rules(), 0, kFour, { 0.25, 0.5, 0.75, 0.9 }))
          == Shape { { 0, { 10 } }, { 1, { 11 } }, { 2, { 12 } }, { 3, { 13 } }, { 6, { 15 } } });
}

TEST_CASE("remapRules: removing a Loop-1 boundary keeps the left rule and shifts later entries", "[editor][remap]")
{
    using dc::ui::remapRulesForBoundaryEdit;
    // first: regions 0 and 1 merge
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop1Rules(), 0, kFour, { 0.5, 0.75 }))
          == Shape { { 0, { 10 } }, { 1, { 12 } }, { 2, { 13 } }, { 4, { 15 } } });
    // middle: regions 1 and 2 merge
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop1Rules(), 0, kFour, { 0.25, 0.75 }))
          == Shape { { 0, { 10 } }, { 1, { 11 } }, { 2, { 13 } }, { 4, { 15 } } });
    // last: regions 2 and 3 merge
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop1Rules(), 0, kFour, { 0.25, 0.5 }))
          == Shape { { 0, { 10 } }, { 1, { 11 } }, { 2, { 12 } }, { 4, { 15 } } });
}

TEST_CASE("remapRules: Loop-2 boundary edits shift targets, a merge drops the right target", "[editor][remap]")
{
    using dc::ui::remapRulesForBoundaryEdit;
    // inserts: first, middle, last
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop2Rules(), 1, kFour, { 0.1, 0.25, 0.5, 0.75 }))
          == Shape { { 7, { 0, 2, 3, 4, 6 } } });
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop2Rules(), 1, kFour, { 0.25, 0.5, 0.6, 0.75 }))
          == Shape { { 7, { 0, 1, 2, 4, 6 } } });
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop2Rules(), 1, kFour, { 0.25, 0.5, 0.75, 0.9 }))
          == Shape { { 7, { 0, 1, 2, 3, 6 } } });
    // removals: first, middle, last
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop2Rules(), 1, kFour, { 0.5, 0.75 })) == Shape { { 7, { 0, 1, 2, 4 } } });
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop2Rules(), 1, kFour, { 0.25, 0.75 })) == Shape { { 7, { 0, 1, 2, 4 } } });
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop2Rules(), 1, kFour, { 0.25, 0.5 })) == Shape { { 7, { 0, 1, 2, 4 } } });
    // A Loop-2 edit never touches region1, a Loop-1 edit never touches region2.
    CHECK(remapRulesForBoundaryEdit(loop2Rules(), 1, kFour, { 0.5, 0.75 })[0].region1 == 7);
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop1Rules(), 0, kFour, { 0.5, 0.75 }))[0] == std::vector<int> { 10 });
}

TEST_CASE("remapRules: stale entries stay stale", "[editor][remap]")
{
    using dc::ui::remapRulesForBoundaryEdit;
    // 4 regions; entry/target 5 is stale (>= 4). After an insert (5 regions) it is 6, after a
    // removal (3 regions) it is 4: still beyond the region count, never revived.
    for (const auto& after : { std::vector<double> { 0.1, 0.25, 0.5, 0.75 }, std::vector<double> { 0.25, 0.75 } })
    {
        const int count = static_cast<int>(after.size()) + 1;
        bool seen1 = false;
        for (const auto& e : remapRulesForBoundaryEdit(loop1Rules(), 0, kFour, after))
            if (e.targets[0].region2 == 15)
            {
                seen1 = true;
                CHECK(e.region1 >= count);
            }
        CHECK(seen1);
        const auto targets = remapRulesForBoundaryEdit(loop2Rules(), 1, kFour, after)[0].targets;
        REQUIRE(!targets.empty());
        CHECK(targets.back().region2 >= count); // the stale target is the last one
    }
}

TEST_CASE("remapRules: a drag, a multi-boundary change or an ambiguous diff leaves rules untouched", "[editor][remap]")
{
    using dc::ui::remapRulesForBoundaryEdit;
    const auto r1 = shapeOf(loop1Rules());
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop1Rules(), 0, kFour, { 0.3, 0.5, 0.75 })) == r1);      // move
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop1Rules(), 0, kFour, { 0.25 })) == r1);                // -2
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop1Rules(), 0, kFour, { 0.1, 0.3, 0.5, 0.75 })) == r1); // +1 and a move
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop1Rules(), 0, kFour, { 0.3, 0.75 })) == r1);           // -1 and a move
    const auto r2 = shapeOf(loop2Rules());
    CHECK(shapeOf(remapRulesForBoundaryEdit(loop2Rules(), 1, kFour, { 0.3, 0.5, 0.75 })) == r2);
}

TEST_CASE("Editor: adding or removing a boundary in a lane moves the rules with their regions", "[editor][remap]")
{
    EditorFixture f;
    f.proc.replaceLoop(0, f.wav("a.wav"), 1);
    f.proc.replaceLoop(1, f.wav("b.wav"), 1);
    f.proc.waitForLoads();
    auto s = f.proc.getPluginState();
    s.loop1.boundaries = kFour;
    s.loop2.boundaries = kFour;
    s.rules = { RuleEntry { 1, { RuleTarget { 2, 0.5f } } }, RuleEntry { 3, { RuleTarget { 3, 0.5f } } } };
    f.proc.applyEdit(s);
    f.editor->refresh();
    const float w0 = static_cast<float>(f.editor->lane(0).getWidth());
    const float w1 = static_cast<float>(f.editor->lane(1).getWidth());
    REQUIRE(w0 > 100.0f);
    REQUIRE(f.editor->lane(0).hasData());
    REQUIRE(f.editor->lane(1).hasData());

    // Loop 1: a boundary at 0.1 splits region 0 -> the entries move to 2 and 4.
    f.editor->lane(0).doubleClickAt(w0 * 0.1f);
    auto st = f.proc.getPluginState();
    REQUIRE(st.loop1.boundaries.size() == 4);
    CHECK(shapeOf(st.rules) == Shape { { 2, { 2 } }, { 4, { 3 } } });

    // Loop 2: a boundary at 0.6 splits region 2 -> target 3 becomes 4, target 2 stays.
    f.editor->refresh();
    f.editor->lane(1).doubleClickAt(w1 * 0.6f);
    st = f.proc.getPluginState();
    REQUIRE(st.loop2.boundaries.size() == 4);
    CHECK(shapeOf(st.rules) == Shape { { 2, { 2 } }, { 4, { 4 } } });

    // Loop 1: removing the boundary at 0.1 again -> back to 1 and 3.
    f.editor->refresh();
    f.editor->lane(0).doubleClickAt(w0 * 0.1f);
    st = f.proc.getPluginState();
    REQUIRE(st.loop1.boundaries.size() == 3);
    CHECK(shapeOf(st.rules) == Shape { { 1, { 2 } }, { 3, { 4 } } });

    // Loop 2: removing 0.5 merges regions 1 and 2 -> target 2 is dropped, 4 becomes 3. Entry 1 is
    // left without targets, which the state sanitizer drops (same as "no rule").
    f.editor->refresh();
    f.editor->lane(1).doubleClickAt(w1 * 0.5f);
    st = f.proc.getPluginState();
    REQUIRE(st.loop2.boundaries.size() == 3);
    CHECK(shapeOf(st.rules) == Shape { { 3, { 3 } } });

    // A drag moves a boundary without touching the rules.
    f.editor->refresh();
    const auto rulesBefore = shapeOf(f.proc.getPluginState().rules);
    f.editor->lane(0).pressAt(w0 * 0.25f);
    REQUIRE(f.editor->lane(0).isDragging());
    f.editor->lane(0).dragTo(w0 * 0.3f);
    f.editor->lane(0).release();
    st = f.proc.getPluginState();
    CHECK(st.loop1.boundaries[0] != 0.25);
    CHECK(shapeOf(st.rules) == rulesBefore);
}

TEST_CASE("remapLabels: a new boundary leaves the new half unnamed, a merge keeps the left name", "[editor][remap][labels]")
{
    using dc::ui::remapLabelsForBoundaryEdit;
    using Labels = std::vector<juce::String>;
    const Labels names { "Kick", "", "Snare", "Hat" };
    // insert in region 0: Kick stays, the new region 1 is unnamed, the rest moves up
    CHECK(remapLabelsForBoundaryEdit(names, kFour, { 0.1, 0.25, 0.5, 0.75 }) == Labels { "Kick", "", "", "Snare", "Hat" });
    // insert in the last region: no later names, nothing to shift
    CHECK(remapLabelsForBoundaryEdit(names, kFour, { 0.25, 0.5, 0.75, 0.9 }) == names);
    // merge 0 and 1: left name stays, the right one goes
    CHECK(remapLabelsForBoundaryEdit(names, kFour, { 0.5, 0.75 }) == Labels { "Kick", "Snare", "Hat" });
    // merge 1 and 2: the empty left name stays, Snare goes
    CHECK(remapLabelsForBoundaryEdit(names, kFour, { 0.25, 0.75 }) == Labels { "Kick", "", "Hat" });
    // a drag (same boundary count) changes nothing; trailing empties are trimmed
    CHECK(remapLabelsForBoundaryEdit(names, kFour, { 0.3, 0.5, 0.75 }) == names);
    CHECK(remapLabelsForBoundaryEdit(Labels { "Kick", "" }, kFour, { 0.25, 0.5, 0.75 }) == Labels { "Kick" });
}

TEST_CASE("Editor: a name typed into a lane is stored and follows its region when a boundary is added", "[editor][labels]")
{
    EditorFixture f;
    f.loadedWithRegionsAndRules(f.wav("a.wav")); // slot 0: boundaries 0.25, 0.5
    f.editor->lane(0).onLabelEdited(1, "Kick");
    CHECK(f.proc.getPluginState().loop1.labels == std::vector<juce::String> { "", "Kick" });
    CHECK(f.editor->rulePanel().region1() >= 0);

    auto& lane = f.editor->lane(0);
    lane.doubleClickAt(0.1f * static_cast<float>(lane.getWidth())); // splits region 0
    CHECK(f.proc.getPluginState().loop1.labels == std::vector<juce::String> { "", "", "Kick" });

    f.editor->lane(0).onLabelEdited(2, ""); // back to the default letter
    CHECK(f.proc.getPluginState().loop1.labels.empty());
}

// --- Effects: strip and panel in the editor --------------------------------------------------------
namespace {
using Masks = std::vector<std::uint8_t>;
const std::vector<double> kThree { 0.25, 0.5 }; // regions 0..2

// Both loops loaded, Loop 2 with boundaries `b2`, the editor refreshed.
void bothLoaded(EditorFixture& f, const std::vector<double>& b2 = kThree)
{
    f.proc.replaceLoop(0, f.wav("a.wav"), 1);
    f.proc.replaceLoop(1, f.wav("b.wav"), 1);
    f.proc.waitForLoads();
    auto s = f.proc.getPluginState();
    s.loop2.boundaries = b2;
    f.proc.applyEdit(s);
    f.editor->refresh();
}

void requireDisjoint(const std::vector<std::pair<const char*, juce::Rectangle<int>>>& rects, juce::Rectangle<int> window)
{
    for (std::size_t i = 0; i < rects.size(); ++i)
    {
        CAPTURE(rects[i].first, rects[i].second.toString(), window.toString());
        CHECK(!rects[i].second.isEmpty());
        CHECK(window.contains(rects[i].second));
        for (std::size_t j = i + 1; j < rects.size(); ++j)
        {
            CAPTURE(rects[j].first, rects[j].second.toString());
            CHECK_FALSE(rects[i].second.intersects(rects[j].second));
        }
    }
}
} // namespace

TEST_CASE("remapFxSwitchesForBoundaryEdit: inserting a boundary gives the new half the switches of the split region", "[editor][remap][fx]")
{
    using dc::ui::remapFxSwitchesForBoundaryEdit;
    CHECK(remapFxSwitchesForBoundaryEdit({ 1, 2, 4 }, kThree, { 0.25, 0.4, 0.5 }) == Masks { 1, 2, 2, 4 });
    CHECK(remapFxSwitchesForBoundaryEdit({ 1, 2, 4 }, kThree, { 0.1, 0.25, 0.5 }) == Masks { 1, 1, 2, 4 });
    CHECK(remapFxSwitchesForBoundaryEdit({ 1, 2, 4 }, kThree, { 0.25, 0.5, 0.9 }) == Masks { 1, 2, 4, 4 });
}

TEST_CASE("remapFxSwitchesForBoundaryEdit: removing a boundary keeps the left region's switches", "[editor][remap][fx]")
{
    using dc::ui::remapFxSwitchesForBoundaryEdit;
    CHECK(remapFxSwitchesForBoundaryEdit({ 1, 2, 4 }, kThree, { 0.5 }) == Masks { 1, 4 });
    CHECK(remapFxSwitchesForBoundaryEdit({ 1, 2, 4 }, kThree, { 0.25 }) == Masks { 1, 2 });
    CHECK(remapFxSwitchesForBoundaryEdit({ 1, 2, 0 }, kThree, { 0.25 }) == Masks { 1, 2 });
}

TEST_CASE("remapFxSwitchesForBoundaryEdit: a drag changes nothing", "[editor][remap][fx]")
{
    using dc::ui::remapFxSwitchesForBoundaryEdit;
    CHECK(remapFxSwitchesForBoundaryEdit({ 1, 2, 4 }, kThree, { 0.3, 0.5 }) == Masks { 1, 2, 4 });
    CHECK(remapFxSwitchesForBoundaryEdit({ 1, 2, 4 }, kThree, { 0.1, 0.3, 0.5 }) == Masks { 1, 2, 4 }); // +1 and a move
    CHECK(remapFxSwitchesForBoundaryEdit({ 1, 2, 4 }, kThree, {}) == Masks { 1, 2, 4 });                // -2
}

TEST_CASE("remapFxSwitchesForBoundaryEdit: no trailing zeros, short lists stay short", "[editor][remap][fx]")
{
    using dc::ui::remapFxSwitchesForBoundaryEdit;
    CHECK(remapFxSwitchesForBoundaryEdit({ 1 }, kThree, { 0.25, 0.5, 0.75 }) == Masks { 1 });
    CHECK(remapFxSwitchesForBoundaryEdit({ 1, 0 }, kThree, { 0.25, 0.5, 0.75 }) == Masks { 1 });
    CHECK(remapFxSwitchesForBoundaryEdit({}, kThree, { 0.1, 0.25, 0.5 }).empty());
}

TEST_CASE("Editor: a click on a strip marker toggles that switch in the processor state and selects the region", "[editor][fx]")
{
    EditorFixture f;
    bothLoaded(f);
    auto& strip = f.editor->fxStrip();
    REQUIRE(f.editor->lane(1).selectedRegion() == 0);
    const auto c = strip.markerBounds(2, FxInsert::Flanger).getCentre();
    REQUIRE(!strip.markerBounds(2, FxInsert::Flanger).isEmpty());

    strip.toggleAt(c.x, c.y);
    auto st = f.proc.getPluginState();
    CHECK(st.fx.switchOn(2, FxInsert::Flanger));
    CHECK(st.fx.maskFor(2) == (1 << static_cast<int>(FxInsert::Flanger)));
    CHECK(f.editor->lane(1).selectedRegion() == 2);
    CHECK(strip.markerOn(2, FxInsert::Flanger));
    CHECK(f.editor->fxPanel().switchOn(FxInsert::Flanger)); // the panel shows region 2 now
    CHECK(f.editor->fxPanel().switchText(FxInsert::Flanger) == "Region C");

    strip.toggleAt(c.x, c.y); // and off again
    st = f.proc.getPluginState();
    CHECK_FALSE(st.fx.switchOn(2, FxInsert::Flanger));
    CHECK(st.fx.switches.empty());
    CHECK_FALSE(f.editor->fxPanel().switchOn(FxInsert::Flanger));
}

TEST_CASE("Editor: the panel switch toggles the selected Loop-2 region", "[editor][fx]")
{
    EditorFixture f;
    bothLoaded(f);
    auto& lane = f.editor->lane(1);
    lane.pressAt(0.4f * static_cast<float>(lane.getWidth())); // selects region 1
    lane.release();
    REQUIRE(lane.selectedRegion() == 1);
    auto& panel = f.editor->fxPanel();
    CHECK(panel.switchText(FxInsert::Reverb) == "Region B"); // the selection reached the panel at once
    REQUIRE(panel.switchEnabled(FxInsert::Reverb));

    panel.setSwitch(FxInsert::Reverb, true);
    auto st = f.proc.getPluginState();
    CHECK(st.fx.switches == Masks { 0, 1 << static_cast<int>(FxInsert::Reverb) });
    CHECK(f.editor->fxStrip().markerOn(1, FxInsert::Reverb));
    CHECK_FALSE(f.editor->fxStrip().markerOn(0, FxInsert::Reverb));

    panel.setSwitch(FxInsert::Reverb, false);
    CHECK(f.proc.getPluginState().fx.switches.empty());
}

TEST_CASE("Editor: a knob change reaches the processor state and the published model", "[editor][fx]")
{
    EditorFixture f;
    bothLoaded(f);
    f.editor->fxPanel().setParam(FxParam::ReverbMix, 0.7);
    f.editor->fxPanel().setParam(FxParam::FilterType, 1);
    const auto st = f.proc.getPluginState();
    CHECK(st.fx.params.get(FxParam::ReverbMix) == Catch::Approx(0.7));
    CHECK(st.fx.params.get(FxParam::FilterType) == 1.0f);
    const auto model = f.proc.getModel();
    REQUIRE(model != nullptr);
    CHECK(model->fx.params == st.fx.params);
}

TEST_CASE("Editor: adding a Loop-2 boundary keeps the sound of the split region", "[editor][fx][remap]")
{
    EditorFixture f;
    bothLoaded(f);
    auto s = f.proc.getPluginState();
    s.fx.switches = { 1, 2, 4 };
    s.rules = { RuleEntry { 0, { RuleTarget { 2, 1.0f } } } };
    s.loop2.labels = { "", "", "Hat" };
    f.proc.applyEdit(s);
    f.editor->refresh();

    auto& lane = f.editor->lane(1);
    lane.doubleClickAt(0.4f * static_cast<float>(lane.getWidth())); // splits region 1
    auto st = f.proc.getPluginState();
    REQUIRE(st.loop2.boundaries.size() == 3);
    CHECK(st.fx.switches == Masks { 1, 2, 2, 4 });
    CHECK(shapeOf(st.rules) == Shape { { 0, { 3 } } }); // same edit moved rules and names
    CHECK(st.loop2.labels == std::vector<juce::String> { "", "", "", "Hat" });
    CHECK(f.proc.getModel()->fx.switches == st.fx.switches);

    f.editor->refresh();
    lane.doubleClickAt(0.25f * static_cast<float>(lane.getWidth())); // removes 0.25: 0 and 1 merge
    st = f.proc.getPluginState();
    REQUIRE(st.loop2.boundaries.size() == 2);
    CHECK(st.fx.switches == Masks { 1, 2, 4 });
}

TEST_CASE("Editor: a Loop-1 boundary edit leaves the fx switches alone", "[editor][fx][remap]")
{
    EditorFixture f;
    bothLoaded(f);
    auto s = f.proc.getPluginState();
    s.loop1.boundaries = kThree;
    s.fx.switches = { 1, 2, 4 };
    f.proc.applyEdit(s);
    f.editor->refresh();

    auto& lane = f.editor->lane(0);
    lane.doubleClickAt(0.4f * static_cast<float>(lane.getWidth()));
    REQUIRE(f.proc.getPluginState().loop1.boundaries.size() == 3);
    CHECK(f.proc.getPluginState().fx.switches == Masks { 1, 2, 4 });
    f.editor->refresh();
    lane.doubleClickAt(0.25f * static_cast<float>(lane.getWidth()));
    REQUIRE(f.proc.getPluginState().loop1.boundaries.size() == 2);
    CHECK(f.proc.getPluginState().fx.switches == Masks { 1, 2, 4 });
}

TEST_CASE("Editor: loading a preset with fx updates strip and panel", "[editor][fx]")
{
    EditorFixture f;
    bothLoaded(f);
    auto s = f.proc.getPluginState();
    s.fx.params.set(FxParam::FlangerMix, 0.8);
    s.fx.switches = { 0, 0, 1 << static_cast<int>(FxInsert::Filter) };
    f.proc.restoreState(s);
    f.proc.waitForLoads();
    f.editor->refresh();

    CHECK(f.editor->fxStrip().markerOn(2, FxInsert::Filter));
    CHECK_FALSE(f.editor->fxStrip().markerOn(0, FxInsert::Filter));
    CHECK(f.editor->fxPanel().param(FxParam::FlangerMix) == Catch::Approx(0.8));
    f.editor->lane(1).setSelectedRegion(2);
    f.editor->refresh();
    CHECK(f.editor->fxPanel().switchOn(FxInsert::Filter));
}

TEST_CASE("Editor: strip, panels and lanes fit in the window without overlap", "[editor][fx][layout]")
{
    EditorFixture f;
    bothLoaded(f, { 0.25, 0.5, 0.75, 0.985 });
    auto s = f.proc.getPluginState();
    s.fx.switches = { 1, 0b0110, 0, 0b1111, 0b1000 };
    s.loop2.labels = { "Intro" };
    f.proc.applyEdit(s);
    f.editor->lane(1).setSelectedRegion(1);
    f.editor->refresh();
    REQUIRE(f.editor->getWidth() == 900);
    REQUIRE(f.editor->getHeight() == 600);
    for (const auto size : { juce::Point<int> { 0, 0 }, juce::Point<int> { 800, 540 }, juce::Point<int> { 1400, 900 } })
    {
        if (size.x > 0)
            f.editor->setSize(size.x, size.y);
        // Look at it: DC_SNAPSHOT_DIR=<dir> writes editor_fx_<w>x<h>.png (as in test_PresetBar).
        if (const auto dir = juce::SystemStats::getEnvironmentVariable("DC_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            const auto img = f.editor->createComponentSnapshot(f.editor->getLocalBounds());
            auto png = juce::File(dir).getChildFile("editor_fx_" + juce::String(f.editor->getWidth()) + "x"
                                                    + juce::String(f.editor->getHeight()) + ".png");
            png.deleteFile();
            if (auto out = png.createOutputStream())
                juce::PNGImageFormat().writeImageToStream(img, *out);
        }
        CAPTURE(f.editor->getWidth(), f.editor->getHeight());
        const auto window = f.editor->getLocalBounds();
        const auto strip = f.editor->fxStrip().getBounds();
        const auto lane1 = f.editor->lane(1).getBounds();
        const auto panel = f.editor->fxPanel().getBounds();
        requireDisjoint({ { "lane0", f.editor->lane(0).getBounds() },
                          { "lane1", lane1 },
                          { "strip", strip },
                          { "rules", f.editor->rulePanel().getBounds() },
                          { "fx", panel },
                          { "presetBar", f.editor->presetBar().getBounds() } },
                        window);
        // The strip sits right under the Loop-2 lane, over the same x range.
        CHECK(strip.getX() == lane1.getX());
        CHECK(strip.getWidth() == lane1.getWidth());
        CHECK(strip.getY() >= lane1.getBottom());
        CHECK(strip.getY() - lane1.getBottom() <= 2);
        CHECK(strip.getHeight() >= 14);
        CHECK(strip.getHeight() <= 18);
        // Room for the four modules; the rule panel stays usable next to it.
        CHECK(panel.getWidth() >= 440);
        CHECK(panel.getHeight() >= 190);
        CHECK(f.editor->rulePanel().getWidth() >= 300);
        CHECK(f.editor->rulePanel().getHeight() >= 190);
    }
}

TEST_CASE("Editor: replacing a loop clears its names", "[editor][labels]")
{
    EditorFixture f;
    f.loadedWithRegionsAndRules(f.wav("a.wav"));
    f.editor->lane(0).onLabelEdited(0, "Kick");
    REQUIRE(f.proc.getPluginState().loop1.labels.size() == 1);
    f.editor->loadFile(0, f.wav("b.wav"));
    REQUIRE(f.pendingAnswer);
    f.pendingAnswer(true);
    f.proc.waitForLoads();
    CHECK(f.proc.getPluginState().loop1.labels.empty());
}
