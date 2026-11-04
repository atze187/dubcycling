#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include "plugin/PluginState.h"
#include "plugin/presets/BankFormat.h"

using namespace dc;

namespace {
Preset makePreset(const juce::String& name, const juce::String& path)
{
    Preset p;
    p.name = name;
    p.state.loop1 = { path, "a.wav", "abc123", 4, { 0.25, 0.5, 0.75 }, { "Intro", {}, "Drop" } };
    p.state.loop2 = { "loops/b.wav", "b.wav", "def456", 2, { 0.1234567890123 }, {} };
    p.state.rules.push_back({ 0, { { 1, 0.5f, PlayMode::Gate }, { 0, 0.25f, PlayMode::OneShot } } });
    p.state.rules.push_back({ 3, { { 1, 1.0f, PlayMode::OneShot } } });
    return p;
}

Bank sampleBank()
{
    Bank b;
    b.presets.push_back(makePreset("First", "loops/a.wav"));
    auto second = makePreset("Second", "other/a2.wav");
    second.state.rules.pop_back();
    b.presets.push_back(second);
    return b;
}
}

TEST_CASE("BankFormat: a bank survives a JSON round trip")
{
    const auto bank = sampleBank();
    const auto r = bankFromJson(bankToJson(bank));
    REQUIRE(r.bank.has_value());
    CHECK_FALSE(r.tooNew);
    REQUIRE(r.bank->presets.size() == 2);
    for (std::size_t i = 0; i < 2; ++i)
    {
        const auto& a = bank.presets[i];
        const auto& b = r.bank->presets[i];
        CHECK(a.name == b.name);
        CHECK(presetFingerprint(a.state) == presetFingerprint(b.state));
        CHECK(a.state.loop1.path == b.state.loop1.path);
        CHECK(a.state.loop2.path == b.state.loop2.path);
        CHECK(a.state.loop1.fileName == b.state.loop1.fileName);
        CHECK(a.state.loop1.bars == b.state.loop1.bars);
        CHECK(a.state.loop1.boundaries == b.state.loop1.boundaries);
        CHECK(a.state.loop2.boundaries == b.state.loop2.boundaries);
        CHECK(a.state.loop1.labels == b.state.loop1.labels);
        REQUIRE(a.state.rules.size() == b.state.rules.size());
    }
    CHECK(r.bank->presets[0].state.rules[0].targets[1].mode == PlayMode::OneShot);
    CHECK(r.bank->presets[0].state.rules[0].targets[0].mode == PlayMode::Gate);
}

TEST_CASE("BankFormat: json carries version and a presets list")
{
    const auto v = juce::JSON::parse(bankToJson(sampleBank()));
    REQUIRE(v.isObject());
    CHECK(static_cast<int>(v["version"]) == 1);
    REQUIRE(v["presets"].isArray());
    CHECK(v["presets"].size() == 2);
}

TEST_CASE("BankFormat: damaged json gives no bank")
{
    CHECK_FALSE(bankFromJson("").bank.has_value());
    CHECK_FALSE(bankFromJson("not json at all").bank.has_value());
    CHECK_FALSE(bankFromJson("[1,2,3]").bank.has_value());
    CHECK_FALSE(bankFromJson("{\"version\": 1}").bank.has_value());
    CHECK_FALSE(bankFromJson("{\"version\": 1, \"presets\": 5}").bank.has_value());
    CHECK_FALSE(bankFromJson("{\"presets\": []}").bank.has_value());
    CHECK_FALSE(bankFromJson("{\"version\": 1, \"presets\": []}").tooNew);
}

TEST_CASE("BankFormat: json nested deeper than kMaxJsonDepth gives no bank without crashing")
{
    // A hostile bank.json: the JSON parser recurses per level, this must not reach it.
    const auto deep = juce::String::repeatedString("[", 100000);
    const auto r = bankFromJson(deep);
    CHECK_FALSE(r.bank.has_value());
    CHECK_FALSE(r.tooNew);
    const auto deepInside = "{\"version\": 1, \"presets\": [], \"x\": " + juce::String::repeatedString("{\"a\":", 100000) + "}";
    CHECK_FALSE(bankFromJson(deepInside).bank.has_value());
    const auto justOver = "{\"version\": 1, \"presets\": [], \"x\": " + juce::String::repeatedString("[", kMaxJsonDepth)
                          + juce::String::repeatedString("]", kMaxJsonDepth) + "}";
    CHECK_FALSE(bankFromJson(justOver).bank.has_value());

    // At the limit, and brackets inside strings (also after an escaped quote) do not count.
    const auto atLimit = "{\"version\": 1, \"presets\": [], \"x\": " + juce::String::repeatedString("[", kMaxJsonDepth - 1)
                         + juce::String::repeatedString("]", kMaxJsonDepth - 1) + "}";
    CHECK(bankFromJson(atLimit).bank.has_value());
    const auto inString = "{\"version\": 1, \"presets\": [{\"name\": \"\\\"" + juce::String::repeatedString("[", 1000) + "\"}]}";
    const auto parsed = bankFromJson(inString);
    REQUIRE(parsed.bank.has_value());
    CHECK(parsed.bank->presets.size() == 1);
}

