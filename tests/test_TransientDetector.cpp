#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include "engine/RegionMap.h"
#include "engine/TransientDetector.h"

using dc::StereoData;
using dc::suggestBoundaries;

namespace {
constexpr double kSr = 44100.0;

StereoData silence(size_t frames)
{
    StereoData d;
    d.sampleRate = kSr;
    d.left.assign(frames, 0.0f);
    d.right.assign(frames, 0.0f);
    return d;
}

// Deterministic white noise in -1..1.
struct Noise
{
    uint32_t s = 12345;
    float next()
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 8388608.0f - 1.0f;
    }
};

// Short click: about 3 ms of noise burst starting at `start`.
void addClick(StereoData& d, size_t start, bool left = true, bool right = true)
{
    Noise n;
    for (size_t i = 0; i < 130 && start + i < d.frames(); ++i)
    {
        const float v = 0.8f * n.next();
        if (left)
            d.left[start + i] += v;
        if (right)
            d.right[start + i] += v;
    }
}

// Noise burst with exponential decay (tau seconds).
void addHit(StereoData& d, size_t start, double tau, float amp, Noise& n)
{
    for (size_t i = 0; start + i < d.frames(); ++i)
    {
        const double env = std::exp(-static_cast<double>(i) / (tau * kSr));
        if (env < 1e-4)
            break;
        const float v = amp * static_cast<float>(env) * n.next();
        d.left[start + i] += v;
        d.right[start + i] += v;
    }
}

StereoData clickLoop(size_t frames, std::initializer_list<double> fractions)
{
    auto d = silence(frames);
    for (double f : fractions)
        addClick(d, static_cast<size_t>(f * static_cast<double>(frames)));
    return d;
}

void checkValid(const std::vector<double>& b)
{
    for (size_t i = 0; i < b.size(); ++i)
    {
        REQUIRE(b[i] > 0.0);
        REQUIRE(b[i] < 1.0);
        if (i > 0)
            REQUIRE(b[i] - b[i - 1] >= dc::kMinRegion);
    }
    REQUIRE(std::is_sorted(b.begin(), b.end()));
}
} // namespace

TEST_CASE("synthetic click track with 4 clicks yields 3 boundaries near the clicks", "[TransientDetector]")
{
    const size_t n = 88200; // 2 s
    const auto d = clickLoop(n, { 0.0, 0.25, 0.5, 0.75 });
    const auto b = suggestBoundaries(d, 15);
    REQUIRE(b.size() == 3);
    checkValid(b);
    const double expect[] = { 0.25, 0.5, 0.75 };
    for (int i = 0; i < 3; ++i)
    {
        CHECK(std::fabs(b[i] - expect[i]) * static_cast<double>(n) / kSr < 0.003); // within 3 ms
        CHECK(b[i] <= expect[i] + 1e-9);                                          // never after the click start
    }
}

TEST_CASE("silence yields no boundaries", "[TransientDetector]")
{
    CHECK(suggestBoundaries(silence(88200), 15).empty());
}

TEST_CASE("result respects maxBoundaries and kMinRegion", "[TransientDetector]")
{
    const size_t n = 176400; // 4 s
    auto d = silence(n);
    for (int i = 1; i <= 8; ++i) // 8 clicks of increasing loudness
    {
        Noise nz;
        nz.s = static_cast<uint32_t>(i);
        const size_t start = static_cast<size_t>(i) * n / 10;
        for (size_t k = 0; k < 130; ++k)
        {
            const float v = (0.1f * static_cast<float>(i)) * nz.next();
            d.left[start + k] = v;
            d.right[start + k] = v;
        }
    }
    const auto all = suggestBoundaries(d, 100);
    CHECK(all.size() == 8);
    checkValid(all);

    const auto top3 = suggestBoundaries(d, 3);
    REQUIRE(top3.size() == 3);
    checkValid(top3);
    for (int j = 0; j < 3; ++j) // the three loudest are clicks 6, 7, 8
        CHECK(std::fabs(top3[j] - (6 + j) / 10.0) < 0.002);

    CHECK(suggestBoundaries(d, 0).empty());
    CHECK(suggestBoundaries(d, -4).empty());

    // clicks closer than the minimum distance: only one survives, spacing still valid
    auto close = silence(n);
    addClick(close, 50000);
    addClick(close, 50000 + 600); // 13.6 ms later
    const auto cb = suggestBoundaries(close, 15);
    CHECK(cb.size() == 1);
    checkValid(cb);
}

