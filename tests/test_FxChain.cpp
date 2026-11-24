#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include "engine/fx/FxChain.h"

using namespace dc;
using namespace dc::fx;

namespace chaintest {

constexpr double kSr = 48000.0;
constexpr float kPi = 3.14159265358979f;

constexpr std::uint8_t bit(FxInsert i) { return static_cast<std::uint8_t>(1u << static_cast<int>(i)); }

std::vector<float> noise(int n, unsigned seed = 1, float amp = 1.0f)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-amp, amp);
    std::vector<float> v(static_cast<size_t>(n));
    for (auto& x : v) x = d(rng);
    return v;
}

std::vector<float> sine(int n, double freq, float amp, double sr = kSr)
{
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i)] = amp * static_cast<float>(std::sin(2.0 * kPi * freq * i / sr));
    return v;
}

// Processes [from, to) of l/r in blocks of at most blockSize frames.
void run(FxChain& c, std::vector<float>& l, std::vector<float>& r, size_t from, size_t to, int blockSize)
{
    for (size_t pos = from; pos < to; pos += static_cast<size_t>(blockSize))
        c.process(l.data() + pos, r.data() + pos, static_cast<int>(std::min<size_t>(blockSize, to - pos)));
}

double peakAbs(const std::vector<float>& v, size_t from, size_t to)
{
    double m = 0;
    for (size_t i = from; i < to && i < v.size(); ++i) m = std::max(m, double(std::abs(v[i])));
    return m;
}

double maxStep(const std::vector<float>& v, size_t from, size_t to)
{
    double m = 0;
    for (size_t i = from + 1; i < to && i < v.size(); ++i) m = std::max(m, double(std::abs(v[i] - v[i - 1])));
    return m;
}

FxParams reverbParams(double mix, double size, double damping)
{
    FxParams p;
    p.set(FxParam::ReverbMix, mix);
    p.set(FxParam::ReverbSize, size);
    p.set(FxParam::ReverbDamping, damping);
    return p;
}

} // namespace chaintest

using namespace chaintest;

TEST_CASE("FxChain: mask 0 passes the input through bit-exactly and nothing is active", "[FxChain]")
{
    FxChain c;
    c.prepare(kSr, 512);
    FxParams p = reverbParams(1.0, 1.0, 0.0);
    p.set(FxParam::FlangerMix, 1.0);
    p.set(FxParam::BitcrushBits, 1.0);
    c.setParams(p);
    c.setMask(0);
    auto l = noise(48000, 1), r = noise(48000, 2);
    const auto l0 = l, r0 = r;
    run(c, l, r, 0, l.size(), 512);
    CHECK(l == l0);
    CHECK(r == r0);
    CHECK_FALSE(c.anyActive());
    for (int i = 0; i < kFxInserts; ++i) CHECK_FALSE(c.isActive(static_cast<FxInsert>(i)));
}

TEST_CASE("FxChain: filter on and off crossfades without a step larger than the signal's own slope", "[FxChain]")
{
    FxChain c;
    c.prepare(kSr, 512);
    FxParams p;
    p.set(FxParam::FilterType, 0);
    p.set(FxParam::FilterCutoff, 200.0);
    c.setParams(p);
    auto l = sine(48000, 1000.0, 0.5f), r = l;
    // Mask switches at frames that do not line up with the 256 blocks of a host.
    const size_t cuts[] = {0, 3000, 9137, 15011, 22222, 30001, 38000, 48000};
    for (size_t k = 0; k + 1 < std::size(cuts); ++k)
    {
        c.setMask(k % 2 == 1 ? bit(FxInsert::Filter) : 0);
        run(c, l, r, cuts[k], cuts[k + 1], 256);
        if (k % 2 == 1) CHECK(c.isActive(FxInsert::Filter));
    }
    CHECK(maxStep(l, 0, l.size()) < 0.1);
    CHECK(maxStep(r, 0, r.size()) < 0.1);
    // The filter really was on: 1 kHz through a 200 Hz low pass is clearly quieter.
    CHECK(peakAbs(l, 6000, 9000) < 0.2);
    CHECK_FALSE(c.isActive(FxInsert::Filter));
}

TEST_CASE("FxChain: turning the reverb off keeps its tail and lets it die out", "[FxChain]")
{
    // Size 1, damping 0 is the longest tail the reverb has.
    FxChain c;
    c.prepare(kSr, 256);
    c.setParams(reverbParams(1.0, 1.0, 0.0));
    c.setMask(bit(FxInsert::Reverb));
    auto l = noise(24000, 3, 0.5f), r = noise(24000, 4, 0.5f);
    run(c, l, r, 0, l.size(), 256);
    CHECK(c.isActive(FxInsert::Reverb));

    c.setMask(0);
    const int seconds = 30;
    std::vector<float> sl(static_cast<size_t>(seconds * 48000), 0.0f), sr = sl;
    run(c, sl, sr, 0, 48000, 256);
    CHECK(peakAbs(sl, 0, 48000) > 1e-3); // above -60 dB in the second after
    CHECK(peakAbs(sl, 47000, 48000) > 1e-4);
    CHECK(c.isActive(FxInsert::Reverb));
    run(c, sl, sr, 48000, sl.size(), 256);
    CHECK_FALSE(c.isActive(FxInsert::Reverb));
    CHECK_FALSE(c.anyActive());
    CHECK(peakAbs(sl, sl.size() - 48000, sl.size()) == 0.0);
    CHECK(peakAbs(sr, sr.size() - 48000, sr.size()) == 0.0);
}

