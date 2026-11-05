#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <numbers>
#include <thread>
#include <vector>
#include "engine/Engine.h"

// --- Allocation counter -------------------------------------------------------------------------
// Replaces the global operator new/delete for this test executable. It only counts while
// gCountAllocs is true (around Engine::process), so Catch2 and the other tests are unaffected.
namespace {
std::atomic<bool> gCountAllocs { false };
std::atomic<long> gAllocCount { 0 };

void* countedAlloc(std::size_t n)
{
    if (gCountAllocs.load(std::memory_order_relaxed))
        gAllocCount.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n != 0 ? n : 1))
        return p;
    throw std::bad_alloc();
}

void countedFree(void* p) noexcept
{
    if (p != nullptr && gCountAllocs.load(std::memory_order_relaxed))
        gAllocCount.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
} // namespace

void* operator new(std::size_t n) { return countedAlloc(n); }
void* operator new[](std::size_t n) { return countedAlloc(n); }
void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { countedFree(p); }

// --- Helpers ------------------------------------------------------------------------------------
namespace {

constexpr double kRate = 48000.0;
constexpr double kBpm = 120.0;
constexpr int kBar = 96000;  // one 4/4 bar at 120 bpm and 48 kHz
constexpr int kFade = 240;   // kCancelFadeMs at 48 kHz
constexpr float kTol = 1e-3f; // grain crossfades between identical source positions are exact up to rounding

struct Out
{
    std::vector<float> l, r;
};

// left = gen(i), right = -gen(i)
std::shared_ptr<const dc::StereoData> makeData(int frames, const std::function<float(int)>& gen)
{
    auto d = std::make_shared<dc::StereoData>();
    d->sampleRate = kRate;
    d->left.resize(static_cast<size_t>(frames));
    d->right.resize(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
    {
        d->left[static_cast<size_t>(i)] = gen(i);
        d->right[static_cast<size_t>(i)] = -gen(i);
    }
    return d;
}

// Constant value per equal-width quarter (or fewer) of the loop: values[k] for region k.
std::shared_ptr<const dc::StereoData> regionData(int frames, std::vector<float> values)
{
    return makeData(frames, [frames, values](int i) {
        const size_t k = static_cast<size_t>(i) * values.size() / static_cast<size_t>(frames);
        return values[k];
    });
}

float sineAt(int i, double hz, float amp)
{
    return amp * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / kRate));
}

dc::RegionMap regions(std::vector<double> b)
{
    dc::RegionMap m;
    for (double x : b)
        m.addBoundary(x);
    return m;
}

// Renders `frames` frames in blocks; ppq advances with bpm.
Out run(dc::Engine& e, double startPpq, int frames, int block = 512, double bpm = kBpm, int num = 4, int den = 4)
{
    Out o;
    o.l.assign(static_cast<size_t>(frames), 7.0f);
    o.r.assign(static_cast<size_t>(frames), 7.0f);
    double ppq = startPpq;
    for (int pos = 0; pos < frames; pos += block)
    {
        const int n = std::min(block, frames - pos);
        dc::TransportInfo t { true, ppq, bpm, num, den };
        e.process(t, o.l.data() + pos, o.r.data() + pos, n);
        ppq += n * bpm / (60.0 * kRate);
    }
    return o;
}

bool allZero(const std::vector<float>& v, size_t from = 0, size_t to = std::numeric_limits<size_t>::max())
{
    to = std::min(to, v.size());
    return std::all_of(v.begin() + static_cast<std::ptrdiff_t>(from), v.begin() + static_cast<std::ptrdiff_t>(to),
                       [](float x) { return x == 0.0f; });
}

// Max |v[i] - value| over [from, to).
float maxDev(const std::vector<float>& v, size_t from, size_t to, float value)
{
    float m = 0.0f;
    for (size_t i = from; i < to; ++i)
        m = std::max(m, std::abs(v[i] - value));
    return m;
}

dc::Engine& prepared(dc::Engine& e)
{
    e.prepare(kRate, 512);
    e.setRandom([] { return 0.5f; });
    return e;
}

// Model with loop 1 = `bars1` bars silent (no data) unless given, loop 2 = data2.
std::shared_ptr<dc::Model> baseModel()
{
    auto m = std::make_shared<dc::Model>();
    m->loop1.bars = 1;
    m->loop2.bars = 1;
    return m;
}

std::function<float()> counting(int& count)
{
    return [&count] { ++count; return 0.5f; };
}

// Model swaps happen at a block start. 31232 = 61 blocks of 512 (phase 0.3253 of a 4/4 bar), not
// aligned with the 40 ms grain grid, so a restarted Loop-1 reader would differ from a continued one.
constexpr int kSwapAt = 61 * 512;

// Like run() from ppq 0 in 512-frame blocks; publishes `next` right before the block at `swapAt`.
Out runSwap(dc::Engine& e, int frames, int swapAt, std::shared_ptr<const dc::Model> next)
{
    Out o;
    o.l.assign(static_cast<size_t>(frames), 7.0f);
    o.r.assign(static_cast<size_t>(frames), 7.0f);
    double ppq = 0.0;
    for (int pos = 0; pos < frames; pos += 512)
    {
        if (pos == swapAt)
            e.setModel(next);
        const int n = std::min(512, frames - pos);
        dc::TransportInfo t { true, ppq, kBpm, 4, 4 };
        e.process(t, o.l.data() + pos, o.r.data() + pos, n);
        ppq += n * kBpm / (60.0 * kRate);
    }
    return o;
}

bool sameRange(const Out& a, const Out& b, size_t from, size_t to)
{
    for (size_t i = from; i < to; ++i)
        if (a.l[i] != b.l[i] || a.r[i] != b.r[i])
            return false;
    return true;
}

} // namespace

TEST_CASE("Engine silent when not playing", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.data = makeData(kBar, [](int i) { return sineAt(i, 220.0, 0.5f); });
    e.setModel(m);
    std::vector<float> l(512, 1.0f), r(512, 1.0f);
    dc::TransportInfo t { false, 0.0, kBpm, 4, 4 };
    e.process(t, l.data(), r.data(), 512);
    REQUIRE(allZero(l));
    REQUIRE(allZero(r));
}

TEST_CASE("Engine silent for bpm 0 and NaN and negative ppq", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.data = makeData(kBar, [](int i) { return 0.5f + 0.0f * i; });
    e.setModel(m);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const dc::TransportInfo bad[] = {
        { true, 0.0, 0.0, 4, 4 }, { true, 0.0, nan, 4, 4 },  { true, 0.0, -120.0, 4, 4 },
        { true, -0.5, kBpm, 4, 4 }, { true, nan, kBpm, 4, 4 }, { true, inf, kBpm, 4, 4 },
        { true, 0.0, inf, 4, 4 },
    };
    for (const auto& t : bad)
    {
        std::vector<float> l(512, 1.0f), r(512, 1.0f);
        e.process(t, l.data(), r.data(), 512);
        REQUIRE(allZero(l));
        REQUIRE(allZero(r));
    }
    // Valid again: plays.
    auto o = run(e, 0.0, 1024);
    REQUIRE(maxDev(o.l, 0, 1024, 0.5f) < kTol);
}

TEST_CASE("Engine loop 1 output matches source at ppq 0 with matching tempo", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.data = makeData(kBar, [](int i) { return sineAt(i, 220.0, 0.5f); });
    e.setModel(m);
    auto o = run(e, 0.0, kBar);
    float dev = 0.0f;
    for (int i = 0; i < kBar; ++i)
    {
        dev = std::max(dev, std::abs(o.l[static_cast<size_t>(i)] - m->loop1.data->left[static_cast<size_t>(i)]));
        dev = std::max(dev, std::abs(o.r[static_cast<size_t>(i)] - m->loop1.data->right[static_cast<size_t>(i)]));
    }
    REQUIRE(dev < kTol);
}

TEST_CASE("Engine output follows a tempo change without discontinuity above a threshold", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.data = makeData(kBar, [](int i) { return sineAt(i, 100.0, 0.5f); });
    e.setModel(m);
    // 0.5 s at 120 bpm, then a jump to 126 bpm, then a slow ramp to 132; ppq stays continuous.
    std::vector<float> y;
    double ppq = 0.0;
    double bpm = kBpm;
    for (int b = 0; b < 300; ++b)
    {
        if (b == 47)
            bpm = 126.0;
        if (b > 100)
            bpm = std::min(132.0, bpm + 0.05);
        std::vector<float> l(512), r(512);
        dc::TransportInfo t { true, ppq, bpm, 4, 4 };
        e.process(t, l.data(), r.data(), 512);
        y.insert(y.end(), l.begin(), l.end());
        ppq += 512 * bpm / (60.0 * kRate);
    }
    // A 100 Hz sine at 0.5 changes by at most ~0.0065 per sample; grain crossfades stay smooth.
    float maxStep = 0.0f;
    double energy = 0.0;
    for (size_t i = 1; i < y.size(); ++i)
    {
        maxStep = std::max(maxStep, std::abs(y[i] - y[i - 1]));
        energy += static_cast<double>(y[i]) * y[i];
    }
    REQUIRE(maxStep < 0.05f);
    REQUIRE(std::sqrt(energy / static_cast<double>(y.size())) > 0.1); // still audible
}

