#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <set>
#include <vector>
#include "engine/fx/Bitcrush.h"
#include "engine/fx/Filter.h"
#include "engine/fx/Flanger.h"
#include "engine/fx/Reverb.h"

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

inline FxParams flangerParams(double rate, double depth, double fb, double mix)
{
    FxParams p;
    p.set(FxParam::FlangerRate, rate);
    p.set(FxParam::FlangerDepth, depth);
    p.set(FxParam::FlangerFeedback, fb);
    p.set(FxParam::FlangerMix, mix);
    return p;
}

inline FxParams reverbParams(double mix, double size, double damping)
{
    FxParams p;
    p.set(FxParam::ReverbMix, mix);
    p.set(FxParam::ReverbSize, size);
    p.set(FxParam::ReverbDamping, damping);
    return p;
}

inline double energy(const std::vector<float>& v, size_t from, size_t to)
{
    double s = 0;
    for (size_t i = from; i < to && i < v.size(); ++i) s += double(v[i]) * v[i];
    return s;
}

inline double peakAbs(const std::vector<float>& v, size_t from, size_t to)
{
    double m = 0;
    for (size_t i = from; i < to && i < v.size(); ++i) m = std::max(m, double(std::abs(v[i])));
    return m;
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
    CHECK(values.size() <= 3); // mid-tread at 1 bit: {-1, 0, +1}
    CHECK(values.size() >= 2);
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

    auto runFlanger = [&](int block) {
        Flanger u;
        u.prepare(kSr);
        u.setParams(flangerParams(2.0, 0.8, 0.6, 0.7));
        auto l = l0, r = r0;
        for (int pos = 0; pos < n; pos += 4096)
        {
            if (pos == 8192) u.setParams(flangerParams(0.5, 0.3, -0.5, 0.4));
            const int len = std::min(4096, n - pos);
            for (int q = 0; q < len; q += block)
                u.process(l.data() + pos + q, r.data() + pos + q, std::min(block, len - q));
        }
        return l;
    };
    auto runReverb = [&](int block) {
        Reverb u;
        u.prepare(kSr);
        u.setParams(reverbParams(0.6, 0.8, 0.3));
        auto l = l0, r = r0;
        for (int pos = 0; pos < n; pos += 4096)
        {
            if (pos == 8192) u.setParams(reverbParams(0.2, 0.2, 0.9));
            const int len = std::min(4096, n - pos);
            for (int q = 0; q < len; q += block)
                u.process(l.data() + pos + q, r.data() + pos + q, std::min(block, len - q));
        }
        return l;
    };

    const auto fl1 = runFlanger(1), fl7 = runFlanger(7), fl4096 = runFlanger(4096);
    const auto rv1 = runReverb(1), rv7 = runReverb(7), rv4096 = runReverb(4096);
    for (int i = 0; i < n; ++i)
    {
        REQUIRE(std::abs(fl1[size_t(i)] - fl7[size_t(i)]) < 1e-5f);
        REQUIRE(std::abs(fl1[size_t(i)] - fl4096[size_t(i)]) < 1e-5f);
        REQUIRE(std::abs(rv1[size_t(i)] - rv7[size_t(i)]) < 1e-5f);
        REQUIRE(std::abs(rv1[size_t(i)] - rv4096[size_t(i)]) < 1e-5f);
    }

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
    CHECK(values.size() <= 3);
    for (size_t i = 0; i < r.size(); ++i)
    {
        CHECK(r[i] == r[i - i % 8]);
        CHECK(r[i] == std::round(r0[i - i % 8]));
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

// ---- Flanger and Reverb ----

TEST_CASE("Flanger: mix 0 outputs silence", "[FxUnits]")
{
    Flanger f;
    f.prepare(kSr);
    f.setParams(flangerParams(1.0, 1.0, 0.9, 0.0));
    auto l = noise(5000, 20), r = noise(5000, 21);
    processBlocks(f, l, r, 128);
    for (size_t i = 0; i < l.size(); ++i)
    {
        REQUIRE(l[i] == 0.0f);
        REQUIRE(r[i] == 0.0f);
    }
}

TEST_CASE("Flanger: feedback 0.9 decays to below -80 dB within 20 seconds after the input stops", "[FxUnits]")
{
    for (double fb : {0.9, -0.9})
    {
        Flanger f;
        f.prepare(kSr);
        f.setParams(flangerParams(1.0, 0.5, fb, 1.0));
        auto l = noise(48000, 22), r = noise(48000, 23);
        processBlocks(f, l, r, 512);
        const int n = 21 * 48000;
        std::vector<float> sl(size_t(n), 0.0f), sr(size_t(n), 0.0f);
        processBlocks(f, sl, sr, 512);
        CHECK(peakAbs(sl, 0, 4800) > 1e-3); // it does ring first
        CHECK(peakAbs(sl, 20 * 48000, size_t(n)) < 1e-4);
        CHECK(peakAbs(sr, 20 * 48000, size_t(n)) < 1e-4);
    }
}

TEST_CASE("Flanger: output stays finite and under 20 for full-scale noise at depth 1, feedback +-0.9", "[FxUnits]")
{
    for (double fb : {0.9, -0.9})
        for (double rate : {0.05, 5.0})
        {
            Flanger f;
            f.prepare(kSr);
            f.setParams(flangerParams(rate, 1.0, fb, 1.0));
            auto l = noise(5 * 48000, 24), r = noise(5 * 48000, 25);
            processBlocks(f, l, r, 512);
            for (size_t i = 0; i < l.size(); ++i)
            {
                REQUIRE(std::isfinite(l[i]));
                REQUIRE(std::abs(l[i]) < 20.0f);
                REQUIRE(std::isfinite(r[i]));
                REQUIRE(std::abs(r[i]) < 20.0f);
            }
        }
}

TEST_CASE("Flanger: without feedback the output is the input delayed by 0.1 to 7 ms", "[FxUnits]")
{
    Flanger f;
    f.prepare(kSr);
    f.setParams(flangerParams(1.0, 1.0, 0.0, 1.0));
    auto l = sine(48000, 440.0), r = l;
    const double inRms = tailRms(l);
    processBlocks(f, l, r, 256);
    CHECK(std::abs(dbOf(tailRms(l) / inRms)) < 1.0);

    Flanger g;
    g.prepare(kSr);
    g.setParams(flangerParams(1.0, 1.0, 0.0, 1.0));
    std::vector<float> il(4800, 0.0f), ir(4800, 0.0f);
    il[10] = 1.0f;
    processBlocks(g, il, ir, 256);
    size_t at = 0;
    for (size_t i = 0; i < il.size(); ++i)
        if (std::abs(il[i]) > std::abs(il[at])) at = i;
    CHECK(at >= 10 + 4);   // >= 0.1 ms
    CHECK(at <= 10 + 337); // <= 7 ms
}

TEST_CASE("Flanger: the right channel is processed independently of the left", "[FxUnits]")
{
    Flanger f;
    f.prepare(kSr);
    f.setParams(flangerParams(1.0, 0.7, 0.5, 1.0));
    std::vector<float> l(4800, 0.0f);
    auto r = noise(4800, 26);
    processBlocks(f, l, r, 100);
    for (float x : l) REQUIRE(x == 0.0f);
    CHECK(energy(r, 0, r.size()) > 1.0);
}

TEST_CASE("Flanger: the LFO keeps running during silence and across reset", "[FxUnits]")
{
    // An impulse is delayed by the instantaneous LFO delay. Two impulses fed at different times
    // (0.1 s of silence apart, at 2 Hz the LFO has advanced 0.2 cycles) must see different delays.
    auto delayOfImpulse = [](Flanger& f) {
        std::vector<float> a(2000, 0.0f), b(2000, 0.0f);
        a[0] = 1.0f;
        f.process(a.data(), b.data(), 2000);
        size_t at = 0;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::abs(a[i]) > std::abs(a[at])) at = i;
        return at;
    };
    Flanger f;
    f.prepare(kSr);
    f.setParams(flangerParams(2.0, 1.0, 0.0, 1.0));
    const size_t first = delayOfImpulse(f);
    std::vector<float> s1(2800, 0.0f), s2(2800, 0.0f);
    f.process(s1.data(), s2.data(), 2800); // 2000 + 2800 = 0.1 s
    const size_t second = delayOfImpulse(f);
    CHECK(first != second);
    f.reset();
    std::vector<float> s3(2800, 0.0f), s4(2800, 0.0f);
    f.process(s3.data(), s4.data(), 2800);
    CHECK(delayOfImpulse(f) != first); // reset did not rewind the LFO
}

TEST_CASE("Reverb: mix 0 outputs silence", "[FxUnits]")
{
    Reverb v;
    v.prepare(kSr);
    v.setParams(reverbParams(0.0, 1.0, 0.0));
    auto l = noise(5000, 27), r = noise(5000, 28);
    processBlocks(v, l, r, 128);
    for (size_t i = 0; i < l.size(); ++i)
    {
        REQUIRE(l[i] == 0.0f);
        REQUIRE(r[i] == 0.0f);
    }
}

namespace {
std::vector<float> reverbImpulse(double size, double damping, int seconds, std::vector<float>* rightOut = nullptr)
{
    Reverb v;
    v.prepare(kSr);
    v.setParams(reverbParams(1.0, size, damping));
    const size_t n = size_t(seconds) * 48000;
    std::vector<float> l(n, 0.0f), r(n, 0.0f);
    l[0] = r[0] = 1.0f;
    processBlocks(v, l, r, 512);
    if (rightOut) *rightOut = r;
    return l;
}
}

TEST_CASE("Reverb: an impulse rings and decays below -60 dB within 30 seconds at size 1, damping 0", "[FxUnits]")
{
    const auto l = reverbImpulse(1.0, 0.0, 30);
    const double peak = peakAbs(l, 0, l.size());
    CHECK(peak > 1e-3);
    CHECK(peakAbs(l, 48000, 96000) > 1e-3 * peak); // still ringing after 1 s
    CHECK(peakAbs(l, 29 * 48000, l.size()) < 1e-3 * peak);
}

TEST_CASE("Reverb: size 0 decays faster than size 1", "[FxUnits]")
{
    const auto small = reverbImpulse(0.0, 0.5, 4);
    const auto big = reverbImpulse(1.0, 0.5, 4);
    CHECK(energy(small, 2 * 48000, 3 * 48000) < 0.1 * energy(big, 2 * 48000, 3 * 48000));
}

TEST_CASE("Reverb: damping 1 has less energy above 8 kHz than damping 0", "[FxUnits]")
{
    // Energy above 8 kHz relative to the energy below 1 kHz (the wet level is normalized, so the
    // spectral tilt is compared, not the absolute level).
    auto tilt = [](double damping) {
        Reverb v;
        v.prepare(kSr);
        v.setParams(reverbParams(1.0, 0.5, damping));
        auto l = noise(48000, 29, 0.5f), r = noise(48000, 30, 0.5f);
        processBlocks(v, l, r, 512);
        auto band = [&](int type, double cutoff) {
            Filter f;
            f.prepare(kSr);
            f.setParams(filterParams(type, cutoff, 0.0));
            auto bl = l, br = r;
            processBlocks(f, bl, br, 512);
            return energy(bl, 24000, bl.size());
        };
        return band(1, 8000.0) / band(0, 1000.0);
    };
    CHECK(tilt(1.0) < 0.5 * tilt(0.0));
}

TEST_CASE("Reverb: full-scale noise at extreme parameters stays finite and under 20", "[FxUnits]")
{
    for (double size : {0.0, 1.0})
        for (double damping : {0.0, 1.0})
        {
            Reverb v;
            v.prepare(kSr);
            v.setParams(reverbParams(1.0, size, damping));
            auto l = noise(5 * 48000, 31), r = noise(5 * 48000, 32);
            processBlocks(v, l, r, 512);
            for (size_t i = 0; i < l.size(); ++i)
            {
                REQUIRE(std::isfinite(l[i]));
                REQUIRE(std::abs(l[i]) < 20.0f);
                REQUIRE(std::isfinite(r[i]));
                REQUIRE(std::abs(r[i]) < 20.0f);
            }
        }
}

TEST_CASE("Reverb: at mix 1 white noise comes out within 3 dB of its input RMS, peak well under 20", "[FxUnits]")
{
    for (double sr : {44100.0, 48000.0, 192000.0})
        for (double size : {0.0, 0.5, 1.0})
            for (double damping : {0.0, 0.5, 1.0})
            {
                Reverb v;
                v.prepare(sr);
                v.setParams(reverbParams(1.0, size, damping));
                const int n = static_cast<int>(6 * sr);
                auto l = noise(n, 41), r = noise(n, 42);
                const double inRms = tailRms(l);
                processBlocks(v, l, r, 512);
                INFO("sr " << sr << " size " << size << " damping " << damping);
                CHECK(std::abs(dbOf(tailRms(l) / inRms)) < 3.0);
                CHECK(std::abs(dbOf(tailRms(r) / inRms)) < 3.0);
                CHECK(std::max(peakAbs(l, 0, l.size()), peakAbs(r, 0, r.size())) < 8.0);
            }
}

TEST_CASE("Flanger: at mix 1 and feedback +-0.9 white noise gains at most 6 dB RMS", "[FxUnits]")
{
    for (double sr : {44100.0, 192000.0})
        for (double fb : {-0.9, 0.9})
            for (double depth : {0.0, 1.0})
            {
                Flanger f;
                f.prepare(sr);
                f.setParams(flangerParams(5.0, depth, fb, 1.0));
                const int n = static_cast<int>(6 * sr);
                auto l = noise(n, 43), r = noise(n, 44);
                const double inRms = tailRms(l);
                processBlocks(f, l, r, 512);
                INFO("sr " << sr << " fb " << fb << " depth " << depth);
                CHECK(dbOf(tailRms(l) / inRms) < 6.0);
                CHECK(dbOf(tailRms(r) / inRms) < 6.0);
            }
}

namespace fxtest {
// Drum-like test signal: a decaying noise burst (25 ms time constant) every 250 ms, peak about amp.
inline std::vector<float> hits(int n, unsigned seed, float amp = 0.5f, double sr = kSr)
{
    auto v = noise(n, seed, amp);
    const int period = static_cast<int>(0.25 * sr);
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i)] *= std::exp(-static_cast<float>(i % period) / static_cast<float>(0.025 * sr));
    return v;
}

