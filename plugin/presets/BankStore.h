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
        BankFull, // kMaxPresetsPerBank reached, no new preset
        ArchiveInvalid,  // import: unsafe, foreign or inconsistent zip content
        ArchiveTooLarge, // import: over the entry or size limit
        SampleNameInvalid, // export: a sample name import would reject (not portable, two spellings)
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
    // the save (SampleMissing). A new name in a bank with kMaxPresetsPerBank presets is refused
    // (BankFull). On any error bank.json stays unchanged; samples already copied stay.
    Result savePreset(const juce::String& bank, const juce::String& presetName, const PluginState& current, bool overwrite);

    // First preset whose name matches case-insensitively. activeBank/activePreset are set to the
    // bank and the stored preset name. Only stored paths of the form "samples/<name>" are resolved
    // (into this bank's samples/); any other path becomes empty while file name and hash stay, so
    // the slot ends up Missing instead of reading a file outside the bank.
    LoadedPreset loadPreset(const juce::String& bank, const juce::String& presetName) const;

    // Error for a bank.json that is missing (BankMissing), too new (BankTooNew) or damaged
    // (BankUnreadable); None when parsed holds a bank.
    static Result checkParsed(const juce::File& bankFile, const BankParseResult& parsed);

private:
    juce::File rootDir;
};

// The file a stored loop path names in bankFolder: only "samples/<name>" resolves (into
// bankFolder/samples); absolute paths, drive letters, backslashes, "..", and subfolders give an
// empty File, so nothing outside the bank is read.
juce::File storedSampleFile(const juce::File& bankFolder, const juce::String& storedPath);

} // namespace dc
