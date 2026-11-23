#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "engine/fx/Delay.h"

using namespace dc;
using namespace dc::fx;

namespace {

constexpr double kSr = 48000.0;
constexpr int kStepQuarter = 8;  // "1/4"
constexpr int kStepEighth = 5;   // "1/8"
constexpr int kStepTwoBars = 14; // "2 bars"

FxParams delayParams(int step, double fb, double mix, bool reverse)
{
    FxParams p;
    p.set(FxParam::DelayTime, step);
    p.set(FxParam::DelayFeedback, fb);
    p.set(FxParam::DelayMix, mix);
    p.set(FxParam::DelayReverse, reverse ? 1.0 : 0.0);
    return p;
}

void run(Delay& d, std::vector<float>& l, std::vector<float>& r, int block = 512)
{
    const int n = static_cast<int>(l.size());
    for (int pos = 0; pos < n; pos += block) d.process(l.data() + pos, r.data() + pos, std::min(block, n - pos));
}

std::vector<float> impulseAt(int n, int at, float v = 1.0f)
{
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    x[static_cast<size_t>(at)] = v;
    return x;
}

std::vector<float> noise(int n, unsigned seed, float amp = 1.0f)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-amp, amp);
    std::vector<float> v(static_cast<size_t>(n));
    for (auto& x : v) x = dist(rng);
    return v;
}

std::vector<float> sine(int n, double freq, float amp = 0.8f)
{
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = amp * static_cast<float>(std::sin(6.283185307179586 * freq * i / kSr));
    return v;
}

int argmaxAbs(const std::vector<float>& v, size_t from, size_t to)
{
    size_t best = from;
    for (size_t i = from; i < to && i < v.size(); ++i)
        if (std::abs(v[i]) > std::abs(v[best])) best = i;
    return static_cast<int>(best);
}

double peakAbs(const std::vector<float>& v, size_t from, size_t to)
{
    double m = 0;
    for (size_t i = from; i < to && i < v.size(); ++i) m = std::max(m, double(std::abs(v[i])));
    return m;
}

double maxJump(const std::vector<float>& v, size_t from, size_t to)
{
    double m = 0;
    for (size_t i = std::max<size_t>(from, 1); i < to && i < v.size(); ++i) m = std::max(m, double(std::abs(v[i] - v[i - 1])));
    return m;
}

bool allFinite(const std::vector<float>& v)
{
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

// Output of a fresh delay for a mono impulse at sample 0 (left channel).
std::vector<float> impulseResponse(const FxParams& p, int n, double bpm = 120.0, double bpb = 4.0, bool setTempo = true)
{
    Delay d;
    d.prepare(kSr);
    if (setTempo) d.setTempo(bpm, bpb);
    d.setParams(p);
    auto l = impulseAt(n, 0), r = l;
    run(d, l, r);
    return l;
}

} // namespace

TEST_CASE("Delay: echoes an impulse one beat later at 120 BPM with time 1/4", "[Delay]")
{
    const auto out = impulseResponse(delayParams(kStepQuarter, 0.0, 1.0, false), 48000);
    const int at = argmaxAbs(out, 0, out.size());
    CHECK(std::abs(at - 24000) <= 1);
    CHECK(out[static_cast<size_t>(at)] > 0.99f);
    CHECK(peakAbs(out, 0, 23999) == 0.0);
}

TEST_CASE("Delay: feedback repeats decay by the feedback factor", "[Delay]")
{
    const auto out = impulseResponse(delayParams(kStepQuarter, 0.5, 1.0, false), 4 * 24000 + 100);
    const double e1 = peakAbs(out, 23990, 24010), e2 = peakAbs(out, 47990, 48010), e3 = peakAbs(out, 71990, 72010);
    CHECK(std::abs(e1 - 1.0) < 1e-4);
    CHECK(std::abs(e2 / e1 - 0.5) < 1e-3);
    CHECK(std::abs(e3 / e2 - 0.5) < 1e-3);
    // Nothing between the repeats.
    CHECK(peakAbs(out, 24010, 47990) < 1e-6);
}

TEST_CASE("Delay: mix 0 is silent", "[Delay]")
{
    for (bool reverse : {false, true})
    {
        Delay d;
        d.prepare(kSr);
        d.setParams(delayParams(kStepEighth, 0.9, 0.0, reverse));
        auto l = noise(96000, 1), r = noise(96000, 2);
        run(d, l, r);
        CHECK(peakAbs(l, 0, l.size()) == 0.0);
        CHECK(peakAbs(r, 0, r.size()) == 0.0);
    }
}