inline double rmsOf(const std::vector<float>& v, size_t from)
{
    double s = 0;
    for (size_t i = from; i < v.size(); ++i) s += double(v[i]) * v[i];
    return std::sqrt(s / double(v.size() - from));
}
} // namespace fxtest

TEST_CASE("Filter: resonance 1 lifts a sine at the cutoff by at most 12 dB", "[FxUnits]")
{
    for (int type : {0, 1, 2})
    {
        INFO("type " << type);
        Filter f;
        f.prepare(kSr);
        f.setParams(filterParams(type, 1000.0, 1.0));
        auto l = sine(48000, 1000.0, 0.5f), r = l;
        processBlocks(f, l, r, 256);
        CHECK(peakAbs(l, 24000, l.size()) <= 2.0 * 1.02);
        if (type != 2) // the band pass is normalized to unity at its cutoff
            CHECK(peakAbs(l, 24000, l.size()) > 1.0); // still a clear resonance peak
    }
}

TEST_CASE("Flanger: at feedback +-0.9 and mix 1 drum-like material gains at most 2 dB RMS", "[FxUnits]")
{
    for (double fb : {-0.9, 0.9})
        for (double depth : {0.0, 0.5, 1.0})
        {
            INFO("fb " << fb << " depth " << depth);
            Flanger f;
            f.prepare(kSr);
            f.setParams(flangerParams(0.5, depth, fb, 1.0));
            auto l = hits(6 * 48000, 51), r = hits(6 * 48000, 52);
            const double inRms = rmsOf(l, 48000);
            processBlocks(f, l, r, 512);
            CHECK(dbOf(rmsOf(l, 48000) / inRms) < 2.0);
            CHECK(dbOf(rmsOf(r, 48000) / inRms) < 2.0);
        }
}

