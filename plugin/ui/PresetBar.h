#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"
#include "plugin/presets/BankStore.h"

namespace dc::ui {

// Preset bar in the editor's top row: field BANK (chooses which presets the PRESET field lists and
// where saving goes; loads nothing) and field PRESET (choosing an entry loads it). Loading with
// unsaved changes first asks in the same row ("Ungespeicherte Aenderungen verwerfen?" with LADEN /
// ABBRECHEN). All file IO runs here on the message thread.
class PresetBar final : public juce::Component
{
public:
    enum class Mode { Normal, NamePrompt, ConfirmDiscard };

    PresetBar(DubcyclingProcessor& proc, BankStore& store);

    Mode mode() const { return mode_; }

    // Updates the fields from the processor. Re-reads the bank and preset lists when they are
    // older than a few seconds (the editor calls this on its timer); opening a list, selecting a
    // bank and loading re-read them at once.
    void refresh();

    juce::StringArray bankNames() const { return banks_; }
    juce::StringArray presetNames() const { return presets_; }
    juce::String selectedBank() const { return selectedBank_; }
    // Only a listed bank is taken (its listed spelling); changes the preset list and the save
    // target, loads nothing.
    void selectBank(const juce::String& bank);
    // The active preset (from the processor state) when it belongs to the selected bank; empty
    // otherwise or when the stored bank name is invalid or not listed.
    juce::String activePresetName() const;

    // presetFingerprint of the current state differs from that of the active preset, read anew
    // from its bank; without an active preset (invalid, missing, or its bank unreadable) the
    // current state is compared against PluginState{}.
    bool hasUnsavedChanges() const;

    // Loads the preset of the selected bank (loadPreset + restoreState). With unsaved changes the
    // bar switches to ConfirmDiscard and loads only after confirmDiscard(true). Errors (too new,
    // damaged bank, missing preset) set message() and change nothing.
    void requestLoad(const juce::String& preset);
    // Answer to ConfirmDiscard. true loads, unless the state changed since the question.
    void confirmDiscard(bool load);
    // Last error or hint (German); empty after a successful action.
    juce::String message() const { return message_; }

    // What the fields open: entries ticked for the selected bank / the active preset.
    juce::PopupMenu bankMenu() const;
    juce::PopupMenu presetMenu() const;

    void resized() override;

private:
    // LCD-style field that opens a list.
    class Field final : public juce::Button
    {
    public:
        Field();
        void setDisplay(const juce::String& shown, juce::Colour colour);
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

    private:
        juce::String text_;
        juce::Colour colour_;
    };

    struct ActivePreset
    {
        juce::String bank, preset; // both empty = none
    };

    // Re-reads banks and the selected bank's presets (a damaged bank lists no presets and sets
    // message()).
    void reloadLists();
    void reloadPresets();
    // The processor's active fields when the bank name is valid and in `banks`; otherwise none.
    ActivePreset validActive(const juce::StringArray& banks) const;
    // restoreState with the loaded preset, selects its bank, re-reads the lists.
    void apply(const BankStore::LoadedPreset& loaded);
    void setMessage(const juce::String& text);
    void openBankMenu();
    void openPresetMenu();
    void updateView();

    DubcyclingProcessor& proc_;
    BankStore& store_;
    Mode mode_ = Mode::Normal;
    juce::StringArray banks_, presets_;
    juce::String selectedBank_;
    juce::String message_;
    juce::uint32 messageUntil_ = 0; // when the message stops showing in the PRESET field
    juce::uint32 listsReadAt_ = 0;  // Time::getMillisecondCounter() of the last reloadLists
    struct PendingLoad
    {
        juce::String bank, preset, fingerprint; // fingerprint of the state when asked
    };
    PendingLoad pending_;

    juce::Label bankCaption_, presetCaption_;
    Field bankField_, presetField_;
    juce::Label confirmText_;
    juce::TextButton confirmLoad_, confirmCancel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBar)
};

} // namespace dc::ui
