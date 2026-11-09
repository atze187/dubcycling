// PassPanel: slot buttons, Play, Save, Learn, Clear and Clear all against the processor's pass API.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"
#include "plugin/ui/PanelLook.h"
#include "plugin/ui/PassPanel.h"

using namespace dc;
using dc::ui::PassPanel;
using SlotState = PassPanel::SlotState;

namespace {

constexpr int kSave = DubcyclingProcessor::kTargetSave;
constexpr int kPlay = DubcyclingProcessor::kTargetPlay;

struct HostHead : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> pos;
    juce::Optional<PositionInfo> getPosition() const override { return pos; }
};

struct PanelFixture
{
    static constexpr int kBlock = 512;
    static constexpr double kRate = 44100.0;
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dcpass", "", false);
    DubcyclingProcessor proc;
    PassPanel panel { proc };
    HostHead head;
    double ppq = 0.0;

    PanelFixture()
    {
        dir.createDirectory();
        panel.setSize(800, 28);
        proc.prepareToPlay(kRate, kBlock);
    }
    ~PanelFixture()
    {
        proc.setPlayHead(nullptr);
        dir.deleteRecursively();
    }

    juce::File wav(const juce::String& name)
    {
        const auto file = dir.getChildFile(name);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream>(file);
        auto writer = format.createWriterFor(out, juce::AudioFormatWriterOptions {}
                                                      .withSampleRate(8000)
                                                      .withNumChannels(2)
                                                      .withBitsPerSample(16));
        REQUIRE(writer != nullptr);
        juce::AudioBuffer<float> buf(2, 16000);
        for (int i = 0; i < 16000; ++i)
            for (int c = 0; c < 2; ++c)
                buf.setSample(c, i, 0.5f * std::sin(static_cast<float>(i) * 0.01f));
        REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, 16000));
        return file;
    }

    // Both loops one bar long, Loop-1 region 0 always starts Loop-2 region 0 (every pass has one
    // entry); Autoplay with a running host at 120 bpm.
    void playLoops()
    {
        proc.replaceLoop(0, wav("l1.wav"), 1);
        proc.replaceLoop(1, wav("l2.wav"), 1);
        proc.waitForLoads();
        auto s = proc.getPluginState();
        s.rules = { RuleEntry { 0, { RuleTarget { 0, 1.0f } } } };
        proc.applyEdit(s);
        proc.setPlayHead(&head);
    }
    void block(juce::MidiBuffer& midi)
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setBpm(120.0);
        info.setIsPlaying(true);
        info.setPpqPosition(ppq);
        info.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 });
        head.pos = info;
        juce::AudioBuffer<float> buf(2, kBlock);
        buf.clear();
        proc.processBlock(buf, midi);
        ppq += kBlock * 120.0 / (60.0 * kRate);
    }
    void block()
    {
        juce::MidiBuffer none;
        block(none);
    }
    void bar()
    {
        const double next = std::floor(ppq / 4.0 + 1e-9) * 4.0 + 4.0;
        while (ppq <= next)
            block();
    }
    void fill(std::initializer_list<int> slots)
    {
        auto s = proc.getPluginState();
        for (int i : slots)
            s.passes[static_cast<std::size_t>(i)].events = { PassEvent { 0.0, false, 0, PlayMode::Gate } };
        proc.applyEdit(s);
    }
    // One note-on and its note-off (binds a learn that is armed).
    void note(int n)
    {
        juce::MidiBuffer on, off;
        on.addEvent(juce::MidiMessage::noteOn(1, n, 1.0f), 0);
        off.addEvent(juce::MidiMessage::noteOff(1, n), 0);
        block(on);
        block(off);
    }
};

void click(juce::Button& b)
{
    REQUIRE(b.onClick != nullptr);
    b.onClick();
}

// A right-button (popup menu) press and release on the button, as the mouse would deliver them.
void rightClick(juce::Button& b)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const juce::Point<float> pos { 2.0f, 2.0f };
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent e(source, pos, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier),
                             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                             juce::MouseInputSource::defaultTiltY, &b, &b, now, pos, now, 1, false);
    juce::MouseListener& listener = b;
    listener.mouseDown(e);
    listener.mouseUp(e);
}

} // namespace

