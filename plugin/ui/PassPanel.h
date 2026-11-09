#pragma once
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"

namespace dc::ui {

// MIDI note name with middle C (60) as "C3", sharps: 61 -> "C#3", 0 -> "C-2".
juce::String midiNoteName(int note);

// Pass slots row of the editor:
//   PASSES [SAVE]  [1][x] [2][x] ... [8][x]  [PLAY]  [LEARN] [CLEAR ALL]  hint
// Slot buttons show empty (dim), filled, queued (requested, waits for the next pass start: amber)
// and playing (green). A click on a filled slot requests its replay (DubcyclingProcessor::
// setReplayRequest(2 + i)); an empty slot does nothing. PLAY is lit while any replay is requested;
// a click turns the chain on (off -> 1) or any replay off. SAVE (enabled while canSave()) stores the
// last finished pass in the lowest free slot. "x" clears one slot, CLEAR ALL every slot.
//
// LEARN toggles Learn mode (not persisted). In Learn mode a click on a slot, SAVE or PLAY arms that
// target (armLearn): the button waits ("...", amber) until the processor has bound the next MIDI
// note; a second click on a waiting button cancels the wait and removes the target's note. Bound
// buttons show their note name in Learn mode and in the tooltip always. Turning Learn off cancels
// an armed learn. Clear and CLEAR ALL work the same in both modes.
//
// refresh() polls the processor (atomics and the message-thread state, no audio-thread locks); the
// editor calls it from its timer, and every click refreshes at once.
class PassPanel final : public juce::Component
{
public:
    enum class SlotState { Empty, Filled, Queued, Playing };

    explicit PassPanel(DubcyclingProcessor& proc);
    // Learn mode ends with the panel (editor closed): an armed learn is cancelled.
    ~PassPanel() override;

    void refresh();

    // As of the last refresh.
    SlotState slotState(int slot) const;
    bool learnMode() const { return learn_; }
    // The target (0..7 slot, kTargetSave, kTargetPlay) waits for its note.
    bool isWaiting(int target) const { return learn_ && target >= 0 && target == waiting_; }
    // Note name bound to the target ("C3"), empty when unbound.
    juce::String noteText(int target) const;

    juce::TextButton& slotButton(int slot) { return slots_[static_cast<std::size_t>(clampSlot(slot))]; }
    juce::TextButton& clearButton(int slot) { return clears_[static_cast<std::size_t>(clampSlot(slot))]; }
    juce::TextButton& saveButton() { return save_; }
    juce::TextButton& playButton() { return play_; }
    juce::TextButton& learnButton() { return learnBtn_; }
    juce::TextButton& clearAllButton() { return clearAll_; }
    juce::String hintText() const { return hint_.getText(); }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // A narrow TextButton: the panel's background, the text centred without side indents (squeezed
    // rather than cut to "..." when it does not fit).
    class CompactButton final : public juce::TextButton
    {
    public:
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    };

    static constexpr int kTargets = DubcyclingProcessor::kBindTargets;
    static int clampSlot(int slot) { return slot < 0 ? 0 : (slot > kPassSlots - 1 ? kPassSlots - 1 : slot); }
    // A click on a slot / SAVE / PLAY: learns in Learn mode, otherwise triggers.
    void targetClicked(int target);
    void trigger(int target);
    void setLearnMode(bool on);
    juce::TextButton& targetButton(int target);
    juce::String targetName(int target) const;    // "slot 1", "SAVE", "PLAY"
    juce::String targetLabel(int target) const;   // "1", "SAVE", "PLAY"
    juce::String normalTooltip(int target) const; // tooltip outside Learn mode, without the note

    DubcyclingProcessor& proc_;
    bool learn_ = false;
    int waiting_ = -1; // learnArmed() at the last refresh
    std::array<SlotState, kPassSlots> state_ {};
    std::array<int, kTargets> notes_ {}; // boundNote() at the last refresh

    juce::Label caption_;
    std::array<CompactButton, kPassSlots> slots_;
    std::array<CompactButton, kPassSlots> clears_;
    juce::TextButton save_, play_, learnBtn_, clearAll_;
    juce::Label hint_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PassPanel)
};

} // namespace dc::ui