TEST_CASE("FxChain: an off insert leaves the dry signal at full level", "[FxChain]")
{
    FxChain c;
    c.prepare(kSr, 256);
    c.setParams(reverbParams(1.0, 0.5, 0.5));
    c.setMask(0);
    auto l = sine(9600, 440.0, 0.8f), r = noise(9600, 5, 0.8f);
    const auto l0 = l, r0 = r;
    run(c, l, r, 0, l.size(), 256);
    CHECK(l == l0);
    CHECK(r == r0);
}

TEST_CASE("FxChain: switching a mask bit inside a block of 64 frames is smooth", "[FxChain]")
{
    FxChain c;
    c.prepare(kSr, 64);
    c.setParams(reverbParams(1.0, 1.0, 0.0));
    auto l = noise(5 * 48000, 6, 0.5f), r = noise(5 * 48000, 7, 0.5f);
    // Toggle every 37 frames, so the 64-frame blocks are split at varying offsets.
    std::uint8_t mask = bit(FxInsert::Reverb);
    for (size_t pos = 0; pos < l.size(); pos += 37)
    {
        c.setMask(mask);
        run(c, l, r, pos, std::min(l.size(), pos + 37), 64);
        mask = mask ? 0 : bit(FxInsert::Reverb);
    }
    for (size_t i = 0; i < l.size(); ++i)
    {
        REQUIRE(std::isfinite(l[i]));
        REQUIRE(std::isfinite(r[i]));
        REQUIRE(std::abs(l[i]) < 4.0f);
        REQUIRE(std::abs(r[i]) < 4.0f);
    }
}

TEST_CASE("FxChain: flush silences all effect output within 60 ms and leaves the dry signal", "[FxChain]")
{
    FxChain c;
    c.prepare(kSr, 64);
    FxParams p = reverbParams(1.0, 1.0, 0.0);
    p.set(FxParam::FlangerFeedback, 0.9);
    p.set(FxParam::FlangerMix, 1.0);
    c.setParams(p);
    c.setMask(bit(FxInsert::Reverb) | bit(FxInsert::Flanger));
    auto l = noise(24000, 8, 0.5f), r = noise(24000, 9, 0.5f);
    run(c, l, r, 0, l.size(), 64);
    REQUIRE(c.isActive(FxInsert::Reverb));
    c.setMask(0);
    c.flush();

    const size_t ms60 = 60 * 48;

    SECTION("silent input gives exact silence after 60 ms")
    {
        std::vector<float> sl(48000, 0.0f), sr = sl;
        run(c, sl, sr, 0, sl.size(), 64);
        CHECK(peakAbs(sl, 0, 480) > 1e-3); // fades, does not cut
        CHECK(peakAbs(sl, ms60, sl.size()) == 0.0);
        CHECK(peakAbs(sr, ms60, sr.size()) == 0.0);
        CHECK_FALSE(c.anyActive());
        // Faded, not cut: the last 5 ms of the fade are far below its first 5 ms.
        CHECK(peakAbs(sl, 45 * 48, 50 * 48) < 0.2 * peakAbs(sl, 0, 5 * 48));
    }
    SECTION("the dry signal passes unchanged after 60 ms")
    {
        auto sl = sine(48000, 220.0, 0.5f), sr = sl;
        const auto s0 = sl;
        run(c, sl, sr, 0, sl.size(), 64);
        CHECK(std::equal(sl.begin() + ms60, sl.end(), s0.begin() + ms60));
        CHECK(std::equal(sr.begin() + ms60, sr.end(), s0.begin() + ms60));
        CHECK_FALSE(c.anyActive());
    }
    SECTION("an insert switched on during the flush comes back after it")
    {
        c.setMask(bit(FxInsert::Reverb));
        std::vector<float> sl(48000, 0.0f), sr = sl;
        sl[ms60 + 100] = 1.0f; // impulse after the flush
        run(c, sl, sr, 0, sl.size(), 64);
        CHECK(c.isActive(FxInsert::Reverb));
        CHECK(peakAbs(sl, ms60 + 2000, sl.size()) > 1e-4); // the reverb rings for the impulse
    }
}

