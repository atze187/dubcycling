#pragma once
#include <cstddef>
#include <optional>
#include <vector>
#include <juce_data_structures/juce_data_structures.h>
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
};

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
