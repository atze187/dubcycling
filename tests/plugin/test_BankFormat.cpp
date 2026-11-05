#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_core/juce_core.h>
#include "engine/FxSettings.h"
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

TEST_CASE("BankFormat: awkward doubles survive the JSON round trip exactly")
{
    // Boundaries come from x / width (e.g. 137 / 700) and need all 17 significant digits.
    Bank bank;
    Preset p;
    p.name = "P";
    p.state.loop1 = { "samples/a.wav", "a.wav", "abc", 2,
                      { 0.012345678901234568, 0.12345678901234568, 137.0 / 700.0, 0.30000000000000004, 2.0 / 3.0,
                        691.0 / 700.0 },
                      {} };
    p.state.loop2 = { "samples/b.wav", "b.wav", "def", 4, { 1.0 / 3.0, 0.5 + 1e-16 * 3 }, {} };
    p.state.rules.push_back({ 0, { { 1, 1.0f / 3.0f, PlayMode::Gate }, { 0, 0.1f, PlayMode::OneShot } } });
    bank.presets.push_back(p);

    const auto r = bankFromJson(bankToJson(bank));
    REQUIRE(r.bank.has_value());
    REQUIRE(r.bank->presets.size() == 1);
    const auto& back = r.bank->presets[0].state;
    CHECK(back.loop1.boundaries == p.state.loop1.boundaries);
    CHECK(back.loop2.boundaries == p.state.loop2.boundaries);
    REQUIRE(back.rules.size() == 1);
    REQUIRE(back.rules[0].targets.size() == 2);
    CHECK(back.rules[0].targets[0].weight == p.state.rules[0].targets[0].weight);
    CHECK(back.rules[0].targets[1].weight == p.state.rules[0].targets[1].weight);
    CHECK(presetFingerprint(back) == presetFingerprint(p.state));
}

TEST_CASE("BankFormat: entries that reading skips are counted")
{
    CHECK(bankFromJson(bankToJson(sampleBank())).skipped == 0);
    CHECK(bankFromJson("{\"version\":1,\"presets\":[]}").skipped == 0);

    const auto nonObject = bankFromJson("{\"version\":1,\"presets\":[{\"name\":\"A\"},5,\"x\"]}");
    REQUIRE(nonObject.bank.has_value());
    CHECK(nonObject.bank->presets.size() == 1);
    CHECK(nonObject.skipped == 2);

    const auto emptyName = bankFromJson("{\"version\":1,\"presets\":[{\"name\":\"A\"},{\"name\":\"  \"},{}]}");
    REQUIRE(emptyName.bank.has_value());
    CHECK(emptyName.bank->presets.size() == 1);
    CHECK(emptyName.skipped == 2);

    juce::String many = "{\"version\":1,\"presets\":[";
    for (int i = 0; i < kMaxPresetsPerBank + 1; ++i)
        many << (i ? "," : "") << "{\"name\":\"p" << i << "\"}";
    many << "]}";
    const auto over = bankFromJson(many);
    REQUIRE(over.bank.has_value());
    CHECK(static_cast<int>(over.bank->presets.size()) == kMaxPresetsPerBank);
    CHECK(over.skipped == 1);
}

TEST_CASE("BankFormat: huge numbers are clamped, not cast")
{
    const auto json = juce::String(
        "{\"version\":1,\"presets\":[{\"name\":\"P\","
        "\"loop1\":{\"bars\":1e30},\"loop2\":{\"bars\":-1e30},"
        "\"rules\":[{\"region1\":-1e30,\"targets\":[{\"region2\":0,\"weight\":1}]},"
        "{\"region1\":1e30,\"targets\":[{\"region2\":0,\"weight\":1}]},"
        "{\"region1\":1,\"targets\":[{\"region2\":-1e30,\"weight\":1},{\"region2\":1e30,\"weight\":1e300,\"mode\":1e30},"
        "{\"region2\":2,\"weight\":-1e300,\"mode\":1}]}]}]}");
    const auto r = bankFromJson(json);
    REQUIRE(r.bank.has_value());
    REQUIRE(r.bank->presets.size() == 1);
    const auto& s = r.bank->presets[0].state;
    CHECK(s.loop1.bars == 1024);
    CHECK(s.loop2.bars == 1);
    REQUIRE(s.rules.size() == 1);
    CHECK(s.rules[0].region1 == 1);
    for (auto& t : s.rules[0].targets)
    {
        CHECK(t.region2 >= 0);
        CHECK(t.weight >= 0.0f);
        CHECK(t.weight <= 1.0f);
    }

    const auto tooNew = bankFromJson("{\"version\":1e30,\"presets\":[]}");
    CHECK(tooNew.tooNew);
    CHECK_FALSE(tooNew.bank.has_value());
    const auto negative = bankFromJson("{\"version\":-1e30,\"presets\":[]}");
    CHECK_FALSE(negative.tooNew);
    CHECK_FALSE(negative.bank.has_value());
}

