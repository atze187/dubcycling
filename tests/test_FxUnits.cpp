#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <set>
#include <vector>
#include "engine/fx/Bitcrush.h"
#include "engine/fx/Filter.h"

using namespace dc;
using namespace dc::fx;

// Shared helpers (reused by the Flanger/Reverb tests).
namespace fxtest {

constexpr double kSr = 48000.0;
constexpr float kPi = 3.14159265358979f;

inline std::vector<float> noise(int n, unsigned seed = 1, float amp = 1.0f)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-amp, amp);
    std::vector<float> v(static_cast<size_t>(n));
    for (auto& x : v) x = d(rng);
    return v;
}

inline std::vector<float> sine(int n, double freq, float amp = 0.8f, double sr = kSr)
{
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i)] = amp * std::sin(2.0f * kPi * static_cast<float>(freq * i / sr));
    return v;
}

// Runs a unit over l/r in place, in blocks of at most blockSize frames.
template <typename Unit>
void processBlocks(Unit& u, std::vector<float>& l, std::vector<float>& r, int blockSize)
{
    const int n = static_cast<int>(l.size());
    for (int pos = 0; pos < n; pos += blockSize)
        u.process(l.data() + pos, r.data() + pos, std::min(blockSize, n - pos));
}

// RMS of the second half (lets filters settle).
inline double tailRms(const std::vector<float>& v)
{
    double s = 0;
    const size_t from = v.size() / 2;
    for (size_t i = from; i < v.size(); ++i) s += double(v[i]) * v[i];
    return std::sqrt(s / double(v.size() - from));
}

inline double dbOf(double ratio) { return 20.0 * std::log10(ratio); }

inline FxParams filterParams(int type, double cutoff, double res)
{
    FxParams p;
    p.set(FxParam::FilterType, type);
    p.set(FxParam::FilterCutoff, cutoff);
    p.set(FxParam::FilterResonance, res);
    return p;
}

// Gain in dB of the filter for a sine at freq.
inline double filterGainDb(const FxParams& p, double freq)
{
    Filter f;
    f.prepare(kSr);
    f.setParams(p);
    auto l = sine(48000, freq), r = l;
    const double inRms = tailRms(l);
    processBlocks(f, l, r, 256);
    return dbOf(tailRms(l) / inRms);
}

} // namespace fxtest

using namespace fxtest;

TEST_CASE("Bitcrush: mix 0 returns the input unchanged", "[FxUnits]")
{
    Bitcrush b;
    b.prepare(kSr);
    FxParams p;
    p.set(FxParam::BitcrushMix, 0.0);
    b.setParams(p);
    auto l = noise(2000, 1), r = noise(2000, 2);
    const auto l0 = l, r0 = r;
    processBlocks(b, l, r, 128);
    CHECK(l == l0);
    CHECK(r == r0);
}

TEST_CASE("Bitcrush: 1 bit leaves at most 2 distinct values for a sine", "[FxUnits]")
{
    Bitcrush b;
    b.prepare(kSr);
    FxParams p;
    p.set(FxParam::BitcrushBits, 1.0);
    p.set(FxParam::BitcrushRate, 1.0);
    p.set(FxParam::BitcrushMix, 1.0);
    b.setParams(p);
    auto l = sine(4800, 440.0, 1.0f), r = l;
    processBlocks(b, l, r, 512);
    std::set<float> values(l.begin(), l.end());
    CHECK(values.size() == 2);
}

TEST_CASE("Bitcrush: bits 16 and rate 1 is nearly transparent", "[FxUnits]")
{
    Bitcrush b;
    b.prepare(kSr);
    FxParams p;
    p.set(FxParam::BitcrushBits, 16.0);
    p.set(FxParam::BitcrushRate, 1.0);
    p.set(FxParam::BitcrushMix, 1.0);
    b.setParams(p);
    auto l = noise(4000, 3, 0.9f), r = l;
    const auto l0 = l;
    processBlocks(b, l, r, 300);
    float maxDiff = 0;
    for (size_t i = 0; i < l.size(); ++i) maxDiff = std::max(maxDiff, std::abs(l[i] - l0[i]));
    CHECK(maxDiff < 1e-3f);
}

TEST_CASE("Bitcrush: rate 8 holds each value for 8 samples", "[FxUnits]")
{
    Bitcrush b;
    b.prepare(kSr);
    FxParams p;
    p.set(FxParam::BitcrushBits, 16.0);
    p.set(FxParam::BitcrushRate, 8.0);
    p.set(FxParam::BitcrushMix, 1.0);
    b.setParams(p);
    auto l = noise(800, 4, 0.9f), r = l;
    processBlocks(b, l, r, 37); // blocks that do not align with the hold length
    for (size_t i = 0; i < l.size(); ++i)
        CHECK(l[i] == l[i - i % 8]);
    CHECK(l[0] != l[8]);
}