TEST_CASE("Flanger: a feedback change does not click", "[FxUnits]")
{
    Flanger f;
    f.prepare(kSr);
    f.setParams(flangerParams(0.5, 0.0, 0.0, 1.0));
    auto l = sine(48000, 200.0, 0.5f), r = l;
    const int half = 24000;
    for (int pos = 0; pos < 48000; pos += 128)
    {
        if (pos == half) f.setParams(flangerParams(0.5, 0.0, 0.9, 1.0));
        f.process(l.data() + pos, r.data() + pos, 128);
    }
    double step = 0;
    for (size_t i = 1; i < l.size(); ++i) step = std::max(step, double(std::abs(l[i] - l[i - 1])));
    CHECK(step < 0.05); // the 200 Hz sine itself moves by up to 0.013 per sample
}

TEST_CASE("Reverb: the two channels are decorrelated for a mono input", "[FxUnits]")
{
    std::vector<float> r;
    const auto l = reverbImpulse(0.7, 0.3, 1, &r);
    CHECK(l != r);
    CHECK(energy(r, 0, r.size()) > 1e-6);
}

TEST_CASE("Reverb: tailLevel is 0 after reset and positive while ringing", "[FxUnits]")
{
    Reverb v;
    v.prepare(kSr);
    Flanger f;
    f.prepare(kSr);
    CHECK(v.tailLevel() == 0.0f);
    CHECK(f.tailLevel() == 0.0f);
    v.setParams(reverbParams(0.5, 0.5, 0.5));
    f.setParams(flangerParams(1.0, 0.5, 0.5, 0.5));
    auto l = noise(4800, 33), r = noise(4800, 34);
    auto l2 = l, r2 = r;
    processBlocks(v, l, r, 256);
    processBlocks(f, l2, r2, 256);
    CHECK(v.tailLevel() > 0.0f);
    CHECK(f.tailLevel() > 0.0f);
    v.reset();
    f.reset();
    CHECK(v.tailLevel() == 0.0f);
    CHECK(f.tailLevel() == 0.0f);

    // The tail eventually reports empty after silence (so the chain can put the insert to sleep).
    v.setParams(reverbParams(0.5, 0.0, 0.5));
    l = noise(4800, 33);
    r = noise(4800, 34);
    processBlocks(v, l, r, 256);
    std::vector<float> sl(10 * 48000, 0.0f), sr(10 * 48000, 0.0f);
    processBlocks(v, sl, sr, 512);
    CHECK(v.tailLevel() == 0.0f);
}

