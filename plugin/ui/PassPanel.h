#pragma once
#include <array>
#include <functional>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"
#include "plugin/ui/PadLook.h"

namespace dc::ui {

// MIDI note name with middle C (60) as "C3", sharps: 61 -> "C#3", 0 -> "C-2".
juce::String midiNoteName(int note);

enum class PadState { Empty, Filled, Queued, Playing };
// State of pass pad `slot` from the processor's view: Playing while replaySlot() names it (also
// while it was just cleared: it plays to the pass end), else Empty when not filled, else Queued
// when replayRequest() asks for it (it waits for the next pass start), else Filled.
PadState padStateFor(int slot, bool filled, int request, int playing);
// Whether a pad shows hover and press feedback: not for an empty pad outside Learn mode (a click
// does nothing there).
bool padReactsToMouse(PadState state, bool learnMode);

// Pad bar at the bottom edge of the editor, over the full window width:
//   [PLAY] [SAVE] [LEARN]            hint LCD            [CLEAR ALL]
//   [  1  ] [  2  ] [  3  ] [  4  ] [  5  ] [  6  ] [  7  ] [  8  ]
// Each pad has its own colour (pads::padColour). Empty: LCD black, dim number. Filled: a dim
// version of its colour, the number in text colour, a strip along the bottom with the pass's Loop-2
// regions (pads::passStripSegments) and a small "x" top-right that clears the slot. Queued
// (requested, waits for the next pass start): 2 px amber frame. Playing: the full colour with dark
// text and a light inner edge.
//
// A click on a filled pad requests its replay (DubcyclingProcessor::setReplayRequest(2 + i)); an
// empty pad does nothing. PLAY is lit (green) while any replay is requested; a click turns the chain
// on (off -> 1) or any replay off. SAVE (enabled while canSave()) stores the last finished pass in
// the lowest free slot. CLEAR ALL clears every slot.
//
// LEARN toggles Learn mode (not persisted). In Learn mode a click on a pad, SAVE or PLAY arms that
// target (armLearn): it waits ("...", amber; a pad also gets an accent frame) until the processor has bound the next MIDI note; a
// second click on a waiting target only cancels the wait (so a double-click never deletes a
// binding). A right-click on a pad, SAVE or PLAY in Learn mode removes that target's note (and
// cancels its wait); outside Learn mode a right-click is a normal click. In Learn mode the pads show
// their note name in a small label bottom-right (SAVE and PLAY as their text); tooltips always name
// it. The pads' clear "x" is inactive in Learn mode; CLEAR ALL works in both. Turning Learn off
// cancels an armed learn.
//
// refresh() polls the processor (atomics and one copy of the message-thread state, no audio-thread
// locks); the editor calls refresh(passes) from its timer with the state it already copied, and
// every click refreshes at once.
class PassPanel final : public juce::Component
{
public:
    using SlotState = PadState;

    explicit PassPanel(DubcyclingProcessor& proc);
    // Learn mode ends with the panel (editor closed): an armed learn is cancelled.
    ~PassPanel() override;

    void refresh();
    // The same with the passes of a PluginState the caller already holds.
    void refresh(const std::array<Pass, kPassSlots>& passes);

    // Height of the bar for an editor of the given height: the pads grow with the window between
    // a minimum (72 px) and a maximum (110 px).
    static int heightFor(int editorHeight);

    // Size of the square the editor's resize corner covers at the panel's bottom-right (0 = none).
    // The pads lay their region strip and note label out around it.
    void setCornerNotch(int size);
    // Where a pad draws its region strip and its note label, in panel coordinates.
    juce::Rectangle<int> padStripBounds(int slot) const;
    juce::Rectangle<int> padNoteBounds(int slot) const;