TEST_CASE("Filter: low pass attenuates 10 kHz by at least 20 dB and passes 100 Hz within 1 dB", "[FxUnits]")
{
    const auto p = filterParams(0, 1000.0, 0.0);
    CHECK(filterGainDb(p, 10000.0) < -20.0);
    CHECK(std::abs(filterGainDb(p, 100.0)) < 1.0);
}

TEST_CASE("Filter: high pass does the opposite", "[FxUnits]")
{
    const auto p = filterParams(1, 1000.0, 0.0);
    CHECK(filterGainDb(p, 100.0) < -20.0);
    CHECK(std::abs(filterGainDb(p, 10000.0)) < 1.0);
}

TEST_CASE("Filter: band pass at its cutoff stays above the same band pass far away", "[FxUnits]")
{
    const auto p = filterParams(2, 1000.0, 0.5);
    const double atCutoff = filterGainDb(p, 1000.0);
    CHECK(atCutoff > filterGainDb(p, 100.0) + 10.0);
    CHECK(atCutoff > filterGainDb(p, 10000.0) + 10.0);
}

TEST_CASE("Fx units: extreme parameters stay finite and bounded", "[FxUnits]")
{
    const int n = 5 * 48000;
    const auto l = noise(n, 5), r = noise(n, 6);

    auto checkBounded = [&](const std::vector<float>& a, const std::vector<float>& b) {
        for (int i = 0; i < n; ++i)
        {
            REQUIRE(std::isfinite(a[size_t(i)]));
            REQUIRE(std::abs(a[size_t(i)]) < 20.0f);
            REQUIRE(std::isfinite(b[size_t(i)]));
            REQUIRE(std::abs(b[size_t(i)]) < 20.0f);
        }
    };

    Bitcrush b;
    b.prepare(kSr);
    FxParams bp;
    bp.set(FxParam::BitcrushBits, 1.0);
    bp.set(FxParam::BitcrushRate, 64.0);
    bp.set(FxParam::BitcrushMix, 1.0);
    b.setParams(bp);
    auto bl = l, br = r;
    processBlocks(b, bl, br, 512);
    checkBounded(bl, br);

    for (int type = 0; type < 3; ++type)
    {
        Filter f;
        f.prepare(kSr);
        auto fl = l, fr = r;
        bool low = false;
        for (int pos = 0; pos < n; pos += 512)
        {
            f.setParams(filterParams(type, low ? 20.0 : 20000.0, 1.0));
            low = !low;
            f.process(fl.data() + pos, fr.data() + pos, std::min(512, n - pos));
        }
        checkBounded(fl, fr);
    }
}

TEST_CASE("Fx units: block sizes 1, 7 and 4096 give the same result", "[FxUnits]")
{
    const int n = 20000;
    const auto l0 = noise(n, 7), r0 = noise(n, 8);

    auto runBitcrush = [&](int block) {
        Bitcrush u;
        u.prepare(kSr);
        FxParams p;
        p.set(FxParam::BitcrushBits, 5.0);
        p.set(FxParam::BitcrushRate, 6.0);
        p.set(FxParam::BitcrushMix, 0.7);
        u.setParams(p);
        auto l = l0, r = r0;
        processBlocks(u, l, r, block);
        return l;
    };
    auto runFilter = [&](int block) {
        Filter u;
        u.prepare(kSr);
        u.setParams(filterParams(2, 500.0, 0.6));
        auto l = l0, r = r0;
        // Parameter change mid-buffer, on a boundary every block size shares.
        for (int pos = 0; pos < n; pos += 4096)
        {
            if (pos == 8192) u.setParams(filterParams(0, 8000.0, 0.3));
            const int len = std::min(4096, n - pos);
            for (int q = 0; q < len; q += block)
                u.process(l.data() + pos + q, r.data() + pos + q, std::min(block, len - q));
        }
        return l;
    };

    const auto b1 = runBitcrush(1), b7 = runBitcrush(7), b4096 = runBitcrush(4096);
    const auto f1 = runFilter(1), f7 = runFilter(7), f4096 = runFilter(4096);
    for (int i = 0; i < n; ++i)
    {
        REQUIRE(std::abs(b1[size_t(i)] - b7[size_t(i)]) < 1e-6f);
        REQUIRE(std::abs(b1[size_t(i)] - b4096[size_t(i)]) < 1e-6f);
        REQUIRE(std::abs(f1[size_t(i)] - f7[size_t(i)]) < 1e-6f);
        REQUIRE(std::abs(f1[size_t(i)] - f4096[size_t(i)]) < 1e-6f);
    }
}