TEST_CASE("Fx units: prepare at 44100, 96000 and 192000 works for all four units", "[FxUnits]")
{
    for (double sr : {44100.0, 96000.0, 192000.0})
    {
        const int n = static_cast<int>(sr);
        Bitcrush b;
        Filter fl;
        Flanger fg;
        Reverb rv;
        b.prepare(sr);
        fl.prepare(sr);
        fg.prepare(sr);
        rv.prepare(sr);
        FxParams p;
        p.set(FxParam::BitcrushBits, 6.0);
        p.set(FxParam::BitcrushRate, 4.0);
        b.setParams(p);
        fl.setParams(filterParams(0, 3000.0, 0.7));
        fg.setParams(flangerParams(1.0, 1.0, 0.9, 1.0));
        rv.setParams(reverbParams(1.0, 1.0, 0.0));
        auto run = [&](auto& unit) {
            auto l = noise(n, 35), r = noise(n, 36);
            processBlocks(unit, l, r, 512);
            for (size_t i = 0; i < l.size(); ++i)
            {
                REQUIRE(std::isfinite(l[i]));
                REQUIRE(std::isfinite(r[i]));
                REQUIRE(std::abs(l[i]) < 20.0f);
            }
        };
        run(b);
        run(fl);
        run(fg);
        run(rv);
    }
}

TEST_CASE("Flanger and Reverb: NaN and infinity in the input give finite output and recover", "[FxUnits]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const int n = 12000;
    auto bad = sine(n, 440.0), badR = bad;
    bad[100] = nan; bad[200] = inf; badR[150] = -inf; badR[250] = nan;

    {
        Flanger f, g;
        f.prepare(kSr);
        g.prepare(kSr);
        f.setParams(flangerParams(1.0, 0.5, 0.5, 1.0));
        g.setParams(flangerParams(1.0, 0.5, 0.5, 1.0));
        auto l = bad, r = badR;
        processBlocks(f, l, r, 128);
        auto cl = sine(n, 440.0), cr = cl;
        processBlocks(g, cl, cr, 128);
        for (int i = 0; i < n; ++i)
        {
            REQUIRE(std::isfinite(l[size_t(i)]));
            REQUIRE(std::isfinite(r[size_t(i)]));
        }
        for (int i = 6000; i < n; ++i)
        {
            REQUIRE(std::abs(l[size_t(i)] - cl[size_t(i)]) < 1e-3f);
            REQUIRE(std::abs(r[size_t(i)] - cr[size_t(i)]) < 1e-3f);
        }
        CHECK(std::isfinite(f.tailLevel()));
    }
    {
        Reverb v;
        v.prepare(kSr);
        v.setParams(reverbParams(1.0, 1.0, 0.0));
        auto l = bad, r = badR;
        processBlocks(v, l, r, 128);
        std::vector<float> sl(48000, 0.0f), sr(48000, 0.0f);
        processBlocks(v, sl, sr, 128);
        for (int i = 0; i < n; ++i)
        {
            REQUIRE(std::isfinite(l[size_t(i)]));
            REQUIRE(std::isfinite(r[size_t(i)]));
        }
        for (size_t i = 0; i < sl.size(); ++i)
        {
            REQUIRE(std::isfinite(sl[i]));
            REQUIRE(std::abs(sl[i]) < 20.0f);
        }
        CHECK(std::isfinite(v.tailLevel()));
        // State is not stuck: the unit still produces output for clean input afterwards.
        auto cl = sine(4800, 440.0), cr = cl;
        processBlocks(v, cl, cr, 128);
        CHECK(energy(cl, 0, cl.size()) > 1e-6);
    }
}

