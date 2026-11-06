#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <limits>
#include <vector>
#include "engine/MidiTrigger.h"

using Catch::Approx;
using dc::GateEvent;
using dc::MidiTrigger;
using dc::TransportInfo;
using dc::TriggerMode;
using dc::TriggerSegment;
using Seg = std::array<TriggerSegment, MidiTrigger::kMaxSegments>;
using Kind = GateEvent::Kind;

namespace {

constexpr double kSr = 48000.0;
constexpr double kPerFrame = 1.0 / 24000.0; // 120 BPM

TransportInfo hostAt(double ppq, bool playing = true, double bpm = 120.0, int num = 4, int den = 4)
{
    TransportInfo t;
    t.playing = playing;
    t.ppq = ppq;
    t.bpm = bpm;
    t.tsNum = num;
    t.tsDen = den;
    return t;
}

GateEvent on(int frame, int note = 60, int ch = 0) { return {frame, Kind::NoteOn, ch, note}; }
GateEvent off(int frame, int note = 60, int ch = 0) { return {frame, Kind::NoteOff, ch, note}; }

MidiTrigger makeMidi()
{
    MidiTrigger m;
    m.prepare(kSr);
    m.setMode(TriggerMode::Midi);
    return m;
}

int run(MidiTrigger& m, const TransportInfo& h, const std::vector<GateEvent>& ev, Seg& out, int n = 512)
{
    return m.plan(h, ev.data(), static_cast<int>(ev.size()), n, out);
}

void requireValid(const Seg& s, int count, int n)
{
    REQUIRE(count >= 1);
    REQUIRE(count <= MidiTrigger::kMaxSegments);
    int pos = 0;
    for (int i = 0; i < count; ++i)
    {
        REQUIRE(s[static_cast<size_t>(i)].frame == pos);
        REQUIRE(s[static_cast<size_t>(i)].length > 0);
        pos += s[static_cast<size_t>(i)].length;
    }
    REQUIRE(pos == n);
}

} // namespace

TEST_CASE("MidiTrigger: autoplay passes the host transport through", "[MidiTrigger]")
{
    MidiTrigger m;
    m.prepare(kSr);
    Seg s;
    const auto host = hostAt(3.99, true, 133.0, 7, 8);
    const std::vector<GateEvent> ev{on(10), off(300)};
    const int c = run(m, host, ev, s);
    REQUIRE(m.mode() == TriggerMode::Autoplay);
    REQUIRE(c == 1);
    REQUIRE(s[0].frame == 0);
    REQUIRE(s[0].length == 512);
    REQUIRE(s[0].t.playing == host.playing);
    REQUIRE(s[0].t.ppq == host.ppq);
    REQUIRE(s[0].t.bpm == host.bpm);
    REQUIRE(s[0].t.tsNum == host.tsNum);
    REQUIRE(s[0].t.tsDen == host.tsDen);
}

TEST_CASE("MidiTrigger: midi mode without notes is silent", "[MidiTrigger]")
{
    auto m = makeMidi();
    Seg s;
    REQUIRE(run(m, hostAt(3.99), {}, s) == 1);
    REQUIRE(s[0].length == 512);
    REQUIRE_FALSE(s[0].t.playing);
}

TEST_CASE("MidiTrigger: a running note-on at frame 100 starts at the next bar line (frame 240) from virtual ppq 0",
          "[MidiTrigger]")
{
    auto m = makeMidi();
    Seg s;
    REQUIRE(run(m, hostAt(3.99), {on(100)}, s) == 2);
    requireValid(s, 2, 512);
    REQUIRE(s[0].frame == 0);
    REQUIRE(s[0].length == 240);
    REQUIRE_FALSE(s[0].t.playing);
    REQUIRE(s[1].frame == 240);
    REQUIRE(s[1].length == 272);
    REQUIRE(s[1].t.playing);
    REQUIRE(s[1].t.ppq == 0.0);
    REQUIRE(s[1].t.bpm == 120.0);

    // Next block: the host is past the bar; the virtual clock continues.
    REQUIRE(run(m, hostAt(4.0 + 272 * kPerFrame), {}, s) == 1);
    REQUIRE(s[0].t.playing);
    REQUIRE(s[0].t.ppq == Approx(272 * kPerFrame).margin(1e-12));
}

