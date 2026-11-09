#include "engine/Pass.h"

#include <algorithm>
#include <cmath>

namespace dc {

Pass sanitizePass(Pass p)
{
    auto bad = [](const PassEvent& e) {
        return !std::isfinite(e.phase) || (!e.stop && e.region2 < 0);
    };
    p.events.erase(std::remove_if(p.events.begin(), p.events.end(), bad), p.events.end());
    for (auto& e : p.events)
        e.phase = std::clamp(e.phase, 0.0, 1.0);
    std::stable_sort(p.events.begin(), p.events.end(),
                     [](const PassEvent& a, const PassEvent& b) { return a.phase < b.phase; });
    if (static_cast<int>(p.events.size()) > kMaxPassEvents)
        p.events.resize(kMaxPassEvents);
    return p;
}

void PassRecorder::begin()
{
    workCount_ = 0;
    valid_ = true;
    open_ = true;
}

void PassRecorder::add(double phase, bool stop, int region2, PlayMode mode)
{
    if (!open_ || !valid_)
        return;
    if (workCount_ >= kMaxPassEvents)
    {
        valid_ = false;
        return;
    }
    work_[static_cast<size_t>(workCount_++)] = PassEvent { phase, stop, region2, mode };
}

void PassRecorder::finish()
{
    if (!open_)
        return;
    open_ = false;
    if (!valid_)
        return;
    const std::uint32_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_relaxed); // odd: write in progress
    std::atomic_thread_fence(std::memory_order_release);
    std::copy(work_.begin(), work_.begin() + workCount_, pub_.begin());
    pubCount_ = workCount_;
    ++pubId_;
    seq_.store(s + 2, std::memory_order_release); // even: stable
}

void PassRecorder::abandon()
{
    open_ = false;
    workCount_ = 0;
    valid_ = true;
}

bool PassRecorder::read(Pass& out, std::uint32_t& id) const
{
    for (;;)
    {
        const std::uint32_t s1 = seq_.load(std::memory_order_acquire);
        if (s1 & 1u)
            continue;
        if (s1 == 0)
            return false; // nothing published yet
        const int n = std::clamp(pubCount_, 0, kMaxPassEvents);
        const std::uint32_t pid = pubId_;
        out.events.assign(pub_.begin(), pub_.begin() + n);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq_.load(std::memory_order_relaxed) == s1)
        {
            id = pid;
            return true;
        }
    }
}

} // namespace dc