TEST_CASE("PassPanel: eight slots labelled 1..8 show empty and filled; clear buttons follow", "[passpanel]")
{
    PanelFixture f;
    f.panel.refresh();
    for (int i = 0; i < 8; ++i)
    {
        CAPTURE(i);
        CHECK(f.panel.slotButton(i).getButtonText() == juce::String(i + 1));
        CHECK(f.panel.slotState(i) == SlotState::Empty);
        CHECK_FALSE(f.panel.clearButton(i).isEnabled());
    }
    CHECK_FALSE(f.panel.clearAllButton().isEnabled());
    CHECK(f.panel.saveButton().getButtonText() == "SAVE");
    CHECK(f.panel.playButton().getButtonText() == "PLAY");
    CHECK(f.panel.learnButton().getButtonText() == "LEARN");

    f.fill({ 1, 4 });
    f.panel.refresh();
    CHECK(f.panel.slotState(0) == SlotState::Empty);
    CHECK(f.panel.slotState(1) == SlotState::Filled);
    CHECK(f.panel.slotState(4) == SlotState::Filled);
    CHECK(f.panel.clearButton(1).isEnabled());
    CHECK_FALSE(f.panel.clearButton(2).isEnabled());
    CHECK(f.panel.clearAllButton().isEnabled());
}

TEST_CASE("PassPanel: a slot click requests replay only for a filled slot; the slot shows playing", "[passpanel]")
{
    PanelFixture f;
    f.playLoops();
    f.fill({ 3 });
    f.panel.refresh();

    click(f.panel.slotButton(0)); // empty: nothing
    CHECK(f.proc.replayRequest() == 0);
    click(f.panel.slotButton(3));
    CHECK(f.proc.replayRequest() == 2 + 3);
    CHECK(f.panel.slotState(3) == SlotState::Queued); // waits for the next pass start
    CHECK(f.panel.playButton().getToggleState());

    f.bar();
    f.bar();
    REQUIRE(f.proc.replaySlot() == 3);
    f.panel.refresh();
    CHECK(f.panel.slotState(3) == SlotState::Playing);
    CHECK(f.panel.slotState(0) == SlotState::Empty);
}

TEST_CASE("PassPanel: Play turns the chain on, and any replay off", "[passpanel]")
{
    PanelFixture f;
    f.fill({ 0, 2 });
    f.panel.refresh();
    CHECK_FALSE(f.panel.playButton().getToggleState());
    click(f.panel.playButton());
    CHECK(f.proc.replayRequest() == 1);
    CHECK(f.panel.playButton().getToggleState());
    click(f.panel.playButton());
    CHECK(f.proc.replayRequest() == 0);
    CHECK_FALSE(f.panel.playButton().getToggleState());

    click(f.panel.slotButton(2));
    REQUIRE(f.proc.replayRequest() == 2 + 2);
    click(f.panel.playButton()); // leaves a slot replay
    CHECK(f.proc.replayRequest() == 0);

    // Polled: a request from elsewhere (a MIDI note) lights it on the next refresh.
    f.proc.setReplayRequest(1);
    f.panel.refresh();
    CHECK(f.panel.playButton().getToggleState());
}

TEST_CASE("PassPanel: Play is disabled while there is nothing to play", "[passpanel]")
{
    PanelFixture f;
    f.panel.refresh();
    CHECK_FALSE(f.panel.playButton().isEnabled());
    f.fill({ 5 });
    f.panel.refresh();
    CHECK(f.panel.playButton().isEnabled());
}

TEST_CASE("PassPanel: Save stores the last pass in the lowest free slot and then reads disabled", "[passpanel]")
{
    PanelFixture f;
    f.playLoops();
    f.fill({ 0 });
    f.panel.refresh();
    CHECK_FALSE(f.panel.saveButton().isEnabled()); // nothing finished yet

    f.bar();
    f.bar();
    f.panel.refresh();
    REQUIRE(f.panel.saveButton().isEnabled());
    click(f.panel.saveButton());
    CHECK(f.proc.passFilled(1));
    CHECK(f.panel.slotState(1) == SlotState::Filled);
    CHECK_FALSE(f.panel.saveButton().isEnabled()); // the same pass again
}

TEST_CASE("PassPanel: Clear empties one slot, Clear all every slot", "[passpanel]")
{
    PanelFixture f;
    f.fill({ 0, 2, 5 });
    f.panel.refresh();
    click(f.panel.clearButton(2));
    CHECK_FALSE(f.proc.passFilled(2));
    CHECK(f.proc.passFilled(0));
    CHECK(f.panel.slotState(2) == SlotState::Empty);
    CHECK_FALSE(f.panel.clearButton(2).isEnabled());

    click(f.panel.clearAllButton());
    for (int i = 0; i < 8; ++i)
    {
        CAPTURE(i);
        CHECK_FALSE(f.proc.passFilled(i));
        CHECK(f.panel.slotState(i) == SlotState::Empty);
    }
    CHECK_FALSE(f.panel.clearAllButton().isEnabled());
}

