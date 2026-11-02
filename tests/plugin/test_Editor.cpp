// Editor: relink vs replace, data-loss protection, bars validation.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <functional>
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
