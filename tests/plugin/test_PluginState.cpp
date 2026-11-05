#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstring>
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

// ---- Hand-written XML (hardening) ----

namespace {
std::optional<PluginState> fromXml(const juce::String& xml)
{
    return deserialize(xml.toRawUTF8(), xml.getNumBytesAsUTF8());
}
}

TEST_CASE("PluginState deserialize fills missing attributes and elements with defaults", "[PluginState][hardening]")
{
    const auto r = fromXml("<DubcyclingState version=\"1\"><Loop1/>"
                           "<Rules><Rule><Target region2=\"1\" weight=\"0.5\"/></Rule>"
                           "<Rule region1=\"2\"><Target/></Rule></Rules></DubcyclingState>");
    REQUIRE(r.has_value());
    CHECK(r->loop1.path.isEmpty());
    CHECK(r->loop1.fileName.isEmpty());
    CHECK(r->loop1.hash.isEmpty());
    CHECK(r->loop1.bars == 1);
    CHECK(r->loop1.boundaries.empty());
    CHECK(r->loop2.bars == 1); // no Loop2 element at all
    CHECK(r->loop2.path.isEmpty());
    REQUIRE(r->rules.size() == 1); // the rule without region1 is dropped
    CHECK(r->rules[0].region1 == 2);
    REQUIRE(r->rules[0].targets.size() == 1);
    CHECK(r->rules[0].targets[0].region2 == 0);
    CHECK(r->rules[0].targets[0].weight == 0.0f);
    CHECK(r->rules[0].targets[0].mode == PlayMode::Gate);

    const auto noLoops = fromXml("<DubcyclingState version=\"1\"/>");
    REQUIRE(noLoops.has_value());
    CHECK(noLoops->loop1.bars == 1);
    CHECK(noLoops->rules.empty());

    CHECK_FALSE(fromXml("<DubcyclingState><Loop1 bars=\"2\"/></DubcyclingState>").has_value()); // no version
}

TEST_CASE("PluginState deserialize treats non-numeric values as invalid", "[PluginState][hardening]")
{
    const auto r = fromXml("<DubcyclingState version=\"1\">"
                           "<Loop1 bars=\"abc\" boundaries=\"x,0.5,,y\"/><Loop2 bars=\"1e99\"/>"
                           "<Rules><Rule region1=\"0\"><Target region2=\"1\" weight=\"abc\"/>"
                           "<Target region2=\"2\" weight=\"nan\"/><Target region2=\"3\" weight=\"0.25\" mode=\"zz\"/>"
                           "</Rule></Rules></DubcyclingState>");
    REQUIRE(r.has_value());
    CHECK(r->loop1.bars == 1);
    CHECK(r->loop1.boundaries == std::vector<double> { 0.5 });
    CHECK(r->loop2.bars >= 1);
    CHECK(r->loop2.bars <= 1024);
    REQUIRE(r->rules.size() == 1);
    REQUIRE(r->rules[0].targets.size() == 3);
    CHECK(r->rules[0].targets[0].weight == 0.0f);
    CHECK(r->rules[0].targets[1].weight == 0.0f);
    CHECK(r->rules[0].targets[2].weight == 0.25f);
    CHECK(r->rules[0].targets[2].mode == PlayMode::Gate);
}

TEST_CASE("PluginState deserialize caps rule and target counts and drops absurd region1", "[PluginState][hardening]")
{
    juce::String xml = "<DubcyclingState version=\"1\"><Rules>";
    xml << "<Rule region1=\"999999999\"><Target region2=\"0\" weight=\"0.5\"/></Rule>";
    xml << "<Rule region1=\"0\">";
    for (int t = 0; t < 3000; ++t)
        xml << "<Target region2=\"" << t << "\" weight=\"0.0001\"/>";
    xml << "</Rule>";
    for (int i = 1; i < 2000; ++i)
        xml << "<Rule region1=\"" << i << "\"><Target region2=\"0\" weight=\"0.5\"/></Rule>";
    xml << "</Rules></DubcyclingState>";

    const auto r = fromXml(xml);
    REQUIRE(r.has_value());
    CHECK(r->rules.size() == 1024);
    REQUIRE_FALSE(r->rules.empty());
    CHECK(r->rules[0].region1 == 0);
    CHECK(r->rules[0].targets.size() == 1024);
    for (const auto& e : r->rules)
        CHECK(e.region1 < 4096);
}