TEST_CASE("MidiTrigger: a note-on after the bar-line frame waits for the next bar", "[MidiTrigger]")
{
    {
        auto m = makeMidi();
        Seg s;
        REQUIRE(run(m, hostAt(3.99), {on(241)}, s) == 1);
        REQUIRE_FALSE(s[0].t.playing);
        // The start is armed for the following bar (ppq 8.0).
        REQUIRE(run(m, hostAt(7.99), {}, s) == 2);
        REQUIRE(s[1].frame == 240);
        REQUIRE(s[1].t.playing);
    }
    {
        auto m = makeMidi();
        Seg s;
        REQUIRE(run(m, hostAt(3.99), {on(240)}, s) == 2);
        REQUIRE(s[1].frame == 240);
        REQUIRE(s[1].t.playing);
    }
}

TEST_CASE("MidiTrigger: a note released before the start bar never plays", "[MidiTrigger]")
{
    auto m = makeMidi();
    Seg s;
    REQUIRE(run(m, hostAt(3.99), {on(10), off(20)}, s) == 1);
    REQUIRE_FALSE(s[0].t.playing);
    REQUIRE(run(m, hostAt(4.0 + 512 * kPerFrame - 0.01 + 0.01), {}, s) == 1);
    REQUIRE_FALSE(s[0].t.playing);
    REQUIRE(run(m, hostAt(7.99), {}, s) == 1);
    REQUIRE_FALSE(s[0].t.playing);
}

TEST_CASE("MidiTrigger: releasing the last note stops at the next bar line", "[MidiTrigger]")
{
    auto m = makeMidi();
    Seg s;
    run(m, hostAt(3.99), {on(0)}, s); // starts at 240
    run(m, hostAt(4.5), {}, s);
    REQUIRE(run(m, hostAt(7.99), {off(100)}, s) == 2);
    requireValid(s, 2, 512);
    REQUIRE(s[0].t.playing);
    REQUIRE(s[0].length == 240);
    REQUIRE_FALSE(s[1].t.playing);
    REQUIRE(s[1].frame == 240);
    REQUIRE(s[1].length == 272);
}

TEST_CASE("MidiTrigger: a re-press before the pending stop cancels it", "[MidiTrigger]")
{
    auto m = makeMidi();
    Seg s;
    run(m, hostAt(3.99), {on(0)}, s);
    run(m, hostAt(4.5), {}, s);
    // Release at 100, press again at 200, bar line at 240: keeps playing.
    REQUIRE(run(m, hostAt(7.99), {off(100), on(200)}, s) == 1);
    REQUIRE(s[0].t.playing);
    // The stop is gone for later blocks too.
    REQUIRE(run(m, hostAt(11.99), {}, s) == 1);
    REQUIRE(s[0].t.playing);
}

TEST_CASE("MidiTrigger: chords and duplicate note-ons", "[MidiTrigger]")
{
    auto m = makeMidi();
    Seg s;
    run(m, hostAt(3.99), {on(0, 60), on(0, 64)}, s); // playing from 240
    run(m, hostAt(4.5), {}, s);
    // One of two notes released: still playing through the bar.
    REQUIRE(run(m, hostAt(7.99), {off(10, 60)}, s) == 1);
    REQUIRE(s[0].t.playing);
    REQUIRE(run(m, hostAt(8.5), {}, s) == 1);
    REQUIRE(s[0].t.playing);
    // Duplicate note-on on the same note, one note-off: released.
    REQUIRE(run(m, hostAt(11.5), {on(0, 62), on(5, 62), off(10, 62), off(20, 64)}, s) == 1);
    REQUIRE(s[0].t.playing);
    REQUIRE(run(m, hostAt(15.99), {}, s) == 2);
    REQUIRE_FALSE(s[1].t.playing);
    REQUIRE(s[1].frame == 240);

    // A note-off without a note-on changes nothing.
    auto m2 = makeMidi();
    REQUIRE(run(m2, hostAt(3.99), {off(10)}, s) == 1);
    REQUIRE_FALSE(s[0].t.playing);
    REQUIRE(run(m2, hostAt(7.99), {on(0), off(5, 61)}, s) == 2);
    REQUIRE(s[1].t.playing);
}

