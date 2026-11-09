#include "plugin/ui/PassPanel.h"
#include <algorithm>
#include "plugin/ui/PanelLook.h"

namespace dc::ui {

namespace {
using namespace panel;

constexpr int kCaptionW = 50;
constexpr int kSaveW = 48;
constexpr int kSlotW = 34;
constexpr int kClearW = 16;
constexpr int kSlotGap = 4;
constexpr int kPlayW = 48;
constexpr int kLearnW = 52;
constexpr int kClearAllW = 72;
constexpr int kGap = 6;
constexpr int kHintMinW = 100;

const juce::Colour kPlayingFill { 0xff24402e }; // dark green behind the green text
const juce::Colour kWaitingFill { 0xff3d3222 }; // dark amber
const juce::Colour kLearnFill { 0xff1f3346 };   // dark accent

void paintButton(juce::TextButton& b, juce::Colour fill, juce::Colour text)
{
    b.setColour(juce::TextButton::buttonColourId, fill);
    b.setColour(juce::TextButton::buttonOnColourId, fill);
    b.setColour(juce::TextButton::textColourOffId, text);
    b.setColour(juce::TextButton::textColourOnId, text);
}
} // namespace

void PassPanel::CompactButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const bool on = getToggleState();
    getLookAndFeel().drawButtonBackground(g, *this, findColour(on ? buttonOnColourId : buttonColourId), highlighted,
                                          down);
    auto text = findColour(on ? textColourOnId : textColourOffId);
    if (!isEnabled())
        text = text.withMultipliedAlpha(0.5f);
    g.setColour(text);
    g.setFont(juce::Font(mono(std::min(11.0f, static_cast<float>(getHeight()) * 0.6f))));
    g.drawFittedText(getButtonText(), getLocalBounds().reduced(1, 0), juce::Justification::centred, 1, 0.7f);
}

juce::String midiNoteName(int note)
{
    if (note < 0 || note > 127)
        return {};
    return juce::MidiMessage::getMidiNoteName(note, true, true, 3);
}

PassPanel::PassPanel(DubcyclingProcessor& proc) : proc_(proc)
{
    notes_.fill(-1);
    caption_.setText("PASSES", juce::dontSendNotification);
    caption_.setFont(mono(10.0f));
    caption_.setColour(juce::Label::textColourId, kDim);
    caption_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(caption_);

    for (int i = 0; i < kPassSlots; ++i)
    {
        auto& s = slots_[static_cast<std::size_t>(i)];
        s.setButtonText(juce::String(i + 1));
        s.onClick = [this, i] { targetClicked(i); };
        addAndMakeVisible(s);

        auto& c = clears_[static_cast<std::size_t>(i)];
        c.setButtonText("x");
        c.setTooltip("Clear slot " + juce::String(i + 1));
        c.onClick = [this, i] {
            proc_.clearPass(i);
            refresh();
        };
        addAndMakeVisible(c);
    }

    save_.setButtonText("SAVE");
    save_.onClick = [this] { targetClicked(DubcyclingProcessor::kTargetSave); };
    addAndMakeVisible(save_);

    play_.setButtonText("PLAY");
    play_.onClick = [this] { targetClicked(DubcyclingProcessor::kTargetPlay); };
    addAndMakeVisible(play_);

    learnBtn_.setButtonText("LEARN");
    learnBtn_.onClick = [this] { setLearnMode(!learn_); };
    addAndMakeVisible(learnBtn_);

    clearAll_.setButtonText("CLEAR ALL");
    clearAll_.setTooltip("Clear every slot (cannot be undone)");
    clearAll_.onClick = [this] {
        proc_.clearAllPasses();
        refresh();
    };
    addAndMakeVisible(clearAll_);

    styleLcd(hint_, kAmber, juce::Justification::centredLeft);
    addChildComponent(hint_);

    refresh();
}

PassPanel::~PassPanel()
{
    if (learn_ && proc_.learnArmed() != -1)
        proc_.armLearn(-1);
}