TEST_CASE("PluginState roundtrip keeps region labels by index", "[PluginState][labels]")
{
    auto s = sample();
    s.loop1.labels = { "Kick", "", "Snare" };
    s.loop2.labels = { "", "Fill" };
    const auto block = serialize(s);
    const auto r = deserialize(block.getData(), block.getSize());
    REQUIRE(r.has_value());
    CHECK(r->loop1.labels == std::vector<juce::String> { "Kick", "", "Snare" });
    CHECK(r->loop2.labels == std::vector<juce::String> { "", "Fill" });
}

TEST_CASE("PluginState without labels loads with none (older sessions)", "[PluginState][labels]")
{
    const auto block = serialize(sample());
    const auto r = deserialize(block.getData(), block.getSize());
    REQUIRE(r.has_value());
    CHECK(r->loop1.labels.empty());
    CHECK(r->loop2.labels.empty());
    CHECK(block.toString().contains("Labels") == false); // nothing written when there is nothing to keep
}

TEST_CASE("sanitizeLabel trims, drops control characters and cuts to kMaxLabelLength", "[PluginState][labels]")
{
    CHECK(sanitizeLabel("  Kick  ") == "Kick");
    CHECK(sanitizeLabel(juce::String("Ki") + juce::String::charToString(7) + "ck\n") == "Kick");
    CHECK(sanitizeLabel("   ") == "");
    CHECK(sanitizeLabel("12345678901234567890") == "1234567890123456");
    CHECK(sanitizeLabel("123456789012345 ABC") == "123456789012345"); // cut after 16, then trimmed again
    CHECK(sanitizeLabel(juce::String(juce::CharPointer_UTF8("Bass \xc3\xa4"))) == juce::String(juce::CharPointer_UTF8("Bass \xc3\xa4")));
}

TEST_CASE("PluginState sanitizes labels after load and drops absurd ones", "[PluginState][labels][hardening]")
{
    const juce::String xml =
        "<DubcyclingState version=\"1\"><Loop1 bars=\"1\"><Labels>"
        "<Label region=\"2\" text=\"  Hat \"/>"
        "<Label region=\"-1\" text=\"neg\"/>"
        "<Label region=\"4096\" text=\"huge\"/>"
        "<Label region=\"0\" text=\"   \"/>"
        "<Label region=\"1\" text=\"12345678901234567890\"/>"
        "</Labels></Loop1><Loop2 bars=\"1\"/></DubcyclingState>";
    const auto utf8 = xml.toRawUTF8();
    const auto r = deserialize(utf8, std::strlen(utf8));
    REQUIRE(r.has_value());
    REQUIRE(r->loop1.labels.size() == 3);
    CHECK(r->loop1.labels[0] == "");
    CHECK(r->loop1.labels[1] == "1234567890123456");
    CHECK(r->loop1.labels[2] == "Hat");
}

TEST_CASE("State: active bank and preset survive a round trip", "[PluginState][presets]")
{
    auto s = sample();
    s.activeBank = "Drums";
    s.activePreset = "Break 1";
    const auto block = serialize(s);
    const auto r = deserialize(block.getData(), block.getSize());
    REQUIRE(r.has_value());
    CHECK(r->activeBank == "Drums");
    CHECK(r->activePreset == "Break 1");
}

TEST_CASE("State: a state without active preset fields loads with both empty", "[PluginState][presets]")
{
    const auto block = serialize(sample());
    CHECK(block.toString().contains("activeBank") == false);
    CHECK(block.toString().contains("activePreset") == false);
    const auto r = deserialize(block.getData(), block.getSize());
    REQUIRE(r.has_value());
    CHECK(r->version == 1);
    CHECK(r->activeBank.isEmpty());
    CHECK(r->activePreset.isEmpty());
}

TEST_CASE("State: active names are sanitized", "[PluginState][presets][hardening]")
{
    const juce::String longName(std::string(100, 'x'));
    const juce::String xml = "<DubcyclingState version=\"1\" activeBank=\"Dr&#7;ums\" activePreset=\"" + longName
                           + "\"><Loop1 bars=\"1\"/><Loop2 bars=\"1\"/></DubcyclingState>";
    const auto utf8 = xml.toRawUTF8();
    const auto r = deserialize(utf8, std::strlen(utf8));
    REQUIRE(r.has_value());
    CHECK(r->activeBank == "Drums");
    CHECK(r->activePreset == juce::String(std::string(64, 'x')));
}

TEST_CASE("sanitizeName drops control characters, trims and cuts", "[PluginState][presets]")
{
    CHECK(sanitizeName(juce::String("  a") + juce::String::charToString(9) + "b  ", 16) == "ab");
    CHECK(sanitizeName("abcdef", 3) == "abc");
}
