#pragma once
#include <optional>
#include <vector>
#include <juce_core/juce_core.h>
#include "plugin/PluginState.h"

namespace dc {

constexpr int kBankVersion = 1;
constexpr int kMaxPresetNameLength = 32;
constexpr int kMaxBankNameLength = 64;
constexpr int kMaxPresetsPerBank = 1024;

// One preset: the session state without audio. state.loopN.path is relative to the bank folder
// (with '/'); activeBank and activePreset are unused.
struct Preset
{
    juce::String name;
    PluginState state;
};

struct Bank
{
    int version = kBankVersion;
    std::vector<Preset> presets;
};

// Empty = the name is fine, otherwise a German message. Problems: empty after sanitizing, longer
// than kMaxPresetNameLength, already in the bank (case-insensitive).
juce::String presetNameProblem(const juce::String& name, const Bank& bank);

// Empty = fine, otherwise a German message. Problems: empty, longer than kMaxBankNameLength,
// characters not allowed in file names, ends with '.' or ' ', reserved Windows name (con, prn,
// aux, nul, com1-9, lpt1-9, also with an extension), already in existingBanks (case-insensitive).
juce::String bankNameProblem(const juce::String& name, const juce::StringArray& existingBanks);

// True for a single file or folder name that is safe on every platform: not empty, not "." or
// "..", none of \ / : * ? " < > | or control characters, no '.' or ' ' at the end, no reserved
// Windows name. No length limit.
bool isPortableFileName(const juce::String& name);

juce::String bankToJson(const Bank& bank);

struct BankParseResult
{
    std::optional<Bank> bank; // nullopt for damaged or too new data
    bool tooNew = false;      // "version" is newer than kBankVersion
};

// Never throws. The loaded presets are sanitized like a loaded session state (see deserialize);
// at most kMaxPresetsPerBank presets are kept, presets without a name are dropped.
BankParseResult bankFromJson(const juce::String& json);

// Canonical text of everything that makes a preset sound the way it does: hash, bars, boundaries
// and labels of both loops plus the rules. Without paths, file names and the active fields.
juce::String presetFingerprint(const PluginState& state);

} // namespace dc
