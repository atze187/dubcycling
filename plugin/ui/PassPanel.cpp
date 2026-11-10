#include "plugin/ui/PassPanel.h"
#include <algorithm>
#include "plugin/ui/PanelLook.h"

namespace dc::ui {

namespace {
using namespace panel;

// Bar: top padding, control strip, gap, pads, bottom padding; side margin as the editor's.
constexpr int kMargin = 8;
constexpr int kTopPad = 6;
constexpr int kStripH = 24;
constexpr int kRowGap = 6;
constexpr int kBottomPad = 8;
constexpr int kPadGap = 6;
constexpr int kPadMinH = 72;
constexpr int kPadMaxH = 110;
// Control strip.
constexpr int kPlayW = 64;
constexpr int kSaveW = 64;
constexpr int kLearnW = 64;
constexpr int kClearAllW = 86;
constexpr int kGap = 6;
constexpr int kHintMinW = 100;
constexpr int kHintMaxW = 320;
// Pad parts.
constexpr int kClearSize = 18; // hit area of the clear "x"
constexpr int kInset = 4;      // strip and labels from the pad edge
constexpr int kRegionStripH = 6;

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

PadState padStateFor(int slot, bool filled, int request, int playing)
{
    if (playing == slot)
        return PadState::Playing; // also while the slot was just cleared: it plays to the pass end
    if (!filled)
        return PadState::Empty;
    return request == 2 + slot ? PadState::Queued : PadState::Filled;
}

int PassPanel::heightFor(int editorHeight)
{
    const int pads = std::clamp(editorHeight * 12 / 100, kPadMinH, kPadMaxH);
    return kTopPad + kStripH + kRowGap + pads + kBottomPad;
}

// ---- Buttons ----

void PassPanel::TargetButton::mouseDown(const juce::MouseEvent& e)
{
    consumed_ = e.mods.isPopupMenu() && onRightClick != nullptr && onRightClick();
    if (!consumed_)
        juce::TextButton::mouseDown(e);
}

void PassPanel::TargetButton::mouseDrag(const juce::MouseEvent& e)
{
    if (!consumed_)
        juce::TextButton::mouseDrag(e);
}

void PassPanel::TargetButton::mouseUp(const juce::MouseEvent& e)
{
    if (consumed_)
        consumed_ = false;
    else
        juce::TextButton::mouseUp(e);
}

void PassPanel::ClearButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto text = findColour(textColourOffId);
    if (highlighted || down)
    {
        g.setColour(text.withAlpha(down ? 0.3f : 0.18f));
        g.fillRect(getLocalBounds().reduced(2));
    }
    g.setColour(highlighted ? text : text.withMultipliedAlpha(0.8f));
    g.setFont(juce::Font(mono(12.0f, true)));
    g.drawText("x", getLocalBounds(), juce::Justification::centred, false);
}

PassPanel::Pad::Pad()
{
    clear.setButtonText("x");
    addChildComponent(clear);
}

void PassPanel::Pad::show(int index, PadState st, bool learn, bool waiting, const juce::String& noteLabel,
                          const std::vector<pads::StripSegment>& segs)
{
    clear.setColour(juce::TextButton::textColourOffId, st == PadState::Playing ? pads::padPlayingText(index) : kText);
    if (index == index_ && st == state && learn == learn_ && waiting == waiting_ && noteLabel == note
        && segs == segments)
        return;
    index_ = index;
    state = st;
    learn_ = learn;
    waiting_ = waiting;
    note = noteLabel;
    segments = segs;
    repaint();
}

void PassPanel::Pad::resized()
{
    clear.setBounds(getWidth() - kClearSize - 2, 2, kClearSize, kClearSize);
}

void PassPanel::Pad::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds();
    const auto colour = pads::padColour(index_);
    const bool playing = state == PadState::Playing;
    const bool empty = state == PadState::Empty;

    auto fill = playing ? colour : (empty ? kLcd : pads::padRestingFill(index_));
    if (down)
        fill = fill.brighter(0.15f);
    g.setColour(fill);
    g.fillRect(r);
    const auto text = playing ? pads::padPlayingText(index_) : (empty ? kDim : kText);