TEST_CASE("FxChain: a flush with only Bitcrush/Filter open completes even when the chain goes idle", "[FxChain]")
{
    // Like Engine::runChain: the chain is only processed while it reports activity (mask 0 here).
    const auto runWhileActive = [](FxChain& c, int frames) {
        std::vector<float> l(64, 0.0f), r = l;
        for (int pos = 0; pos < frames; pos += 64)
            if (c.anyActive())
            {
                std::fill(l.begin(), l.end(), 0.0f);
                std::fill(r.begin(), r.end(), 0.0f);
                c.process(l.data(), r.data(), 64);
            }
    };
    for (FxInsert ins : {FxInsert::Filter, FxInsert::Bitcrush})
    {
        INFO("insert " << static_cast<int>(ins));
        FxParams p;
        p.set(FxParam::FilterCutoff, 200.0);
        p.set(FxParam::BitcrushBits, 2.0);
        p.set(FxParam::BitcrushRate, 8.0);
        FxChain c;
        c.prepare(kSr, 64);
        c.setParams(p);
        c.setMask(bit(ins));
        auto l = noise(9600, 3, 0.5f), r = noise(9600, 4, 0.5f);
        run(c, l, r, 0, l.size(), 64);
        c.flush();      // e.g. a preset load ...
        c.setMask(0);   // ... whose first regions have no switches
        runWhileActive(c, 48000);
        CHECK_FALSE(c.anyActive());

        // Much later the insert opens again: it sounds like a fresh chain from the first block on.
        // (The reference has opened and closed the insert once, so its units jumped to p on sleep
        // instead of smoothing towards it from the defaults as after prepare.)
        FxChain fresh;
        fresh.prepare(kSr, 64);
        fresh.setParams(p);
        fresh.setMask(bit(ins));
        {
            auto wl = noise(960, 7, 0.5f), wr = wl;
            run(fresh, wl, wr, 0, wl.size(), 64);
            fresh.setMask(0);
            std::vector<float> sl(960, 0.0f), sr = sl;
            run(fresh, sl, sr, 0, sl.size(), 64);
            REQUIRE_FALSE(fresh.anyActive());
        }
        fresh.setMask(bit(ins));
        c.setMask(bit(ins));
        auto a = noise(9600, 5, 0.5f), ar = noise(9600, 6, 0.5f);
        auto b = a, br = ar;
        run(c, a, ar, 0, a.size(), 64);
        run(fresh, b, br, 0, b.size(), 64);
        double dev = 0;
        for (size_t i = 0; i < a.size(); ++i) dev = std::max(dev, double(std::abs(a[i] - b[i])));
        CHECK(dev < 1e-6);
    }
}

TEST_CASE("FxChain: parameter changes do not click", "[FxChain]")
{
    FxChain c;
    c.prepare(kSr, 128);
    FxParams p;
    p.set(FxParam::FilterType, 0);
    p.set(FxParam::FilterCutoff, 200.0);
    c.setParams(p);
    c.setMask(bit(FxInsert::Filter));
    auto l = sine(48000, 100.0, 0.5f), r = l;
    run(c, l, r, 0, 20011, 128);
    p.set(FxParam::FilterCutoff, 8000.0);
    c.setParams(p);
    run(c, l, r, 20011, l.size(), 128);
    CHECK(maxStep(l, 0, l.size()) < 0.1);
    CHECK(maxStep(r, 0, r.size()) < 0.1);
}

namespace chaintest {

// Extreme settings: bitcrush 1 bit / rate 64 / mix 1, filter resonance 1, flanger depth 1 rate 5
// mix 1, reverb mix 1 size 1 damping 0; filter type/cutoff and flanger feedback vary.
struct Extreme { int filterType; double cutoff; double fb; };
constexpr Extreme kExtremes[] = {{0, 20.0, 0.9},     {0, 20.0, -0.9},   {0, 20000.0, 0.9}, {0, 20000.0, -0.9},
                                 {2, 1000.0, 0.9},   {2, 1000.0, -0.9}, {1, 20000.0, 0.9}, {1, 20000.0, -0.9}};

FxParams extremeParams(const Extreme& e)
{
    FxParams p = reverbParams(1.0, 1.0, 0.0);
    p.set(FxParam::BitcrushBits, 1.0);
    p.set(FxParam::BitcrushRate, 64.0);
    p.set(FxParam::BitcrushMix, 1.0);
    p.set(FxParam::FilterType, e.filterType);
    p.set(FxParam::FilterCutoff, e.cutoff);
    p.set(FxParam::FilterResonance, 1.0);
    p.set(FxParam::FlangerRate, 5.0);
    p.set(FxParam::FlangerDepth, 1.0);
    p.set(FxParam::FlangerFeedback, e.fb);
    p.set(FxParam::FlangerMix, 1.0);
    return p;
}

// With filter Q at most 4 and the flanger's feedback compensation the worst peak over all subsets,
// settings and rates is about 6.5 (filter + flanger + reverb, LP 20 kHz resonance 1, feedback 0.9,
// 96 kHz, 3 s of full-scale noise; before the level tame it was about 17.4 with all four inserts).
constexpr float kChainBound = 12.0f;

void requireBounded(const std::vector<float>& l, const std::vector<float>& r)
{
    for (size_t i = 0; i < l.size(); ++i)
    {
        REQUIRE(std::isfinite(l[i]));
        REQUIRE(std::isfinite(r[i]));
        REQUIRE(std::abs(l[i]) < kChainBound);
        REQUIRE(std::abs(r[i]) < kChainBound);
    }
}

} // namespace chaintest