TEST_CASE("Bitcrush: silence stays exactly silent for every bits, rate and mix", "[FxUnits]")
{
    for (int bits = 1; bits <= 16; ++bits)
        for (int rate : {1, 2, 3, 8, 64})
            for (double mix : {0.0, 0.3, 1.0})
            {
                Bitcrush b;
                b.prepare(kSr);
                FxParams p;
                p.set(FxParam::BitcrushBits, bits);
                p.set(FxParam::BitcrushRate, rate);
                p.set(FxParam::BitcrushMix, mix);
                b.setParams(p);
                std::vector<float> l(600, 0.0f), r(600, 0.0f);
                processBlocks(b, l, r, 77);
                for (size_t i = 0; i < l.size(); ++i)
                {
                    REQUIRE(l[i] == 0.0f);
                    REQUIRE(r[i] == 0.0f);
                }
            }
}

TEST_CASE("Bitcrush: a small DC input stays bounded", "[FxUnits]")
{
    for (int bits : {1, 4, 16})
    {
        Bitcrush b;
        b.prepare(kSr);
        FxParams p;
        p.set(FxParam::BitcrushBits, bits);
        p.set(FxParam::BitcrushRate, 4.0);
        p.set(FxParam::BitcrushMix, 1.0);
        b.setParams(p);
        std::vector<float> l(600, 0.01f), r(600, -0.01f);
        processBlocks(b, l, r, 100);
        const float step = 1.0f / std::exp2(float(bits) - 1.0f);
        for (size_t i = 0; i < l.size(); ++i)
        {
            REQUIRE(std::abs(l[i]) <= 0.01f + step);
            REQUIRE(std::abs(r[i]) <= 0.01f + step);
        }
    }
}

// ---- Flanger standing mode (rate 0 -> fixed position from Manual) ----

namespace {
FxParams flangerParamsManual(double rate, double manual)
{
    auto p = fxtest::flangerParams(rate, 1.0, 0.0, 1.0);
    p.set(FxParam::FlangerManual, manual);
    return p;
}
}

TEST_CASE("Flanger: at rate 0 it is stationary, independent of the LFO phase", "[FxUnits]")
{
    Flanger a;
    a.prepare(kSr);
    a.setParams(flangerParamsManual(0.0, 0.5));
    Flanger b;
    b.prepare(kSr);
    b.setParams(flangerParamsManual(1.0, 0.5));
    std::vector<float> s1(17800, 0.0f), s2(17800, 0.0f);
    b.process(s1.data(), s2.data(), 17800); // advance the LFO phase by an odd amount
    b.setParams(flangerParamsManual(0.0, 0.5));
    std::vector<float> s3(48000, 0.0f), s4(48000, 0.0f);
    b.process(s3.data(), s4.data(), 48000); // let the rate settle at 0
    b.reset();

    auto la = noise(4800, 7), ra = noise(4800, 8);
    auto lb = la, rb = ra;
    a.process(la.data(), ra.data(), 4800);
    b.process(lb.data(), rb.data(), 4800);
    for (size_t i = 0; i < la.size(); ++i)
    {
        REQUIRE(std::abs(la[i] - lb[i]) < 1e-6f);
        REQUIRE(std::abs(ra[i] - rb[i]) < 1e-6f);
    }
}

TEST_CASE("Flanger: Manual 0 and 1 give different wet output at rate 0", "[FxUnits]")
{
    auto render = [](double manual) {
        Flanger f;
        f.prepare(kSr);
        f.setParams(flangerParamsManual(0.0, manual));
        auto l = noise(4800, 21), r = noise(4800, 22);
        f.process(l.data(), r.data(), 4800);
        return l;
    };
    const auto m0 = render(0.0), m1 = render(1.0);
    double diff = 0;
    for (size_t i = 0; i < m0.size(); ++i) diff += std::abs(m0[i] - m1[i]);
    CHECK(diff > 10.0);
}

TEST_CASE("Flanger: with rate > 0 Manual is ignored", "[FxUnits]")
{
    Flanger a, b;
    a.prepare(kSr);
    b.prepare(kSr);
    a.setParams(flangerParamsManual(0.5, 0.0));
    b.setParams(flangerParamsManual(0.5, 1.0));
    auto la = noise(9600, 5), ra = noise(9600, 6);
    auto lb = la, rb = ra;
    a.process(la.data(), ra.data(), 9600);
    b.process(lb.data(), rb.data(), 9600);
    CHECK(la == lb);
    CHECK(ra == rb);
}

TEST_CASE("Flanger: rate 0 to 0.3 and back does not jump", "[FxUnits]")
{
    Flanger f;
    f.prepare(kSr);
    f.setParams(flangerParamsManual(0.0, 0.0));
    auto l = sine(72000, 200.0, 0.5f), r = l;
    for (int pos = 0; pos < 72000; pos += 128)
    {
        if (pos == 12032) f.setParams(flangerParamsManual(0.3, 0.0));
        if (pos == 36096) f.setParams(flangerParamsManual(0.0, 0.0));
        f.process(l.data() + pos, r.data() + pos, std::min(128, 72000 - pos));
    }
    double step = 0;
    for (size_t i = 1; i < l.size(); ++i) step = std::max(step, double(std::abs(l[i] - l[i - 1])));
    CHECK(step < 0.05); // the 200 Hz sine itself moves by up to 0.013 per sample
}

// ---- Filter modes Notch, Peak, LP 24 --------------------------------------------------------------

namespace fxtest {
// Platform-independent noise (std::uniform_real_distribution differs between standard libraries):
// a 32-bit LCG, top 24 bits mapped exactly to [-0.5, 0.5).
inline std::vector<float> lcgNoise(int n, uint32_t seed)
{
    std::vector<float> v(static_cast<size_t>(n));
    uint32_t s = seed;
    for (auto& x : v)
    {
        s = s * 1664525u + 1013904223u;
        x = static_cast<float>(s >> 8) * (1.0f / 16777216.0f) - 0.5f;
    }
    return v;
}

// Reference render for the bit-identity test: 64 frames of LCG noise through one filter type.
inline std::vector<float> filterReferenceRender(int type)
{
    Filter f;
    f.prepare(kSr);
    f.setParams(filterParams(type, 3000.0, 0.5));
    auto l = lcgNoise(64, 12345u), r = lcgNoise(64, 678u);
    f.process(l.data(), r.data(), 64);
    return l;
}
} // namespace fxtest

