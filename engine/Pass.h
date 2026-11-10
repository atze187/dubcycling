#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include "engine/RuleSet.h"

namespace dc {

constexpr int kPassSlots = 8;
constexpr int kMaxPassEvents = 256;

// One Loop-2 event inside a pass. region2 and mode are only meaningful when !stop.
struct PassEvent
{
    double phase = 0.0;
    bool stop = false;
    int region2 = -1;
    PlayMode mode = PlayMode::Gate;

    bool operator==(const PassEvent&) const = default;
};

struct Pass
{
    std::vector<PassEvent> events;
    bool empty() const { return events.empty(); }
    bool operator==(const Pass&) const = default;
};

// Drops non-finite phases and Starts with region2 < 0, clamps phase to [0, 1],
// stable-sorts by phase and cuts to kMaxPassEvents.
Pass sanitizePass(Pass p);

// Records one pass. The audio thread writes, any thread reads. Fixed storage, no allocation,
// no locks. Readers get a consistent snapshot via a seqlock. Only one thread may write.
class PassRecorder
{
public:
    void begin();
    void add(double phase, bool stop, int region2, PlayMode mode);
    void finish();
    void abandon();
    bool isOpen() const { return open_; }

    // False if no pass has finished yet. Retries while a write is in progress.
    bool read(Pass& out, std::uint32_t& id) const;

private:
    // Writer-private open pass.
    std::array<PassEvent, kMaxPassEvents> work_{};
    int workCount_ = 0;
    bool open_ = false;
    bool valid_ = true;

    // Published pass, guarded by seq_ (odd = write in progress).
    std::array<PassEvent, kMaxPassEvents> pub_{};
    int pubCount_ = 0;
    std::uint32_t pubId_ = 0;
    std::atomic<std::uint32_t> seq_ { 0 };
};

} // namespace dc
