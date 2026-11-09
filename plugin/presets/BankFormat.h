#pragma once
#include <optional>
#include <vector>
#include <juce_core/juce_core.h>
#include "plugin/PluginState.h"

namespace dc {

// 2 adds the per-preset "fx" object, 3 the per-target "mix" in the rules, 4 the per-preset "passes"
// array (8 slots, each a list of events); version 1 to 3 files are still read (default fx, mix 0.5,
// empty pass slots).
constexpr int kBankVersion = 4;
constexpr int kMaxPresetNameLength = 32;
constexpr int kMaxBankNameLength = 64;
constexpr int kMaxPresetsPerBank = 1024;
constexpr int kMaxJsonDepth = 64; // deeper nesting of [ and { is treated as damaged (parser recursion)

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

// Doubles are written with as many digits as it takes (at most 17 significant) to read them back
// exactly, so a saved preset compares equal to the state it was saved from.
juce::String bankToJson(const Bank& bank);

struct BankParseResult
{
    std::optional<Bank> bank; // nullopt for damaged or too new data
    bool tooNew = false;      // "version" is newer than kBankVersion
    int skipped = 0;          // entries of "presets" that were not read (see bankFromJson)
};

// Never throws. The loaded presets are sanitized like a loaded session state (see deserialize);
// at most kMaxPresetsPerBank presets are kept, entries that are no object or have no name are
// dropped; every dropped entry counts in `skipped`. Numbers are clamped as doubles before they
// become ints. JSON nested deeper than kMaxJsonDepth counts as damaged (checked before parsing,
// the parser recurses).
BankParseResult bankFromJson(const juce::String& json);

// Canonical text of everything that makes a preset sound the way it does: hash, bars, boundaries
// and labels of both loops plus the rules and, unless default, the effect settings and the non-empty pass slots. Without paths, file names and the active fields.
juce::String presetFingerprint(const PluginState& state);

} // namespace dc
