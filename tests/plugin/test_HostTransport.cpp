#include <catch2/catch_test_macros.hpp>
#include "plugin/HostTransport.h"

using Pos = juce::AudioPlayHead::PositionInfo;

namespace {
Pos full()
{
    Pos p;
    p.setIsPlaying(true);
    p.setPpqPosition(5.5);
    p.setBpm(98.0);
    p.setTimeSignature(juce::AudioPlayHead::TimeSignature { 6, 8 });
    return p;
}
} // namespace

TEST_CASE("HostTransport maps a complete position", "[HostTransport]")
{
    const auto t = dc::toTransportInfo(full());
    REQUIRE(t.playing);
    REQUIRE(t.ppq == 5.5);
    REQUIRE(t.bpm == 98.0);
    REQUIRE(t.tsNum == 6);
    REQUIRE(t.tsDen == 8);
}

TEST_CASE("HostTransport not playing stays not playing", "[HostTransport]")
{
    auto p = full();
    p.setIsPlaying(false);
    REQUIRE_FALSE(dc::toTransportInfo(p).playing);
}

TEST_CASE("HostTransport missing position or fields is invalid", "[HostTransport]")
{
    REQUIRE_FALSE(dc::toTransportInfo(juce::Optional<Pos> {}).playing);
    {
        auto p = full();
        p.setPpqPosition(juce::nullopt);
        REQUIRE_FALSE(dc::toTransportInfo(p).playing);
    }
    {
        auto p = full();
        p.setBpm(juce::nullopt);
        REQUIRE_FALSE(dc::toTransportInfo(p).playing);
    }
}

TEST_CASE("HostTransport missing time signature defaults to 4/4", "[HostTransport]")
{
    auto p = full();
    p.setTimeSignature(juce::nullopt);
    const auto t = dc::toTransportInfo(p);
    REQUIRE(t.playing);
    REQUIRE(t.tsNum == 4);
    REQUIRE(t.tsDen == 4);
    REQUIRE(t.ppq == 5.5);
    REQUIRE(t.bpm == 98.0);
}

TEST_CASE("HostTransport derives ppq from seconds when ppq is missing", "[HostTransport]")
{
    auto p = full();
    p.setPpqPosition(juce::nullopt);
    p.setTimeInSeconds(3.0);
    const auto t = dc::toTransportInfo(p);
    REQUIRE(t.playing);
    REQUIRE(t.ppq == 3.0 * 98.0 / 60.0);
}

TEST_CASE("HostTransport without ppq and seconds stays invalid", "[HostTransport]")
{
    auto p = full();
    p.setPpqPosition(juce::nullopt);
    p.setTimeInSeconds(juce::nullopt);
    REQUIRE_FALSE(dc::toTransportInfo(p).playing);
}

TEST_CASE("HostTransport without time signature still drives the Engine in 4/4", "[HostTransport]")
{
    dc::Engine e;
    e.prepare(48000.0, 256);
    auto m = std::make_shared<dc::Model>();
    auto d = std::make_shared<dc::StereoData>();
    d->sampleRate = 48000.0;
    d->left.assign(96000, 0.5f); // one 4/4 bar at 120 bpm
    d->right.assign(96000, -0.5f);
    m->loop1.data = d;
    e.setModel(m);
    auto p = full();
    p.setBpm(120.0);
    p.setPpqPosition(0.0);
    p.setTimeSignature(juce::nullopt);
    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    e.process(dc::toTransportInfo(p), l.data(), r.data(), 256);
    REQUIRE(l[100] > 0.49f);
    REQUIRE(r[100] < -0.49f);
}

TEST_CASE("HostTransport result drives the Engine", "[HostTransport]")
{
    // Processor-level path without the plugin wrapper: playhead -> TransportInfo -> Engine.
    dc::Engine e;
    e.prepare(48000.0, 256);
    auto m = std::make_shared<dc::Model>();
    auto d = std::make_shared<dc::StereoData>();
    d->sampleRate = 48000.0;
    d->left.assign(72000, 0.5f); // one 6/8 bar at 120 bpm
    d->right.assign(72000, -0.5f);
    m->loop1.data = d;
    e.setModel(m);
    auto p = full();
    p.setBpm(120.0);
    p.setPpqPosition(0.0);
    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    e.process(dc::toTransportInfo(p), l.data(), r.data(), 256);
    REQUIRE(l[100] > 0.49f);
    REQUIRE(r[100] < -0.49f);

    p.setBpm(juce::nullopt);
    e.process(dc::toTransportInfo(p), l.data(), r.data(), 256);
    REQUIRE(l[100] == 0.0f);
}
