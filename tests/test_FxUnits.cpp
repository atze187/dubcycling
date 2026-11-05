#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
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
