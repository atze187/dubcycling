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

TEST_CASE("FxChain: all four inserts on with extreme parameters stay finite and under 50 for 10 s of noise at 44.1, 96 and 192 kHz, blocks of 1 and 4096", "[FxChain]")
{
    // The units' gains multiply along the chain: each unit alone stays under 20 for full-scale
    // noise, but reverb (size 1, mix 1, wet x3) after the flanger (feedback +-0.9: up to 10x at
    // its comb peaks) after a resonant filter peaks at about 32 here. 50 bounds that with margin;
    // the point of the test is stability (finite, bounded), not a level limit.
    constexpr float kChainBound = 50.0f;
    struct Extreme { int filterType; double cutoff; double fb; };
    const Extreme sets[] = {{0, 20.0, 0.9}, {0, 20000.0, -0.9}, {2, 1000.0, 0.9}, {1, 20000.0, -0.9}};
    for (double sr : {44100.0, 96000.0, 192000.0})
        for (int block : {1, 4096})
            for (const auto& e : sets)
            {
                FxChain c;
                c.prepare(sr, 4096);
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
                c.setParams(p);
                c.setMask(0x0F);
                const int n = static_cast<int>(10 * sr);
                auto l = noise(n, 10), r = noise(n, 11);
                run(c, l, r, 0, l.size(), block);
                for (size_t i = 0; i < l.size(); ++i)
                {
                    REQUIRE(std::isfinite(l[i]));
                    REQUIRE(std::isfinite(r[i]));
                    REQUIRE(std::abs(l[i]) < kChainBound);
                    REQUIRE(std::abs(r[i]) < kChainBound);
                }
                CHECK(c.anyActive());
            }
}