TEST_CASE("FxChain: all four inserts on with extreme parameters stay finite and bounded for 10 s of noise at 44.1, 96 and 192 kHz, blocks of 1 and 4096, then die out", "[FxChain]")
{
    for (double sr : {44100.0, 96000.0, 192000.0})
        for (int block : {1, 4096})
            for (const auto& e : kExtremes)
            {
                INFO("sr " << sr << " block " << block << " type " << e.filterType << " cutoff " << e.cutoff << " fb " << e.fb);
                FxChain c;
                c.prepare(sr, 4096);
                c.setParams(extremeParams(e));
                c.setMask(0x0F);
                const int n = static_cast<int>(10 * sr);
                auto l = noise(n, 10), r = noise(n, 11);
                const auto l0 = l;
                run(c, l, r, 0, l.size(), block);
                requireBounded(l, r);
                // The effects really act: the output differs clearly from the input.
                double diff = 0, in = 0;
                for (size_t i = 0; i < l.size(); ++i)
                {
                    diff += double(l[i] - l0[i]) * (l[i] - l0[i]);
                    in += double(l0[i]) * l0[i];
                }
                CHECK(diff > 0.1 * in);

                // Then 2 s of silence with everything off: the tails decay ...
                c.setMask(0);
                const size_t s = static_cast<size_t>(sr);
                std::vector<float> sl(2 * s, 0.0f), srr = sl;
                run(c, sl, srr, 0, sl.size(), block);
                requireBounded(sl, srr);
                CHECK(peakAbs(sl, sl.size() - s / 10, sl.size()) < 0.5 * peakAbs(sl, 0, s / 10));
                // ... and eventually stop: nothing active, exact silence.
                std::vector<float> ql(s, 0.0f), qr = ql;
                for (int sec = 2; sec < 60 && c.anyActive(); ++sec)
                {
                    std::fill(ql.begin(), ql.end(), 0.0f);
                    std::fill(qr.begin(), qr.end(), 0.0f);
                    run(c, ql, qr, 0, ql.size(), 4096);
                }
                CHECK_FALSE(c.anyActive());
                std::fill(ql.begin(), ql.end(), 0.0f);
                std::fill(qr.begin(), qr.end(), 0.0f);
                run(c, ql, qr, 0, ql.size(), block);
                CHECK(peakAbs(ql, 0, ql.size()) == 0.0);
                CHECK(peakAbs(qr, 0, qr.size()) == 0.0);
            }
}

TEST_CASE("FxChain: every subset of inserts with extreme parameters stays finite and bounded", "[FxChain]")
{
    for (double sr : {44100.0, 96000.0, 192000.0})
        for (int mask = 1; mask < 16; ++mask)
            for (const auto& e : kExtremes)
            {
                INFO("sr " << sr << " mask " << mask << " type " << e.filterType << " cutoff " << e.cutoff << " fb " << e.fb);
                FxChain c;
                c.prepare(sr, 512);
                c.setParams(extremeParams(e));
                c.setMask(static_cast<std::uint8_t>(mask));
                const int n = static_cast<int>(3 * sr);
                auto l = noise(n, 12), r = noise(n, 13);
                run(c, l, r, 0, l.size(), 512);
                requireBounded(l, r);
            }
}

TEST_CASE("FxChain: sleeping reverb and flanger wake up without a click while a sine runs", "[FxChain]")
{
    FxChain c;
    c.prepare(kSr, 256);
    c.setParams(reverbParams(1.0, 1.0, 0.0)); // flanger at its defaults
    const std::uint8_t both = bit(FxInsert::Flanger) | bit(FxInsert::Reverb);
    c.setMask(both);
    auto l = noise(24000, 14, 0.5f), r = noise(24000, 15, 0.5f);
    run(c, l, r, 0, l.size(), 256);
    c.setMask(0);
    std::vector<float> sl(40 * 48000, 0.0f), sr = sl;
    run(c, sl, sr, 0, sl.size(), 256);
    REQUIRE_FALSE(c.isActive(FxInsert::Flanger));
    REQUIRE_FALSE(c.isActive(FxInsert::Reverb));

    auto tl = sine(48000, 220.0, 0.5f), tr = tl;
    run(c, tl, tr, 0, 4811, 256);
    c.setMask(both);
    run(c, tl, tr, 4811, tl.size(), 256);
    CHECK(c.isActive(FxInsert::Reverb));
    // Dry sine slope is 0.0144; with flanger and reverb running the signal's own steps reach about
    // 0.03. Around the switch (10 ms) the steps stay within that: no click from the woken state.
    CHECK(maxStep(tl, 0, 4811) < 0.015);
    CHECK(maxStep(tl, 4700, 4811 + 480) < 0.04);
    CHECK(maxStep(tr, 4700, 4811 + 480) < 0.04);
    CHECK(maxStep(tl, 0, tl.size()) < 0.05);
    CHECK(maxStep(tr, 0, tr.size()) < 0.05);
    CHECK(peakAbs(tl, 24000, tl.size()) > 0.6); // the effects are audible on top of the dry 0.5
}

// --- Chain order, Delay, tempo ---------------------------------------------------------------------

namespace chaintest {

// The separate units of a chain, set up exactly as FxChain::prepare + setParams does it.
struct Units
{
    Bitcrush bitcrush;
    Filter filter;
    Flanger flanger;
    Reverb reverb;
    Delay delay;