TEST_CASE("Engine loop 2 silent without rule", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop2.data = regionData(kBar, { 0.2f, 0.6f });
    m->loop2.regions = regions({ 0.5 });
    e.setModel(m);
    auto o = run(e, 0.0, kBar);
    REQUIRE(allZero(o.l));
    REQUIRE(allZero(o.r));
}

TEST_CASE("Engine loop 2 plays target region when rule weight is 1", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop2.data = regionData(kBar, { 0.2f, 0.6f });
    m->loop2.regions = regions({ 0.5 });
    m->rules.setTargets(0, { { 1, 1.0f, dc::PlayMode::Gate } });
    e.setModel(m);
    auto o = run(e, 0.0, kBar);
    REQUIRE(o.l[0] == 0.0f); // fade-in starts at zero
    REQUIRE(o.l[kFade / 2] > 0.1f);
    REQUIRE(o.l[kFade / 2] < 0.5f);
    REQUIRE(maxDev(o.l, kFade, kBar, 0.6f) < kTol);
    REQUIRE(maxDev(o.r, kFade, kBar, -0.6f) < kTol);
}

TEST_CASE("Engine playhead: nothing to show while stopped or before the first block", "[Engine][playhead]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.data = makeData(kBar, [](int i) { return sineAt(i, 220.0, 0.5f); });
    e.setModel(m);
    CHECK(e.playhead().phase1 == -1.0f);
    CHECK(e.playhead().phase2 == -1.0f);
    CHECK(e.playhead().region2 == -1);

    std::vector<float> l(512), r(512);
    e.process(dc::TransportInfo { true, 0.0, kBpm, 4, 4 }, l.data(), r.data(), 512);
    REQUIRE(e.playhead().phase1 >= 0.0f);
    e.process(dc::TransportInfo { false, 0.0, kBpm, 4, 4 }, l.data(), r.data(), 512);
    CHECK(e.playhead().phase1 == -1.0f);
}

TEST_CASE("Engine playhead follows the host position in Loop 1 and the voice in Loop 2", "[Engine][playhead]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.regions = regions({ 0.75 });
    m->loop2.data = regionData(kBar, { 0.1f, 0.2f, 0.3f, 0.4f });
    m->loop2.regions = regions({ 0.25, 0.5, 0.75 });
    m->rules.setTargets(0, { { 1, 1.0f, dc::PlayMode::Gate } }); // Loop-2 region 1 = 0.25..0.5
    e.setModel(m);
    run(e, 0.0, 24000); // a quarter of the bar
    const auto h = e.playhead();
    CHECK(h.phase1 == Catch::Approx(0.25).margin(0.002));
    REQUIRE(h.region2 == 1);
    // Gate over 24000 frames of a 24000-frame region: back at its start, a quarter into the loop.
    CHECK(h.phase2 >= 0.25f);
    CHECK(h.phase2 <= 0.5f);

    run(e, 0.75 * 4.0, 1024); // Loop-1 region 1 has no rule: the Gate ended, the voice is gone
    run(e, 0.75 * 4.0 + 1024 * kBpm / (60.0 * kRate), 4096);
    CHECK(e.playhead().phase2 == -1.0f);
    CHECK(e.playhead().region2 == -1);
}

TEST_CASE("Engine gate region repeats until region 1 ends", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.regions = regions({ 0.75 }); // region 0 = 72000 frames
    m->loop2.data = regionData(kBar, { 0.1f, 0.2f, 0.3f, 0.4f });
    m->loop2.regions = regions({ 0.25, 0.5, 0.75 }); // region 0 = 24000 frames
    m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
    e.setModel(m);
    auto o = run(e, 0.0, kBar);
    const size_t end1 = 72000;
    REQUIRE(maxDev(o.l, kFade, end1, 0.1f) < kTol); // three passes of region 0, never region 1
    REQUIRE(allZero(o.l, end1 + kFade, kBar));       // region 1 of loop 1 has no rule
    REQUIRE(std::abs(o.l[end1 + kFade / 2]) < 0.1f); // fading out
}

TEST_CASE("Engine one-shot plays once and then is silent", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.regions = regions({ 0.75 });
    m->loop2.data = regionData(kBar, { 0.1f, 0.2f, 0.3f, 0.4f });
    m->loop2.regions = regions({ 0.25, 0.5, 0.75 });
    m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::OneShot } });
    e.setModel(m);
    auto o = run(e, 0.0, kBar);
    REQUIRE(maxDev(o.l, kFade, 24000 - kFade, 0.1f) < kTol);
    REQUIRE(allZero(o.l, 24000, kBar));
    REQUIRE(allZero(o.r, 24000, kBar));
}

TEST_CASE("Engine one-shot below loop tempo never reads its region start at the end", "[Engine]")
{
    // Loop 2 is 120 bpm, the host plays 84 bpm: the region advances at 0.7 source frames per output
    // frame while the grains read at pitch rate 1.0, so the last grain overshoots the region end.
    // Region 0 = [0, 24000): its start [0, 6000) is 0.5, the rest 0.1; the continuation is 0.3.
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.regions = regions({ 0.75 });
    m->loop2.data = makeData(kBar, [](int i) { return i < 6000 ? 0.5f : (i < 24000 ? 0.1f : 0.3f); });
    m->loop2.regions = regions({ 0.25, 0.5, 0.75 });
    m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::OneShot } });
    e.setModel(m);
    constexpr double hostBpm = 84.0;
    constexpr double adv = 0.7;
    auto o = run(e, 0.0, 40000, 512, hostBpm);
    // The end fade begins when the region has fewer than kFade output frames left.
    const auto fadeStart = static_cast<size_t>((24000.0 - kFade * adv) / adv);
    const size_t from = fadeStart - 960; // last 20 ms before the end fade
    float hi = 0.0f, lo = 1.0f;
    double sq = 0.0, sqRef = 0.0;
    for (size_t i = from; i < fadeStart; ++i)
    {
        hi = std::max(hi, o.l[i]);
        lo = std::min(lo, o.l[i]);
        sq += static_cast<double>(o.l[i]) * o.l[i];
        sqRef += 0.3 * 0.3; // reference: the loudest material a correct read can reach (continuation)
    }
    REQUIRE(lo > 0.05f);      // still playing at full gain
    REQUIRE(hi < 0.3f + kTol); // nothing of the 0.5 start
    REQUIRE(sq <= sqRef + 1e-6);
    // And the one-shot still ends at its own region end.
    const auto end = static_cast<size_t>(24000.0 / adv) + 2;
    REQUIRE(allZero(o.l, end, o.l.size()));
}

TEST_CASE("Engine back-to-back draws crossfade on the Loop-1 grid", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.regions = regions({ 0.5 }); // entry into region 1 at frame 48000
    m->loop2.data = makeData(kBar, [](int i) { return static_cast<float>(i) / kBar; }); // ramp
    m->loop2.regions = regions({ 0.25, 0.5, 0.75 });
    m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
    m->rules.setTargets(1, { { 2, 1.0f, dc::PlayMode::Gate } });
    e.setModel(m);
    auto o = run(e, 0.0, kBar, 700); // 48000 is not a block boundary
    const auto oldAt = [](int k) { return static_cast<float>(k % 24000) / kBar; };        // region 0, gate
    const auto newAt = [](int k) { return static_cast<float>(48000 + k % 24000) / kBar; }; // region 2 from its start
    for (int j = 0; j <= kFade; ++j)
    {
        const float g = static_cast<float>(std::min(j, kFade)) / kFade;
        const float expected = (1.0f - g) * oldAt(48000 + j) + g * newAt(j);
        REQUIRE(std::abs(o.l[48000 + static_cast<size_t>(j)] - expected) < kTol);
    }
    // After the crossfade the new region runs on the grid, including its Gate repeat at frame 72000.
    float dev = 0.0f;
    for (int k = kFade; k < 48000; ++k)
        dev = std::max(dev, std::abs(o.l[48000 + static_cast<size_t>(k)] - newAt(k)));
    REQUIRE(dev < kTol);
}

TEST_CASE("Engine one-shot plays past a rule-less Loop-1 region entry", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.regions = regions({ 0.25 }); // region 1 (no rule) from frame 24000
    m->loop2.data = regionData(kBar, { 0.2f, 0.6f });
    m->loop2.regions = regions({ 0.5 }); // region 1 = 48000 frames
    m->rules.setTargets(0, { { 1, 1.0f, dc::PlayMode::OneShot } });
    e.setModel(m);
    auto o = run(e, 0.0, kBar);
    REQUIRE(maxDev(o.l, kFade, 48000 - kFade, 0.6f) < kTol);
    REQUIRE(allZero(o.l, 48000, kBar));
}

TEST_CASE("Engine silence draw cancels a running one-shot", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    e.setRandom([] { return 0.9f; });
    auto m = baseModel();
    m->loop1.regions = regions({ 0.25 });
    m->loop2.data = regionData(kBar, { 0.2f, 0.6f });
    m->loop2.regions = regions({ 0.5 });
    m->rules.setTargets(0, { { 1, 1.0f, dc::PlayMode::OneShot } });
    m->rules.setTargets(1, { { 0, 0.5f, dc::PlayMode::OneShot } }); // 0.9 lands in the silence remainder
    e.setModel(m);
    auto o = run(e, 0.0, kBar);
    REQUIRE(maxDev(o.l, kFade, 24000, 0.6f) < kTol);
    REQUIRE(std::abs(o.l[24000] - 0.6f) < kTol);
    REQUIRE(std::abs(o.l[24000 + kFade / 2] - 0.3f) < kTol);
    REQUIRE(allZero(o.l, 24000 + kFade, kBar));
}

