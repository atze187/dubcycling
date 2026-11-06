#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"
#include "plugin/presets/BankStore.h"

namespace dc::ui {

// Preset bar in the editor's top row: field BANK (chooses which presets the PRESET field lists and
// where saving goes; loads nothing), field PRESET (choosing an entry loads it) and the buttons SAVE,
// SAVE AS and "..." (bank menu). Loading with unsaved changes first asks in the same row
// ("Ungespeicherte Aenderungen verwerfen?" with LADEN / ABBRECHEN); asking for a name shows a name
// field with OK / ABBRECHEN in place of the PRESET field. All file IO runs here on the message
// thread.
class PresetBar final : public juce::Component
{
public:
    enum class Mode { Normal, NamePrompt, ConfirmDiscard };

    // Width at which the fields have their full size and the buttons fit; the editor lays the top
    // row out with it.
    static constexpr int kPreferredWidth = 548;

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

    // SAVE: overwrites the active preset when it is in the selected bank (and still stored there);
    // otherwise like requestSaveAs(). A Missing or Failed slot refuses with a message.
    void requestSave();
    // SAVE AS: switches to NamePrompt; without any bank the prompt also asks for a bank name.
    void requestSaveAs();
    // OK in the name field. Saves under presetName (sanitized) into the selected bank, or creates
    // bankName first when the prompt asks for a bank. A bad or taken name, a Missing or Failed slot
    // or a failed save sets message() and stays in NamePrompt. On success the preset is active.
    void submitName(const juce::String& presetName, const juce::String& bankName = {});
    // ABBRECHEN in the name field: back to Normal, nothing written.
    void cancel();
    // "New bank...": NamePrompt asking for the name of a new bank (OK calls createBank).
    void requestNewBank();
    // Creates and selects the bank; a bad or taken name sets message() (and keeps the name field
    // open when it asked for the name).
    void createBank(const juce::String& name);
    // Writes the selected bank as a zip; the message names the file or the error.
    void exportBankTo(const juce::File& zip);
    // Imports a zip as a new bank and selects it; the message names the bank or the error.
    void importBankFrom(const juce::File& zip);

    // Last error or hint (German); empty after a successful load, save or bank creation.
    juce::String message() const { return message_; }

    // What the fields open: entries ticked for the selected bank / the active preset.
    juce::PopupMenu bankMenu() const;
    juce::PopupMenu presetMenu() const;
    // What "..." opens: New bank..., Export bank..., Import bank..., Open presets folder.
    juce::PopupMenu moreMenu() const;

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

    // What the open name field asks for.
    enum class Prompt { Preset, PresetAndBank, NewBank };

    // Re-reads banks and the selected bank's presets (a damaged bank lists no presets and sets
    // message()).
    void reloadLists();
    void reloadPresets();
    // The processor's active fields when the bank name is valid and in `banks`; otherwise none.
    ActivePreset validActive(const juce::StringArray& banks) const;
    // restoreState with the loaded preset, selects its bank, re-reads the lists.
    void apply(const BankStore::LoadedPreset& loaded);
    // Message for a Missing or Failed slot (Spec 4); empty when both slots can be saved.
    juce::String slotProblem() const;
    // savePreset + setActivePreset; on success back to Normal with the lists re-read, otherwise
    // sets message() and leaves the mode as it is.
    void save(const juce::String& bank, const juce::String& name, bool overwrite);
    void openPrompt(Prompt prompt);
    void confirmPrompt(); // OK / Enter in the name field
    // Error (warn colour) or hint (green); empty clears.
    void setMessage(const juce::String& text);
    void setHint(const juce::String& text);
    void refuse(const juce::String& text); // setMessage + updateView
    void openBankMenu();
    void openPresetMenu();
    void openMoreMenu();
    void chooseExportFile();
    void chooseImportFile();
    void openPresetFolder();
    void updateView();

    DubcyclingProcessor& proc_;
    BankStore& store_;
    Mode mode_ = Mode::Normal;
    juce::StringArray banks_, presets_;
    juce::String selectedBank_;
    juce::String message_;
    juce::Colour messageColour_;
    juce::uint32 messageUntil_ = 0; // when the message stops showing (PRESET field or name prompt)
    Prompt prompt_ = Prompt::Preset; // meaningful in NamePrompt
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
    juce::TextButton save_, saveAs_, more_;
    juce::TextEditor bankNameEditor_, nameEditor_; // bankNameEditor_ only for Prompt::PresetAndBank
    juce::Label promptMessage_;
    juce::TextButton promptOk_, promptCancel_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBar)
};

} // namespace dc::ui