TEST_CASE("MidiTrigger: all-notes-off clears the gate", "[MidiTrigger]")
{
    auto m = makeMidi();
    Seg s;
    run(m, hostAt(3.99), {on(0, 60), on(0, 64, 3)}, s);
    run(m, hostAt(4.5), {}, s);
    GateEvent all{100, Kind::AllOff, 0, 0};
    REQUIRE(run(m, hostAt(7.99), {all}, s) == 2);
    REQUIRE(s[0].t.playing);
    REQUIRE_FALSE(s[1].t.playing);
}

TEST_CASE("MidiTrigger: stopped host starts at the event frame and stops at once", "[MidiTrigger]")
{
    auto m = makeMidi();
    Seg s;
    // Tempo known from an earlier running block.
    run(m, hostAt(0.0, true, 90.0), {}, s);
    REQUIRE(run(m, hostAt(0.0, false, 90.0), {on(50)}, s) == 2);
    requireValid(s, 2, 512);
    REQUIRE_FALSE(s[0].t.playing);
    REQUIRE(s[0].length == 50);
    REQUIRE(s[1].frame == 50);
    REQUIRE(s[1].length == 462);
    REQUIRE(s[1].t.playing);
    REQUIRE(s[1].t.ppq == 0.0);
    REQUIRE(s[1].t.bpm == 90.0);

    // Playing host, release at 50 -> stop at 50 only if the host is stopped; a stopped host here.
    REQUIRE(run(m, hostAt(0.0, false, 90.0), {off(50)}, s) == 2);
    REQUIRE(s[0].t.playing);
    REQUIRE(s[0].length == 50);
    REQUIRE_FALSE(s[1].t.playing);
    REQUIRE(s[1].frame == 50);

    // No tempo ever known: 120 BPM.
    auto m2 = makeMidi();
    REQUIRE(run(m2, hostAt(0.0, false, 0.0), {on(50)}, s) == 2);
    REQUIRE(s[1].t.bpm == 120.0);
}

TEST_CASE("MidiTrigger: no valid position counts as stopped", "[MidiTrigger]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    Seg s;
    {
        auto m = makeMidi();
        REQUIRE(run(m, hostAt(nan), {on(50)}, s) == 2);
        REQUIRE(s[1].frame == 50);
        REQUIRE(s[1].t.playing);
        REQUIRE(s[1].t.bpm == 120.0);
    }
    {
        auto m = makeMidi();
        run(m, hostAt(1.0, true, 100.0), {}, s); // last known tempo
        REQUIRE(run(m, hostAt(3.99, true, 0.0), {on(50)}, s) == 2);
        REQUIRE(s[1].frame == 50);
        REQUIRE(s[1].t.playing);
        REQUIRE(s[1].t.bpm == 100.0);
    }
    {
        auto m = makeMidi();
        REQUIRE(run(m, hostAt(-1.0), {on(50)}, s) == 2);
        REQUIRE(s[1].frame == 50);
    }
}

TEST_CASE("MidiTrigger: an armed start becomes immediate when the host stops", "[MidiTrigger]")
{
    auto m = makeMidi();
    Seg s;
    REQUIRE(run(m, hostAt(3.0), {on(10)}, s) == 1); // bar at 4.0 is beyond this block
    REQUIRE_FALSE(s[0].t.playing);
    REQUIRE(run(m, hostAt(3.5, false), {}, s) == 1);
    REQUIRE(s[0].t.playing);
    REQUIRE(s[0].t.ppq == 0.0);
}