    auto inner = r.reduced(kInset);
    // Loop-2 regions of the pass along the bottom: segments in the pad's colour (dark on a playing
    // pad), gaps where Loop 2 is silent, a 1 px cut between touching segments.
    if (!segments.empty())
    {
        const auto track = inner.removeFromBottom(kRegionStripH);
        g.setColour(playing ? text.withAlpha(0.2f) : kLcd.withAlpha(0.7f));
        g.fillRect(track);
        g.setColour(playing ? text : colour);
        const float w = static_cast<float>(track.getWidth());
        for (std::size_t i = 0; i < segments.size(); ++i)
        {
            const auto& s = segments[i];
            const int x0 = track.getX() + juce::roundToInt(s.start * w);
            int x1 = track.getX() + juce::roundToInt(s.end * w);
            if (i + 1 < segments.size() && segments[i + 1].start <= s.end)
                --x1;
            g.fillRect(x0, track.getY(), std::max(1, x1 - x0), track.getHeight());
        }
        inner.removeFromBottom(2);
    }

    // The number, large, in the middle of what is left.
    const float numberH = std::clamp(static_cast<float>(inner.getHeight()) * 0.55f, 16.0f, 44.0f);
    g.setColour(text);
    g.setFont(juce::Font(mono(numberH, true)));
    g.drawText(getButtonText(), inner, juce::Justification::centred, false);

    // Learn mode: "..." while waiting, otherwise the bound note, small, bottom-right.
    if (note.isNotEmpty())
    {
        g.setColour(playing ? text : (waiting_ ? kAmber : kText));
        g.setFont(juce::Font(mono(11.0f, true)));
        g.drawText(note, inner.removeFromBottom(14), juce::Justification::bottomRight, false);
    }

    // Edges: 1 px edge (lighter on hover); playing adds a light inner edge, queued an amber frame,
    // a waiting learn an accent frame.
    g.setColour(highlighted ? kEdgeLight : kEdge);
    g.drawRect(r, 1);
    if (playing)
    {
        g.setColour(colour.brighter(0.8f).withAlpha(0.9f));
        g.drawRect(r.reduced(1), 1);
    }
    if (state == PadState::Queued)
    {
        g.setColour(kAmber);
        g.drawRect(r, 2);
    }
    if (learn_ && waiting_)
    {
        g.setColour(kAccent);
        g.drawRect(r, 2);
    }
}

// ---- Panel ----

juce::String midiNoteName(int note)
{
    if (note < 0 || note > 127)
        return {};
    return juce::MidiMessage::getMidiNoteName(note, true, true, 3);
}