TEST_CASE("Engine gate voice ends at the entry of a rule-less region", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.regions = regions({ 0.25 });
    m->loop2.data = regionData(kBar, { 0.2f, 0.6f });
    m->loop2.regions = regions({ 0.5 });
    m->rules.setTargets(0, { { 1, 1.0f, dc::PlayMode::Gate } });
    e.setModel(m);
    auto o = run(e, 0.0, kBar, 700);
    REQUIRE(std::abs(o.l[24000] - 0.6f) < kTol);
    REQUIRE(std::abs(o.l[24000 + kFade / 2] - 0.3f) < kTol);
    REQUIRE(allZero(o.l, 24000 + kFade, kBar));
}

TEST_CASE("Engine transport jump into a rule-less region ends loop 2", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.regions = regions({ 0.25 });
    m->loop2.data = regionData(kBar, { 0.2f, 0.6f });
    m->loop2.regions = regions({ 0.5 });
    m->rules.setTargets(0, { { 1, 1.0f, dc::PlayMode::OneShot } });
    e.setModel(m);
    run(e, 0.0, 4096);
    auto o = run(e, 3.0, 4096); // region 1 (no rule): the one-shot is ended (spec: a jump ends loop 2)
    REQUIRE(allZero(o.l, kFade, 4096));
}

TEST_CASE("Engine transport jump restarts the draw", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    int draws = 0;
    e.setRandom(counting(draws));
    auto m = baseModel();
    m->loop2.data = makeData(kBar, [](int i) { return static_cast<float>(i) / kBar; }); // ramp
    m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::OneShot } });
    e.setModel(m);
    run(e, 0.0, 4096);
    REQUIRE(draws == 1);
    run(e, 4096 * kBpm / (60.0 * kRate), 4096); // continuous: no new draw
    REQUIRE(draws == 1);
    auto o = run(e, 2.0, 4096); // jump inside the same region
    REQUIRE(draws == 2);
    // Loop 2 restarted at its region start (ramp near 0), not at the host-derived position (0.5).
    REQUIRE(o.l[2000] < 0.05f);
    REQUIRE(o.l[2000] > 0.0f);

    // Transport stop and restart: a fresh draw.
    std::vector<float> l(512), r(512);
    dc::TransportInfo stopped { false, 3.0, kBpm, 4, 4 };
    e.process(stopped, l.data(), r.data(), 512);
    run(e, 3.0, 512);
    REQUIRE(draws == 3);
}

TEST_CASE("Engine 6/8 time signature uses 3 beats per bar", "[Engine]")
{
    const double frames3p5 = 3.5 * 60.0 / kBpm * kRate; // ppq 0 -> 3.5
    {
        dc::Engine e;
        prepared(e);
        int draws = 0;
        e.setRandom(counting(draws));
        auto m = baseModel();
        m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
        e.setModel(m);
        run(e, 0.0, static_cast<int>(frames3p5), 512, kBpm, 6, 8);
        REQUIRE(draws == 2); // entry at 0 and wrap at ppq 3
    }
    {
        dc::Engine e;
        prepared(e);
        int draws = 0;
        e.setRandom(counting(draws));
        auto m = baseModel();
        m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
        e.setModel(m);
        run(e, 0.0, static_cast<int>(frames3p5), 512, kBpm, 4, 4);
        REQUIRE(draws == 1); // 4/4: the wrap is at ppq 4
    }
}

TEST_CASE("Engine region entry is sample accurate", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.regions = regions({ 0.5 }); // entry into region 1 at frame 48000
    m->loop2.data = regionData(kBar, { 0.5f });
    m->rules.setTargets(1, { { 0, 1.0f, dc::PlayMode::Gate } });
    e.setModel(m);
    auto o = run(e, 0.0, kBar, 700); // 48000 is not a block boundary
    REQUIRE(allZero(o.l, 0, 48001)); // fade-in starts at gain 0 on frame 48000
    REQUIRE(o.l[48001] > 0.0f);
    REQUIRE(maxDev(o.l, 48000 + kFade, kBar, 0.5f) < kTol);
}

TEST_CASE("Engine output is always finite", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    m->loop1.data = makeData(kBar, [nan](int i) { return i % 3 == 0 ? nan : 0.5f; });
    m->loop2.data = makeData(kBar, [](int i) { return i % 5 == 0 ? std::numeric_limits<float>::infinity() : 0.1f; });
    m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
    e.setModel(m);
    auto o = run(e, 0.0, 8192);
    REQUIRE(std::all_of(o.l.begin(), o.l.end(), [](float x) { return std::isfinite(x); }));
    REQUIRE(std::all_of(o.r.begin(), o.r.end(), [](float x) { return std::isfinite(x); }));
}

TEST_CASE("Engine setModel from another thread while processing", "[Engine]")
{
    dc::Engine e;
    prepared(e);
    auto a = baseModel();
    a->loop1.data = makeData(kBar, [](int) { return 0.25f; });
    auto b = baseModel();
    b->loop2.data = regionData(kBar, { 0.5f });
    b->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
    std::atomic<bool> stop { false };
    std::thread writer([&] {
        for (int i = 0; !stop.load(); ++i)
            e.setModel(i % 2 == 0 ? std::shared_ptr<const dc::Model>(a) : std::shared_ptr<const dc::Model>(b));
    });
    auto o = run(e, 0.0, kBar * 2, 256);
    stop = true;
    writer.join();
    REQUIRE(std::all_of(o.l.begin(), o.l.end(), [](float x) { return std::isfinite(x) && std::abs(x) <= 1.0f; }));
}

TEST_CASE("Engine swap with only new rules keeps loop 1 sample-identical", "[Engine][swap]")
{
    auto m = baseModel();
    m->loop1.data = makeData(kBar, [](int i) { return sineAt(i, 220.0, 0.5f); });
    m->loop1.regions = regions({ 0.5 });
    auto m2 = std::make_shared<dc::Model>(*m);
    m2->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
    dc::Engine ref;
    prepared(ref);
    ref.setModel(m);
    const Out a = run(ref, 0.0, kBar);
    dc::Engine e;
    prepared(e);
    e.setModel(m);
    const Out b = runSwap(e, kBar, kSwapAt, m2);
    REQUIRE(sameRange(a, b, 0, kBar)); // tolerance 0
}

TEST_CASE("Engine loop-2 voice running at a swap continues sample-continuously", "[Engine][swap]")
{
    for (dc::PlayMode mode : { dc::PlayMode::Gate, dc::PlayMode::OneShot })
    {
        auto m = baseModel();
        m->loop2.data = makeData(kBar, [](int i) { return static_cast<float>(i) / kBar; }); // ramp
        m->loop2.regions = regions({ 0.5 });
        m->rules.setTargets(0, { { 1, 1.0f, mode } });
        auto m2 = std::make_shared<dc::Model>(*m);
        m2->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } }); // takes effect at the next entry
        int refDraws = 0, draws = 0;
        dc::Engine ref;
        prepared(ref);
        ref.setRandom(counting(refDraws));
        ref.setModel(m);
        const Out a = run(ref, 0.0, kBar + 4096);
        dc::Engine e;
        prepared(e);
        e.setRandom(counting(draws));
        e.setModel(m);
        const Out b = runSwap(e, kBar + 4096, kSwapAt, m2);
        REQUIRE(sameRange(a, b, 0, kBar));
        REQUIRE(draws == 2); // entry at 0 and the wrap only; the swap is not a draw
        // The new rule plays from the next entry (wrap): region 0 of the ramp, from its start.
        REQUIRE(std::abs(b.l[kBar + 1000] - 1000.0f / kBar) < kTol);
    }
}

TEST_CASE("Engine loop-2 voice keeps its bounds when its region is resized", "[Engine][swap]")
{
    auto m = baseModel();
    m->loop2.data = makeData(kBar, [](int i) { return static_cast<float>(i) / kBar; });
    m->loop2.regions = regions({ 0.5 });
    m->rules.setTargets(0, { { 1, 1.0f, dc::PlayMode::Gate } });
    auto m2 = std::make_shared<dc::Model>(*m);
    m2->loop2.regions = regions({ 0.4 }); // region 1 still exists, but moved
    dc::Engine ref;
    prepared(ref);
    ref.setModel(m);
    const Out a = run(ref, 0.0, kBar);
    dc::Engine e;
    prepared(e);
    e.setModel(m);
    const Out b = runSwap(e, kBar, kSwapAt, m2);
    REQUIRE(sameRange(a, b, 0, kBar));
}