// ---- Fix round 1: right channel, sample rates, smoothing, silence, bad input ----

TEST_CASE("Fx units: the right channel is processed independently of the left", "[FxUnits]")
{
    // Filter: L low frequency, R high frequency.
    for (int type = 0; type < 2; ++type)
    {
        Filter f;
        f.prepare(kSr);
        f.setParams(filterParams(type, 1000.0, 0.0));
        auto l = sine(48000, 100.0), r = sine(48000, 10000.0);
        const double inL = tailRms(l), inR = tailRms(r);
        processBlocks(f, l, r, 256);
        const double gL = dbOf(tailRms(l) / inL), gR = dbOf(tailRms(r) / inR);
        if (type == 0) { CHECK(std::abs(gL) < 1.0); CHECK(gR < -20.0); }
        else           { CHECK(gL < -20.0);         CHECK(std::abs(gR) < 1.0); }
    }

    // Bitcrush: R is crushed on its own (rate 8 hold, 1 bit) and differs from L.
    Bitcrush b;
    b.prepare(kSr);
    FxParams p;
    p.set(FxParam::BitcrushBits, 1.0);
    p.set(FxParam::BitcrushRate, 8.0);
    p.set(FxParam::BitcrushMix, 1.0);
    b.setParams(p);
    auto l = sine(4800, 440.0, 1.0f), r = noise(4800, 9);
    const auto r0 = r;
    processBlocks(b, l, r, 53);
    std::set<float> values(r.begin(), r.end());
    CHECK(values.size() == 2);
    for (size_t i = 0; i < r.size(); ++i)
    {
        CHECK(r[i] == r[i - i % 8]);
        CHECK(r[i] == (r0[i - i % 8] < 0.0f ? -0.5f : 0.5f));
    }
    CHECK(l != r);
}

TEST_CASE("Filter: stays finite and keeps its response at 44.1, 96 and 192 kHz", "[FxUnits]")
{
    for (double sr : {44100.0, 96000.0, 192000.0})
    {
        auto gainDb = [&](const FxParams& p, double freq) {
            Filter f;
            f.prepare(sr);
            f.setParams(p);
            auto l = sine(static_cast<int>(sr), freq, 0.8f, sr), r = l;
            const double inRms = tailRms(l);
            processBlocks(f, l, r, 256);
            for (float x : l) REQUIRE(std::isfinite(x));
            return dbOf(tailRms(l) / inRms);
        };
        CHECK(gainDb(filterParams(0, 1000.0, 0.0), 10000.0) < -20.0);
        CHECK(std::abs(gainDb(filterParams(0, 1000.0, 0.0), 100.0)) < 1.0);
        // Cutoff 20 kHz is clamped below 0.45 * sr (19.8 kHz at 44.1 kHz): still stable, passes 100 Hz.
        CHECK(std::abs(gainDb(filterParams(0, 20000.0, 0.5), 100.0)) < 1.0);
        for (int type = 0; type < 3; ++type)
        {
            Filter f;
            f.prepare(sr);
            f.setParams(filterParams(type, 20000.0, 1.0));
            auto l = noise(static_cast<int>(sr), 10), r = noise(static_cast<int>(sr), 11);
            processBlocks(f, l, r, 512);
            for (size_t i = 0; i < l.size(); ++i)
            {
                REQUIRE(std::isfinite(l[i]));
                REQUIRE(std::abs(l[i]) < 20.0f);
            }
        }
    }
}

TEST_CASE("Bitcrush: block sizes agree with a mix change mid-buffer", "[FxUnits]")
{
    const int n = 20000;
    const auto l0 = noise(n, 12), r0 = noise(n, 13);
    auto run = [&](int block) {
        Bitcrush u;
        u.prepare(kSr);
        FxParams p;
        p.set(FxParam::BitcrushBits, 4.0);
        p.set(FxParam::BitcrushRate, 3.0);
        p.set(FxParam::BitcrushMix, 1.0);
        u.setParams(p);
        auto l = l0, r = r0;
        for (int pos = 0; pos < n; pos += 4096)
        {
            if (pos == 8192)
            {
                p.set(FxParam::BitcrushMix, 0.2);
                u.setParams(p);
            }
            const int len = std::min(4096, n - pos);
            for (int q = 0; q < len; q += block)
                u.process(l.data() + pos + q, r.data() + pos + q, std::min(block, len - q));
        }
        return l;
    };
    const auto a = run(1), b = run(7), c = run(4096);
    for (int i = 0; i < n; ++i)
    {
        REQUIRE(std::abs(a[size_t(i)] - b[size_t(i)]) < 1e-6f);
        REQUIRE(std::abs(a[size_t(i)] - c[size_t(i)]) < 1e-6f);
    }
}

