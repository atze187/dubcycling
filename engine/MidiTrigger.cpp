#include "engine/MidiTrigger.h"
#include "engine/LoopTiming.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace dc {

namespace {
constexpr int kNever = std::numeric_limits<int>::max();

bool usableBpm(double bpm) { return std::isfinite(bpm) && bpm > 0.0; }

enum class Out { Pass, Silent, Play };

struct Piece
{
    int frame = 0;
    Out type = Out::Silent;
    double base = 0.0; // virtual ppq at the piece start (Play)
};
} // namespace

void MidiTrigger::prepare(double sampleRate)
{
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0) ? sampleRate : 48000.0;
    reset();
}

void MidiTrigger::reset()
{
    for (auto& b : held_)
        b.reset();
    heldCount_ = 0;
    state_ = State::Silent;
    virtualPpq_ = 0.0;
    lastBpm_ = 0.0;
    hasLastBpm_ = false;
    if (hasPendingMode_)
        mode_ = pendingMode_;
    hasPendingMode_ = false;
    planned_ = false;
}

void MidiTrigger::setMode(TriggerMode m)
{
    if (!planned_)
    {
        // Nothing has been rendered yet: there is no bar line to wait for.
        mode_ = m;
        hasPendingMode_ = false;
        return;
    }
    if (m == mode_)
    {
        hasPendingMode_ = false;
        return;
    }
    pendingMode_ = m;
    hasPendingMode_ = true;
}

void MidiTrigger::applyEvent(const GateEvent& e)
{
    if (e.kind == GateEvent::Kind::AllOff)
    {
        for (auto& b : held_)
            b.reset();
        heldCount_ = 0;
        return;
    }
    if (e.channel < 0 || e.channel > 15 || e.note < 0 || e.note > 127)
        return;
    auto& bit = held_[static_cast<size_t>(e.channel)];
    const auto idx = static_cast<size_t>(e.note);
    if (e.kind == GateEvent::Kind::NoteOn)
    {
        if (!bit.test(idx))
        {
            bit.set(idx);
            ++heldCount_;
        }
    }
    else if (bit.test(idx))
    {
        bit.reset(idx);
        --heldCount_;
    }
}