TEST_CASE("Engine adding a loop-1 boundary during playback is not an entry", "[Engine][swap]")
{
    auto m = baseModel();
    m->loop1.data = makeData(kBar, [](int i) { return sineAt(i, 220.0, 0.25f); });
    m->loop1.regions = regions({ 0.5 });
    m->loop2.data = makeData(kBar, [](int i) { return static_cast<float>(i) / kBar; });
    m->loop2.regions = regions({ 0.5 });
    m->rules.setTargets(0, { { 1, 1.0f, dc::PlayMode::Gate } });
    m->rules.setTargets(1, { { 0, 1.0f, dc::PlayMode::Gate } });
    auto m2 = std::make_shared<dc::Model>(*m);
    m2->loop1.regions = regions({ 0.25, 0.5 }); // the phase (0.325) is now in region index 1 (has a rule)
    int refDraws = 0, draws = 0;
    dc::Engine ref;
    prepared(ref);
    ref.setRandom(counting(refDraws));
    ref.setModel(m);
    const Out a = run(ref, 0.0, 48000);
    dc::Engine e;
    prepared(e);
    e.setRandom(counting(draws));
    e.setModel(m);
    const Out b = runSwap(e, 48000, kSwapAt, m2);
    REQUIRE(draws == 1); // no spurious draw
    // No discontinuity: loop 1 and the Gate voice (re-mapped to [0.25, 0.5), which keeps the end
    // edge of its trigger region [0, 0.5)) run on exactly as without the swap.
    REQUIRE(sameRange(a, b, 0, 48000));
}

TEST_CASE("Engine gate ends when its trigger region is gone after a swap", "[Engine][swap]")
{
    auto m = baseModel();
    m->loop1.regions = regions({ 0.25, 0.5 }); // gate triggered in region 1 = [0.25, 0.5) at frame 24000
    m->loop2.data = regionData(kBar, { 0.2f, 0.6f });
    m->loop2.regions = regions({ 0.5 });
    m->rules.setTargets(1, { { 1, 1.0f, dc::PlayMode::Gate } });
    auto m2 = std::make_shared<dc::Model>(*m);
    m2->loop1.regions = regions({ 0.125, 0.375 }); // phase region [0.125, 0.375) shares no edge with [0.25, 0.5)
    int draws = 0;
    dc::Engine e;
    prepared(e);
    e.setRandom(counting(draws));
    e.setModel(m);
    const Out b = runSwap(e, kBar, kSwapAt, m2);
    REQUIRE(draws == 1);
    REQUIRE(std::abs(b.l[kSwapAt] - 0.6f) < kTol); // fades out via shouldGateStop, no hard cut
    REQUIRE(std::abs(b.l[kSwapAt + kFade / 2] - 0.3f) < kTol);
    REQUIRE(allZero(b.l, kSwapAt + kFade, kBar));
}

TEST_CASE("Engine removing the loop-2 region of the running voice fades it out", "[Engine][swap]")
{
    auto m = baseModel();
    m->loop2.data = regionData(kBar, { 0.2f, 0.6f });
    m->loop2.regions = regions({ 0.5 });
    m->rules.setTargets(0, { { 1, 1.0f, dc::PlayMode::Gate } });
    auto m2 = std::make_shared<dc::Model>(*m);
    m2->loop2.regions = regions({}); // region 1 no longer exists
    int draws = 0;
    dc::Engine e;
    prepared(e);
    e.setRandom(counting(draws));
    e.setModel(m);
    const Out b = runSwap(e, kBar, kSwapAt, m2);
    REQUIRE(draws == 1);
    REQUIRE(maxDev(b.l, kFade, kSwapAt, 0.6f) < kTol);
    REQUIRE(std::abs(b.l[kSwapAt] - 0.6f) < kTol);
    REQUIRE(std::abs(b.l[kSwapAt + kFade / 2] - 0.3f) < kTol);
    REQUIRE(allZero(b.l, kSwapAt + kFade, kBar));
}

TEST_CASE("Engine changing bars or a data pointer resets playback", "[Engine][swap]")
{
    for (int variant = 0; variant < 4; ++variant)
    {
        auto m = baseModel();
        m->loop2.data = regionData(kBar, { 0.5f });
        m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
        auto m2 = std::make_shared<dc::Model>(*m);
        if (variant == 0)
            m2->loop1.bars = 2;
        else if (variant == 1)
            m2->loop2.bars = 2;
        else if (variant == 2)
            m2->loop2.data = regionData(kBar, { 0.5f }); // same content, new pointer
        else
            m2->loop1.data = regionData(kBar, { 0.0f });
        int draws = 0;
        dc::Engine e;
        prepared(e);
        e.setRandom(counting(draws));
        e.setModel(m);
        const Out b = runSwap(e, kSwapAt + 4096, kSwapAt, m2);
        REQUIRE(draws == 2);              // fresh entry draw after the reset
        REQUIRE(b.l[kSwapAt - 1] > 0.4f);
        REQUIRE(b.l[kSwapAt] == 0.0f);    // restart: hard cut, fade-in from 0
        REQUIRE(b.l[kSwapAt + kFade + 100] > 0.4f);
    }
}

TEST_CASE("Engine allocation counter hook is active", "[Engine]")
{
    const long before = gAllocCount.load();
    gCountAllocs = true;
    auto p = std::make_unique<std::vector<float>>(16);
    p.reset();
    gCountAllocs = false;
    REQUIRE(gAllocCount.load() - before >= 2);
    gAllocCount = 0;
}

TEST_CASE("Engine process does not allocate", "[Engine]")
{
    gAllocCount = 0;
    dc::Engine e;
    prepared(e);
    auto m = baseModel();
    m->loop1.data = makeData(kBar, [](int i) { return sineAt(i, 220.0, 0.5f); });
    m->loop1.regions = regions({ 0.25, 0.5, 0.75 });
    m->loop2.data = regionData(kBar, { 0.1f, 0.2f, 0.3f, 0.4f });
    m->loop2.regions = regions({ 0.25, 0.5, 0.75 });
    m->rules.setTargets(0, { { 1, 1.0f, dc::PlayMode::Gate } });
    m->rules.setTargets(1, { { 2, 0.5f, dc::PlayMode::OneShot }, { 0, 0.5f, dc::PlayMode::Gate } });
    m->rules.setTargets(3, { { 3, 1.0f, dc::PlayMode::OneShot } });
    // Seamless swaps (same data and bars): a boundary edit with a rule edit, then a Loop-2 region
    // removal; then a resetting swap (new Loop-1 data pointer).
    auto edited = std::make_shared<dc::Model>(*m);
    edited->loop1.regions = regions({ 0.25, 0.4, 0.5, 0.75 });
    edited->rules.setTargets(2, { { 2, 1.0f, dc::PlayMode::Gate } });
    auto shrunk = std::make_shared<dc::Model>(*edited);
    shrunk->loop2.regions = regions({ 0.5 });
    auto reloaded = std::make_shared<dc::Model>(*shrunk);
    reloaded->loop1.data = makeData(kBar, [](int k) { return sineAt(k, 330.0, 0.5f); });
    int i = 0;
    e.setRandom([&i] { return (i++ % 4) * 0.25f; });
    e.setModel(m);
    std::vector<float> l(512), r(512);
    auto block = [&](dc::TransportInfo t) {
        gCountAllocs = true;
        e.process(t, l.data(), r.data(), 512);
        gCountAllocs = false;
    };
    double ppq = 0.0;
    for (int b = 0; b < 2000; ++b)
    {
        if (b == 400)
            e.setModel(edited); // swaps are taken over inside process()
        if (b == 450)
            e.setModel(shrunk);
        if (b == 500)
            e.setModel(reloaded);
        if (b == 700)
            ppq = 1.3; // jump
        block({ b % 300 != 299, ppq, b < 1000 ? kBpm : 97.0, 4, 4 });
        ppq += 512 * (b < 1000 ? kBpm : 97.0) / (60.0 * kRate);
    }
    REQUIRE(gAllocCount.load() == 0);
}

namespace {
// Renders in 512-frame blocks from ppq 0; right before the block at `swapAt` publishes every model in
// `next` in order (the audio thread only ever sees the last one).
Out runMultiSwap(dc::Engine& e, int frames, int swapAt, const std::vector<std::shared_ptr<const dc::Model>>& next)
{
    Out o;
    o.l.assign(static_cast<size_t>(frames), 7.0f);
    o.r.assign(static_cast<size_t>(frames), 7.0f);
    double ppq = 0.0;
    for (int pos = 0; pos < frames; pos += 512)
    {
        if (pos == swapAt)
            for (const auto& m : next)
                e.setModel(m);
        const int n = std::min(512, frames - pos);
        dc::TransportInfo t { true, ppq, kBpm, 4, 4 };
        e.process(t, o.l.data() + pos, o.r.data() + pos, n);
        ppq += n * kBpm / (60.0 * kRate);
    }
    return o;
}
} // namespace