    Units(double sr, const FxParams& p)
    {
        bitcrush.prepare(sr);
        filter.prepare(sr);
        flanger.prepare(sr);
        reverb.prepare(sr);
        delay.prepare(sr);
        const FxParams def;
        bitcrush.reset(); bitcrush.setParams(def); bitcrush.setParams(p);
        filter.reset(); filter.setParams(def); filter.setParams(p);
        flanger.reset(); flanger.setParams(def); flanger.setParams(p);
        reverb.reset(); reverb.setParams(def); reverb.setParams(p);
        delay.reset(); delay.setParams(def); delay.setParams(p);
    }

    void unit(FxInsert i, float* l, float* r, int n)
    {
        switch (i)
        {
            case FxInsert::Bitcrush: bitcrush.process(l, r, n); break;
            case FxInsert::Filter: filter.process(l, r, n); break;
            case FxInsert::Flanger: flanger.process(l, r, n); break;
            case FxInsert::Reverb: reverb.process(l, r, n); break;
            case FxInsert::Delay: delay.process(l, r, n); break;
        }
    }

    // The chain's arithmetic with every gate fully open and no flush (gain 1 * fade 1), inserts in
    // the given sequence: Bitcrush/Filter blend, the others add their wet output.
    void compose(const std::vector<FxInsert>& seq, std::vector<float>& l, std::vector<float>& r, int block)
    {
        std::vector<float> tl(static_cast<size_t>(block)), tr = tl;
        const float g = 1.0f, f = 1.0f;
        for (size_t pos = 0; pos < l.size(); pos += static_cast<size_t>(block))
        {
            const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), l.size() - pos));
            float* bl = l.data() + pos;
            float* br = r.data() + pos;
            for (FxInsert ins : seq)
            {
                const bool blend = ins == FxInsert::Bitcrush || ins == FxInsert::Filter;
                for (int i = 0; i < n; ++i)
                {
                    tl[size_t(i)] = blend ? bl[i] : g * bl[i];
                    tr[size_t(i)] = blend ? br[i] : g * br[i];
                }
                unit(ins, tl.data(), tr.data(), n);
                for (int i = 0; i < n; ++i)
                {
                    if (blend)
                    {
                        const float w = g * f;
                        bl[i] += w * (tl[size_t(i)] - bl[i]);
                        br[i] += w * (tr[size_t(i)] - br[i]);
                    }
                    else
                    {
                        bl[i] += f * tl[size_t(i)];
                        br[i] += f * tr[size_t(i)];
                    }
                }
            }
        }
    }
};

// A chain whose gates for `mask` are fully open from the first sample (the mask is set before
// prepare, whose reset() jumps the gates to their targets).
void openChain(FxChain& c, double sr, int block, std::uint8_t mask, const FxParams& p)
{
    c.setMask(mask);
    c.prepare(sr, block);
    c.setParams(p);
}

FxParams orderParams()
{
    FxParams p = reverbParams(0.5, 0.6, 0.4);
    p.set(FxParam::FilterType, 0);
    p.set(FxParam::FilterCutoff, 800.0);
    p.set(FxParam::FilterResonance, 0.6);
    p.set(FxParam::BitcrushBits, 4.0);
    p.set(FxParam::BitcrushRate, 6.0);
    p.set(FxParam::FlangerMix, 0.7);
    p.set(FxParam::DelayTime, 2.0); // 1/16
    p.set(FxParam::DelayMix, 0.6);
    p.set(FxParam::DelayFeedback, 0.5);
    return p;
}

const FxOrder kReversed = {FxInsert::Reverb, FxInsert::Delay, FxInsert::Flanger, FxInsert::Filter, FxInsert::Bitcrush};

std::vector<FxInsert> seqOf(const FxOrder& o) { return {o.begin(), o.end()}; }

double maxDev(const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
{
    double m = 0;
    for (size_t i = from; i < to && i < a.size() && i < b.size(); ++i) m = std::max(m, double(std::abs(a[i] - b[i])));
    return m;
}

} // namespace chaintest

TEST_CASE("FxChain: default order with delay off is bit-identical to the pre-change chain", "[FxChain][order]")
{
    // The pre-change chain: Bitcrush -> Filter (blend), then Flanger -> Reverb (additive).
    for (int block : {1, 64, 512})
    {
        INFO("block " << block);
        FxParams p; // defaults, with every effect audible
        p.set(FxParam::ReverbMix, 0.6);
        FxChain c;
        openChain(c, kSr, block, 0x0F, p);
        CHECK(c.order() == defaultFxOrder());
        auto l = noise(48000, 21, 0.5f), r = noise(48000, 22, 0.5f);
        auto el = l, er = r;
        run(c, l, r, 0, l.size(), block);
        Units u(kSr, p);
        u.compose({FxInsert::Bitcrush, FxInsert::Filter, FxInsert::Flanger, FxInsert::Reverb}, el, er, block);
        CHECK(l == el);
        CHECK(r == er);
    }
}