TEST_CASE("PassPanel: in Learn mode a click arms the target and triggers nothing", "[passpanel][learn]")
{
    PanelFixture f;
    f.playLoops();
    f.fill({ 2 });
    f.bar();
    f.bar(); // Save would be possible
    f.panel.refresh();
    REQUIRE(f.panel.saveButton().isEnabled());

    click(f.panel.learnButton());
    CHECK(f.panel.learnMode());
    CHECK(f.panel.learnButton().getToggleState());

    click(f.panel.slotButton(2));
    CHECK(f.proc.learnArmed() == 2);
    CHECK(f.proc.replayRequest() == 0);
    CHECK(f.panel.isWaiting(2));
    CHECK(f.panel.slotButton(2).getTooltip().containsIgnoreCase("waiting for note"));

    click(f.panel.slotButton(6)); // an empty slot can be learned too; the arm moves
    CHECK(f.proc.learnArmed() == 6);
    CHECK_FALSE(f.panel.isWaiting(2));
    CHECK(f.panel.isWaiting(6));

    click(f.panel.saveButton());
    CHECK(f.proc.learnArmed() == kSave);
    CHECK_FALSE(f.proc.passFilled(0)); // not saved

    click(f.panel.playButton());
    CHECK(f.proc.learnArmed() == kPlay);
    CHECK(f.proc.replayRequest() == 0); // not toggled
    CHECK(f.panel.isWaiting(kPlay));
}

TEST_CASE("PassPanel: a learned note shows on the button; the wait ends when it is bound", "[passpanel][learn]")
{
    PanelFixture f;
    f.fill({ 0 });
    f.panel.refresh();
    click(f.panel.learnButton());
    click(f.panel.slotButton(0));
    REQUIRE(f.panel.isWaiting(0));

    f.note(60);
    REQUIRE(f.proc.boundNote(0) == 60);
    f.panel.refresh(); // polled by the editor timer
    CHECK_FALSE(f.panel.isWaiting(0));
    CHECK(f.panel.noteText(0) == "C3");
    CHECK(f.panel.slotButton(0).getButtonText().contains("C3"));
    CHECK(f.panel.slotButton(0).getTooltip().contains("C3"));

    click(f.panel.playButton());
    f.note(61);
    f.panel.refresh();
    CHECK(f.panel.noteText(kPlay) == "C#3");
    CHECK(f.panel.playButton().getTooltip().contains("C#3"));

    // Learn off: labels return, the note stays in the tooltip.
    click(f.panel.learnButton());
    CHECK_FALSE(f.panel.learnMode());
    CHECK(f.panel.slotButton(0).getButtonText() == "1");
    CHECK(f.panel.playButton().getButtonText() == "PLAY");
    CHECK(f.panel.slotButton(0).getTooltip().contains("C3"));
    CHECK(f.panel.noteText(1).isEmpty());
}

TEST_CASE("PassPanel: turning Learn off cancels an armed learn", "[passpanel][learn]")
{
    PanelFixture f;
    click(f.panel.learnButton());
    click(f.panel.slotButton(4));
    REQUIRE(f.proc.learnArmed() == 4);
    click(f.panel.learnButton());
    CHECK(f.proc.learnArmed() == -1);
    CHECK_FALSE(f.panel.isWaiting(4));
    f.note(62);
    CHECK(f.proc.boundNote(4) == -1);
}

TEST_CASE("PassPanel: closing the panel (editor) cancels an armed learn", "[passpanel][learn]")
{
    PanelFixture f;
    {
        PassPanel other(f.proc);
        click(other.learnButton());
        click(other.slotButton(1));
        REQUIRE(f.proc.learnArmed() == 1);
    }
    CHECK(f.proc.learnArmed() == -1);
}

TEST_CASE("PassPanel: a second click on a waiting button only cancels the wait; the note stays", "[passpanel][learn]")
{
    PanelFixture f;
    click(f.panel.learnButton());
    click(f.panel.saveButton());
    f.note(70);
    REQUIRE(f.proc.boundNote(kSave) == 70);
    click(f.panel.saveButton());
    REQUIRE(f.panel.isWaiting(kSave));
    click(f.panel.saveButton()); // e.g. the second half of a double-click
    CHECK(f.proc.learnArmed() == -1);
    CHECK(f.proc.boundNote(kSave) == 70);
    CHECK_FALSE(f.panel.isWaiting(kSave));
}

