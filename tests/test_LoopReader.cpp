#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>
#include "engine/LoopReader.h"
#include "engine/LoopTiming.h"
#include "engine/StereoData.h"

namespace {

constexpr double kRate = 48000.0;

std::shared_ptr<const dc::StereoData> makeData(size_t frames, double rate, float (*gen)(size_t, size_t))
{
    auto d = std::make_shared<dc::StereoData>();
    d->sampleRate = rate;
    d->left.resize(frames);
    d->right.resize(frames);
    for (size_t i = 0; i < frames; ++i)
    {
        d->left[i] = gen(i, frames);
        d->right[i] = -gen(i, frames);
    }
    return d;
}

float sine440(size_t i, size_t)
{
    return static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(i) / kRate));
}

float ramp(size_t i, size_t frames)
{
    return static_cast<float>(static_cast<double>(i) / static_cast<double>(frames));
}

bool allFinite(const std::vector<float>& v)
{
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

bool allZero(const std::vector<float>& v)
{
    return std::all_of(v.begin(), v.end(), [](float x) { return x == 0.0f; });
}

// Median der Abstände aufsteigender Nulldurchgänge (linear interpoliert) -> Frequenz in Hz.
// Der Median ignoriert die wenigen Perioden, in denen eine Überblendung die Phase versetzt.
double medianZeroCrossFreq(const std::vector<float>& y, size_t from, double rate)
{
    std::vector<double> crossings;
    for (size_t i = from + 1; i < y.size(); ++i)
        if (y[i - 1] < 0.0f && y[i] >= 0.0f)
            crossings.push_back(static_cast<double>(i - 1) + y[i - 1] / (y[i - 1] - y[i]));
    std::vector<double> periods;
    for (size_t i = 1; i < crossings.size(); ++i)
        periods.push_back(crossings[i] - crossings[i - 1]);
    if (periods.empty())
        return 0.0;
    std::nth_element(periods.begin(), periods.begin() + static_cast<std::ptrdiff_t>(periods.size() / 2), periods.end());
    return rate / periods[periods.size() / 2];
}

} // namespace

TEST_CASE("LoopReader silence without data", "[LoopReader]")
{
    dc::LoopReader r;
    r.prepare(kRate);
    std::vector<float> l(256, 1.0f), rr(256, 1.0f);
    r.render(l.data(), rr.data(), 256, 0.25, 1.0 / 48000.0, 1.0);
    REQUIRE(allZero(l));
    REQUIRE(allZero(rr));

    // Leere Daten zählen ebenfalls als "keine Daten".
    r.setData(std::make_shared<dc::StereoData>());
    std::fill(l.begin(), l.end(), 1.0f);
    std::fill(rr.begin(), rr.end(), 1.0f);
    r.render(l.data(), rr.data(), 256, 0.25, 1.0 / 48000.0, 1.0);
    REQUIRE(allZero(l));
    REQUIRE(allZero(rr));
}

TEST_CASE("LoopReader sine keeps pitch at double tempo", "[LoopReader]")
{
    // 2 s Sinus = 4 Beats bei 120 BPM; der Host spielt 240 BPM, die Phase läuft also doppelt so schnell.
    const size_t frames = static_cast<size_t>(2.0 * kRate);
    dc::LoopReader r;
    r.prepare(kRate);
    r.setData(makeData(frames, kRate, sine440));

    const double step = dc::sourceStep(frames, 4.0, 240.0, kRate);
    REQUIRE(step == 2.0);
    const double phaseStep = 2.0 / static_cast<double>(frames);

    const int block = 512;
    const int total = static_cast<int>(kRate); // 1 s Ausgabe
    std::vector<float> l(static_cast<size_t>(total)), rr(static_cast<size_t>(total));
    double phase = 0.0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min(block, total - pos);
        r.render(l.data() + pos, rr.data() + pos, n, phase, phaseStep, step);
        phase = std::fmod(phase + n * phaseStep, 1.0);
    }
    REQUIRE(allFinite(l));
    REQUIRE(allFinite(rr));

    const size_t skip = static_cast<size_t>(r.grainLength());
    const double fl = medianZeroCrossFreq(l, skip, kRate);
    const double fr = medianZeroCrossFreq(rr, skip, kRate);
    INFO("left " << fl << " Hz, right " << fr << " Hz");
    REQUIRE(std::abs(fl - 440.0) <= 440.0 * 0.02);
    REQUIRE(std::abs(fr - 440.0) <= 440.0 * 0.02);
}

