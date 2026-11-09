#pragma once
#include <array>
#include <cstddef>
#include <optional>
#include <vector>
#include <juce_data_structures/juce_data_structures.h>
#include "engine/FxSettings.h"
#include "engine/Pass.h"
#include "engine/RuleSet.h"

namespace dc {

constexpr int kStateVersion = 1;
// Limits applied when loading a state (damaged or hand-edited data): further entries are dropped.
constexpr int kMaxRuleEntries = 1024; // rule entries (Loop-1 regions with a rule)
constexpr int kMaxRuleTargets = 1024; // targets per rule entry
constexpr int kMaxRegion1 = 4096;     // region1 at or above this is dropped (RuleSet's limit)

constexpr int kMaxLabelLength = 16;    // characters of a region label
constexpr int kMaxLabelEntries = 1024; // custom labels kept per loop when loading

// Trimmed, control characters removed, cut to kMaxLabelLength characters. Empty = "no custom label".
juce::String sanitizeLabel(const juce::String& text);

constexpr int kMaxNameStoredLength = 64; // characters of a stored bank or preset name

// Trimmed, control characters removed, cut to maxLength characters.
juce::String sanitizeName(const juce::String& text, int maxLength);

// Reference to a loop file (never the audio itself) plus its region boundaries.
struct LoopRef
{
    juce::String path, fileName, hash; // hash = SHA-256 (hex) of the whole file
    int bars = 1;
    std::vector<double> boundaries;
    // Custom region names by region index; an empty or missing entry means the default letter
    // (A, B, ...). Optional in the stored state: sessions saved without labels load unchanged, so
    // kStateVersion stays 1. After deserialize the vector has no trailing empty entries.
    std::vector<juce::String> labels;
};

struct RuleEntry
{
    int region1 = 0;
    std::vector<RuleTarget> targets;
};

struct PluginState
{
    int version = kStateVersion;
    LoopRef loop1, loop2;
    std::vector<RuleEntry> rules;
    // Names of the bank and preset last loaded or saved; empty = none. Optional in the stored
    // state (only written when not empty), so kStateVersion stays 1.
    juce::String activeBank, activePreset;
    // Effect settings (knobs and per-Loop-2-region switches). Optional in the stored state (an Fx
    // element is only written when not the default), so kStateVersion stays 1.
    FxSettings fx;
    // The 8 recorded pass slots. Optional in the stored state (a Passes element is only written when
    // any slot is not empty; a state without it loads 8 empty slots), so kStateVersion stays 1.
    // Sanitized on load through sanitizePass.
    std::array<Pass, kPassSlots> passes;
};

// Shared sanitizing steps of deserialize (also used by the bank file format).
// Boundaries re-added through RegionMap::addBoundary (invalid dropped, sorted, spaced).
std::vector<double> sanitizeBoundaries(const std::vector<double>& raw);
// Stores one custom label; false = kMaxLabelEntries reached, the caller stops. Invalid ones are skipped.
bool addLabelEntry(LoopRef& loop, int& kept, int region, const juce::String& text);
// Appends one target; false = kMaxRuleTargets reached, the caller stops. Negative region2 is skipped.
bool addRuleTarget(std::vector<RuleTarget>& targets, const RuleTarget& t);
// Weights through RuleSet::setTargets; nullopt for region1 outside 0..kMaxRegion1-1, or when the
// result is empty and the source entry had no targets at all (hadTargets = false).
std::optional<RuleEntry> sanitizeRuleEntry(int region1, std::vector<RuleTarget> targets, bool hadTargets);

// Params through sanitizeFxParam, masks to 0..15, switches cut to loop2RegionCount regions.
FxSettings sanitizeFx(FxSettings fx, int loop2RegionCount);

juce::MemoryBlock serialize(const PluginState&);

// nullopt for empty, damaged or unknown-version data; never throws. The result is sanitized:
// boundaries are re-added through RegionMap::addBoundary (invalid ones dropped, sorted, spaced),
// rule weights go through RuleSet::setTargets, entries with negative region1 and targets with
// negative region2 are dropped (region indices that do not exist are kept). Entries with
// region1 >= kMaxRegion1 are dropped; at most kMaxRuleEntries entries with kMaxRuleTargets targets
// each are kept (the first ones in document order).
std::optional<PluginState> deserialize(const void* data, std::size_t size);

// SHA-256 (lowercase hex) of the whole file; empty string if it cannot be read.
juce::String hashFile(const juce::File& file);

} // namespace dc
