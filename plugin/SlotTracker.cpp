#include "plugin/SlotTracker.h"

namespace dc {

void SlotTracker::resetAll()
{
    for (auto& s : slots_)
    {
        const ReplaceFailure keep = s.failure; // ids only grow, so an old failure is never shown twice
        s = Slot {};
        s.failure = keep;
    }
}

void SlotTracker::setMissing(int slot)
{
    if (!valid(slot))
        return;
    Slot& s = slots_[slot];
    s.status = SlotStatus::Missing;
    s.data = nullptr;
    s.error = {};
    s.ticket = 0;
    s.pending.reset();
    s.pendingTicket = 0;
}

std::uint64_t SlotTracker::beginLoad(int slot)
{
    if (!valid(slot))
        return 0;
    Slot& s = slots_[slot];
    s.status = SlotStatus::Loading;
    s.data = nullptr;
    s.error = {};
    s.pending.reset();
    s.pendingTicket = 0;
    s.ticket = nextTicket_++;
    return s.ticket;
}

std::uint64_t SlotTracker::beginReplace(int slot, LoopRef replacement)
{
    if (!valid(slot))
        return 0;
    Slot& s = slots_[slot];
    replacement.boundaries.clear();
    s.pending = std::move(replacement);
    s.pendingTicket = nextTicket_++;
    return s.pendingTicket;
}

bool SlotTracker::apply(const LoopLoadResult& r, PluginState& state)
{
    if (!valid(r.slot) || r.ticket == 0)
        return false;
    Slot& s = slots_[r.slot];
    LoopRef& ref = r.slot == 0 ? state.loop1 : state.loop2;

    if (s.pending && r.ticket == s.pendingTicket)
    {
        LoopRef next = std::move(*s.pending);
        s.pending.reset();
        s.pendingTicket = 0;
        if (r.data == nullptr)
        {
            s.failure = { nextFailureId_++, next.fileName, r.error };
            return false; // nothing changed
        }
        if (r.hash.isNotEmpty())
            next.hash = r.hash;
        ref = std::move(next);
        state.rules.clear(); // their region indices no longer mean anything
        s.status = SlotStatus::Loaded;
        s.data = r.data;
        s.error = {};
        s.ticket = 0; // a load of the old file still in flight is stale now
        return true;
    }

    if (r.ticket != s.ticket)
        return false; // stale
    s.ticket = 0;
    if (r.data == nullptr)
    {
        s.data = nullptr;
        s.status = r.error == LoopLoader::kErrorFileNotFound ? SlotStatus::Missing : SlotStatus::Failed;
        s.error = s.status == SlotStatus::Failed ? r.error : juce::String();
        return true;
    }
    s.status = SlotStatus::Loaded;
    s.data = r.data;
    s.error = {};
    // The file at the reference decoded: remember its current hash (e.g. re-exported in place), so a
    // later move of the folder still relinks.
    if (r.hash.isNotEmpty())
        ref.hash = r.hash;
    return true;
}

SlotStatus SlotTracker::status(int slot) const
{
    if (!valid(slot))
        return SlotStatus::Empty;
    const Slot& s = slots_[slot];
    return s.pending ? SlotStatus::Loading : s.status;
}

std::shared_ptr<const StereoData> SlotTracker::data(int slot) const
{
    return valid(slot) ? slots_[slot].data : nullptr;
}

juce::String SlotTracker::error(int slot) const
{
    return valid(slot) ? slots_[slot].error : juce::String();
}

juce::String SlotTracker::pendingFileName(int slot) const
{
    return valid(slot) && slots_[slot].pending ? slots_[slot].pending->fileName : juce::String();
}

SlotTracker::ReplaceFailure SlotTracker::replaceFailure(int slot) const
{
    return valid(slot) ? slots_[slot].failure : ReplaceFailure {};
}

} // namespace dc