TEST_CASE("LoopReader position follows phase after a jump", "[LoopReader]")
{
    const size_t frames = 48000; // 1 s Rampe, Phase 0.6 = Frame 28800
    dc::LoopReader r;
    r.prepare(kRate);
    r.setData(makeData(frames, kRate, ramp));
    const double phaseStep = 1.0 / static_cast<double>(frames);
    const int g = r.grainLength();
    REQUIRE(g == 1920); // 40 ms bei 48 kHz

    std::vector<float> l(1000), rr(1000);
    r.render(l.data(), rr.data(), 1000, 0.1, phaseStep, 1.0);
    REQUIRE(std::abs(l[0] - 0.1f) < 1e-4f);

    const int n = 2 * g;
    l.assign(static_cast<size_t>(n), 0.0f);
    rr.assign(static_cast<size_t>(n), 0.0f);
    r.render(l.data(), rr.data(), n, 0.6, phaseStep, 1.0);
    REQUIRE(allFinite(l));
    // Spätestens nach einer halben Grain-Länge muss die Ausgabe dem Quellmaterial bei 0.6 folgen.
    for (int i = g / 2; i < n; ++i)
    {
        const float expected = static_cast<float>((28800.0 + i) / static_cast<double>(frames));
        INFO("frame " << i);
        REQUIRE(std::abs(l[static_cast<size_t>(i)] - expected) < 1e-4f);
        REQUIRE(std::abs(rr[static_cast<size_t>(i)] + expected) < 1e-4f);
    }
}

TEST_CASE("LoopReader no NaN with step 0 or phase outside 0..1", "[LoopReader]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const size_t frames = 48000;
    auto data = makeData(frames, kRate, ramp);
    const double ps = 1.0 / static_cast<double>(frames);
    std::vector<float> l(512), rr(512);

    dc::LoopReader r;
    r.prepare(kRate);
    r.setData(data);

    SECTION("step 0, NaN and inf give silence")
    {
        for (double step : {0.0, nan, inf, -1.0})
        {
            std::fill(l.begin(), l.end(), 1.0f);
            std::fill(rr.begin(), rr.end(), 1.0f);
            r.render(l.data(), rr.data(), 512, 0.3, ps, step);
            REQUIRE(allZero(l));
            REQUIRE(allZero(rr));
        }
        for (double phase : {nan, inf, -inf})
        {
            std::fill(l.begin(), l.end(), 1.0f);
            r.render(l.data(), rr.data(), 512, phase, ps, 1.0);
            REQUIRE(allZero(l));
        }
        for (double phaseStep : {nan, inf})
        {
            std::fill(l.begin(), l.end(), 1.0f);
            r.render(l.data(), rr.data(), 512, 0.3, phaseStep, 1.0);
            REQUIRE(allZero(l));
        }
    }

    SECTION("phase outside 0..1 is wrapped")
    {
        for (double phase : {-0.3, 1.7, -2.3, 5.7})
        {
            dc::LoopReader a, b;
            a.prepare(kRate);
            b.prepare(kRate);
            a.setData(data);
            b.setData(data);
            std::vector<float> la(512), ra(512), lb(512), rb(512);
            a.render(la.data(), ra.data(), 512, phase, ps, 1.0);
            b.render(lb.data(), rb.data(), 512, 0.7, ps, 1.0);
            REQUIRE(allFinite(la));
            for (size_t i = 0; i < 512; ++i)
                REQUIRE(std::abs(la[i] - lb[i]) < 1e-5f);
        }
    }

    SECTION("wrap across the loop end stays finite")
    {
        std::vector<float> big(4096), bigR(4096);
        r.render(big.data(), bigR.data(), 4096, 0.99, ps, 1.0);
        REQUIRE(allFinite(big));
        REQUIRE(allFinite(bigR));
    }
}