TEST_CASE("MidiTrigger: an armed stop becomes immediate when the host stops", "[MidiTrigger]")
{
    auto m = makeMidi();
    Seg s;
    run(m, hostAt(3.99), {on(0)}, s);
    run(m, hostAt(4.5), {}, s);
    REQUIRE(run(m, hostAt(5.0), {off(10)}, s) == 1); // armed, bar at 8.0 not in this block
    REQUIRE(s[0].t.playing);
    REQUIRE(run(m, hostAt(5.2, false), {}, s) == 1);
    REQUIRE_FALSE(s[0].t.playing);
}

TEST_CASE("MidiTrigger: playback is decoupled from host jumps", "[MidiTrigger]")
{
    auto m = makeMidi();
    Seg s;
    run(m, hostAt(3.99), {on(0)}, s); // virtual ppq 272/24000 after this block
    REQUIRE(run(m, hostAt(1.0), {}, s) == 1);
    REQUIRE(s[0].t.playing);
    REQUIRE(s[0].t.ppq == Approx(272 * kPerFrame).margin(1e-12));
    REQUIRE(run(m, hostAt(100.0), {}, s) == 1);
    REQUIRE(s[0].t.ppq == Approx((272 + 512) * kPerFrame).margin(1e-12));
    // Host stops and restarts: still continuous.
    REQUIRE(run(m, hostAt(0.0, false), {}, s) == 1);
    REQUIRE(s[0].t.ppq == Approx((272 + 1024) * kPerFrame).margin(1e-12));
}

TEST_CASE("MidiTrigger: bar lines follow the time signature", "[MidiTrigger]")
{
    Seg s;
    {
        auto m = makeMidi();
        REQUIRE(run(m, hostAt(2.99, true, 120.0, 3, 4), {on(0)}, s) == 2);
        REQUIRE(s[1].frame == 240);
        REQUIRE(s[1].t.tsNum == 3);
    }
    {
        auto m = makeMidi();
        REQUIRE(run(m, hostAt(2.99, true, 120.0, 6, 8), {on(0)}, s) == 2);
        REQUIRE(s[1].frame == 240);
        REQUIRE(s[1].t.tsDen == 8);
    }
    {
        // 4/4 has no bar at 3.0.
        auto m = makeMidi();
        REQUIRE(run(m, hostAt(2.99), {on(0)}, s) == 1);
    }
}

TEST_CASE("MidiTrigger: a block longer than a bar", "[MidiTrigger]")
{
    auto m = makeMidi();
    Seg s;
    const int n = 200000;
    REQUIRE(run(m, hostAt(0.5), {on(0)}, s, n) == 2);
    requireValid(s, 2, n);
    REQUIRE(s[1].frame == 84000); // ppq 4.0 is 3.5 beats = 84000 frames in
    REQUIRE(s[1].t.playing);
    REQUIRE(s[1].t.ppq == 0.0);
}