PassPanel::PassPanel(DubcyclingProcessor& proc) : proc_(proc)
{
    notes_.fill(-1);
    for (int i = 0; i < kPassSlots; ++i)
    {
        auto& p = pads_[static_cast<std::size_t>(i)];
        p.setButtonText(juce::String(i + 1));
        p.onClick = [this, i] { targetClicked(i); };
        p.onRightClick = [this, i] { return targetRightClicked(i); };
        p.clear.setTooltip("Clear slot " + juce::String(i + 1));
        p.clear.onClick = [this, i] {
            if (learn_)
                return; // inactive in Learn mode
            proc_.clearPass(i);
            refresh();
        };
        p.show(i, PadState::Empty, false, false, {}, {});
        addAndMakeVisible(p);
    }

    play_.setButtonText("PLAY");
    play_.onClick = [this] { targetClicked(DubcyclingProcessor::kTargetPlay); };
    play_.onRightClick = [this] { return targetRightClicked(DubcyclingProcessor::kTargetPlay); };
    addAndMakeVisible(play_);

    save_.setButtonText("SAVE");
    save_.onClick = [this] { targetClicked(DubcyclingProcessor::kTargetSave); };
    save_.onRightClick = [this] { return targetRightClicked(DubcyclingProcessor::kTargetSave); };
    addAndMakeVisible(save_);

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

juce::String PassPanel::padNote(int slot) const
{
    return pads_[static_cast<std::size_t>(clampSlot(slot))].note;
}

std::vector<pads::StripSegment> PassPanel::padSegments(int slot) const
{
    return pads_[static_cast<std::size_t>(clampSlot(slot))].segments;
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
    // A second click on the waiting target only cancels the wait (a double-click must never delete
    // a binding; removing is the right-click).
    proc_.armLearn(proc_.learnArmed() == target ? -1 : target);
    refresh();
}

bool PassPanel::targetRightClicked(int target)
{
    if (!learn_)
        return false;
    if (proc_.learnArmed() == target)
        proc_.armLearn(-1);
    if (proc_.boundNote(target) >= 0)
        proc_.clearBinding(target);
    refresh();
    return true;
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
    refresh(proc_.getPluginState().passes);
}

void PassPanel::refresh(const std::array<Pass, kPassSlots>& passes)
{
    const int request = proc_.replayRequest();
    const int playing = proc_.replaySlot();
    waiting_ = proc_.learnArmed();
    for (int t = 0; t < kTargets; ++t)
        notes_[static_cast<std::size_t>(t)] = proc_.boundNote(t);

    bool anyFilled = false;
    for (int i = 0; i < kPassSlots; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        const bool filled = !passes[idx].empty();
        state_[idx] = padStateFor(i, filled, request, playing);
        anyFilled = anyFilled || filled;
        auto& x = pads_[idx].clear;
        x.setEnabled(filled && !learn_);
        x.setVisible(filled && !learn_);
    }
    clearAll_.setEnabled(anyFilled);

    play_.setToggleState(request != 0, juce::dontSendNotification);
    play_.setEnabled(learn_ || anyFilled || request != 0);
    save_.setEnabled(learn_ || proc_.canSave());
    learnBtn_.setToggleState(learn_, juce::dontSendNotification);
    paintButton(learnBtn_, learn_ ? kLearnFill : kButton, learn_ ? kAccent : kText);
    learnBtn_.setTooltip(learn_ ? "Learn mode: click a pad, SAVE or PLAY, then play a MIDI note to bind it; "
                                  "right-click one to remove its note. Click LEARN again to leave."
                                : "Bind MIDI notes to the pads, SAVE and PLAY");

    for (int t = 0; t < kTargets; ++t)
    {
        const auto note = noteText(t);
        const bool slot = t < kPassSlots;
        juce::String label = targetLabel(t);
        juce::String tip = normalTooltip(t);
        if (note.isNotEmpty())
            tip << " - MIDI note " << note << " (" << notes_[static_cast<std::size_t>(t)] << ")";
        if (learn_)
        {
            if (isWaiting(t))
            {
                label = "...";
                tip = "Waiting for note: play a MIDI note to bind it to " + targetName(t)
                      + " (click again to cancel, right-click to remove its note)";
            }
            else
            {
                if (note.isNotEmpty())
                    label = note;
                tip = (note.isNotEmpty() ? "MIDI note " + note + " (" + juce::String(notes_[static_cast<std::size_t>(t)])
                                               + ") is bound to " + targetName(t)
                                               + ". Click to learn a new note, right-click to remove it."
                                         : juce::String("No MIDI note bound to " + targetName(t)
                                                        + ". Click to learn a note."));
            }
        }
        auto& b = targetButton(t);
        b.setTooltip(tip);
        if (slot)
        {
            // A pad keeps its number; the Learn label goes to its corner.
            const auto idx = static_cast<std::size_t>(t);
            pads_[idx].show(t, state_[idx], learn_, isWaiting(t), learn_ && label != targetLabel(t) ? label : juce::String(),
                            pads::passStripSegments(passes[idx]));
            continue;
        }
        // SAVE and PLAY: the label itself changes; PLAY lights green while a replay is requested.
        juce::Colour fill = kButton;
        juce::Colour text = kText;
        if (t == DubcyclingProcessor::kTargetPlay && request != 0)
        {
            fill = kPlayingFill;
            text = kGreen;
        }
        if (isWaiting(t))
        {
            fill = kWaitingFill;
            text = kAmber;
        }
        b.setButtonText(label);
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
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void PassPanel::resized()
{
    auto area = getLocalBounds().reduced(kMargin, 0);
    area.removeFromTop(kTopPad);
    area.removeFromBottom(kBottomPad);

    // Control strip: PLAY, SAVE, LEARN | hint | CLEAR ALL.
    auto strip = area.removeFromTop(kStripH);
    play_.setBounds(strip.removeFromLeft(kPlayW));
    strip.removeFromLeft(kGap);
    save_.setBounds(strip.removeFromLeft(kSaveW));
    strip.removeFromLeft(kGap);
    learnBtn_.setBounds(strip.removeFromLeft(kLearnW));
    clearAll_.setBounds(strip.removeFromRight(kClearAllW));
    strip.reduce(kGap * 2, 0);
    const auto hint = strip.withSizeKeepingCentre(std::min(strip.getWidth(), kHintMaxW), strip.getHeight());
    hint_.setBounds(hint.getWidth() >= kHintMinW ? hint : juce::Rectangle<int>());
    hint_.setVisible(learn_ && hint_.getWidth() >= kHintMinW);
    area.removeFromTop(kRowGap);

    // Pads: equal widths (within 1 px) and gaps from margin to margin.
    const int avail = area.getWidth() - kPadGap * (kPassSlots - 1);
    for (int i = 0; i < kPassSlots; ++i)
    {
        const int x0 = area.getX() + i * kPadGap + avail * i / kPassSlots;
        const int x1 = area.getX() + i * kPadGap + avail * (i + 1) / kPassSlots;
        pads_[static_cast<std::size_t>(i)].setBounds(x0, area.getY(), x1 - x0, area.getHeight());
    }
}

} // namespace dc::ui
