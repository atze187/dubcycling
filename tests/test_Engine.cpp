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
#include "engine/FxSlot.h"

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

TEST_CASE("FxSlot default is a no-op", "[Engine]")
{
    dc::FxSlot fx;
    float l[2] = { 0.5f, -0.5f }, r[2] = { 0.25f, -0.25f };
    fx.process(l, r, 2);
    REQUIRE(l[0] == 0.5f);
    REQUIRE(r[1] == -0.25f);
}