TEST_CASE("FxChain: chain applies effects in the chosen order", "[FxChain][order]")
{
    const FxParams p = orderParams();
    SECTION("Filter then Bitcrush differs from Bitcrush then Filter")
    {
        const std::uint8_t mask = bit(FxInsert::Bitcrush) | bit(FxInsert::Filter);
        FxChain a, b;
        openChain(a, kSr, 256, mask, p);
        openChain(b, kSr, 256, mask, p);
        b.setOrder({FxInsert::Filter, FxInsert::Bitcrush, FxInsert::Flanger, FxInsert::Delay, FxInsert::Reverb});
        auto al = noise(24000, 23, 0.5f), ar = noise(24000, 24, 0.5f);
        auto bl = al, br = ar;
        run(a, al, ar, 0, al.size(), 256);
        run(b, bl, br, 0, bl.size(), 256);
        CHECK(maxDev(al, bl, 0, al.size()) > 0.01);
    }
    SECTION("Reverb, Delay, Flanger, Filter, Bitcrush equals the manual composition")
    {
        FxChain c;
        c.prepare(kSr, 256);
        c.setOrder(kReversed); // idle: at once
        c.setMask(0x1F);
        c.reset(); // gates jump open
        c.setParams(p);
        CHECK(c.order() == kReversed);
        auto l = noise(48000, 25, 0.5f), r = noise(48000, 26, 0.5f);
        auto el = l, er = r;
        run(c, l, r, 0, l.size(), 256);
        Units u(kSr, p);
        u.compose(seqOf(kReversed), el, er, 256);
        CHECK(l == el);
        CHECK(r == er);
    }
}

TEST_CASE("FxChain: reorder while idle applies immediately", "[FxChain][order]")
{
    FxChain c;
    c.prepare(kSr, 128);
    c.setParams(orderParams());
    REQUIRE_FALSE(c.anyActive());
    c.setOrder(kReversed);
    CHECK(c.order() == kReversed);
    CHECK_FALSE(c.anyActive()); // no flush started
    // An invalid order (a duplicate) becomes the default order.
    FxOrder bad = kReversed;
    bad[1] = FxInsert::Reverb;
    c.setOrder(bad);
    CHECK(c.order() == defaultFxOrder());
    // The idle chain still passes the signal bit-exactly.
    auto l = noise(4800, 27), r = noise(4800, 28);
    const auto l0 = l, r0 = r;
    run(c, l, r, 0, l.size(), 128);
    CHECK(l == l0);
    CHECK(r == r0);
}

TEST_CASE("FxChain: reorder while an effect is active fades and then applies", "[FxChain][order]")
{
    FxParams p = orderParams();
    p.set(FxParam::BitcrushBits, 12.0); // small quantisation steps of its own
    p.set(FxParam::BitcrushRate, 1.0);
    const std::uint8_t mask = bit(FxInsert::Filter) | bit(FxInsert::Bitcrush) | bit(FxInsert::Reverb) | bit(FxInsert::Delay);
    for (int block : {1, 64, 4096})
    {
        INFO("block " << block);
        FxChain c;
        c.prepare(kSr, 4096);
        c.setParams(p);
        c.setMask(mask);
        auto l = sine(3 * 48000, 220.0, 0.5f), r = l;
        const size_t at = 48000 + 77;
        run(c, l, r, 0, at, block);
        REQUIRE(c.anyActive());
        c.setOrder(kReversed);
        CHECK(c.order() == defaultFxOrder()); // not yet: the fade runs first
        c.setOrder(kReversed);                // repeated calls (every engine slice) change nothing
        const size_t ms40 = 40 * 48;
        run(c, l, r, at, at + ms40, block == 4096 ? 64 : block);
        CHECK(c.order() == defaultFxOrder());
        const size_t ms60 = 60 * 48;
        run(c, l, r, at + ms40, at + ms60, block == 4096 ? 64 : block);
        CHECK(c.order() == kReversed);
        run(c, l, r, at + ms60, l.size(), block);
        CHECK(c.order() == kReversed);
        CHECK(c.isActive(FxInsert::Reverb));
        for (size_t i = 0; i < l.size(); ++i) REQUIRE(std::isfinite(l[i]));
        // No click: around the reorder (fade out to the dry signal, clear, ramp in) the steps stay
        // within those of the steady signal in the old order before it, in the new order well after
        // it, and of the dry sine the fade passes through (0.0144; the effects here smooth it).
        const double drySlope = 2.0 * kPi * 220.0 / kSr * 0.5;
        const double before = maxStep(l, 24000, at);
        const double after = maxStep(l, at + 48000, l.size());
        const double around = maxStep(l, at, at + 120 * 48);
        INFO("steady old " << before << ", steady new " << after << ", around reorder " << around);
        CHECK(around <= 1.2 * std::max({before, after, drySlope}));

        // The measure does see a click: a hard switch (states dropped at once) exceeds the bound.
        FxChain hard;
        hard.prepare(kSr, 4096);
        hard.setParams(p);
        hard.setMask(mask);
        auto hl = sine(3 * 48000, 220.0, 0.5f), hr = hl;
        run(hard, hl, hr, 0, at, block);
        hard.setOrder(kReversed); // pending (the inserts are open) ...
        hard.reset();             // ... and applied by the reset, which drops every state at once
        REQUIRE(hard.order() == kReversed);
        run(hard, hl, hr, at, hl.size(), block);
        INFO("hard switch " << maxStep(hl, at - 1, at + 120 * 48));
        CHECK(maxStep(hl, at - 1, at + 120 * 48) > 1.2 * std::max({before, after, drySlope}));
    }
}