TEST_CASE("Filter: types 0..2 are bit-identical to a stored reference render", "[FxUnits]")
{
    // Captured with filterReferenceRender() from the Filter before Notch, Peak and LP 24 existed
    // (printed with %.9g, which round-trips a float exactly).
    static constexpr float kRef[3][16] = {
        {-0.0163889695f, -0.0764880776f, -0.161809355f, -0.216542944f, -0.205788836f, -0.149712428f,
         -0.0972434282f, -0.0540736392f, -0.00588434702f, 0.0511703975f, 0.0965063274f, 0.105957553f,
         0.115850657f, 0.133485094f, 0.122872047f, 0.0953484029f},
        {-0.414216995f, -0.27630192f, 0.329352796f, 0.390673757f, 0.544444382f, -0.334080577f,
         0.0325424522f, 0.0339658931f, 0.0263893511f, 0.137321994f, -0.597216249f, 0.15015538f,
         0.30807361f, -0.570646882f, 0.119289614f, -0.0953329653f},
        {-0.0489913523f, -0.130662203f, -0.124387637f, -0.0392267928f, 0.0713739246f, 0.0962546319f,
         0.060590338f, 0.0684565902f, 0.0755950883f, 0.094957985f, 0.0405641794f, -0.0123117622f,
         0.0418850854f, 0.0108293351f, -0.0425547659f, -0.0397213101f},
    };
#if defined(_MSC_VER)
    constexpr float kTol = 0.0f; // the platform the constants were captured on: exact
#else
    constexpr float kTol = 1e-6f; // other libm (tan/exp/pow) may differ in the last bit
#endif
    for (int type = 0; type < 3; ++type)
    {
        INFO("type " << type);
        const auto out = filterReferenceRender(type);
        for (int i = 0; i < 16; ++i)
        {
            INFO("sample " << i);
            CHECK(std::abs(out[size_t(i)] - kRef[type][i]) <= kTol);
        }
    }
}

TEST_CASE("Filter: notch removes a sine at the cutoff by at least 20 dB and passes 100 Hz and 10 kHz within 1.5 dB", "[FxUnits]")
{
    const auto p = filterParams(3, 2000.0, 0.2);
    CHECK(filterGainDb(p, 2000.0) < -20.0);
    CHECK(std::abs(filterGainDb(p, 100.0)) < 1.5);
    CHECK(std::abs(filterGainDb(p, 10000.0)) < 1.5);
}

TEST_CASE("Filter: peak lifts a sine at the cutoff above the same sine far away", "[FxUnits]")
{
    const auto p = filterParams(4, 1000.0, 1.0);
    const double atCutoff = filterGainDb(p, 1000.0);
    CHECK(atCutoff > filterGainDb(p, 100.0) + 3.0);
    CHECK(atCutoff > filterGainDb(p, 10000.0) + 3.0);
}

TEST_CASE("Filter: peak is unity far from the cutoff at resonance 0.2", "[FxUnits]")
{
    const auto p = filterParams(4, 1000.0, 0.2);
    CHECK(std::abs(filterGainDb(p, 100.0)) < 1.5);
    CHECK(std::abs(filterGainDb(p, 10000.0)) < 1.5);
    CHECK(filterGainDb(p, 1000.0) > 3.0);
}

TEST_CASE("Filter: LP24 attenuates 8 kHz at least 30 dB more than LP at the same cutoff, passes 100 Hz within 1 dB", "[FxUnits]")
{
    const auto lp = filterParams(0, 1000.0, 0.2), lp24 = filterParams(5, 1000.0, 0.2);
    CHECK(filterGainDb(lp24, 8000.0) < filterGainDb(lp, 8000.0) - 30.0);
    CHECK(std::abs(filterGainDb(lp24, 100.0)) < 1.0);
}

TEST_CASE("Filter: notch, peak and LP24 stay finite and below 4x the input peak at resonance 1 and 44.1, 96, 192 kHz", "[FxUnits]")
{
    for (double sr : {44100.0, 96000.0, 192000.0})
        for (int type = 3; type < 6; ++type)
            for (double cutoff : {100.0, 1000.0, 20000.0})
            {
                INFO("sr " << sr << " type " << type << " cutoff " << cutoff);
                Filter f;
                f.prepare(sr);
                f.setParams(filterParams(type, cutoff, 1.0));
                const int n = static_cast<int>(sr);
                auto l = lcgNoise(n, 21u), r = lcgNoise(n, 22u);
                for (auto* v : {&l, &r})
                    for (auto& x : *v) x *= 2.0f; // peak 1
                double inPeak = std::max(peakAbs(l, 0, l.size()), peakAbs(r, 0, r.size()));
                processBlocks(f, l, r, 512);
                for (size_t i = 0; i < l.size(); ++i)
                {
                    REQUIRE(std::isfinite(l[i]));
                    REQUIRE(std::isfinite(r[i]));
                }
                CHECK(std::max(peakAbs(l, 0, l.size()), peakAbs(r, 0, r.size())) < 4.0 * inPeak);
                // A sine at the cutoff (the resonance peak) stays below 4x as well.
                Filter g;
                g.prepare(sr);
                g.setParams(filterParams(type, cutoff, 1.0));
                const double hz = std::min(cutoff, 0.45 * sr);
                auto sl = sine(n, hz, 1.0f, sr), sr2 = sl;
                processBlocks(g, sl, sr2, 512);
                CHECK(peakAbs(sl, 0, sl.size()) < 4.0);
            }
}