PassPanel::SlotState PassPanel::slotState(int slot) const
{
    return state_[static_cast<std::size_t>(clampSlot(slot))];
}

juce::String PassPanel::noteText(int target) const
{
    if (target < 0 || target >= kTargets)
        return {};
    return midiNoteName(notes_[static_cast<std::size_t>(target)]);
}

juce::TextButton& PassPanel::targetButton(int target)
{
    if (target == DubcyclingProcessor::kTargetSave)
        return save_;
    if (target == DubcyclingProcessor::kTargetPlay)
        return play_;
    return slotButton(target);
}

juce::String PassPanel::targetName(int target) const
{
    if (target == DubcyclingProcessor::kTargetSave)
        return "SAVE";
    if (target == DubcyclingProcessor::kTargetPlay)
        return "PLAY";
    return "slot " + juce::String(target + 1);
}

juce::String PassPanel::targetLabel(int target) const
{
    if (target == DubcyclingProcessor::kTargetSave)
        return "SAVE";
    if (target == DubcyclingProcessor::kTargetPlay)
        return "PLAY";
    return juce::String(target + 1);
}

juce::String PassPanel::normalTooltip(int target) const
{
    if (target == DubcyclingProcessor::kTargetSave)
        return "Save the last finished pass to the lowest free slot";
    if (target == DubcyclingProcessor::kTargetPlay)
        return "Play all saved slots in a chain from the next pass start; click again to return to random playing";
    const juce::String slot = "Slot " + juce::String(target + 1);
    switch (state_[static_cast<std::size_t>(target)])
    {
        case SlotState::Empty:   return slot + ": empty";
        case SlotState::Filled:  return slot + ": click to repeat this pass from the next pass start";
        case SlotState::Queued:  return slot + ": starts at the next pass start (PLAY returns to random playing)";
        case SlotState::Playing: return slot + ": repeating (PLAY returns to random playing)";
    }
    return slot;
}

void PassPanel::targetClicked(int target)
{
    if (!learn_)
    {
        trigger(target);
        refresh();
        return;
    }
    if (proc_.learnArmed() == target)
    {
        // A second click: no note wanted after all, and the old one goes too.
        proc_.armLearn(-1);
        proc_.clearBinding(target);
    }
    else
    {
        proc_.armLearn(target);
    }
    refresh();
}

void PassPanel::trigger(int target)
{
    if (target == DubcyclingProcessor::kTargetSave)
        proc_.savePass();
    else if (target == DubcyclingProcessor::kTargetPlay)
        proc_.setReplayRequest(proc_.replayRequest() != 0 ? 0 : 1);
    else if (proc_.passFilled(target))
        proc_.setReplayRequest(2 + target);
}

void PassPanel::setLearnMode(bool on)
{
    learn_ = on;
    if (!on && proc_.learnArmed() != -1)
        proc_.armLearn(-1);
    refresh();
}