TEST_CASE("BankFormat: json carries version and a presets list")
{
    const auto v = juce::JSON::parse(bankToJson(sampleBank()));
    REQUIRE(v.isObject());
    CHECK(static_cast<int>(v["version"]) == 2);
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
    const auto r = bankFromJson("{\"version\": 3, \"presets\": []}");
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

namespace {
Preset fxPreset()
{
    auto p = makePreset("Fx", "loops/a.wav");
    p.state.loop2.boundaries = { 0.25, 0.5 }; // 3 regions
    p.state.fx.params.set(FxParam::ReverbMix, 0.37);
    p.state.fx.params.set(FxParam::FilterCutoff, 1234.5);
    p.state.fx.params.set(FxParam::FilterType, 2);
    p.state.fx.setSwitch(0, FxInsert::Reverb, true);
    p.state.fx.setSwitch(2, FxInsert::Bitcrush, true);
    p.state.fx.setSwitch(2, FxInsert::Filter, true);
    return p;
}

BankParseResult parseOnePreset(const juce::String& fxJson, const juce::String& loop2 = "{\"boundaries\":[0.5]}")
{
    return bankFromJson("{\"version\":2,\"presets\":[{\"name\":\"P\",\"loop2\":" + loop2 + ",\"fx\":" + fxJson + "}]}");
}
}

TEST_CASE("Bank: bankToJson writes version 2")
{
    CHECK(kBankVersion == 2);
    Bank b;
    b.presets.push_back(makePreset("A", "a.wav"));
    const auto v = juce::JSON::parse(bankToJson(b));
    CHECK(static_cast<int>(v["version"]) == 2);
    CHECK(v["presets"][0]["fx"].isObject()); // written even for default fx
    CHECK(v["presets"][0]["fx"]["params"].isObject());
    CHECK(v["presets"][0]["fx"]["switches"].isObject());
}

TEST_CASE("Bank: version 2 round trips fx params and switches")
{
    Bank bank;
    bank.presets.push_back(fxPreset());
    const auto json = bankToJson(bank);
    const auto v = juce::JSON::parse(json);
    CHECK(static_cast<double>(v["presets"][0]["fx"]["params"][fxParamInfo(FxParam::ReverbMix).id]) == Catch::Approx(0.37).margin(1e-6));
    CHECK(static_cast<int>(v["presets"][0]["fx"]["switches"]["2"]) == 3);

    const auto r = bankFromJson(json);
    REQUIRE(r.bank.has_value());
    REQUIRE(r.bank->presets.size() == 1);
    CHECK(r.bank->presets[0].state.fx == bank.presets[0].state.fx);
    CHECK(presetFingerprint(r.bank->presets[0].state) == presetFingerprint(bank.presets[0].state));
}

TEST_CASE("Bank: a version 1 file still loads, with default fx")
{
    const auto r = bankFromJson(
        "{\"version\":1,\"presets\":[{\"name\":\"Old\",\"loop1\":{\"bars\":2},\"loop2\":{\"boundaries\":[0.5]},\"rules\":[]}]}");
    REQUIRE(r.bank.has_value());
    CHECK_FALSE(r.tooNew);
    REQUIRE(r.bank->presets.size() == 1);
    CHECK(r.bank->presets[0].state.fx == FxSettings {});
    CHECK(r.bank->version == 2); // saving it writes version 2

    const auto noFx = bankFromJson("{\"version\":2,\"presets\":[{\"name\":\"X\"}]}");
    REQUIRE(noFx.bank.has_value());
    CHECK(noFx.bank->presets[0].state.fx == FxSettings {});
}

TEST_CASE("Bank: a version 3 file is refused as too new")
{
    const auto r = bankFromJson("{\"version\":3,\"presets\":[{\"name\":\"X\"}]}");
    CHECK(r.tooNew);
    CHECK_FALSE(r.bank.has_value());
}

TEST_CASE("Bank: invalid fx numbers are sanitized")
{
    const auto cutoffId = juce::String(fxParamInfo(FxParam::FilterCutoff).id);
    const auto mixId = juce::String(fxParamInfo(FxParam::ReverbMix).id);
    const auto sizeId = juce::String(fxParamInfo(FxParam::ReverbSize).id);
    const auto dampId = juce::String(fxParamInfo(FxParam::ReverbDamping).id);
    const auto r = parseOnePreset("{\"params\":{\"" + cutoffId + "\":\"abc\",\"" + mixId + "\":null,\"" + sizeId + "\":1e99,\""
                                  + dampId + "\":-1e99},\"switches\":{}}");
    REQUIRE(r.bank.has_value());
    const auto& fx = r.bank->presets[0].state.fx;
    CHECK(fx.params.get(FxParam::FilterCutoff) == sanitizeFxParam(FxParam::FilterCutoff, fxParamInfo(FxParam::FilterCutoff).def));
    CHECK(fx.params.get(FxParam::ReverbMix) == sanitizeFxParam(FxParam::ReverbMix, fxParamInfo(FxParam::ReverbMix).def));
    CHECK(fx.params.get(FxParam::ReverbSize) == static_cast<float>(fxParamInfo(FxParam::ReverbSize).max));
    CHECK(fx.params.get(FxParam::ReverbDamping) == static_cast<float>(fxParamInfo(FxParam::ReverbDamping).min));

    // bad switch keys and masks
    const auto s = parseOnePreset(
        "{\"switches\":{\"-3\":5,\"abc\":5,\"100000\":5,\"0\":256,\"1\":-1,\"2\":\"x\"}}", "{\"boundaries\":[0.25,0.5]}");
    REQUIRE(s.bank.has_value());
    const auto& sw = s.bank->presets[0].state.fx;
    CHECK(sw.maskFor(0) == 15);
    CHECK(sw.maskFor(1) == 0);
    CHECK(sw.maskFor(2) == 0);
    CHECK(sw.switches.size() == 1);

    // fx of the wrong shape
    const auto bad = parseOnePreset("[1,2]");
    REQUIRE(bad.bank.has_value());
    CHECK(bad.bank->presets[0].state.fx == FxSettings {});
}

TEST_CASE("Bank: surplus switches beyond the preset's Loop-2 regions are dropped")
{
    const auto r = parseOnePreset("{\"switches\":{\"0\":1,\"1\":2,\"2\":4,\"7\":8}}", "{\"boundaries\":[0.5]}"); // 2 regions
    REQUIRE(r.bank.has_value());
    const auto& fx = r.bank->presets[0].state.fx;
    CHECK(fx.maskFor(0) == 1);
    CHECK(fx.maskFor(1) == 2);
    CHECK(fx.maskFor(2) == 0);
    CHECK(fx.maskFor(7) == 0);
    CHECK(fx.switches.size() <= 2);
}

TEST_CASE("Bank: a changed switch or knob changes the fingerprint, a default fx state does not")
{
    auto base = makePreset("A", "a.wav").state;
    base.loop2.boundaries = { 0.5 };
    auto untouched = base;
    auto explicitDefault = base;
    explicitDefault.fx = FxSettings {};
    explicitDefault.fx.setSwitch(0, FxInsert::Reverb, true);
    explicitDefault.fx.setSwitch(0, FxInsert::Reverb, false);
    explicitDefault.fx.params.set(FxParam::ReverbMix, fxParamInfo(FxParam::ReverbMix).def);
    CHECK(presetFingerprint(untouched) == presetFingerprint(explicitDefault));
    CHECK_FALSE(presetFingerprint(base).contains("F|"));

    auto knob = base;
    knob.fx.params.set(FxParam::ReverbMix, 0.9);
    CHECK(presetFingerprint(knob) != presetFingerprint(base));
    auto sw = base;
    sw.fx.setSwitch(1, FxInsert::Filter, true);
    CHECK(presetFingerprint(sw) != presetFingerprint(base));
    auto sw2 = base;
    sw2.fx.setSwitch(1, FxInsert::Flanger, true);
    CHECK(presetFingerprint(sw2) != presetFingerprint(sw));
}

TEST_CASE("Bank: saving a loaded version 1 bank writes version 2")
{
    const auto r = bankFromJson("{\"version\":1,\"presets\":[{\"name\":\"Old\"}]}");
    REQUIRE(r.bank.has_value());
    CHECK(static_cast<int>(juce::JSON::parse(bankToJson(*r.bank))["version"]) == 2);
}
