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
// t.playing == false: silent.
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
    double virtualPpq_ = 0.0; // virtual ppq at the first frame of the next block while playing
    double lastBpm_ = 0.0;
    bool hasLastBpm_ = false;
};

} // namespace dc