TEST_CASE("Delay: with reverse plays the echo block backwards", "[Delay]")
{
    const int D = 24000; // 1/4 at 120 BPM
    const float mix = 0.7f;
    Delay d;
    d.prepare(kSr);
    d.setParams(delayParams(kStepQuarter, 0.0, mix, true));
    std::vector<float> l(static_cast<size_t>(3 * D), 0.0f);
    for (int i = 0; i < D; ++i) l[static_cast<size_t>(i)] = static_cast<float>(i + 1) / D; // ramp over one block
    const auto in = l;
    auto r = l;
    run(d, l, r);

    // The dry ramp is not in the output: silence during the input block.
    CHECK(peakAbs(l, 0, D) == 0.0);
    // Over the following block the output is the ramp reversed (after the block crossfade, <= 20 ms).
    const int xf = static_cast<int>(0.020 * kSr);
    double err = 0;
    for (int j = xf; j < D; ++j)
        err = std::max(err, double(std::abs(l[static_cast<size_t>(D + j)] - mix * in[static_cast<size_t>(D - 1 - j)])));
    CHECK(err < 1e-5);
    // No feedback: the block after is silent again (past its fade-out).
    CHECK(peakAbs(l, 2 * D + xf, 3 * D) == 0.0);
    CHECK(l == r);
}

TEST_CASE("Delay: with reverse and feedback every repeat plays reversed", "[Delay]")
{
    const int D = 24000; // 1/4 at 120 BPM
    const float mix = 0.6f;
    Delay d;
    d.prepare(kSr);
    d.setParams(delayParams(kStepQuarter, 0.5, mix, true));
    std::vector<float> l(static_cast<size_t>(4 * D), 0.0f);
    for (int i = 0; i < D; ++i) l[static_cast<size_t>(i)] = static_cast<float>(i + 1) / D; // ramp over one block
    const auto in = l;
    auto r = l;
    run(d, l, r);

    // Repeat k (k = 1, 2, 3) sounds in [k D, (k + 1) D), reversed, at mix * 0.5^(k - 1).
    const int xf = static_cast<int>(0.020 * kSr);
    for (int k = 1; k <= 3; ++k)
    {
        const float scale = mix * std::pow(0.5f, static_cast<float>(k - 1));
        double err = 0;
        for (int j = xf; j < D; ++j)
            err = std::max(err, double(std::abs(l[static_cast<size_t>(k * D + j)] - scale * in[static_cast<size_t>(D - 1 - j)])));
        INFO("repeat " << k);
        CHECK(err < 1e-5);
    }
}

TEST_CASE("Delay: reverse echo arrives one delay time after the input block", "[Delay]")
{
    const int D = 24000;
    const int p = 12000; // impulse in the middle of the first block
    Delay d;
    d.prepare(kSr);
    d.setParams(delayParams(kStepQuarter, 0.0, 1.0, true));
    auto l = impulseAt(3 * D, p), r = l;
    run(d, l, r);
    CHECK(peakAbs(l, 0, D) == 0.0);
    // Reversed within the block: offset p from the block start lands D - 1 - p into the next block.
    const int at = argmaxAbs(l, 0, l.size());
    CHECK(at == D + (D - 1 - p));
    CHECK(l[static_cast<size_t>(at)] > 0.99f);

    // A constant block starts sounding right at the next block (within the fade-in).
    Delay e;
    e.prepare(kSr);
    e.setParams(delayParams(kStepQuarter, 0.0, 1.0, true));
    std::vector<float> c(static_cast<size_t>(2 * D), 0.0f);
    std::fill(c.begin(), c.begin() + D, 0.5f);
    auto c2 = c;
    run(e, c, c2);
    CHECK(peakAbs(c, 0, D) == 0.0);
    CHECK(peakAbs(c, D, D + static_cast<size_t>(0.020 * kSr)) > 0.4);
}

TEST_CASE("Delay: a tempo change does not click", "[Delay]")
{
    for (bool reverse : {false, true})
    {
        Delay d;
        d.prepare(kSr);
        d.setTempo(120.0, 4.0);
        d.setParams(delayParams(kStepEighth, 0.0, 1.0, reverse));
        const int n = 3 * 48000;
        auto l = sine(n, 440.0), r = l;
        const int change = 188 * 256; // about 1 s, on a block boundary
        int changes = 0;
        for (int pos = 0; pos < n; pos += 256)
        {
            if (pos == change) { d.setTempo(97.0, 4.0); ++changes; }
            if (pos == change + 50 * 256) { d.setTempo(140.0, 3.0); ++changes; }
            d.process(l.data() + pos, r.data() + pos, std::min(256, n - pos));
        }
        // Natural max slope of the sine is 0.8 * 2 pi 440 / 48000 = 0.046.
        INFO("reverse " << reverse);
        REQUIRE(changes == 2);
        CHECK(maxJump(l, 36000, l.size()) < 0.1);
        CHECK(allFinite(l));
    }
}