TEST_CASE("Engine compares data ids, not addresses: an equal pointer with another identity resets", "[Engine][swap][hardening]")
{
    // The audio thread cannot tell a re-published buffer from a new buffer that the allocator put at
    // a freed address (ABA). Publishing other data in between gives the same pointer a new id, which
    // simulates exactly that case: it must be a hard restart.
    auto m = baseModel();
    m->loop2.data = regionData(kBar, { 0.5f });
    m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
    auto other = std::make_shared<dc::Model>(*m);
    other->loop2.data = regionData(kBar, { 0.5f });
    auto again = std::make_shared<dc::Model>(*m); // same data pointer as m

    int draws = 0;
    dc::Engine e;
    prepared(e);
    e.setRandom(counting(draws));
    e.setModel(m);
    const Out b = runMultiSwap(e, kSwapAt + 4096, kSwapAt, { other, again });
    CHECK(draws == 2);           // fresh entry draw after the reset
    CHECK(b.l[kSwapAt - 1] > 0.4f);
    CHECK(b.l[kSwapAt] == 0.0f); // hard cut, fade-in from 0

    // Same data all along (same id): seamless, no new draw, no cut.
    auto m2 = std::make_shared<dc::Model>(*m);
    auto m3 = std::make_shared<dc::Model>(*m);
    int draws2 = 0;
    dc::Engine e2;
    prepared(e2);
    e2.setRandom(counting(draws2));
    e2.setModel(m);
    const Out c = runMultiSwap(e2, kSwapAt + 4096, kSwapAt, { m2, m3 });
    CHECK(draws2 == 1);
    CHECK(c.l[kSwapAt] > 0.4f);
}

// --- Audition (preview) ----------------------------------------------------------------------------

namespace {
// Loop 2 = four constant quarters 0.1 .. 0.4, the transport is stopped (the audition needs none).
std::shared_ptr<dc::Model> previewModel()
{
    auto m = baseModel();
    m->loop1.data = makeData(kBar, [](int i) { return sineAt(i, 220.0, 0.25f); });
    m->loop1.regions = regions({ 0.5 });
    m->loop2.data = regionData(kBar, { 0.1f, 0.2f, 0.3f, 0.4f });
    m->loop2.regions = regions({ 0.25, 0.5, 0.75 });
    return m;
}

Out runStopped(dc::Engine& e, int frames, double bpm = kBpm)
{
    Out o;
    o.l.assign(static_cast<size_t>(frames), 7.0f);
    o.r.assign(static_cast<size_t>(frames), 7.0f);
    for (int pos = 0; pos < frames; pos += 512)
    {
        const int n = std::min(512, frames - pos);
        e.process(dc::TransportInfo { false, 0.0, bpm, 4, 4 }, o.l.data() + pos, o.r.data() + pos, n);
    }
    return o;
}
} // namespace

TEST_CASE("Engine preview plays the region once, also while the transport is stopped", "[Engine][preview]")
{
    dc::Engine e;
    prepared(e);
    e.setModel(previewModel());
    e.requestPreview(1, 1); // Loop 2, region 1 = 0.2, 24000 frames at 120 bpm
    const auto o = runStopped(e, 40000);
    CHECK(o.l[0] == 0.0f);                          // fades in from silence
    CHECK(maxDev(o.l, kFade, 24000 - kFade, 0.2f) < kTol);
    CHECK(maxDev(o.r, kFade, 24000 - kFade, -0.2f) < kTol); // data is stereo-inverted
    CHECK(allZero(o.l, 24000 + 64, 40000));         // one-shot: ends with its region
}

TEST_CASE("Engine preview of Loop 1 and tempo follow the host's last bpm", "[Engine][preview]")
{
    dc::Engine e;
    prepared(e);
    auto m = previewModel();
    m->loop1.data = regionData(kBar, { 0.5f, 0.6f });
    e.setModel(m);
    e.requestPreview(0, 1); // Loop 1, region 1 = second half = 0.6
    const auto o = runStopped(e, 60000, 240.0); // twice the tempo: the half bar is 24000 frames
    CHECK(maxDev(o.l, kFade, 24000 - kFade, 0.6f) < kTol);
    CHECK(allZero(o.l, 24000 + 64, 60000));
}

TEST_CASE("Engine preview mixes on top of running playback", "[Engine][preview]")
{
    dc::Engine e;
    prepared(e);
    auto m = previewModel();
    m->loop1.data = regionData(kBar, { 0.05f });
    e.setModel(m);
    run(e, 0.0, 2048);
    e.requestPreview(1, 3); // 0.4
    const auto o = run(e, 2048 * 120.0 / (60.0 * kRate), 2048);
    CHECK(maxDev(o.l, 1024, 2048, 0.45f) < kTol); // 0.05 (Loop 1) + 0.4 (audition)
}

TEST_CASE("Engine preview: a new request cancels with a crossfade, bad requests do nothing", "[Engine][preview]")
{
    dc::Engine e;
    prepared(e);
    e.setModel(previewModel());
    e.requestPreview(1, 7); // no such region
    e.requestPreview(1, -1);
    e.requestPreview(2, 0);
    CHECK(allZero(runStopped(e, 2048).l));

    e.requestPreview(1, 0); // 0.1
    runStopped(e, 4096);
    e.requestPreview(1, 3); // 0.4, cancels
    const auto o = runStopped(e, 4096);
    CHECK(maxDev(o.l, kFade + 600, 4000, 0.4f) < kTol);
    for (float v : o.l) // never louder than the two together
        REQUIRE(v <= 0.5f + kTol);
}

TEST_CASE("Engine preview ends when its loop's data is replaced", "[Engine][preview]")
{
    dc::Engine e;
    prepared(e);
    e.setModel(previewModel());
    e.requestPreview(1, 0);
    runStopped(e, 2048);
    auto m2 = previewModel();
    m2->loop2.data = regionData(kBar, { 0.9f, 0.9f, 0.9f, 0.9f }); // new buffer
    e.setModel(m2);
    CHECK(allZero(runStopped(e, 2048).l));
}

TEST_CASE("Engine preview does not allocate", "[Engine][preview]")
{
    dc::Engine e;
    prepared(e);
    e.setModel(previewModel());
    std::vector<float> l(512), r(512);
    gAllocCount = 0;
    gCountAllocs = true;
    for (int i = 0; i < 40; ++i)
    {
        if (i % 7 == 0)
            e.requestPreview(i % 2, i % 3);
        e.process(dc::TransportInfo { i % 2 == 0, 0.0, kBpm, 4, 4 }, l.data(), r.data(), 512);
    }
    gCountAllocs = false;
    CHECK(gAllocCount.load() == 0);
}

// --- Early draw (teaser) and loop-wrap guard ---------------------------------------------------------

namespace {
std::function<float()> sequence(std::vector<float> values, int& calls)
{
    return [values = std::move(values), &calls] { return values[static_cast<size_t>(calls++) % values.size()]; };
}

// Loop 1 = two regions (boundary at half a bar), Loop 2 = four regions; both Loop-1 regions have rules.
std::shared_ptr<dc::Model> teaserModel()
{
    auto m = baseModel();
    m->loop1.data = regionData(kBar, { 0.05f, 0.06f });
    m->loop1.regions = regions({ 0.5 });
    m->loop2.data = regionData(kBar, { 0.1f, 0.2f, 0.3f, 0.4f });
    m->loop2.regions = regions({ 0.25, 0.5, 0.75 });
    m->rules.setTargets(0, { { 0, 0.5f, dc::PlayMode::Gate }, { 1, 0.3f, dc::PlayMode::OneShot } }); // 20 % silence
    m->rules.setTargets(1, { { 2, 0.6f, dc::PlayMode::Gate }, { 3, 0.4f, dc::PlayMode::OneShot } });
    return m;
}

constexpr double kStep = kBpm / (60.0 * kRate); // beats per frame
} // namespace

TEST_CASE("Sequencer: onRegion1 is draw then apply, and draw changes no state", "[Sequencer][teaser]")
{
    dc::RuleSet rules;
    rules.setTargets(0, { { 1, 0.5f, dc::PlayMode::OneShot } });
    dc::Sequencer s;
    s.setRandom([] { return 0.25f; });
    const auto d = s.draw(0, rules);
    CHECK(d.type == dc::Sequencer::Draw::Type::Target);
    CHECK(d.region2 == 1);
    CHECK_FALSE(s.active()); // nothing happened yet
    const auto c = s.apply(0, d);
    CHECK(c.type == dc::Sequencer::Command::Type::Start);
    CHECK(s.active());

    s.setRandom([] { return 0.75f; }); // beyond the 50 % weight: silence
    CHECK(s.draw(0, rules).type == dc::Sequencer::Draw::Type::Silence);
    CHECK(s.draw(5, rules).type == dc::Sequencer::Draw::Type::None); // no rule: no draw, no RNG use
}

TEST_CASE("Engine early draw: same audio as drawing at the entry", "[Engine][teaser]")
{
    int callsOff = 0, callsOn = 0;
    dc::Engine off, on;
    prepared(off);
    prepared(on);
    off.setRandom(sequence({ 0.1f, 0.55f, 0.9f, 0.3f, 0.7f }, callsOff));
    on.setRandom(sequence({ 0.1f, 0.55f, 0.9f, 0.3f, 0.7f }, callsOn));
    on.setTeaserLookahead(0.15);
    off.setModel(teaserModel());
    on.setModel(teaserModel());
    // Stop 10000 frames before the next entry, so no early draw is left pending at the end.
    const int frames = 2 * kBar - 10000;
    const auto a = run(off, 0.0, frames);
    const auto b = run(on, 0.0, frames);
    CHECK(a.l == b.l);
    CHECK(a.r == b.r);
    CHECK(callsOff == 4); // entries at frames 0, 48000, 96000 and 144000
    CHECK(callsOn == callsOff); // one draw per entry either way
}