TEST_CASE("stereo with right channel only is detected", "[TransientDetector]")
{
    const size_t n = 88200;
    auto d = silence(n);
    for (double f : { 0.3, 0.6 })
        addClick(d, static_cast<size_t>(f * static_cast<double>(n)), false, true);
    const auto b = suggestBoundaries(d, 15);
    REQUIRE(b.size() == 2);
    CHECK(std::fabs(b[0] - 0.3) < 0.002);
    CHECK(std::fabs(b[1] - 0.6) < 0.002);

    auto l = silence(n);
    addClick(l, 40000, true, false);
    CHECK(suggestBoundaries(l, 15).size() == 1);
}

TEST_CASE("NaN and inf samples do not crash", "[TransientDetector]")
{
    auto d = clickLoop(88200, { 0.25, 0.5 });
    d.left[100] = std::numeric_limits<float>::quiet_NaN();
    d.right[2000] = std::numeric_limits<float>::infinity();
    d.left[60000] = -std::numeric_limits<float>::infinity();
    const auto b = suggestBoundaries(d, 15);
    checkValid(b);
    CHECK(b.size() == 2);

    auto all = silence(88200);
    std::fill(all.left.begin(), all.left.end(), std::numeric_limits<float>::quiet_NaN());
    CHECK(suggestBoundaries(all, 15).empty());

    StereoData bad = clickLoop(88200, { 0.5 });
    bad.sampleRate = std::numeric_limits<double>::quiet_NaN();
    CHECK(suggestBoundaries(bad, 15).empty());
    bad.sampleRate = 0.0;
    CHECK(suggestBoundaries(bad, 15).empty());
}

TEST_CASE("very short input gives no boundaries", "[TransientDetector]")
{
    StereoData e;
    CHECK(suggestBoundaries(e, 15).empty());
    for (size_t n : { size_t(1), size_t(10), size_t(500), size_t(1000) })
    {
        auto d = silence(n);
        d.left[n / 2] = 1.0f;
        CHECK(suggestBoundaries(d, 15).empty());
    }
    StereoData uneven = clickLoop(88200, { 0.5 });
    uneven.right.resize(10);
    CHECK(suggestBoundaries(uneven, 15).size() <= 1); // must not read out of range
}

TEST_CASE("decaying drum-like hits at 4/4 positions give boundaries near the hits", "[TransientDetector]")
{
    const size_t n = 4 * 22050; // 2 s = 4 beats at 120 bpm
    auto d = silence(n);
    Noise nz;
    for (int beat = 0; beat < 4; ++beat)
        addHit(d, static_cast<size_t>(beat) * 22050, 0.06, beat % 2 == 0 ? 0.9f : 0.6f, nz);
    const auto b = suggestBoundaries(d, 15);
    REQUIRE(b.size() == 3);
    checkValid(b);
    for (int i = 0; i < 3; ++i)
        CHECK(std::fabs(b[i] - (i + 1) * 0.25) * static_cast<double>(n) / kSr < 0.004);
}

TEST_CASE("suggestBoundaries is deterministic", "[TransientDetector]")
{
    auto d = silence(88200);
    Noise nz;
    for (int beat = 0; beat < 8; ++beat)
        addHit(d, static_cast<size_t>(beat) * 11025, 0.03, 0.8f, nz);
    const auto a = suggestBoundaries(d, 15);
    const auto b = suggestBoundaries(d, 15);
    CHECK(a == b);
    CHECK_FALSE(a.empty());
}

TEST_CASE("a loud hit at sample 0 does not hide a hat 120 ms later", "[TransientDetector][hardening]")
{
    const size_t n = 44100; // 1 s
    auto d = silence(n);
    Noise nz;
    addHit(d, 0, 0.05, 0.9f, nz);
    const size_t hat = static_cast<size_t>(0.120 * kSr);
    Noise hz;
    hz.s = 99;
    for (size_t i = 0; i < 130; ++i)
    {
        const float v = 0.2f * hz.next();
        d.left[hat + i] += v;
        d.right[hat + i] += v;
    }
    const auto b = suggestBoundaries(d, 15);
    checkValid(b);
    REQUIRE(b.size() == 1);
    // The attack search may start up to one window (6 ms) early on the loud tail of the kick.
    CHECK(std::fabs(b[0] * static_cast<double>(n) - static_cast<double>(hat)) / kSr < 0.008);
}

TEST_CASE("tiny inputs at a low sample rate never suggest a boundary at 0", "[TransientDetector][hardening]")
{
    StereoData d;
    d.sampleRate = 1000.0; // hop 2 samples, window 6
    d.left.assign(40, 0.0f);
    d.left[0] = 0.2f;
    for (size_t i = 6; i < 12; ++i)
        d.left[i] = 1.0f;
    d.right = d.left;
    const auto b = suggestBoundaries(d, 15);
    for (double x : b)
    {
        CHECK(x > 0.0);
        CHECK(x < 1.0);
    }
}