TEST_CASE("Delay: at 20 BPM with 2 bars clamps to the buffer and stays finite", "[Delay]")
{
    // 20 BPM, 2 bars of 4/4 = 24 s: fits the 30 s buffer.
    {
        const auto out = impulseResponse(delayParams(kStepTwoBars, 0.0, 1.0, false), 25 * 48000, 20.0, 4.0);
        CHECK(std::abs(argmaxAbs(out, 0, out.size()) - 24 * 48000) <= 1);
    }
    // 20 BPM, 2 bars of 12/4 = 72 s: clamped to the 30 s buffer.
    {
        const auto out = impulseResponse(delayParams(kStepTwoBars, 0.0, 1.0, false), 31 * 48000, 20.0, 12.0);
        CHECK(std::abs(argmaxAbs(out, 0, out.size()) - 30 * 48000) <= 1);
        CHECK(allFinite(out));
    }
    // Reverse and feedback at the clamp: finite and bounded.
    {
        Delay d;
        d.prepare(kSr);
        d.setTempo(20.0, 12.0);
        d.setParams(delayParams(kStepTwoBars, 0.9, 1.0, true));
        auto l = noise(70 * 48000, 5, 0.5f), r = noise(70 * 48000, 6, 0.5f);
        run(d, l, r, 4096);
        CHECK(allFinite(l));
        CHECK(allFinite(r));
        CHECK(peakAbs(l, 0, l.size()) < 10.0);
    }
}

TEST_CASE("Delay: NaN, 0 or negative bpm keeps the last tempo", "[Delay]")
{
    // Initial tempo is 120 BPM.
    {
        const auto out = impulseResponse(delayParams(kStepQuarter, 0.0, 1.0, false), 30000, 0, 0, false);
        CHECK(std::abs(argmaxAbs(out, 0, out.size()) - 24000) <= 1);
    }
    Delay d;
    d.prepare(kSr);
    d.setTempo(60.0, 4.0);
    d.setTempo(std::numeric_limits<double>::quiet_NaN(), 4.0);
    d.setTempo(0.0, 4.0);
    d.setTempo(-30.0, 4.0);
    d.setTempo(std::numeric_limits<double>::infinity(), 4.0);
    d.setParams(delayParams(kStepQuarter, 0.0, 1.0, false));
    auto l = impulseAt(60000, 0), r = l;
    run(d, l, r);
    CHECK(std::abs(argmaxAbs(l, 0, l.size()) - 48000) <= 1);

    // Same for the beats per bar of the bar values.
    Delay e;
    e.prepare(kSr);
    e.setTempo(120.0, 3.0);
    e.setTempo(120.0, std::numeric_limits<double>::quiet_NaN());
    e.setTempo(120.0, 0.0);
    e.setParams(delayParams(13, 0.0, 1.0, false)); // 1 bar of 3/4 = 1.5 s
    auto l2 = impulseAt(80000, 0), r2 = l2;
    run(e, l2, r2);
    CHECK(std::abs(argmaxAbs(l2, 0, l2.size()) - 72000) <= 1);
}

TEST_CASE("Delay: at 192 kHz allocates only in prepare", "[Delay]")
{
    constexpr double sr = 192000.0;
    Delay d;
    d.prepare(sr);
    const float* b0 = d.bufferData(0);
    const float* b1 = d.bufferData(1);
    REQUIRE(b0 != nullptr);
    REQUIRE(b1 != nullptr);
    d.setParams(delayParams(kStepTwoBars, 0.6, 1.0, false));
    std::vector<float> l(4096), r(4096);
    std::mt19937 rng(9);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    const int blocks = static_cast<int>(10 * sr / 4096);
    for (int b = 0; b < blocks; ++b)
    {
        for (auto& x : l) x = dist(rng);
        for (auto& x : r) x = dist(rng);
        if (b % 50 == 0) d.setTempo(20.0 + b % 200, 4.0);
        if (b % 70 == 0) d.setParams(delayParams(b % kDelayTimeSteps, 0.6, 1.0, (b / 70) % 2 == 1));
        d.process(l.data(), r.data(), 4096);
    }
    d.reset();
    CHECK(d.bufferData(0) == b0);
    CHECK(d.bufferData(1) == b1);
}