TEST_CASE("Filter: switching the type while sounding does not jump by more than 0.05 between samples", "[FxUnits]")
{
    // Cutoff at the sine frequency: LP and HP are in antiphase there, an abrupt switch jumps by up to 1.
    Filter f;
    f.prepare(kSr);
    f.setParams(filterParams(0, 100.0, 0.2));
    const int n = 12 * 1000 + 2000;
    auto l = sine(n, 100.0, 0.5f), r = l;
    for (int pos = 0, type = 0; pos < n; pos += 1000, type = (type + 1) % 6)
    {
        f.setParams(filterParams(type, 100.0, 0.2));
        f.process(l.data() + pos, r.data() + pos, std::min(1000, n - pos));
    }
    double step = 0;
    for (size_t i = 1; i < l.size(); ++i) step = std::max(step, double(std::abs(l[i] - l[i - 1])));
    CHECK(step < 0.05);
}

// ---- Filter LFO ------------------------------------------------------------------------------------

namespace fxtest {
inline FxParams filterLfoParams(int type, double cutoff, double res, int rate, double depth, int shape)
{
    FxParams p = filterParams(type, cutoff, res);
    p.set(FxParam::FilterLfoRate, rate);
    p.set(FxParam::FilterLfoDepth, depth);
    p.set(FxParam::FilterLfoShape, shape);
    return p;
}

constexpr int kLfoRateQuarter = 8; // 1/4: one cycle per beat
constexpr int kVariedBlocks[] = {17, 256, 1, 999, 64, 512, 3, 128, 777, 2048};

// Renders in varied block sizes; when hostPos is true it calls setPosition before every block with the position of the
// block start at bpm (startBeat + frames / samples per beat), as a playing host would.
inline void renderLfo(Filter& f, std::vector<float>& l, std::vector<float>& r, bool hostPos, double startBeat,
                      double bpm, double sr = kSr)
{
    const int n = static_cast<int>(l.size());
    for (int pos = 0, k = 0; pos < n; k = (k + 1) % 10)
    {
        const int len = std::min(kVariedBlocks[k], n - pos);
        if (hostPos) f.setPosition(startBeat + pos * bpm / (60.0 * sr));
        f.process(l.data() + pos, r.data() + pos, len);
        pos += len;
    }
}

inline double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    double m = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) m = std::max(m, double(std::abs(a[i] - b[i])));
    return m;
}

inline double rmsOf(const std::vector<float>& v, size_t from, size_t to)
{
    return std::sqrt(energy(v, from, to) / double(to - from));
}
} // namespace fxtest

TEST_CASE("Filter: depth 0 leaves the output identical for any rate and shape", "[FxUnits]")
{
    const int n = 20000;
    for (int type = 0; type < Filter::kNumTypes; ++type)
    {
        Filter ref;
        ref.prepare(kSr);
        ref.setParams(filterParams(type, 1500.0, 0.6));
        auto l0 = lcgNoise(n, 31u), r0 = lcgNoise(n, 32u);
        processBlocks(ref, l0, r0, 300);
        for (int rate : {0, 5, 8, 14})
            for (int shape = 0; shape < 4; ++shape)
            {
                INFO("type " << type << " rate " << rate << " shape " << shape);
                Filter f;
                f.prepare(kSr);
                f.setTempo(173.0, 3.0);
                f.setPosition(2.37);
                f.setParams(filterLfoParams(type, 1500.0, 0.6, rate, 0.0, shape));
                auto l = lcgNoise(n, 31u), r = lcgNoise(n, 32u);
                renderLfo(f, l, r, true, 2.37, 173.0);
                CHECK(l == l0);
                CHECK(r == r0);
            }
    }
}

TEST_CASE("Filter: LFO square at depth 1 moves the cutoff by plus/minus two octaves on the beat", "[FxUnits]")
{
    // LP at 2 kHz, 120 BPM, 1/4: high half (beats 0..0.5) at 8 kHz, low half at 500 Hz.
    // A 6 kHz sine passes the 8 kHz low pass (about +1 dB at Q 1) and is about -43 dB at 500 Hz.
    Filter f;
    f.prepare(kSr);
    f.setTempo(120.0, 4.0);
    f.setParams(filterLfoParams(0, 2000.0, 0.2, kLfoRateQuarter, 1.0, 3));
    const int beat = 24000; // samples per beat at 120 BPM, 48 kHz
    auto l = sine(2 * beat, 6000.0, 0.5f), r = l;
    const double inRms = tailRms(l);
    renderLfo(f, l, r, true, 0.0, 120.0);
    const size_t half = beat / 2, margin = 480; // skip 10 ms after each edge (slew and filter settling)
    for (size_t c = 0; c < 2; ++c)
    {
        INFO("cycle " << c);
        const size_t start = c * beat;
        const double high = dbOf(rmsOf(l, start + margin, start + half) / inRms);
        const double low = dbOf(rmsOf(l, start + half + margin, start + beat) / inRms);
        CHECK(high > -2.0); // +2 octaves (a 1-octave swing would give about -6 dB)
        CHECK(high < 3.0);
        CHECK(low < -30.0); // -2 octaves
        CHECK(high > low + 25.0);
    }
}