TEST_CASE("Engine teaser: the coming draw is visible inside the lookahead window", "[Engine][teaser]")
{
    dc::Engine e;
    prepared(e);
    e.setRandom([] { return 0.5f; }); // region 1's rule: target Loop-2 region 2
    e.setTeaserLookahead(0.15);
    e.setModel(teaserModel());

    run(e, 0.0, 40000); // 0.167 s before the boundary at frame 48000: outside the window
    CHECK(e.playhead().teaser == dc::Engine::kNoTeaser);
    CHECK(e.playhead().teaserWindow == Catch::Approx(0.15f));

    run(e, 40000 * kStep, 3200 + 100); // 0.1 s before the boundary
    auto h = e.playhead();
    REQUIRE(h.teaser == 2);
    CHECK(h.teaserEta == Catch::Approx(0.1f).margin(0.015f));

    run(e, 43300 * kStep, 6000); // across the boundary: applied
    CHECK(e.playhead().teaser == dc::Engine::kNoTeaser);
    CHECK(e.playhead().region2 == 2);
}

TEST_CASE("Engine teaser: a silence draw is shown as silence, a region without rule shows nothing", "[Engine][teaser]")
{
    dc::Engine e;
    prepared(e);
    e.setRandom([] { return 0.9f; }); // region 1's rule has no silence: 0.9 -> still its last target
    e.setTeaserLookahead(0.15);
    auto m = teaserModel();
    m->rules.setTargets(1, { { 2, 0.5f, dc::PlayMode::Gate } }); // 50 % silence
    e.setModel(m);
    run(e, 0.0, 46000);
    CHECK(e.playhead().teaser == dc::Engine::kTeaserSilence);

    dc::Engine n;
    prepared(n);
    int calls = 0;
    n.setRandom(sequence({ 0.5f }, calls));
    n.setTeaserLookahead(0.15);
    auto m2 = teaserModel();
    m2->rules.setTargets(1, {}); // no rule for the coming region
    n.setModel(m2);
    run(n, 0.0, 46000);
    CHECK(n.playhead().teaser == dc::Engine::kNoTeaser);
    CHECK(calls == 1); // only region 0's entry at frame 0 drew
}

TEST_CASE("Engine teaser: a model edit discards the early draw and the new rules are drawn", "[Engine][teaser]")
{
    dc::Engine e;
    prepared(e);
    e.setRandom([] { return 0.5f; });
    e.setTeaserLookahead(0.15);
    const auto original = teaserModel();
    e.setModel(original);
    run(e, 0.0, 46000);
    REQUIRE(e.playhead().teaser == 2);

    auto edited = std::make_shared<dc::Model>(*original); // same data buffers: a seamless swap
    edited->rules.setTargets(1, { { 3, 1.0f, dc::PlayMode::OneShot } });
    e.setModel(edited);
    run(e, 46000 * kStep, 512);
    CHECK(e.playhead().teaser == 3);
}

TEST_CASE("Engine: a host that wraps without splitting the block does not draw twice", "[Engine][teaser]")
{
    auto m = baseModel();
    m->loop1.data = regionData(kBar, { 0.05f });
    m->loop2.data = regionData(kBar, { 0.1f, 0.2f });
    m->loop2.regions = regions({ 0.5 });
    m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });

    int draws = 0;
    dc::Engine e;
    prepared(e);
    e.setRandom(counting(draws));
    e.setModel(m);
    std::vector<float> l(512), r(512);
    auto block = [&](double ppq) { e.process(dc::TransportInfo { true, ppq, kBpm, 4, 4 }, l.data(), r.data(), 512); };

    double ppq = 0.0;
    for (int i = 0; i < 187; ++i, ppq += 512 * kStep)
        block(ppq); // up to frame 95744, 256 frames before the end of the bar
    REQUIRE(draws == 1); // the entry at frame 0
    block(ppq);         // runs over the cycle boundary: the entry draw of the next cycle
    CHECK(draws == 2);
    block(0.0); // the host's loop restart lands on the start of the same region
    CHECK(draws == 2);

    block(1.0); // a real jump into the middle of the loop draws
    CHECK(draws == 3);
    ppq = 1.0 + 512 * kStep;
    for (int i = 0; i < 20; ++i, ppq += 512 * kStep)
        block(ppq); // 0.2 s of normal playback
    block(0.0);     // jump back to a region start, but long after the last entry: a real jump
    CHECK(draws == 4);
}

// --- Effects (insert chain on Loop 2) -----------------------------------------------------------

namespace {
constexpr std::uint8_t fxBit(dc::FxInsert i) { return static_cast<std::uint8_t>(1u << static_cast<int>(i)); }

// Deterministic white noise in [-amp, amp].
float noiseAt(int i, float amp = 0.5f)
{
    std::uint32_t x = static_cast<std::uint32_t>(i) * 2654435761u + 12345u;
    x ^= x >> 13;
    x *= 0x5bd1e995u;
    x ^= x >> 15;
    return amp * (static_cast<float>(x & 0xffffffu) / static_cast<float>(0xffffff) * 2.0f - 1.0f);
}

// Both channels equal (the reverb feeds on L + R; the inverted right channel of makeData would cancel).
std::shared_ptr<const dc::StereoData> makeMono(int frames, const std::function<float(int)>& gen)
{
    auto d = std::make_shared<dc::StereoData>();
    d->sampleRate = kRate;
    d->left.resize(static_cast<size_t>(frames));
    d->right.resize(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
        d->left[static_cast<size_t>(i)] = d->right[static_cast<size_t>(i)] = gen(i);
    return d;
}

// Energy above about 2 kHz over [from, to): two cascaded 2nd-order high-passes at 2 kHz.
double hfEnergy(const std::vector<float>& v, size_t from, size_t to)
{
    const double w0 = 2.0 * std::numbers::pi * 2000.0 / kRate;
    const double cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * 0.70710678);
    const double a0 = 1.0 + alpha;
    const double b0 = (1.0 + cw) / 2.0 / a0, b1 = -(1.0 + cw) / a0, b2 = b0;
    const double a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;
    double s[2][4] = {};
    double e = 0.0;
    for (size_t i = 0; i < to && i < v.size(); ++i)
    {
        double x = v[i];
        for (auto& st : s)
        {
            const double y = b0 * x + b1 * st[0] + b2 * st[1] - a1 * st[2] - a2 * st[3];
            st[1] = st[0];
            st[0] = x;
            st[3] = st[2];
            st[2] = y;
            x = y;
        }
        if (i >= from)
            e += x * x;
    }
    return e;
}

float peakOf(const std::vector<float>& v, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = from; i < to && i < v.size(); ++i)
        m = std::max(m, std::abs(v[i]));
    return m;
}

float maxNeighbourStep(const std::vector<float>& v, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t>(from, 1); i < to && i < v.size(); ++i)
        m = std::max(m, std::abs(v[i] - v[i - 1]));
    return m;
}

bool allFinite(const Out& o)
{
    const auto fin = [](float x) { return std::isfinite(x); };
    return std::all_of(o.l.begin(), o.l.end(), fin) && std::all_of(o.r.begin(), o.r.end(), fin);
}

float maxDiff(const Out& a, const Out& b, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = from; i < to; ++i)
        m = std::max({ m, std::abs(a.l[i] - b.l[i]), std::abs(a.r[i] - b.r[i]) });
    return m;
}

void setAll(dc::FxSettings& fx, int regionCount, int mask)
{
    for (int k = 0; k < regionCount; ++k)
        for (int i = 0; i < dc::kFxInserts; ++i)
            fx.setSwitch(k, static_cast<dc::FxInsert>(i), (mask >> i) & 1);
}

// Loop 2 = mono noise, one region, played as a Gate from frame 0 (Loop 1 is silent, one region).
std::shared_ptr<dc::Model> noiseModel()
{
    auto m = baseModel();
    m->loop2.data = makeMono(kBar, [](int i) { return noiseAt(i); });
    m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
    return m;
}

Out runEngine(std::shared_ptr<const dc::Model> m, int frames, int block = 512)
{
    dc::Engine e;
    prepared(e);
    e.setModel(std::move(m));
    return run(e, 0.0, frames, block);
}
} // namespace

TEST_CASE("Engine fx: with all switches off the output equals the old behavior", "[Engine][fx]")
{
    auto m = baseModel();
    m->loop1.data = makeData(kBar, [](int i) { return sineAt(i, 220.0, 0.25f); });
    m->loop1.regions = regions({ 0.25, 0.5, 0.75 });
    m->loop2.data = makeData(kBar, [](int i) { return noiseAt(i, 0.3f); });
    m->loop2.regions = regions({ 0.25, 0.5, 0.75 });
    m->rules.setTargets(0, { { 1, 1.0f, dc::PlayMode::Gate } });
    m->rules.setTargets(1, { { 2, 1.0f, dc::PlayMode::OneShot } });
    m->rules.setTargets(3, { { 0, 1.0f, dc::PlayMode::Gate } });
    auto withFx = std::make_shared<dc::Model>(*m);
    withFx->fx.params.set(dc::FxParam::FilterCutoff, 100.0);
    withFx->fx.params.set(dc::FxParam::ReverbMix, 1.0);
    withFx->fx.switches.assign(4, 0); // entries present, all off
    auto only1 = std::make_shared<dc::Model>(*m);
    only1->rules = dc::RuleSet {};
    auto only2 = std::make_shared<dc::Model>(*m);
    only2->loop1.data = nullptr;

    const int frames = 2 * kBar;
    const Out a = runEngine(m, frames, 700);
    const Out b = runEngine(withFx, frames, 700);
    CHECK(maxDiff(a, b, 0, frames) <= 1e-5f);
    // Old behavior: Loop 1 and Loop 2 summed.
    const Out l1 = runEngine(only1, frames, 700);
    const Out l2 = runEngine(only2, frames, 700);
    float dev = 0.0f;
    for (size_t i = 0; i < static_cast<size_t>(frames); ++i)
        dev = std::max({ dev, std::abs(b.l[i] - (l1.l[i] + l2.l[i])), std::abs(b.r[i] - (l1.r[i] + l2.r[i])) });
    CHECK(dev <= 1e-5f);
    CHECK(peakOf(l2.l, 0, frames) > 0.1f); // Loop 2 really played
}