TEST_CASE("MidiTrigger: mode switching", "[MidiTrigger]")
{
    Seg s;
    SECTION("midi playing to autoplay, running host: pass-through from the next bar")
    {
        auto m = makeMidi();
        run(m, hostAt(3.99), {on(0)}, s);
        run(m, hostAt(4.5), {}, s);
        m.setMode(TriggerMode::Autoplay);
        REQUIRE(m.mode() == TriggerMode::Autoplay);
        const auto host = hostAt(7.99);
        REQUIRE(run(m, host, {}, s) == 2);
        requireValid(s, 2, 512);
        REQUIRE(s[0].t.playing);
        REQUIRE(s[1].frame == 240);
        REQUIRE(s[1].t.playing);
        REQUIRE(s[1].t.ppq == Approx(7.99 + 240 * kPerFrame));
        // And on from then on.
        REQUIRE(run(m, hostAt(8.5), {}, s) == 1);
        REQUIRE(s[0].t.ppq == 8.5);
    }
    SECTION("midi playing to autoplay, stopped host: at once")
    {
        auto m = makeMidi();
        run(m, hostAt(0.0, false), {on(0)}, s);
        m.setMode(TriggerMode::Autoplay);
        REQUIRE(run(m, hostAt(0.0, false), {}, s) == 1);
        REQUIRE_FALSE(s[0].t.playing); // the host is stopped, so pass-through is silent too
        REQUIRE(s[0].t.ppq == 0.0);
        REQUIRE(run(m, hostAt(2.0, true), {}, s) == 1);
        REQUIRE(s[0].t.ppq == 2.0);
    }
    SECTION("autoplay to midi without a note: silent from the next bar")
    {
        MidiTrigger m;
        m.prepare(kSr);
        run(m, hostAt(3.0), {}, s);
        m.setMode(TriggerMode::Midi);
        REQUIRE(run(m, hostAt(3.99), {}, s) == 2);
        REQUIRE(s[0].t.playing);
        REQUIRE(s[1].frame == 240);
        REQUIRE_FALSE(s[1].t.playing);
    }
    SECTION("autoplay to midi with a held note: restarts from virtual ppq 0 at the next bar")
    {
        MidiTrigger m;
        m.prepare(kSr);
        run(m, hostAt(3.0), {on(5)}, s);
        m.setMode(TriggerMode::Midi);
        REQUIRE(run(m, hostAt(3.99), {}, s) == 2);
        REQUIRE(s[0].t.playing);
        REQUIRE(s[0].t.ppq == 3.99);
        REQUIRE(s[1].frame == 240);
        REQUIRE(s[1].t.playing);
        REQUIRE(s[1].t.ppq == 0.0);
    }
    SECTION("a switch back before the bar cancels the pending change")
    {
        auto m = makeMidi();
        run(m, hostAt(1.0), {}, s);
        m.setMode(TriggerMode::Autoplay);
        m.setMode(TriggerMode::Midi);
        REQUIRE(run(m, hostAt(3.99), {}, s) == 1);
        REQUIRE_FALSE(s[0].t.playing);
    }
}

TEST_CASE("MidiTrigger: segments are always valid", "[MidiTrigger]")
{
    Seg s;
    std::vector<GateEvent> ev;
    for (int i = 0; i < 1000; ++i)
        ev.push_back(i % 2 == 0 ? on(i * 2 / 4 + 1) : off(i * 2 / 4 + 1));
    const int n = 1024;
    for (const bool hostRunning : {false, true})
    {
        auto m = makeMidi();
        for (int block = 0; block < 4; ++block)
        {
            const int c = run(m, hostAt(3.99 + block * 0.02, hostRunning), ev, s, n);
            requireValid(s, c, n);
        }
    }

    // reset() clears held notes and arms.
    auto m = makeMidi();
    run(m, hostAt(3.99), {on(0)}, s); // playing from 240
    m.reset();
    REQUIRE(run(m, hostAt(7.99), {}, s) == 1);
    REQUIRE_FALSE(s[0].t.playing);
    run(m, hostAt(3.0), {on(10)}, s); // armed start
    m.reset();
    REQUIRE(run(m, hostAt(3.99), {}, s) == 1);
    REQUIRE_FALSE(s[0].t.playing);
    REQUIRE(run(m, hostAt(7.99), {}, s) == 1);
    REQUIRE_FALSE(s[0].t.playing);
}

TEST_CASE("MidiTrigger: deferred changes still see every held-note event", "[MidiTrigger]")
{
    // Stopped host: every event is an immediate transition, far more than kMaxSegments.
    auto m = makeMidi();
    Seg s;
    std::vector<GateEvent> ev;
    for (int i = 0; i < 100; ++i)
        ev.push_back(i % 2 == 0 ? on(i + 1) : off(i + 1)); // ends with a note-off
    const int c = run(m, hostAt(0.0, false), ev, s, 256);
    requireValid(s, c, 256);
    REQUIRE(c == MidiTrigger::kMaxSegments);
    // The gate is off; the leftover transitions settle in the following blocks.
    for (int b = 0; b < 5; ++b)
        requireValid(s, run(m, hostAt(0.0, false), {}, s, 256), 256);
    REQUIRE_FALSE(s[0].t.playing);
}