TEST_CASE("BankFormat: a newer version is flagged tooNew")
{
    const auto r = bankFromJson("{\"version\": 2, \"presets\": []}");
    CHECK_FALSE(r.bank.has_value());
    CHECK(r.tooNew);
}

TEST_CASE("BankFormat: loading applies the state sanitizing")
{
    const auto json = juce::String(
        "{\"version\":1,\"presets\":[{\"name\":\"  P\\u0001x  \","
        "\"loop1\":{\"path\":\"a.wav\",\"bars\":99999,\"boundaries\":[0.5,0.5,-1,2,0.25],"
        "\"labels\":{\"0\":\"A very long label that is cut\",\"-3\":\"neg\"}},"
        "\"loop2\":{\"bars\":0},"
        "\"rules\":[{\"region1\":-2,\"targets\":[{\"region2\":0,\"weight\":1}]},"
        "{\"region1\":1,\"targets\":[{\"region2\":-1,\"weight\":1},{\"region2\":2,\"weight\":0.9},"
        "{\"region2\":3,\"weight\":0.9,\"mode\":1}]}]}]}");
    const auto r = bankFromJson(json);
    REQUIRE(r.bank.has_value());
    REQUIRE(r.bank->presets.size() == 1);
    const auto& p = r.bank->presets[0];
    CHECK(p.name == "Px");
    CHECK(p.state.loop1.bars == 1024);
    CHECK(p.state.loop2.bars == 1);
    CHECK(p.state.loop1.boundaries == std::vector<double> { 0.25, 0.5 });
    REQUIRE(p.state.loop1.labels.size() == 1);
    CHECK(p.state.loop1.labels[0].length() == kMaxLabelLength);
    REQUIRE(p.state.rules.size() == 1);
    CHECK(p.state.rules[0].region1 == 1);
    REQUIRE(p.state.rules[0].targets.size() == 2);
    float sum = 0.0f;
    for (auto& t : p.state.rules[0].targets)
        sum += t.weight;
    CHECK(sum <= 1.0001f);
    CHECK(p.state.rules[0].targets[1].mode == PlayMode::OneShot);

    juce::String many = "{\"version\":1,\"presets\":[";
    for (int i = 0; i < kMaxPresetsPerBank + 5; ++i)
        many << (i ? "," : "") << "{\"name\":\"p" << i << "\"}";
    many << "]}";
    const auto capped = bankFromJson(many);
    REQUIRE(capped.bank.has_value());
    CHECK(static_cast<int>(capped.bank->presets.size()) == kMaxPresetsPerBank);
}

TEST_CASE("BankFormat: bank names")
{
    const juce::StringArray existing { "jump up" };
    CHECK(bankNameProblem("con", existing).isNotEmpty());
    CHECK(bankNameProblem("CON.txt", existing).isNotEmpty());
    CHECK(bankNameProblem("a:b", existing).isNotEmpty());
    CHECK(bankNameProblem("x.", existing).isNotEmpty());
    CHECK(bankNameProblem("x ", existing).isNotEmpty());
    CHECK(bankNameProblem("  ", existing).isNotEmpty());
    CHECK(bankNameProblem("", existing).isNotEmpty());
    CHECK(bankNameProblem("lpt3", existing).isNotEmpty());
    CHECK(bankNameProblem("a|b", existing).isNotEmpty());
    CHECK(bankNameProblem("a\tb", existing).isNotEmpty());
    CHECK(bankNameProblem(juce::String::repeatedString("x", 65), existing).isNotEmpty());
    CHECK(bankNameProblem("JUMP up", existing).isNotEmpty());
    CHECK(bankNameProblem("ok name", existing).isEmpty());
    CHECK(bankNameProblem(juce::String::repeatedString("x", 64), existing).isEmpty());
    CHECK(bankNameProblem("console", existing).isEmpty());
}

TEST_CASE("BankFormat: preset names")
{
    Bank bank;
    bank.presets.push_back(makePreset("Drop One", "a.wav"));
    CHECK(presetNameProblem("", bank).isNotEmpty());
    CHECK(presetNameProblem("   ", bank).isNotEmpty());
    CHECK(presetNameProblem(juce::String::repeatedString("x", 33), bank).isNotEmpty());
    CHECK(presetNameProblem("DROP one", bank).isNotEmpty());
    CHECK(presetNameProblem(juce::String::repeatedString("x", 32), bank).isEmpty());
    CHECK(presetNameProblem("Drop Two", bank).isEmpty());
}

TEST_CASE("BankFormat: fingerprint ignores paths and active fields but not rules")
{
    const auto a = makePreset("A", "loops/a.wav").state;
    auto b = a;
    b.loop1.path = "elsewhere/x.wav";
    b.loop1.fileName = "x.wav";
    b.loop2.path = "";
    b.activeBank = "bank";
    b.activePreset = "preset";
    CHECK(presetFingerprint(a) == presetFingerprint(b));

    auto c = a;
    c.rules[0].targets[0].weight = 0.4f;
    CHECK(presetFingerprint(a) != presetFingerprint(c));
    auto d = a;
    d.rules[0].targets[1].mode = PlayMode::Gate;
    CHECK(presetFingerprint(a) != presetFingerprint(d));
    auto e = a;
    e.loop1.labels[0] = "Other";
    CHECK(presetFingerprint(a) != presetFingerprint(e));
}