TEST_CASE("Delay: the tail dies out and tailLevel falls below the sleep threshold after the input stops", "[Delay]")
{
    constexpr float kSleepThreshold = 1e-5f; // as in FxChain
    for (bool reverse : {false, true})
    {
        INFO("reverse " << reverse);
        Delay d;
        d.prepare(kSr);
        CHECK(d.tailLevel() == 0.0f);
        d.setParams(delayParams(kStepQuarter, 0.4, 1.0, reverse));
        // An impulse: the output is still silent before the echo, but the delay must stay awake.
        auto l = impulseAt(19200, 0), r = l;
        run(d, l, r);
        CHECK(peakAbs(l, 0, l.size()) == 0.0);
        CHECK(d.tailLevel() > kSleepThreshold);

        auto n1 = noise(24000, 3), n2 = noise(24000, 4);
        run(d, n1, n2);
        CHECK(d.tailLevel() > kSleepThreshold);
        std::vector<float> sl(20 * 48000, 0.0f), sr(20 * 48000, 0.0f);
        run(d, sl, sr);
        CHECK(d.tailLevel() <= kSleepThreshold);
        CHECK(peakAbs(sl, sl.size() - 48000, sl.size()) < 1e-5);
    }
}

TEST_CASE("Delay: reset clears the buffers", "[Delay]")
{
    for (bool reverse : {false, true})
    {
        Delay d;
        d.prepare(kSr);
        d.setParams(delayParams(kStepQuarter, 0.8, 1.0, reverse));
        auto l = noise(30000, 7), r = noise(30000, 8);
        run(d, l, r);
        d.reset();
        CHECK(d.tailLevel() == 0.0f);
        d.setParams(delayParams(kStepQuarter, 0.8, 1.0, reverse));
        std::vector<float> sl(4 * 48000, 0.0f), sr(4 * 48000, 0.0f);
        run(d, sl, sr);
        CHECK(peakAbs(sl, 0, sl.size()) == 0.0);
        CHECK(peakAbs(sr, 0, sr.size()) == 0.0);
    }
}

TEST_CASE("Delay: toggling Reverse while sounding stays finite and bounded", "[Delay]")
{
    // Noise with peak 1 at feedback 0.4: |out| <= 2.
    {
        Delay d;
        d.prepare(kSr);
        d.setParams(delayParams(kStepEighth, 0.4, 1.0, false));
        const int n = 6 * 48000;
        auto l = noise(n, 11), r = noise(n, 12);
        for (int pos = 0, k = 0; pos < n; pos += 512, ++k)
        {
            if (k % 28 == 0) d.setParams(delayParams(kStepEighth, 0.4, 1.0, (k / 28) % 2 == 1));
            d.process(l.data() + pos, r.data() + pos, std::min(512, n - pos));
        }
        CHECK(allFinite(l));
        CHECK(allFinite(r));
        CHECK(peakAbs(l, 0, l.size()) <= 2.0);
        CHECK(peakAbs(r, 0, r.size()) <= 2.0);
    }
    // A sine: no click at the toggles (bounded sample-to-sample jump).
    {
        Delay d;
        d.prepare(kSr);
        d.setParams(delayParams(kStepEighth, 0.4, 1.0, false));
        const int n = 6 * 48000;
        auto l = sine(n, 220.0), r = l;
        for (int pos = 0, k = 0; pos < n; pos += 512, ++k)
        {
            if (k % 31 == 0) d.setParams(delayParams(kStepEighth, 0.4, 1.0, (k / 31) % 2 == 1));
            d.process(l.data() + pos, r.data() + pos, std::min(512, n - pos));
        }
        // Wet sine amplitude <= 0.8 / (1 - 0.4) = 1.33, natural slope <= 1.33 * 2 pi 220 / 48000 = 0.04.
        CHECK(maxJump(l, 0, l.size()) < 0.1);
    }
}

TEST_CASE("Delay: block sizes 1, 16 and 3000 give the same result", "[Delay]")
{
    auto runWith = [](int block) {
        Delay d;
        d.prepare(kSr);
        d.setParams(delayParams(kStepEighth, 0.5, 0.8, false));
        const int n = 3 * 48000;
        auto l = noise(n, 21), r = noise(n, 22);
        for (int pos = 0; pos < n; pos += block)
        {
            const int m = std::min(block, n - pos);
            if (pos <= 48000 && pos + m > 48000) d.setParams(delayParams(kStepEighth, 0.5, 0.8, true));
            d.process(l.data() + pos, r.data() + pos, m);
        }
        return l;
    };
    // The Reverse switch is applied before the block that starts at sample 48000 (all three sizes divide it).
    const auto a = runWith(1), b = runWith(3000), c = runWith(16);
    CHECK(a == b);
    CHECK(a == c);
}

TEST_CASE("Delay: NaN and infinity in the input give finite output and recover", "[Delay]")
{
    Delay d;
    d.prepare(kSr);
    d.setParams(delayParams(kStepEighth, 0.9, 1.0, false));
    auto l = noise(48000, 31), r = noise(48000, 32);
    l[100] = std::numeric_limits<float>::quiet_NaN();
    r[200] = std::numeric_limits<float>::infinity();
    run(d, l, r);
    CHECK(allFinite(l));
    CHECK(allFinite(r));
}
