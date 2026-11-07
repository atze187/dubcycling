#include "engine/MidiTrigger.h"
#include "engine/LoopTiming.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace dc {

namespace {
constexpr int kNever = std::numeric_limits<int>::max();
// A host position this far behind the extrapolated one is a jump back (as Engine's jump tolerance).
constexpr double kJumpBackBeats = 0.01;

bool usableBpm(double bpm) { return std::isfinite(bpm) && bpm > 0.0; }

enum class Out { Pass, Silent, Play, Release };

struct Piece
{
    int frame = 0;
    Out type = Out::Silent;
    double base = 0.0; // ppq at the piece start (Play, Release)
};
} // namespace

void MidiTrigger::prepare(double sampleRate)
{
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0) ? sampleRate : 48000.0;
    // Same length as Engine's voice cancel fade (Engine::prepare), so both end together.
    releaseFrames_ = std::max(1, static_cast<int>(std::lround(kCancelFadeMs * 0.001 * sampleRate_)));
    reset();
}

void MidiTrigger::reset()
{
    for (auto& b : held_)
        b.reset();
    heldCount_ = 0;
    state_ = State::Silent;
    virtualPpq_ = 0.0;
    releaseLeft_ = 0;
    hasExpectedHost_ = false;
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
    if (events == nullptr)
        numEvents = 0;
    if (n <= 0)
    {
        // No frame to put a change on, but a released note must not get lost (it would stick).
        for (int k = 0; k < numEvents; ++k)
            if (events[k].kind != GateEvent::Kind::NoteOn)
                applyEvent(events[k]);
        return 0;
    }

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

    // The host jumped back since the last block (a loop wrap or a relocation): a pending change fires
    // at frame 0 instead of waiting for a bar line that a host loop may never reach.
    const bool jumpedBack = running && hasExpectedHost_ && host.ppq < expectedHostPpq_ - kJumpBackBeats;
    hasExpectedHost_ = running;
    expectedHostPpq_ = running ? host.ppq + n * hostPerFrame : 0.0;

    // First bar line that has not fired yet. A bar line between two frames fires on the later one:
    // one less than a frame before the block start fell after the previous block's last frame, so it
    // fires at frame 0 here; one exactly a frame before fired on that last frame already.
    const double barPpq0 =
        running ? (std::floor((host.ppq - (1.0 - 1e-9) * hostPerFrame) / bpb) + 1.0) * bpb : 0.0;

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
        modeFrame = (running && !jumpedBack) ? barAtOrAfter(0) : 0;

    std::array<Piece, kMaxSegments> pieces{};
    int count = 1;
    int relEnd = kNever;       // frame at which a running fade-out ends (may lie beyond this block)
    bool piece0Carried = true; // pieces[0] continues what sounded at the end of the last block
    if (curMode == TriggerMode::Autoplay)
        pieces[0] = {0, Out::Pass, 0.0};
    else if (state_ == State::Playing || state_ == State::ArmedStop)
        pieces[0] = {0, Out::Play, virtualPpq_};
    else if (releaseLeft_ > 0)
    {
        pieces[0] = {0, Out::Release, virtualPpq_};
        relEnd = releaseLeft_;
    }
    else
        pieces[0] = {0, Out::Silent, 0.0};

    int armFrame = kNever;
    if (curMode == TriggerMode::Midi && (state_ == State::ArmedStart || state_ == State::ArmedStop))
        armFrame = jumpedBack ? 0 : armFrameFor(0);

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
            if (count == 1)
                piece0Carried = false;
            lastPiece = {lastPiece.frame, type, base};
            return true;
        }
        if (count >= kMaxSegments)
            return false;
        pieces[static_cast<size_t>(count++)] = {f, type, base};
        return true;
    };

    // Ends what sounds at frame f: a fade-out on the clock that played (silence follows at relEnd).
    // A piece that starts at f itself never sounded and simply becomes silent.
    auto silenceAt = [&](int f) -> bool {
        const Piece& lp = pieces[static_cast<size_t>(count - 1)];
        const bool sounded = lp.frame < f || (count == 1 && piece0Carried);
        if (sounded && lp.type == Out::Release)
            return true; // already fading out
        double base = 0.0;
        if (sounded && lp.type == Out::Play)
            base = lp.base + (f - lp.frame) * virtPerFrame;
        else if (sounded && lp.type == Out::Pass && running)
            base = host.ppq + f * hostPerFrame;
        else
            return emit(f, Out::Silent, 0.0);
        if (!emit(f, Out::Release, base))
            return false;
        relEnd = f + releaseFrames_;
        return true;
    };

    reconcile(0);

    int i = 0;
    int lastF = 0;
    bool full = false;
    for (;;)
    {
        // A start waits until a fade-out has ended.
        const int armDue =
            (relEnd != kNever && state_ == State::ArmedStart) ? std::max(armFrame, relEnd) : armFrame;
        const int due = std::min({armDue, modeFrame, relEnd});
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
        bool ok = true;
        if (relEnd <= std::min(armDue, modeFrame))
        {
            // The fade-out has ended.
            ok = emit(due, Out::Silent, 0.0);
            if (ok)
                relEnd = kNever;
        }
        else if (modeFrame <= armDue)
        {
            const Out o = (pendingMode_ == TriggerMode::Autoplay) ? Out::Pass
                          : (heldCount_ > 0)                       ? Out::Play
                                                                   : Out::Silent;
            if (o == Out::Silent)
                ok = silenceAt(due);
            else if ((ok = emit(due, o, 0.0)))
                relEnd = kNever;
            if (ok)
            {
                curMode = pendingMode_;
                mode_ = curMode;
                hasPendingMode_ = false;
                modeFrame = kNever;
                armFrame = kNever;
                state_ = (curMode == TriggerMode::Midi && heldCount_ > 0) ? State::Playing : State::Silent;
            }
        }
        else
        {
            const bool start = (state_ == State::ArmedStart);
            ok = start ? emit(due, Out::Play, 0.0) : silenceAt(due);
            if (ok)
            {
                state_ = start ? State::Playing : State::Silent;
                armFrame = kNever;
            }
        }
        if (!ok)
        {
            full = true;
            break;
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
            s.t.release = false;
            if (pc.frame > 0 && host.playing && std::isfinite(host.ppq) && usableBpm(host.bpm))
                s.t.ppq = host.ppq + pc.frame * host.bpm / (60.0 * sampleRate_);
        }
        else
        {
            const bool sounds = (pc.type == Out::Play || pc.type == Out::Release);
            s.t.playing = sounds;
            s.t.ppq = sounds ? pc.base : 0.0;
            s.t.bpm = blockBpm;
            s.t.tsNum = host.tsNum;
            s.t.tsDen = host.tsDen;
            s.t.release = (pc.type == Out::Release);
        }
    }

    const Piece& last = pieces[static_cast<size_t>(count - 1)];
    const bool lastSounds = (last.type == Out::Play || last.type == Out::Release);
    virtualPpq_ = lastSounds ? last.base + (n - last.frame) * virtPerFrame : 0.0;
    releaseLeft_ = (last.type == Out::Release && relEnd != kNever && relEnd > n) ? relEnd - n : 0;
    planned_ = true;
    return count;
}

} // namespace dc