TEST_CASE("Engine fx: a region with the filter on sounds different from the same region with it off", "[Engine][fx]")
{
    auto dry = noiseModel();
    auto wet = std::make_shared<dc::Model>(*dry);
    wet->fx.setSwitch(0, dc::FxInsert::Filter, true);
    wet->fx.params.set(dc::FxParam::FilterType, 0.0);
    wet->fx.params.set(dc::FxParam::FilterCutoff, 200.0);
    const Out a = runEngine(dry, kBar);
    const Out b = runEngine(wet, kBar);
    const double eDry = hfEnergy(a.l, 4800, kBar);
    const double eWet = hfEnergy(b.l, 4800, kBar);
    REQUIRE(eDry > 0.0);
    CHECK(10.0 * std::log10(eWet / eDry) <= -20.0);
}

TEST_CASE("Engine fx: loop 1 stays dry", "[Engine][fx]")
{
    auto m = baseModel();
    m->loop1.data = makeMono(kBar, [](int i) { return noiseAt(i); });
    m->loop1.regions = regions({ 0.5 });
    // Loop 2 plays a silent buffer as a Gate, so its region's switches really open the inserts while
    // only Loop 1 is audible. Filter, Flanger and Reverb are linear (silence in, silence out); a Loop 1
    // routed through the chain would come out low-passed at 100 Hz. (Bitcrush is left out: it turns
    // a silent bus into a DC offset of half a step.)
    m->loop2.data = makeMono(kBar, [](int) { return 0.0f; });
    m->loop2.regions = regions({ 0.5 });
    m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
    m->rules.setTargets(1, { { 0, 1.0f, dc::PlayMode::Gate } });
    auto wet = std::make_shared<dc::Model>(*m);
    setAll(wet->fx, 2, fxBit(dc::FxInsert::Filter) | fxBit(dc::FxInsert::Flanger) | fxBit(dc::FxInsert::Reverb));
    wet->fx.params.set(dc::FxParam::FilterCutoff, 100.0);
    wet->fx.params.set(dc::FxParam::ReverbMix, 1.0);
    const Out a = runEngine(m, kBar);
    dc::Engine e;
    prepared(e);
    e.setModel(wet);
    const Out b = run(e, 0.0, kBar);
    CHECK(e.fxActive()); // the inserts are switched on
    CHECK(peakOf(a.l, 0, kBar) > 0.1f);
    CHECK(maxDiff(a, b, 0, kBar) == 0.0f);
}

TEST_CASE("Engine fx: switches follow the region that started last", "[Engine][fx]")
{
    auto dry = baseModel();
    dry->loop1.regions = regions({ 0.5 });
    dry->loop2.data = makeMono(kBar, [](int i) { return noiseAt(i); });
    dry->loop2.regions = regions({ 0.5 }); // A = 0, B = 1
    dry->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
    dry->rules.setTargets(1, { { 1, 1.0f, dc::PlayMode::Gate } });
    auto wet = std::make_shared<dc::Model>(*dry);
    wet->fx.setSwitch(0, dc::FxInsert::Filter, true);
    wet->fx.params.set(dc::FxParam::FilterCutoff, 200.0);
    const Out a = runEngine(dry, kBar, 700);
    const Out b = runEngine(wet, kBar, 700);
    CHECK(10.0 * std::log10(hfEnergy(b.l, 4800, 48000) / hfEnergy(a.l, 4800, 48000)) <= -20.0);
    // In B (from frame 48000) dry again once the crossfade and the 5 ms switch ramp are over.
    CHECK(maxDiff(a, b, 48000 + 960, kBar) <= 1e-5f);
}

TEST_CASE("Engine fx: stopping with silence turns the inputs off and the reverb rings out", "[Engine][fx]")
{
    auto m = baseModel();
    m->loop1.bars = 4;
    m->loop1.regions = regions({ 0.125 }); // region 0 = frames [0, 48000), region 1 = the rest (7 s)
    m->loop2.data = makeMono(kBar, [](int i) { return noiseAt(i); });
    m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
    m->rules.setTargets(1, { { 0, 0.5f, dc::PlayMode::Gate } }); // 0.9 lands in the silence remainder
    m->fx.setSwitch(0, dc::FxInsert::Reverb, true);
    m->fx.params.set(dc::FxParam::ReverbMix, 1.0);
    dc::Engine e;
    prepared(e);
    e.setRandom([] { return 0.9f; });
    e.setModel(m);
    const int frames = 7 * 48000;
    const Out o = run(e, 0.0, frames);
    REQUIRE(e.playhead().region2 == -1); // silence was drawn
    const size_t stop = 48000 + kFade;
    CHECK(peakOf(o.l, stop, stop + 4800) > 1e-3f);       // the tail still sounds after the stop
    CHECK(peakOf(o.l, frames - 48000, frames) < 0.5e-3f); // later below -60 dB (input amplitude 0.5)
    CHECK(!e.fxActive());
    CHECK(allZero(o.l, frames - 4800, frames));
}

TEST_CASE("Engine fx: a stopped transport lets the reverb ring out", "[Engine][fx]")
{
    auto m = noiseModel();
    m->fx.setSwitch(0, dc::FxInsert::Reverb, true);
    m->fx.params.set(dc::FxParam::ReverbMix, 1.0);
    dc::Engine e;
    prepared(e);
    e.setModel(m);
    run(e, 0.0, 48000);
    REQUIRE(e.fxActive());
    const Out o = runStopped(e, 6 * 48000);
    CHECK(peakOf(o.l, 0, 512) > 1e-3f); // not cut
    CHECK(peakOf(o.l, 4800, 9600) > 1e-4f);
    CHECK(allZero(o.l, o.l.size() - 48000, o.l.size())); // rang out completely
    CHECK(allZero(o.r, o.r.size() - 48000, o.r.size()));
    CHECK(!e.fxActive());
}

TEST_CASE("Engine fx: audition of a Loop-2 region uses its switches, also while stopped", "[Engine][fx][preview]")
{
    auto dry = previewModel();
    dry->loop1.data = makeMono(kBar, [](int i) { return noiseAt(i + 3); });
    dry->loop2.data = makeMono(kBar, [](int i) { return noiseAt(i); });
    auto wet = std::make_shared<dc::Model>(*dry);
    wet->fx.setSwitch(1, dc::FxInsert::Filter, true);
    wet->fx.params.set(dc::FxParam::FilterCutoff, 200.0);
    auto all = std::make_shared<dc::Model>(*dry);
    setAll(all->fx, 4, 0x0f);

    const auto audition = [](std::shared_ptr<const dc::Model> m, int slot, int region) {
        dc::Engine e;
        prepared(e);
        e.setModel(std::move(m));
        e.requestPreview(slot, region);
        return runStopped(e, 24000);
    };
    const Out a = audition(dry, 1, 1);
    const Out b = audition(wet, 1, 1);
    REQUIRE(hfEnergy(a.l, 2400, 22000) > 0.0);
    CHECK(10.0 * std::log10(hfEnergy(b.l, 2400, 22000) / hfEnergy(a.l, 2400, 22000)) <= -20.0);
    // Loop-1 audition: dry, also while the inserts are open. A silent Loop-2 Gate voice opens them
    // (linear inserts only, see "loop 1 stays dry"); audible are Loop-1 playback and the Loop-1
    // audition, both of which must stay dry.
    auto open = previewModel();
    open->loop1.data = makeMono(kBar, [](int i) { return noiseAt(i + 3); });
    open->loop2.data = makeMono(kBar, [](int) { return 0.0f; });
    open->loop2.regions = dc::RegionMap {};
    open->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
    open->rules.setTargets(1, { { 0, 1.0f, dc::PlayMode::Gate } });
    auto openWet = std::make_shared<dc::Model>(*open);
    setAll(openWet->fx, 1, fxBit(dc::FxInsert::Filter) | fxBit(dc::FxInsert::Flanger) | fxBit(dc::FxInsert::Reverb));
    openWet->fx.params.set(dc::FxParam::FilterCutoff, 100.0);
    openWet->fx.params.set(dc::FxParam::ReverbMix, 1.0);
    const auto auditionWhilePlaying = [](std::shared_ptr<const dc::Model> m, bool& active) {
        dc::Engine e;
        prepared(e);
        e.setModel(std::move(m));
        run(e, 0.0, 4800); // the Loop-2 voice started, its switches are open
        e.requestPreview(0, 1);
        Out o = run(e, 4800 * kStep, 24000);
        active = e.fxActive();
        return o;
    };
    bool activeDry = false, activeWet = false;
    const Out c = auditionWhilePlaying(open, activeDry);
    const Out d = auditionWhilePlaying(openWet, activeWet);
    CHECK(!activeDry);
    CHECK(activeWet);
    CHECK(peakOf(c.l, 0, 24000) > 0.1f);
    CHECK(maxDiff(c, d, 0, 24000) == 0.0f);
}