void PassPanel::refresh()
{
    const int request = proc_.replayRequest();
    const int playing = proc_.replaySlot();
    waiting_ = proc_.learnArmed();
    for (int t = 0; t < kTargets; ++t)
        notes_[static_cast<std::size_t>(t)] = proc_.boundNote(t);

    bool anyFilled = false;
    for (int i = 0; i < kPassSlots; ++i)
    {
        const bool filled = proc_.passFilled(i);
        auto& st = state_[static_cast<std::size_t>(i)];
        if (playing == i)
            st = SlotState::Playing; // also while the slot was just cleared: it plays to the pass end
        else if (!filled)
            st = SlotState::Empty;
        else
            st = request == 2 + i ? SlotState::Queued : SlotState::Filled;
        anyFilled = anyFilled || filled;
        clears_[static_cast<std::size_t>(i)].setEnabled(filled);
    }
    clearAll_.setEnabled(anyFilled);

    play_.setToggleState(request != 0, juce::dontSendNotification);
    play_.setEnabled(learn_ || anyFilled || request != 0);
    save_.setEnabled(learn_ || proc_.canSave());
    learnBtn_.setToggleState(learn_, juce::dontSendNotification);
    paintButton(learnBtn_, learn_ ? kLearnFill : kButton, learn_ ? kAccent : kText);
    learnBtn_.setTooltip(learn_ ? "Learn mode: click a slot, SAVE or PLAY, then play a MIDI note to bind it. "
                                  "Click LEARN again to leave."
                                : "Bind MIDI notes to the slots, SAVE and PLAY");

    for (int t = 0; t < kTargets; ++t)
    {
        auto& b = targetButton(t);
        const auto note = noteText(t);
        const bool slot = t < kPassSlots;
        const auto slotState = slot ? state_[static_cast<std::size_t>(t)] : SlotState::Filled;
        juce::Colour fill = kButton;
        juce::Colour text = kText;
        if (slot && slotState == SlotState::Empty)
            text = kDim;
        if (slot && slotState == SlotState::Queued)
            text = kAmber;
        if ((slot && slotState == SlotState::Playing) || (t == DubcyclingProcessor::kTargetPlay && request != 0))
        {
            fill = kPlayingFill;
            text = kGreen;
        }

        juce::String label = targetLabel(t);
        juce::String tip = normalTooltip(t);
        if (note.isNotEmpty())
            tip << " - MIDI note " << note << " (" << notes_[static_cast<std::size_t>(t)] << ")";
        if (learn_)
        {
            if (isWaiting(t))
            {
                label = "...";
                fill = kWaitingFill;
                text = kAmber;
                tip = "Waiting for note: play a MIDI note to bind it to " + targetName(t)
                      + " (click again to cancel and remove its note)";
            }
            else
            {
                if (note.isNotEmpty())
                    label = note;
                tip = (note.isNotEmpty() ? "MIDI note " + note + " (" + juce::String(notes_[static_cast<std::size_t>(t)])
                                               + ") is bound to " + targetName(t) + ". "
                                         : juce::String("No MIDI note bound to " + targetName(t) + ". "))
                      + "Click to learn a new note.";
            }
        }
        b.setButtonText(label);
        b.setTooltip(tip);
        paintButton(b, fill, text);
    }

    if (learn_)
        hint_.setText(waiting_ >= 0 ? "Waiting for note: " + targetName(waiting_) : juce::String("Learn: click a target"),
                      juce::dontSendNotification);
    else
        hint_.setText({}, juce::dontSendNotification);
    hint_.setVisible(learn_ && hint_.getWidth() >= kHintMinW);
}

void PassPanel::paint(juce::Graphics& g)
{
    g.setColour(kStrip);
    g.fillRect(getLocalBounds());
    g.setColour(kEdge);
    g.drawRect(getLocalBounds(), 1);
}

void PassPanel::resized()
{
    auto row = getLocalBounds().reduced(6, 4);
    caption_.setBounds(row.removeFromLeft(kCaptionW));
    row.removeFromLeft(kGap);
    save_.setBounds(row.removeFromLeft(kSaveW));
    row.removeFromLeft(kGap * 2);
    for (int i = 0; i < kPassSlots; ++i)
    {
        if (i > 0)
            row.removeFromLeft(kSlotGap);
        slots_[static_cast<std::size_t>(i)].setBounds(row.removeFromLeft(kSlotW));
        clears_[static_cast<std::size_t>(i)].setBounds(row.removeFromLeft(kClearW));
    }
    row.removeFromLeft(kGap);
    play_.setBounds(row.removeFromLeft(kPlayW));
    row.removeFromLeft(kGap * 2);
    learnBtn_.setBounds(row.removeFromLeft(kLearnW));
    row.removeFromLeft(kGap);
    clearAll_.setBounds(row.removeFromLeft(kClearAllW));
    row.removeFromLeft(kGap * 2);
    hint_.setBounds(row.getWidth() >= kHintMinW ? row : juce::Rectangle<int>());
    hint_.setVisible(learn_ && hint_.getWidth() >= kHintMinW);
}

} // namespace dc::ui
