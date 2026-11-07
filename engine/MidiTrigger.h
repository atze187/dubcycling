#pragma once
#include <array>
#include <bitset>
#include "engine/Engine.h"

namespace dc {

enum class TriggerMode { Autoplay, Midi };

// A gate-relevant MIDI event inside one block. frame is in [0, n).
struct GateEvent
{
    enum class Kind { NoteOn, NoteOff, AllOff };
    int frame = 0;
    Kind kind = Kind::NoteOn;
    int channel = 0; // 0..15
    int note = 0;    // 0..127
};

// One contiguous piece of a block with the transport the engine should see.
// t.playing == false: silent. t.release: the fade-out stretch after a gate-off (see plan()).
struct TriggerSegment
{
    int frame = 0;
    int length = 0;
    TransportInfo t;
};

// Held-note gating with bar-quantised start and stop. Pure logic, no JUCE, audio-thread safe:
// fixed-size storage, no allocation, no locks, no exceptions.
class MidiTrigger
{
public:
    static constexpr int kMaxSegments = 32;
    static constexpr double kDefaultBpm = 120.0;

    void prepare(double sampleRate); // stores the rate, then reset()
    void reset();                    // held notes, gate, armed start/stop, virtual clock, known tempo
    void setMode(TriggerMode m);     // audio thread, between blocks
    // The requested mode (a switch that waits for the next bar line already reads as the new mode).
    TriggerMode mode() const { return hasPendingMode_ ? pendingMode_ : mode_; }

    // events sorted by frame ascending, frame in [0, n). Writes contiguous segments covering [0, n)
    // exactly into `out` and returns their count (1..kMaxSegments; 0 only if n <= 0).
    //
    // Bar lines: a bar line between two frames counts for the next frame, also when that frame is
    // the first one of the next block. A host jump backwards (a loop wrap or a relocation; more than
    // 0.01 beats, the engine's jump tolerance) fires a pending start, stop or mode switch at frame 0 of that block, so a
    // host loop without a bar line cannot keep it waiting. A jump forwards waits for the next bar.
    //
    // Stops: whatever sounded fades out over kCancelFadeMs after the stop frame (a segment with
    // t.playing and t.release, continuing the clock that played; it may continue into the next
    // block), then silence. A start during that fade-out waits until it has ended.
    //
    // n <= 0: note-offs and all-off events still update the held notes (note-ons are ignored).
    int plan(const TransportInfo& host, const GateEvent* events, int numEvents, int n,
             std::array<TriggerSegment, kMaxSegments>& out);

private:
    enum class State { Silent, ArmedStart, Playing, ArmedStop };

    void applyEvent(const GateEvent& e);

    double sampleRate_ = 48000.0;
    TriggerMode mode_ = TriggerMode::Autoplay;
    TriggerMode pendingMode_ = TriggerMode::Autoplay;
    bool hasPendingMode_ = false;
    bool planned_ = false; // a block was planned since the last reset()

    std::array<std::bitset<128>, 16> held_{};
    int heldCount_ = 0;

    State state_ = State::Silent;
    double virtualPpq_ = 0.0; // virtual ppq at the first frame of the next block while playing or fading out
    int releaseFrames_ = 240; // length of the fade-out after a stop (kCancelFadeMs, as Engine's fade)
    int releaseLeft_ = 0;     // frames of a fade-out that continue at the start of the next block
    double expectedHostPpq_ = 0.0; // host ppq the next block should start at (running host)
    bool hasExpectedHost_ = false;
    double lastBpm_ = 0.0;
    bool hasLastBpm_ = false;
};

} // namespace dc