    // As of the last refresh.
    SlotState slotState(int slot) const;
    bool learnMode() const { return learn_; }
    // The target (0..7 slot, kTargetSave, kTargetPlay) waits for its note.
    bool isWaiting(int target) const { return learn_ && target >= 0 && target == waiting_; }
    // Note name bound to the target ("C3"), empty when unbound.
    juce::String noteText(int target) const;
    // Small label bottom-right on a pad: "..." while its learn waits, its note name in Learn mode.
    juce::String padNote(int slot) const;
    // Loop-2 strip of the pad's pass (passStripSegments).
    std::vector<pads::StripSegment> padSegments(int slot) const;

    juce::TextButton& slotButton(int slot) { return pads_[static_cast<std::size_t>(clampSlot(slot))]; }
    // The pad's clear "x" (a child of the pad).
    juce::TextButton& clearButton(int slot) { return pads_[static_cast<std::size_t>(clampSlot(slot))].clear; }
    juce::TextButton& saveButton() { return save_; }
    juce::TextButton& playButton() { return play_; }
    juce::TextButton& learnButton() { return learnBtn_; }
    juce::TextButton& clearAllButton() { return clearAll_; }
    juce::String hintText() const { return hint_.getText(); }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // A TextButton with a right-click (popup-menu click) handler: when onRightClick returns true the
    // press is consumed (no onClick); otherwise, or without a handler, it acts as a normal click.
    class TargetButton : public juce::TextButton
    {
    public:
        std::function<bool()> onRightClick;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;

    private:
        bool consumed_ = false; // the current press was a consumed right-click
    };
    // The small "x" in a pad's corner: no background, the letter only.
    class ClearButton final : public juce::TextButton
    {
    public:
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    };
    // One pass pad; paints itself from what refresh() hands it.
    class Pad final : public TargetButton
    {
    public:
        Pad();
        // Repaints only when something changed.
        void show(int index, PadState state, bool learn, bool waiting, const juce::String& note,
                  const std::vector<pads::StripSegment>& segments);
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
        void resized() override;
        // Part of the pad (local coordinates) covered by something on top, e.g. the resize corner.
        void setNotch(juce::Rectangle<int> notch);
        // Local rectangles of the region strip and the note label (around the notch).
        juce::Rectangle<int> stripBounds() const;
        juce::Rectangle<int> noteBounds() const;

        ClearButton clear;
        PadState state = PadState::Empty;
        juce::String note;
        std::vector<pads::StripSegment> segments;

    private:
        int index_ = 0;
        bool learn_ = false;
        bool waiting_ = false;
        juce::Rectangle<int> notch_;
    };

    static constexpr int kTargets = DubcyclingProcessor::kBindTargets;
    static int clampSlot(int slot) { return slot < 0 ? 0 : (slot > kPassSlots - 1 ? kPassSlots - 1 : slot); }
    // A click on a pad / SAVE / PLAY: learns in Learn mode, otherwise triggers.
    void targetClicked(int target);
    // A right-click on a pad / SAVE / PLAY: in Learn mode removes the target's note (and cancels
    // its wait) and returns true; outside Learn mode returns false (a normal click).
    bool targetRightClicked(int target);
    void trigger(int target);
    void setLearnMode(bool on);
    juce::TextButton& targetButton(int target);
    juce::String targetName(int target) const;    // "slot 1", "SAVE", "PLAY"
    juce::String targetLabel(int target) const;   // "1", "SAVE", "PLAY"
    juce::String normalTooltip(int target) const; // tooltip outside Learn mode, without the note

    DubcyclingProcessor& proc_;
    bool learn_ = false;
    int waiting_ = -1; // learnArmed() at the last refresh
    int cornerNotch_ = 0;
    std::array<SlotState, kPassSlots> state_ {};
    std::array<int, kTargets> notes_ {}; // boundNote() at the last refresh

    std::array<Pad, kPassSlots> pads_;
    TargetButton save_, play_;
    juce::TextButton learnBtn_, clearAll_;
    juce::Label hint_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PassPanel)
};

} // namespace dc::ui