int MidiTrigger::plan(const TransportInfo& host, const GateEvent* events, int numEvents, int n,
                      std::array<TriggerSegment, kMaxSegments>& out)
{
    if (n <= 0)
        return 0;
    if (events == nullptr)
        numEvents = 0;

    // Tempo: host if usable, else the last known one, else the default.
    if (usableBpm(host.bpm))
    {
        lastBpm_ = host.bpm;
        hasLastBpm_ = true;
    }
    const double blockBpm = usableBpm(host.bpm) ? host.bpm : (hasLastBpm_ ? lastBpm_ : kDefaultBpm);
    const bool running = host.playing && std::isfinite(host.ppq) && host.ppq >= 0.0 && usableBpm(host.bpm);
    const double virtPerFrame = blockBpm / (60.0 * sampleRate_);
    const double hostPerFrame = running ? host.bpm / (60.0 * sampleRate_) : 0.0;
    const double bpb = beatsPerBar(host.tsNum, host.tsDen);
    const double barPpq0 = running ? std::ceil(host.ppq / bpb - 1e-9) * bpb : 0.0;

    // First host bar line at a frame >= f; kNever if it lies beyond this block.
    auto barAtOrAfter = [&](int f) -> int {
        if (!running)
            return f;
        auto frameOfBar = [&](double k) {
            const double fr = std::ceil((barPpq0 + k * bpb - host.ppq) / hostPerFrame - 1e-9);
            return std::max(0.0, fr);
        };
        double k = std::max(0.0, std::floor((host.ppq + f * hostPerFrame - barPpq0) / bpb) - 1.0);
        for (int guard = 0; guard < 8 && frameOfBar(k) < f; ++guard)
            k += 1.0;
        const double fr = frameOfBar(k);
        return fr >= n ? kNever : static_cast<int>(fr);
    };

    // Armed start/stop happen at a bar line when running, at the edge's own frame otherwise.
    auto armFrameFor = [&](int f) { return running ? barAtOrAfter(f) : f; };

    TriggerMode curMode = mode_;
    int modeFrame = kNever;
    if (hasPendingMode_)
        modeFrame = running ? barAtOrAfter(0) : 0;

    std::array<Piece, kMaxSegments> pieces{};
    int count = 1;
    if (curMode == TriggerMode::Autoplay)
        pieces[0] = {0, Out::Pass, 0.0};
    else if (state_ == State::Playing || state_ == State::ArmedStop)
        pieces[0] = {0, Out::Play, virtualPpq_};
    else
        pieces[0] = {0, Out::Silent, 0.0};

    int armFrame = kNever;
    if (curMode == TriggerMode::Midi && (state_ == State::ArmedStart || state_ == State::ArmedStop))
        armFrame = armFrameFor(0);

    // Bring the state in line with the gate (after an event, a mode switch or a deferred block).
    auto reconcile = [&](int f) {
        if (curMode != TriggerMode::Midi)
            return;
        const bool gate = heldCount_ > 0;
        if (gate && state_ == State::Silent)
        {
            state_ = State::ArmedStart;
            armFrame = armFrameFor(f);
        }
        else if (gate && state_ == State::ArmedStop)
        {
            state_ = State::Playing;
            armFrame = kNever;
        }
        else if (!gate && state_ == State::Playing)
        {
            state_ = State::ArmedStop;
            armFrame = armFrameFor(f);
        }
        else if (!gate && state_ == State::ArmedStart)
        {
            state_ = State::Silent;
            armFrame = kNever;
        }
    };

    // Opens a new output piece at frame f. False if the segment budget is exhausted.
    auto emit = [&](int f, Out type, double base) -> bool {
        Piece& lastPiece = pieces[static_cast<size_t>(count - 1)];
        if (f <= lastPiece.frame)
        {
            lastPiece = {lastPiece.frame, type, base};
            return true;
        }
        if (count >= kMaxSegments)
            return false;
        pieces[static_cast<size_t>(count++)] = {f, type, base};
        return true;
    };

    reconcile(0);

    int i = 0;
    int lastF = 0;
    bool full = false;
    for (;;)
    {
        const int due = std::min(armFrame, modeFrame);
        const bool haveEvent = i < numEvents;
        if (!haveEvent && due >= n)
            break;

        if (haveEvent)
        {
            // Events at or before a bar line count for that bar.
            const int f = std::clamp(std::max(events[i].frame, lastF), 0, n - 1);
            if (f <= due)
            {
                lastF = f;
                applyEvent(events[i]);
                ++i;
                reconcile(f);
                continue;
            }
        }

        lastF = due;
        if (modeFrame <= armFrame)
        {
            const Out o = (pendingMode_ == TriggerMode::Autoplay) ? Out::Pass
                          : (heldCount_ > 0)                       ? Out::Play
                                                                   : Out::Silent;
            if (!emit(due, o, 0.0))
            {
                full = true;
                break;
            }
            curMode = pendingMode_;
            mode_ = curMode;
            hasPendingMode_ = false;
            modeFrame = kNever;
            armFrame = kNever;
            state_ = (curMode == TriggerMode::Midi && heldCount_ > 0) ? State::Playing : State::Silent;
        }
        else
        {
            const bool start = (state_ == State::ArmedStart);
            if (!emit(due, start ? Out::Play : Out::Silent, 0.0))
            {
                full = true;
                break;
            }
            state_ = start ? State::Playing : State::Silent;
            armFrame = kNever;
        }
    }

    // Segment budget exhausted: the remaining state changes wait for the next block, but the
    // held notes must still see every event.
    if (full)
        for (; i < numEvents; ++i)
            applyEvent(events[i]);

    // Convert pieces to segments.
    for (int p = 0; p < count; ++p)
    {
        const Piece& pc = pieces[static_cast<size_t>(p)];
        const int end = (p + 1 < count) ? pieces[static_cast<size_t>(p + 1)].frame : n;
        TriggerSegment& s = out[static_cast<size_t>(p)];
        s.frame = pc.frame;
        s.length = end - pc.frame;
        if (pc.type == Out::Pass)
        {
            s.t = host;
            if (pc.frame > 0 && host.playing && std::isfinite(host.ppq) && usableBpm(host.bpm))
                s.t.ppq = host.ppq + pc.frame * host.bpm / (60.0 * sampleRate_);
        }
        else
        {
            s.t.playing = (pc.type == Out::Play);
            s.t.ppq = (pc.type == Out::Play) ? pc.base : 0.0;
            s.t.bpm = blockBpm;
            s.t.tsNum = host.tsNum;
            s.t.tsDen = host.tsDen;
        }
    }

    const Piece& last = pieces[static_cast<size_t>(count - 1)];
    virtualPpq_ = (last.type == Out::Play) ? last.base + (n - last.frame) * virtPerFrame : 0.0;
    planned_ = true;
    return count;
}

} // namespace dc