TEST_CASE("FxChain: delay mask bit 4 gates the delay and its tail rings out", "[FxChain][delay]")
{
    FxParams p;
    p.set(FxParam::DelayTime, 8.0); // 1/4 = 24000 samples at 120 bpm
    p.set(FxParam::DelayMix, 1.0);
    p.set(FxParam::DelayFeedback, 0.5);
    SECTION("bit 4 off: the delay does nothing")
    {
        FxChain c;
        c.prepare(kSr, 256);
        c.setParams(p);
        c.setMask(0);
        auto l = noise(48000, 29), r = noise(48000, 30);
        const auto l0 = l;
        run(c, l, r, 0, l.size(), 256);
        CHECK(l == l0);
        CHECK_FALSE(c.isActive(FxInsert::Delay));
    }
    SECTION("bit 4 on: echoes, then off: the tail rings out and the delay sleeps")
    {
        FxChain c;
        c.prepare(kSr, 256);
        c.setParams(p);
        c.setMask(bit(FxInsert::Delay));
        CHECK(c.isActive(FxInsert::Delay));
        std::vector<float> l(24000, 0.0f), r = l;
        for (size_t i = 1000; i < 1100; ++i) l[i] = r[i] = 0.5f; // a burst after the 5 ms gate ramp
        const auto l0 = l;
        run(c, l, r, 0, l.size(), 256);
        CHECK(l == l0); // no echo before one delay time
        c.setMask(0);   // the gate closes before the first echo
        std::vector<float> sl(10 * 48000, 0.0f), sr = sl;
        run(c, sl, sr, 0, 48000, 256);
        // First echo 24000 after the burst = 1000 into the silence, the second one 24000 later.
        CHECK(peakAbs(sl, 900, 1200) > 0.4);
        CHECK(peakAbs(sl, 24900, 25200) > 0.2);
        CHECK(peakAbs(sl, 2000, 24000) < 1e-6);
        CHECK(c.isActive(FxInsert::Delay));
        run(c, sl, sr, 48000, sl.size(), 256);
        CHECK_FALSE(c.isActive(FxInsert::Delay)); // asleep after the echoes fell below -120 dB
        CHECK_FALSE(c.anyActive());
        std::vector<float> ql(48000, 0.0f), qr = ql;
        run(c, ql, qr, 0, ql.size(), 256);
        CHECK(peakAbs(ql, 0, ql.size()) == 0.0);
        CHECK(peakAbs(qr, 0, qr.size()) == 0.0);
    }
}

TEST_CASE("FxChain: chain sleeps all five inserts when masks are 0 and tails are gone", "[FxChain][delay]")
{
    FxChain c;
    c.prepare(kSr, 512);
    FxParams p = orderParams();
    p.set(FxParam::DelayTime, 14.0); // 2 bars, feedback 0.5
    c.setParams(p);
    c.setMask(0x1F);
    auto l = noise(48000, 31, 0.5f), r = noise(48000, 32, 0.5f);
    run(c, l, r, 0, l.size(), 512);
    for (int i = 0; i < kFxInserts; ++i) CHECK(c.isActive(static_cast<FxInsert>(i)));
    c.setMask(0);
    std::vector<float> ql(48000, 0.0f), qr = ql;
    int seconds = 0;
    for (; seconds < 600 && c.anyActive(); ++seconds)
    {
        std::fill(ql.begin(), ql.end(), 0.0f);
        std::fill(qr.begin(), qr.end(), 0.0f);
        run(c, ql, qr, 0, ql.size(), 512);
    }
    INFO("asleep after " << seconds << " s");
    CHECK(seconds > 4); // the delay's 4 s echoes kept it awake for a while
    CHECK_FALSE(c.anyActive());
    for (int i = 0; i < kFxInserts; ++i) CHECK_FALSE(c.isActive(static_cast<FxInsert>(i)));
    std::fill(ql.begin(), ql.end(), 0.0f);
    std::fill(qr.begin(), qr.end(), 0.0f);
    run(c, ql, qr, 0, ql.size(), 512);
    CHECK(peakAbs(ql, 0, ql.size()) == 0.0);
    CHECK(peakAbs(qr, 0, qr.size()) == 0.0);
}