TEST_CASE("Fx units: the first setParams after prepare or reset jumps without a ramp", "[FxUnits]")
{
    // Bitcrush: default mix is 1, so a ramp towards 0 would alter the first samples.
    Bitcrush b;
    b.prepare(kSr);
    FxParams p;
    p.set(FxParam::BitcrushMix, 0.0);
    for (int round = 0; round < 2; ++round)
    {
        b.setParams(p);
        auto l = noise(256, 14), r = l;
        const auto l0 = l;
        b.process(l.data(), r.data(), 256);
        CHECK(l == l0);
        b.setParams(FxParams()); // a later change ramps ...
        b.reset();               // ... but after reset it jumps again
    }

    // Filter: default cutoff is 2 kHz; a 20 kHz low pass must pass 10 kHz straight away.
    Filter f;
    f.prepare(kSr);
    for (int round = 0; round < 2; ++round)
    {
        f.setParams(filterParams(0, 20000.0, 0.0));
        auto l = sine(1200, 10000.0), r = l;
        const auto in = l;
        f.process(l.data(), r.data(), 1200);
        std::vector<float> a(in.begin() + 200, in.end()), o(l.begin() + 200, l.end());
        CHECK(std::abs(dbOf(tailRms(o) / tailRms(a))) < 1.5);
        f.setParams(FxParams());
        f.reset();
    }
}

TEST_CASE("Filter: output decays after silence at resonance 1", "[FxUnits]")
{
    for (int type = 0; type < 3; ++type)
    {
        Filter f;
        f.prepare(kSr);
        f.setParams(filterParams(type, 1000.0, 1.0));
        auto l = noise(48000, 15), r = noise(48000, 16);
        processBlocks(f, l, r, 512);
        std::vector<float> sl(48000, 0.0f), sr(48000, 0.0f);
        processBlocks(f, sl, sr, 512);
        // Within 0.5 s of silence: below -80 dB.
        for (size_t i = 24000; i < sl.size(); ++i)
        {
            REQUIRE(std::abs(sl[i]) < 1e-4f);
            REQUIRE(std::abs(sr[i]) < 1e-4f);
        }
    }
}

TEST_CASE("Fx units: NaN and infinity in the input give finite output and recover", "[FxUnits]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const int n = 6000;
    auto bad = sine(n, 440.0), badR = bad;
    bad[100] = nan; bad[200] = inf; badR[150] = -inf; badR[250] = nan;

    {
        Filter f;
        f.prepare(kSr);
        f.setParams(filterParams(0, 5000.0, 0.5));
        auto l = bad, r = badR;
        processBlocks(f, l, r, 128);
        Filter g;
        g.prepare(kSr);
        g.setParams(filterParams(0, 5000.0, 0.5));
        auto cl = sine(n, 440.0), cr = cl;
        processBlocks(g, cl, cr, 128);
        for (int i = 0; i < n; ++i)
        {
            REQUIRE(std::isfinite(l[size_t(i)]));
            REQUIRE(std::isfinite(r[size_t(i)]));
        }
        // Recovered: matches the clean run once the disturbance has died out.
        for (int i = 3000; i < n; ++i)
        {
            REQUIRE(std::abs(l[size_t(i)] - cl[size_t(i)]) < 1e-3f);
            REQUIRE(std::abs(r[size_t(i)] - cr[size_t(i)]) < 1e-3f);
        }
    }
    {
        Bitcrush b;
        b.prepare(kSr);
        FxParams p;
        p.set(FxParam::BitcrushBits, 16.0);
        p.set(FxParam::BitcrushRate, 1.0);
        p.set(FxParam::BitcrushMix, 0.5);
        b.setParams(p);
        auto l = bad, r = badR;
        processBlocks(b, l, r, 128);
        const auto clean = sine(n, 440.0);
        for (int i = 0; i < n; ++i)
        {
            REQUIRE(std::isfinite(l[size_t(i)]));
            REQUIRE(std::isfinite(r[size_t(i)]));
        }
        for (int i = 300; i < n; ++i)
            REQUIRE(std::abs(l[size_t(i)] - clean[size_t(i)]) < 1e-3f);
    }
}