TEST_CASE("PassPanel: in Learn mode a right-click removes the target's note", "[passpanel][learn]")
{
    PanelFixture f;
    f.fill({ 2 });
    click(f.panel.learnButton());
    click(f.panel.slotButton(2));
    f.note(62);
    click(f.panel.playButton());
    f.note(64);
    click(f.panel.saveButton());
    f.note(65);
    f.panel.refresh();
    REQUIRE(f.proc.boundNote(2) == 62);
    REQUIRE(f.proc.boundNote(kPlay) == 64);
    REQUIRE(f.proc.boundNote(kSave) == 65);
    CHECK(f.panel.slotButton(2).getTooltip().containsIgnoreCase("right-click to remove"));

    rightClick(f.panel.slotButton(2));
    CHECK(f.proc.boundNote(2) == -1);
    CHECK(f.proc.learnArmed() == -1); // removing does not arm
    CHECK(f.panel.noteText(2).isEmpty());
    rightClick(f.panel.playButton());
    CHECK(f.proc.boundNote(kPlay) == -1);
    CHECK(f.proc.boundNote(kSave) == 65); // only the clicked target

    click(f.panel.saveButton()); // waiting: a right-click cancels the wait and removes the note
    REQUIRE(f.panel.isWaiting(kSave));
    rightClick(f.panel.saveButton());
    CHECK(f.proc.boundNote(kSave) == -1);
    CHECK(f.proc.learnArmed() == -1);

    // Outside Learn mode a right-click removes nothing.
    click(f.panel.slotButton(2));
    f.note(62);
    click(f.panel.learnButton());
    REQUIRE_FALSE(f.panel.learnMode());
    rightClick(f.panel.slotButton(2));
    CHECK(f.proc.boundNote(2) == 62);
}

TEST_CASE("PassPanel: all strings are English and the buttons fit side by side", "[passpanel][layout]")
{
    PanelFixture f;
    f.fill({ 0 });
    f.panel.refresh();
    for (const int w : { 772, 884 })
    {
        f.panel.setSize(w, 28);
        CAPTURE(w);
        std::vector<juce::Component*> parts { &f.panel.saveButton(), &f.panel.playButton(), &f.panel.learnButton(),
                                              &f.panel.clearAllButton() };
        for (int i = 0; i < 8; ++i)
        {
            parts.push_back(&f.panel.slotButton(i));
            parts.push_back(&f.panel.clearButton(i));
        }
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            const auto r = parts[i]->getBounds();
            CHECK(r.getWidth() >= 12);
            CHECK(r.getHeight() >= 16);
            CHECK(f.panel.getLocalBounds().contains(r));
            for (std::size_t j = i + 1; j < parts.size(); ++j)
                CHECK_FALSE(r.intersects(parts[j]->getBounds()));
        }
        // Play sits right of the slot group.
        CHECK(f.panel.playButton().getX() >= f.panel.clearButton(7).getRight());
    }
    CHECK(f.panel.clearAllButton().getButtonText() == "CLEAR ALL");
    CHECK(f.panel.clearButton(0).getTooltip() == "Clear slot 1");

    // Look at it: DC_SNAPSHOT_DIR=<dir> writes passpanel_normal.png and passpanel_learn.png.
    if (const auto dir = juce::SystemStats::getEnvironmentVariable("DC_SNAPSHOT_DIR", {}); dir.isNotEmpty())
    {
        dc::ui::PanelLook look;
        f.panel.setLookAndFeel(&look);
        auto snap = [&](const juce::String& name) {
            const auto img = f.panel.createComponentSnapshot(f.panel.getLocalBounds());
            auto png = juce::File(dir).getChildFile(name);
            png.deleteFile();
            if (auto out = png.createOutputStream())
                juce::PNGImageFormat().writeImageToStream(img, *out);
        };
        f.proc.setReplayRequest(2);
        f.panel.refresh();
        snap("passpanel_normal.png");
        click(f.panel.learnButton());
        click(f.panel.slotButton(1));
        f.note(61);
        click(f.panel.playButton());
        snap("passpanel_learn.png");
        click(f.panel.learnButton());
        f.panel.setLookAndFeel(nullptr);
    }
}

TEST_CASE("midiNoteName names middle C C3", "[passpanel]")
{
    CHECK(dc::ui::midiNoteName(60) == "C3");
    CHECK(dc::ui::midiNoteName(0) == "C-2");
    CHECK(dc::ui::midiNoteName(61) == "C#3");
    CHECK(dc::ui::midiNoteName(127) == "G8");
}
