#pragma once
#include <juce_core/juce_core.h>
#include "plugin/PluginState.h"
#include "plugin/presets/BankFormat.h"

namespace dc {

// Banks on disk: <root>/<bank>/bank.json plus <root>/<bank>/samples/. Holds no cache: every call
// reads the files anew (several plugin instances may share the folder). Plain file IO on the
// calling thread, never the audio thread. Never deletes a sample, never overwrites a damaged or
// too new bank.json.
class BankStore
{
public:
    enum class Error
    {
        None,
        InvalidName,
        NameTaken,
        BankMissing,
        BankUnreadable,
        BankTooNew,
        PresetMissing,
        SampleMissing,
        CopyFailed,
        WriteFailed,
    };

    struct Result
    {
        Error error = Error::None;
        juce::String message; // German, empty when ok
        bool ok() const { return error == Error::None; }
    };

    struct LoadedPreset
    {
        Result result;
        PluginState state; // absolute loop paths into the bank folder
    };

    explicit BankStore(juce::File root);

    // Documents/Dubcycling/Presets
    static juce::File defaultRoot();

    juce::File root() const;
    juce::File bankDir(const juce::String& bank) const;

    // Folders under root() that contain a bank.json, alphabetical (case-insensitive).
    juce::StringArray listBanks() const;

    // Checks bankNameProblem; a name already used by any folder or file in root() is taken.
    // Creates the folder, samples/ and a bank.json without presets.
    Result createBank(const juce::String& name);

    BankParseResult readBank(const juce::String& bank) const;

    // Re-reads bank.json, checks the name, copies the used loops into samples/ (a loop already in
    // samples/ is used as it is; same file name with other content gets a counter, same content is
    // reused) and replaces bank.json atomically. With overwrite an existing preset of that name
    // (case-insensitive, first match) is replaced in place, otherwise the name must be new. A slot
    // with an empty path is stored empty; a used slot whose file is missing or unreadable refuses
    // the save (SampleMissing). On any error bank.json stays unchanged; samples already copied stay.
    Result savePreset(const juce::String& bank, const juce::String& presetName, const PluginState& current, bool overwrite);

    // First preset whose name matches case-insensitively. activeBank/activePreset are set to the
    // bank and the stored preset name.
    LoadedPreset loadPreset(const juce::String& bank, const juce::String& presetName) const;

private:
    juce::File rootDir;
};

} // namespace dc