TEST_CASE("Engine fx: a transport jump while the reverb rings does not flush it", "[Engine][fx]")
{
    auto m = baseModel();
    m->loop1.regions = regions({ 0.5 });
    m->loop2.data = makeMono(kBar, [](int i) { return noiseAt(i); });
    m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } }); // region 1 has no rule
    m->fx.setSwitch(0, dc::FxInsert::Reverb, true);
    m->fx.params.set(dc::FxParam::ReverbMix, 1.0);
    dc::Engine e;
    prepared(e);
    e.setModel(m);
    run(e, 0.0, 24000);
    const Out o = run(e, 3.0, 24000); // jump into region 1: Loop 2 ends, the tail rings on
    CHECK(peakOf(o.l, 2880, 4800) > 1e-4f); // later than a 50 ms flush fade would allow
    CHECK(peakOf(o.l, 9600, 14400) > 1e-5f);
    CHECK(e.fxActive());
}

TEST_CASE("Engine fx: a same-data model edit while stopped keeps the ringing tail", "[Engine][fx][swap]")
{
    auto m = noiseModel();
    m->fx.setSwitch(0, dc::FxInsert::Reverb, true);
    m->fx.params.set(dc::FxParam::ReverbMix, 1.0);
    dc::Engine e;
    prepared(e);
    e.setModel(m);
    run(e, 0.0, 48000);
    runStopped(e, 512); // stopped: the voice is gone, the reverb rings
    auto edited = std::make_shared<dc::Model>(*m); // same data and bars, edited rules and a switch
    edited->rules.setTargets(0, { { 0, 0.5f, dc::PlayMode::OneShot } });
    edited->fx.setSwitch(0, dc::FxInsert::Filter, true);
    e.setModel(edited);
    const Out o = runStopped(e, 9600);
    CHECK(peakOf(o.l, 2880, 4800) > 1e-4f); // not flushed
    CHECK(e.fxActive());
}

TEST_CASE("Engine fx: toggling the sounding region's switch in a seamless swap takes effect without a click", "[Engine][fx][swap]")
{
    auto m = baseModel();
    m->loop2.data = makeMono(kBar, [](int i) { return sineAt(i, 220.0, 0.5f); });
    m->rules.setTargets(0, { { 0, 1.0f, dc::PlayMode::Gate } });
    m->fx.params.set(dc::FxParam::FilterCutoff, 60.0);
    m->fx.params.set(dc::FxParam::FilterResonance, 0.0);
    auto on = std::make_shared<dc::Model>(*m); // same data and bars: seamless
    on->fx.setSwitch(0, dc::FxInsert::Filter, true);
    on->fx.setSwitch(0, dc::FxInsert::Flanger, true);

    dc::Engine e;
    prepared(e);
    e.setModel(m);
    const Out o = runSwap(e, kSwapAt + 24000, kSwapAt, on);
    CHECK(maxNeighbourStep(o.l, kFade, o.l.size()) < 0.3f);
    CHECK(maxNeighbourStep(o.r, kFade, o.r.size()) < 0.3f);
    // It took effect on the running voice: the low-pass at 60 Hz pulls the 220 Hz sine down.
    CHECK(peakOf(o.l, kSwapAt - 4800, kSwapAt) > 0.45f);
    CHECK(peakOf(o.l, kSwapAt + 12000, kSwapAt + 24000) < 0.35f);
}

TEST_CASE("Engine fx: a hard model restart flushes the tail", "[Engine][fx][swap]")
{
    auto m = noiseModel();
    m->fx.setSwitch(0, dc::FxInsert::Reverb, true);
    m->fx.params.set(dc::FxParam::ReverbMix, 1.0);
    m->fx.params.set(dc::FxParam::ReverbSize, 1.0);
    auto other = baseModel(); // another Loop-2 data object, no rules: nothing plays after the swap
    other->loop2.data = makeMono(kBar, [](int i) { return noiseAt(i); });
    dc::Engine e;
    prepared(e);
    e.setModel(m);
    const Out o = runSwap(e, kSwapAt + 48000, kSwapAt, other);
    CHECK(peakOf(o.l, kSwapAt, kSwapAt + 480) > 1e-3f); // fades, no hard cut
    CHECK(allZero(o.l, kSwapAt + 2880, o.l.size()));    // 60 ms later: nothing left
    CHECK(allZero(o.r, kSwapAt + 2880, o.r.size()));
    CHECK(!e.fxActive());
}

TEST_CASE("Engine fx: process with more frames than prepared, and with single frames, works and stays finite", "[Engine][fx]")
{
    auto m = noiseModel();
    m->loop1.data = makeData(kBar, [](int i) { return sineAt(i, 220.0, 0.25f); });
    m->loop1.regions = regions({ 0.005 }); // an entry (new voice) inside the first 1000 frames
    m->rules.setTargets(1, { { 0, 1.0f, dc::PlayMode::OneShot } });
    setAll(m->fx, 1, 0x0f);
    const auto make = [&](dc::Engine& e) {
        e.prepare(kRate, 64);
        e.setRandom([] { return 0.5f; });
        e.setModel(m);
    };
    dc::Engine a, b;
    make(a);
    make(b);
    const Out big = run(a, 0.0, 1000, 1000);
    const Out ref = run(b, 0.0, 1000, 64);
    CHECK(allFinite(big));
    CHECK(maxDiff(big, ref, 0, 1000) <= 1e-4f);
    CHECK(peakOf(big.l, 0, 1000) > 0.01f);

    const Out single = run(a, 1000 * kStep, 1000, 1);
    const Out singleRef = run(b, 1000 * kStep, 1000, 64);
    CHECK(allFinite(single));
    CHECK(maxDiff(single, singleRef, 0, 1000) <= 1e-4f);
    CHECK(peakOf(single.l, 0, 1000) > 0.01f);
}

TEST_CASE("Engine fx: switching mid-block (rule start inside a block) is finite and bounded", "[Engine][fx]")
{
    // 64 Loop-1 regions alternating between a Loop-2 region with all inserts and one without: in one
    // 96000-frame block that is more switch changes than the per-block list holds.
    std::vector<double> bounds;
    for (int k = 1; k < 64; ++k)
        bounds.push_back(k / 64.0);
    auto m = baseModel();
    m->loop1.regions = regions(bounds);
    m->loop2.data = makeMono(kBar, [](int i) { return noiseAt(i); });
    m->loop2.regions = regions({ 0.5 });
    for (int k = 0; k < 64; ++k)
        m->rules.setTargets(k, { { k % 2, 1.0f, dc::PlayMode::Gate } });
    setAll(m->fx, 1, 0x0f);
    m->fx.params.set(dc::FxParam::ReverbMix, 1.0);
    m->fx.params.set(dc::FxParam::FlangerFeedback, 0.9);

    for (int block : { 700, kBar })
    {
        dc::Engine e;
        e.prepare(kRate, kBar);
        e.setRandom([] { return 0.5f; });
        e.setModel(m);
        Out o;
        o.l.assign(3 * kBar, 0.0f);
        o.r.assign(3 * kBar, 0.0f);
        gAllocCount = 0;
        gCountAllocs = true;
        double ppq = 0.0;
        for (int pos = 0; pos < 3 * kBar; pos += block)
        {
            const int n = std::min(block, 3 * kBar - pos);
            e.process(dc::TransportInfo { true, ppq, kBpm, 4, 4 }, o.l.data() + pos, o.r.data() + pos, n);
            ppq += n * kStep;
        }
        gCountAllocs = false;
        CHECK(gAllocCount.load() == 0);
        CHECK(allFinite(o));
        CHECK(peakOf(o.l, 0, o.l.size()) < 4.0f);
        CHECK(peakOf(o.r, 0, o.r.size()) < 4.0f);
        CHECK(peakOf(o.l, 0, o.l.size()) > 0.1f);
    }
}

TEST_CASE("Engine fx: extreme parameters stay finite", "[Engine][fx]")
{
    for (bool atMax : { false, true })
    {
        auto m = noiseModel();
        m->loop2.data = makeMono(kBar, [](int i) { return noiseAt(i, 1.0f); });
        setAll(m->fx, 1, 0x0f);
        for (int p = 0; p < dc::kFxParamCount; ++p)
        {
            const auto& info = dc::fxParamInfo(static_cast<dc::FxParam>(p));
            m->fx.params.set(static_cast<dc::FxParam>(p), atMax ? info.max : info.min);
        }
        const Out o = runEngine(m, 10 * 48000);
        CHECK(allFinite(o));
        CHECK(peakOf(o.l, 0, o.l.size()) < 16.0f);
    }
}