TEST_CASE("Filter: the LFO phase follows setPosition", "[FxUnits]")
{
    // 1/4 = one cycle per beat: positions 0 and 4 beats are the same phase, 0.5 beats is half a cycle off.
    auto render = [](double startBeat) {
        Filter f;
        f.prepare(kSr);
        f.setTempo(120.0, 4.0);
        f.setParams(filterLfoParams(0, 2000.0, 0.3, kLfoRateQuarter, 1.0, 0));
        auto l = lcgNoise(60000, 41u), r = lcgNoise(60000, 42u);
        renderLfo(f, l, r, true, startBeat, 120.0);
        return l;
    };
    const auto a = render(0.0), b = render(4.0), c = render(0.5);
    CHECK(maxAbsDiff(a, b) < 1e-4);
    CHECK(maxAbsDiff(a, c) > 0.1);
    CHECK(rmsOf(a, 0, a.size()) > 0.01); // not trivially silent
}

TEST_CASE("Filter: the LFO keeps running at the last tempo without setPosition", "[FxUnits]")
{
    const int n = 3 * 32000; // 3 beats at 90 BPM
    auto make = [] {
        Filter f;
        f.prepare(kSr);
        f.setParams(filterLfoParams(0, 2000.0, 0.3, kLfoRateQuarter, 1.0, 0));
        return f;
    };
    // Host playing at 90 BPM: a position every block.
    Filter host = make();
    host.setTempo(90.0, 4.0);
    auto lh = lcgNoise(n, 51u), rh = lcgNoise(n, 52u);
    renderLfo(host, lh, rh, true, 1.25, 90.0);
    // Host stops after the first position at 90 BPM; an invalid tempo afterwards keeps 90.
    Filter freeRun = make();
    freeRun.setTempo(90.0, 4.0);
    freeRun.setPosition(1.25);
    freeRun.setTempo(0.0, 4.0);
    auto lf = lcgNoise(n, 51u), rf = lcgNoise(n, 52u);
    renderLfo(freeRun, lf, rf, false, 0.0, 90.0);
    CHECK(maxAbsDiff(lh, lf) < 1e-4);
    // Free-running at the default 120 BPM drifts away from the 90 BPM render.
    Filter other = make();
    other.setPosition(1.25);
    auto lo = lcgNoise(n, 51u), ro = lcgNoise(n, 52u);
    renderLfo(other, lo, ro, false, 0.0, 120.0);
    CHECK(maxAbsDiff(lh, lo) > 0.1);
}

TEST_CASE("Filter: extreme depth and cutoff stay finite at 44.1, 96 and 192 kHz", "[FxUnits]")
{
    for (double sr : {44100.0, 96000.0, 192000.0})
        for (double cutoff : {20.0, 0.45 * sr})
            for (int shape = 0; shape < 4; ++shape)
                for (int type : {0, 1, 2, 3, 4, 5})
                {
                    INFO("sr " << sr << " cutoff " << cutoff << " shape " << shape << " type " << type);
                    Filter f;
                    f.prepare(sr);
                    f.setTempo(300.0, 4.0);
                    f.setParams(filterLfoParams(type, cutoff, 1.0, 0, 1.0, shape)); // 1/32
                    const int n = static_cast<int>(sr / 2);
                    auto l = lcgNoise(n, 61u), r = lcgNoise(n, 62u);
                    renderLfo(f, l, r, false, 0.0, 300.0, sr);
                    for (size_t i = 0; i < l.size(); ++i)
                    {
                        REQUIRE(std::isfinite(l[i]));
                        REQUIRE(std::isfinite(r[i]));
                    }
                    CHECK(std::max(peakAbs(l, 0, l.size()), peakAbs(r, 0, r.size())) < 4.0);
                }
}

TEST_CASE("Filter: invalid tempo or position keeps the output finite", "[FxUnits]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const int n = 30000;
    const auto p = filterLfoParams(4, 1000.0, 0.8, 5, 1.0, 1);
    // Reference: valid tempo once, free-running.
    Filter ref;
    ref.prepare(kSr);
    ref.setTempo(100.0, 3.0);
    ref.setParams(p);
    auto l0 = lcgNoise(n, 71u), r0 = lcgNoise(n, 72u);
    processBlocks(ref, l0, r0, 333);
    // Same, with invalid tempo and position calls before every block: all ignored.
    Filter f;
    f.prepare(kSr);
    f.setTempo(100.0, 3.0);
    f.setParams(p);
    auto l = lcgNoise(n, 71u), r = lcgNoise(n, 72u);
    for (int pos = 0; pos < n; pos += 333)
    {
        f.setTempo(0.0, 0.0);
        f.setTempo(-120.0, -4.0);
        f.setTempo(nan, nan);
        f.setTempo(inf, inf);
        f.setTempo(-inf, -inf);
        f.setPosition(nan);
        f.setPosition(inf);
        f.setPosition(-inf);
        f.process(l.data() + pos, r.data() + pos, std::min(333, n - pos));
    }
    for (size_t i = 0; i < l.size(); ++i)
    {
        REQUIRE(std::isfinite(l[i]));
        REQUIRE(std::isfinite(r[i]));
    }
    CHECK(l == l0);
    CHECK(r == r0);
    // Never a valid tempo: the initial 120 BPM / 4 beats per bar, as without any call.
    Filter a, b;
    a.prepare(kSr);
    b.prepare(kSr);
    a.setParams(p);
    b.setParams(p);
    b.setTempo(nan, 0.0);
    b.setPosition(nan);
    auto la = lcgNoise(n, 73u), ra = la, lb = la, rb = la;
    processBlocks(a, la, ra, 512);
    processBlocks(b, lb, rb, 512);
    CHECK(la == lb);
    // reset() after a NaN position keeps working.
    b.reset();
    auto lr = lcgNoise(4000, 74u), rr = lr;
    processBlocks(b, lr, rr, 100);
    for (float x : lr) REQUIRE(std::isfinite(x));
}
