#pragma once
#include <cstddef>
#include <optional>
#include <vector>
#include <juce_data_structures/juce_data_structures.h>
#include "engine/RuleSet.h"

namespace dc {

constexpr int kStateVersion = 1;

// Reference to a loop file (never the audio itself) plus its region boundaries.
struct LoopRef
{
    juce::String path, fileName, hash; // hash = SHA-256 (hex) of the whole file
    int bars = 1;
    std::vector<double> boundaries;
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
};

juce::MemoryBlock serialize(const PluginState&);

// nullopt for empty, damaged or unknown-version data; never throws. The result is sanitized:
// boundaries are re-added through RegionMap::addBoundary (invalid ones dropped, sorted, spaced),
// rule weights go through RuleSet::setTargets, entries with negative region1 and targets with
// negative region2 are dropped (region indices that do not exist are kept).
std::optional<PluginState> deserialize(const void* data, std::size_t size);

// SHA-256 (lowercase hex) of the whole file; empty string if it cannot be read.
juce::String hashFile(const juce::File& file);

} // namespace dc
