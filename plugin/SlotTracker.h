#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include "plugin/LoopLoader.h"
#include "plugin/PluginState.h"

namespace dc {

// Missing: the file is not found (relink). Failed: the file exists but does not decode (error()).
enum class SlotStatus { Empty, Loading, Loaded, Missing, Failed };

// Load bookkeeping for the two loop slots: status, decoded data, tickets of in-flight loads and a
// staged replacement. Pure logic, no MessageManager and no thread of its own; not thread-safe
// (DubcyclingProcessor calls it on the message thread under its state lock).
//
// Every request gets a ticket; a loader result counts only if its ticket is still the slot's current
// one, so results of superseded requests are dropped. A replacement (a different loop) is staged:
// the slot keeps its reference, data, regions and all rules until the new file has decoded; only
// then apply() swaps the reference, sets bars and drops the slot's boundaries and all rules. A failed
// replacement changes nothing and is reported through replaceFailure().
class SlotTracker
{
public:
    struct ReplaceFailure
    {
        std::uint64_t id = 0; // 0 = none; increases with every failure (any slot)
        juce::String fileName, error;
    };

    // All slots Empty; every in-flight load and staged replacement becomes stale (new state).
    void resetAll();
    // The slot's file was not found: Missing (drops a staged replacement).
    void setMissing(int slot);
    // Starts loading the slot's own reference: Loading, data cleared, a staged replacement dropped.
    // Returns the ticket for LoopLoader::request.
    std::uint64_t beginLoad(int slot);
    // Stages `replacement` (path, fileName, hash, bars; boundaries are ignored). The slot itself is
    // untouched (a running load of its own file continues). Returns the ticket for LoopLoader::request.
    std::uint64_t beginReplace(int slot, LoopRef replacement);
    // Takes a loader result. Updates the slot and, for a successful load, the stored hash (or, for
    // a successful replacement, the slot's reference, bars, boundaries and all rules) in `state`.
    // True if the data or the state changed (the model must be republished). Stale results: false.
    bool apply(const LoopLoadResult& r, PluginState& state);

    // Loading while a replacement is staged, else the slot's own status. Empty for invalid slots.
    SlotStatus status(int slot) const;
    std::shared_ptr<const StereoData> data(int slot) const;
    // Failed: the loader's error text; empty otherwise.
    juce::String error(int slot) const;
    // File name of the staged replacement; empty if none.
    juce::String pendingFileName(int slot) const;
    // The last replacement of this slot that failed to decode (id 0 = none).
    ReplaceFailure replaceFailure(int slot) const;

private:
    struct Slot
    {
        SlotStatus status = SlotStatus::Empty;
        std::shared_ptr<const StereoData> data;
        std::uint64_t ticket = 0; // 0 = nothing in flight
        juce::String error;
        std::optional<LoopRef> pending;
        std::uint64_t pendingTicket = 0;
        ReplaceFailure failure;
    };
    static bool valid(int slot) { return slot == 0 || slot == 1; }

    Slot slots_[2];
    std::uint64_t nextTicket_ = 1;
    std::uint64_t nextFailureId_ = 1;
};

} // namespace dc