TEST_CASE("FxChain: tempo change reaches the delay", "[FxChain][delay]")
{
    // Position of the first echo of an impulse at frame 1000 (after the gate ramp).
    const auto echoAt = [](double bpm, double bpb, double step) {
        FxParams q;
        q.set(FxParam::DelayTime, step);
        q.set(FxParam::DelayMix, 1.0);
        q.set(FxParam::DelayFeedback, 0.0);
        FxChain c;
        c.prepare(kSr, 256);
        c.setTempo(bpm, bpb);
        c.setParams(q);
        c.setMask(bit(FxInsert::Delay));
        std::vector<float> l(5 * 48000, 0.0f), r = l;
        l[1000] = r[1000] = 1.0f;
        run(c, l, r, 0, l.size(), 256);
        for (size_t i = 1001; i < l.size(); ++i)
            if (std::abs(l[i]) > 0.5f) return static_cast<long>(i) - 1000;
        return -1L;
    };
    CHECK(echoAt(120.0, 4.0, 8.0) == 24000);
    CHECK(echoAt(60.0, 4.0, 8.0) == 48000);
    CHECK(echoAt(120.0, 3.0, 13.0) == 72000); // 1 bar of 3/4
    CHECK(echoAt(120.0, 4.0, 13.0) == 96000);

    // A tempo change on a running chain retimes the delay too.
    FxParams p;
    p.set(FxParam::DelayTime, 8.0);
    p.set(FxParam::DelayMix, 1.0);
    p.set(FxParam::DelayFeedback, 0.0);
    FxChain c;
    c.prepare(kSr, 256);
    c.setParams(p);
    c.setMask(bit(FxInsert::Delay));
    std::vector<float> w(4800, 0.0f), wr = w;
    run(c, w, wr, 0, w.size(), 256);
    c.setTempo(240.0, 4.0); // 1/4 = 12000
    std::vector<float> l(60000, 0.0f), r = l;
    l[10000] = r[10000] = 1.0f; // long after the 30 ms retime crossfade
    run(c, l, r, 0, l.size(), 256);
    CHECK(peakAbs(l, 22000 - 2, 22000 + 3) > 0.5);
    CHECK(peakAbs(l, 33000, 35000) < 1e-6);
}

namespace chaintest {
// 2880000 / 32768 BPM at 48 kHz: exactly 2^-15 beats per sample, so beat positions add up without
// rounding and renders from positions a whole cycle apart are bit-identical. 1 beat = 32768 samples.
constexpr double kExactBpm = 87.890625;
constexpr int kBeat = 32768;

FxParams lfoParams()
{
    FxParams p;
    p.set(FxParam::FilterType, 0.0);
    p.set(FxParam::FilterCutoff, 1000.0);
    p.set(FxParam::FilterResonance, 0.0);
    p.set(FxParam::FilterLfoRate, 8.0); // 1/4: one cycle per beat
    p.set(FxParam::FilterLfoShape, 3.0); // Square: +1 for the first half of the cycle
    p.set(FxParam::FilterLfoDepth, 1.0);
    return p;
}

// Filter-only chain at the given tempo and start position, `frames` of noise in blocks of 512.
std::vector<float> renderLfo(double bpm, double ppq, int frames)
{
    FxChain c;
    openChain(c, kSr, 512, bit(FxInsert::Filter), lfoParams());
    c.setTempo(bpm, 4.0);
    c.setPosition(ppq);
    auto l = noise(frames, 5), r = noise(frames, 6);
    // One position per slice, as the Engine does: the Filter counts on from there by itself.
    run(c, l, r, 0, l.size(), 512);
    return l;
}
} // namespace chaintest

TEST_CASE("FxChain: the Filter LFO follows setPosition and setTempo", "[FxChain][lfo]")
{
    const int frames = 2 * kBeat;
    const auto a = renderLfo(kExactBpm, 0.0, frames);
    const auto b = renderLfo(kExactBpm, 4.0, frames);  // four cycles later: same phase
    const auto c = renderLfo(kExactBpm, 0.5, frames);  // half a cycle later: inverted square
    CHECK(maxDev(a, b, 0, a.size()) == 0.0);
    CHECK(maxDev(a, c, 0, a.size()) > 0.1);
    // Double tempo: the first quarter beat is high in both, the second quarter is low only at the
    // double tempo (its cycle is over after half a beat of the base tempo).
    const auto d = renderLfo(2.0 * kExactBpm, 0.0, frames);
    CHECK(maxDev(a, d, kBeat / 4, kBeat / 2) > 0.1);
}

TEST_CASE("FxChain: a Filter that wakes up inside a slice is on the beat position of that frame", "[FxChain][lfo]")
{
    // The Engine sets the position once per slice and may open the Filter at a frame inside it.
    // 20000 samples = 0.61 cycle: a Filter that did not count while asleep would start high, not low.
    const int asleep = 20000, frames = kBeat;
    FxChain a;
    openChain(a, kSr, 512, 0, lfoParams());
    a.setTempo(kExactBpm, 4.0);
    a.setPosition(0.0);
    auto al = noise(asleep + frames, 5), ar = noise(asleep + frames, 6);
    a.process(al.data(), ar.data(), asleep);
    a.setMask(bit(FxInsert::Filter));
    a.process(al.data() + asleep, ar.data() + asleep, frames);

    FxChain b;
    openChain(b, kSr, 512, 0, lfoParams());
    b.setTempo(kExactBpm, 4.0);
    b.setPosition(asleep / static_cast<double>(kBeat));
    b.setMask(bit(FxInsert::Filter));
    std::vector<float> bl(al.size()), br(ar.size());
    auto nl = noise(asleep + frames, 5), nr = noise(asleep + frames, 6);
    std::copy(nl.begin() + asleep, nl.end(), bl.begin());
    std::copy(nr.begin() + asleep, nr.end(), br.begin());
    b.process(bl.data(), br.data(), frames);

    double dev = 0.0;
    for (int i = 0; i < frames; ++i)
        dev = std::max(dev, double(std::abs(al[static_cast<size_t>(asleep + i)] - bl[static_cast<size_t>(i)])));
    CHECK(dev == 0.0);
}
