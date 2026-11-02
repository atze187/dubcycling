#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>
#include <juce_core/juce_core.h>
#include "plugin/PluginState.h"

using namespace dc;
using Catch::Matchers::WithinAbs;

namespace {
PluginState sample()
{
    PluginState s;
    s.loop1 = { "C:/loops/a.wav", "a.wav", "abc123", 4, { 0.25, 0.5, 0.75 } };
    s.loop2 = { "C:/loops/b.wav", "b.wav", "def456", 2, { 0.1234567890123 } };
    s.rules.push_back({ 0, { { 1, 0.5f, PlayMode::Gate }, { 0, 0.25f, PlayMode::OneShot } } });
    s.rules.push_back({ 3, { { 1, 1.0f, PlayMode::OneShot } } });
    return s;
}
}

TEST_CASE("PluginState roundtrip keeps boundaries rules and bars")
{
    const auto block = serialize(sample());
    const auto r = deserialize(block.getData(), block.getSize());
    REQUIRE(r.has_value());
    CHECK(r->version == 1);
    CHECK(r->loop1.path == "C:/loops/a.wav");
    CHECK(r->loop1.fileName == "a.wav");
    CHECK(r->loop1.hash == "abc123");
    CHECK(r->loop1.bars == 4);
    REQUIRE(r->loop1.boundaries.size() == 3);
    CHECK(r->loop1.boundaries[1] == 0.5);
    CHECK(r->loop2.bars == 2);
    REQUIRE(r->loop2.boundaries.size() == 1);
    CHECK(r->loop2.boundaries[0] == 0.1234567890123);
    REQUIRE(r->rules.size() == 2);
    CHECK(r->rules[0].region1 == 0);
    REQUIRE(r->rules[0].targets.size() == 2);
    CHECK(r->rules[0].targets[0].region2 == 1);
    CHECK(r->rules[0].targets[0].weight == 0.5f);
    CHECK(r->rules[0].targets[1].mode == PlayMode::OneShot);
    CHECK(r->rules[1].region1 == 3);
}

TEST_CASE("PluginState deserialize of empty block returns nullopt")
{
    CHECK_FALSE(deserialize(nullptr, 0).has_value());
    juce::MemoryBlock empty;
    CHECK_FALSE(deserialize(empty.getData(), empty.getSize()).has_value());
}

TEST_CASE("PluginState deserialize of garbage returns nullopt")
{
    const char junk[] = "\x01\x02 not xml at all <<<";
    CHECK_FALSE(deserialize(junk, sizeof(junk)).has_value());
    const char otherXml[] = "<Other version=\"1\"/>";
    CHECK_FALSE(deserialize(otherXml, sizeof(otherXml) - 1).has_value());
    auto block = serialize(sample());
    CHECK_FALSE(deserialize(block.getData(), block.getSize() / 2).has_value());
}

TEST_CASE("PluginState deserialize of version 99 returns nullopt")
{
    auto s = sample();
    s.version = 99;
    const auto block = serialize(s);
    CHECK_FALSE(deserialize(block.getData(), block.getSize()).has_value());
}

TEST_CASE("PluginState boundaries are sanitized after load")
{
    auto s = sample();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    s.loop1.boundaries = { 0.7, nan, -0.2, 1.5, 0.3, 0.3, 0.301, 0.0, 1.0 };
    const auto block = serialize(s);
    const auto r = deserialize(block.getData(), block.getSize());
    REQUIRE(r.has_value());
    REQUIRE(r->loop1.boundaries.size() == 2);
    CHECK(r->loop1.boundaries[0] == 0.3);
    CHECK(r->loop1.boundaries[1] == 0.7);
}

TEST_CASE("PluginState rules over 100 percent are scaled after load")
{
    PluginState s = sample();
    s.rules = { { 0, { { 0, 0.9f, PlayMode::Gate }, { 1, 0.9f, PlayMode::Gate } } },
                { -1, { { 0, 0.5f, PlayMode::Gate } } },
                { 1, { { -2, 0.5f, PlayMode::Gate }, { 5, 0.3f, PlayMode::Gate } } } };
    const auto block = serialize(s);
    const auto r = deserialize(block.getData(), block.getSize());
    REQUIRE(r.has_value());
    REQUIRE(r->rules.size() == 2); // negative region1 dropped
    double sum = 0.0;
    for (auto& t : r->rules[0].targets)
        sum += t.weight;
    CHECK_THAT(sum, WithinAbs(1.0, 1e-5));
    REQUIRE(r->rules[1].targets.size() == 1); // negative region2 dropped, missing region 5 kept
    CHECK(r->rules[1].targets[0].region2 == 5);
}

TEST_CASE("PluginState hashFile is SHA-256 of the whole file")
{
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dchash", ".bin", false);
    REQUIRE(f.replaceWithText("abc", false, false, nullptr));
    CHECK(hashFile(f).equalsIgnoreCase("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    f.deleteFile();
    CHECK(hashFile(f).isEmpty());
}
