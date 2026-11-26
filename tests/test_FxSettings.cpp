#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include "engine/FxSettings.h"

using Catch::Approx;
using namespace dc;

TEST_CASE("FxSettings: defaults match the spec table", "[FxSettings]")
{
    FxParams p;
    CHECK(p.get(FxParam::ReverbMix) == Approx(0.3f));
    CHECK(p.get(FxParam::ReverbSize) == Approx(0.5f));
    CHECK(p.get(FxParam::ReverbDamping) == Approx(0.5f));
    CHECK(p.get(FxParam::FilterType) == Approx(0.0f));
    CHECK(p.get(FxParam::FilterCutoff) == Approx(2000.0f));
    CHECK(p.get(FxParam::FilterResonance) == Approx(0.2f));
    CHECK(p.get(FxParam::BitcrushBits) == Approx(8.0f));
    CHECK(p.get(FxParam::BitcrushRate) == Approx(4.0f));
    CHECK(p.get(FxParam::BitcrushMix) == Approx(1.0f));
    CHECK(p.get(FxParam::FlangerRate) == Approx(0.3f));
    CHECK(p.get(FxParam::FlangerDepth) == Approx(0.5f));
    CHECK(p.get(FxParam::FlangerFeedback) == Approx(0.4f));
    CHECK(p.get(FxParam::FlangerMix) == Approx(0.5f));
}

TEST_CASE("FxSettings: sanitize clamps, rounds the filter type and replaces NaN and infinity by the default", "[FxSettings]")
{
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(sanitizeFxParam(FxParam::ReverbMix, 5.0) == Approx(1.0f));
    CHECK(sanitizeFxParam(FxParam::ReverbMix, -1.0) == Approx(0.0f));
    CHECK(sanitizeFxParam(FxParam::FilterCutoff, 1.0) == Approx(20.0f));
    CHECK(sanitizeFxParam(FxParam::FilterCutoff, 1e9) == Approx(20000.0f));
    CHECK(sanitizeFxParam(FxParam::FlangerFeedback, -5.0) == Approx(-0.9f));
    CHECK(sanitizeFxParam(FxParam::FilterType, 1.4) == Approx(1.0f));
    CHECK(sanitizeFxParam(FxParam::FilterType, 1.6) == Approx(2.0f));
    CHECK(sanitizeFxParam(FxParam::FilterType, 7.0) == Approx(5.0f));
    CHECK(sanitizeFxParam(FxParam::FilterType, -3.0) == Approx(0.0f));
    CHECK(sanitizeFxParam(FxParam::FilterCutoff, std::nan("")) == Approx(2000.0f));
    CHECK(sanitizeFxParam(FxParam::BitcrushBits, inf) == Approx(8.0f));
    CHECK(sanitizeFxParam(FxParam::BitcrushBits, -inf) == Approx(8.0f));

    FxParams p;
    p.set(FxParam::ReverbSize, 9.0);
    CHECK(p.get(FxParam::ReverbSize) == Approx(1.0f));
    CHECK_FALSE(p == FxParams());
}

TEST_CASE("FxSettings: masks default to 0 and unknown regions read 0", "[FxSettings]")
{
    FxSettings s;
    CHECK(s.maskFor(0) == 0);
    CHECK(s.maskFor(5) == 0);
    CHECK(s.maskFor(-1) == 0);
    CHECK_FALSE(s.switchOn(0, FxInsert::Reverb));
    CHECK(s.switches.empty());
}

TEST_CASE("FxSettings: setSwitch grows, ignores negative and huge indices", "[FxSettings]")
{
    FxSettings s;
    s.setSwitch(2, FxInsert::Filter, true);
    REQUIRE(s.switches.size() == 3);
    CHECK(s.maskFor(2) == 2);
    CHECK(s.switchOn(2, FxInsert::Filter));
    s.setSwitch(2, FxInsert::Reverb, true);
    CHECK(s.maskFor(2) == (2 | 8));
    s.setSwitch(2, FxInsert::Filter, false);
    CHECK(s.maskFor(2) == 8);

    s.setSwitch(-1, FxInsert::Bitcrush, true);
    s.setSwitch(kMaxFxRegions, FxInsert::Bitcrush, true);
    CHECK(s.switches.size() == 3);
}

TEST_CASE("FxSettings: trimTo drops surplus entries and trailing zeros", "[FxSettings]")
{
    FxSettings s;
    s.switches = { 1, 0, 3, 0, 0 };
    s.trimTo(4);
    CHECK(s.switches == std::vector<std::uint8_t>{ 1, 0, 3 });
    s.trimTo(2);
    CHECK(s.switches == std::vector<std::uint8_t>{ 1 });
    s.trimTo(0);
    CHECK(s.switches.empty());
}

TEST_CASE("FxSettings: every FxParamInfo id is unique and min <= def <= max", "[FxSettings]")
{
    std::set<std::string> ids;
    for (int i = 0; i < kFxParamCount; ++i)
    {
        const auto& info = fxParamInfo(static_cast<FxParam>(i));
        CHECK(ids.insert(info.id).second);
        CHECK(info.min <= info.def);
        CHECK(info.def <= info.max);
    }
    CHECK(ids.size() == static_cast<size_t>(kFxParamCount));
}

TEST_CASE("FxSettings: default order is Bitcrush, Filter, Flanger, Delay, Reverb", "[FxSettings]")
{
    const FxOrder o = defaultFxOrder();
    CHECK(o == FxOrder { FxInsert::Bitcrush, FxInsert::Filter, FxInsert::Flanger, FxInsert::Delay, FxInsert::Reverb });
    CHECK(FxSettings {}.order == o);
}

TEST_CASE("FxSettings: sanitizeFxOrder keeps a valid permutation and replaces duplicates with the default", "[FxSettings]")
{
    const FxOrder valid { FxInsert::Reverb, FxInsert::Delay, FxInsert::Flanger, FxInsert::Filter, FxInsert::Bitcrush };
    CHECK(sanitizeFxOrder(valid) == valid);
    const FxOrder dup { FxInsert::Filter, FxInsert::Filter, FxInsert::Flanger, FxInsert::Delay, FxInsert::Reverb };
    CHECK(sanitizeFxOrder(dup) == defaultFxOrder());
    const FxOrder bad { FxInsert::Bitcrush, FxInsert::Filter, FxInsert::Flanger, FxInsert::Delay, static_cast<FxInsert>(9) };
    CHECK(sanitizeFxOrder(bad) == defaultFxOrder());
    const FxOrder neg { FxInsert::Bitcrush, FxInsert::Filter, FxInsert::Flanger, FxInsert::Delay, static_cast<FxInsert>(-1) };
    CHECK(sanitizeFxOrder(neg) == defaultFxOrder());
}

TEST_CASE("FxSettings: Delay switch uses bit 4", "[FxSettings]")
{
    FxSettings s;
    s.setSwitch(0, FxInsert::Delay, true);
    CHECK(s.maskFor(0) == 16);
    CHECK(s.switchOn(0, FxInsert::Delay));
}

TEST_CASE("FxSettings: flanger.rate range is 0..5 and default 0.3", "[FxSettings]")
{
    const auto& i = fxParamInfo(FxParam::FlangerRate);
    CHECK(i.min == Approx(0.0));
    CHECK(i.max == Approx(5.0));
    CHECK(i.def == Approx(0.3));
}

TEST_CASE("FxSettings: delay time steps are ascending in length and contain the required values", "[FxSettings]")
{
    CHECK(kDelayTimeSteps > 0);
    std::set<std::string> names;
    for (int s = 0; s < kDelayTimeSteps; ++s)
    {
        names.insert(delayTimeName(s));
        if (s > 0)
            CHECK(delayTimeBeats(s, 4.0) > delayTimeBeats(s - 1, 4.0));
    }
    for (const char* n : { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "1/8 dot", "1/8 trip" })
        CHECK(names.count(n) == 1);
    auto find = [](const char* n) {
        for (int s = 0; s < kDelayTimeSteps; ++s)
            if (std::string(delayTimeName(s)) == n) return s;
        return -1;
    };
    CHECK(delayTimeBeats(find("2 bars"), 4.0) == Approx(8.0));
    CHECK(delayTimeBeats(find("2 bars"), 6.0) == Approx(12.0));
    CHECK(delayTimeBeats(find("1 bar"), 3.0) == Approx(3.0));
    CHECK(delayTimeBeats(find("1/8 trip"), 4.0) == Approx(1.0 / 3.0));
    CHECK(delayTimeBeats(find("1/8 dot"), 4.0) == Approx(0.75));
    CHECK(delayTimeBeats(find("1/4"), 6.0) == Approx(1.0));
    CHECK(delayTimeBeats(find("1/32"), 4.0) == Approx(0.125));
}

TEST_CASE("FxSettings: new params have their defaults and ranges", "[FxSettings]")
{
    FxParams p;
    CHECK(p.get(FxParam::FlangerManual) == Approx(0.5f));
    CHECK(p.get(FxParam::DelayFeedback) == Approx(0.4f));
    CHECK(p.get(FxParam::DelayMix) == Approx(0.3f));
    CHECK(p.get(FxParam::DelayReverse) == Approx(0.0f));
    const int def = static_cast<int>(p.get(FxParam::DelayTime));
    CHECK(std::string(delayTimeName(def)) == "1/8");
    CHECK(kFxParamCount == 21);
    CHECK(std::string(fxParamInfo(FxParam::FlangerManual).id) == "flanger.manual");
    CHECK(std::string(fxParamInfo(FxParam::DelayTime).id) == "delay.time");
    CHECK(std::string(fxParamInfo(FxParam::DelayFeedback).id) == "delay.feedback");
    CHECK(std::string(fxParamInfo(FxParam::DelayMix).id) == "delay.mix");
    CHECK(std::string(fxParamInfo(FxParam::DelayReverse).id) == "delay.reverse");
    CHECK(fxParamInfo(FxParam::DelayTime).max == kDelayTimeSteps - 1);
}

TEST_CASE("FxSettings: delay.feedback is clamped to 0.9", "[FxSettings]")
{
    FxParams p;
    p.set(FxParam::DelayFeedback, 5.0);
    CHECK(p.get(FxParam::DelayFeedback) == Approx(0.9f));
    p.set(FxParam::DelayReverse, 0.7);
    CHECK(p.get(FxParam::DelayReverse) == Approx(1.0f));
}

TEST_CASE("FxSettings: equality compares the order", "[FxSettings]")
{
    FxSettings a, b;
    CHECK(a == b);
    b.order = { FxInsert::Reverb, FxInsert::Delay, FxInsert::Flanger, FxInsert::Filter, FxInsert::Bitcrush };
    CHECK_FALSE(a == b);
}

TEST_CASE("FxSettings: filter LFO defaults and ranges", "[FxSettings]")
{
    FxParams p;
    CHECK(p.get(FxParam::FilterLfoRate) == Approx(8.0f));
    CHECK(p.get(FxParam::FilterLfoDepth) == Approx(0.0f));
    CHECK(p.get(FxParam::FilterLfoShape) == Approx(0.0f));
    CHECK(fxParamInfo(FxParam::FilterType).max == 5);
    CHECK(fxParamInfo(FxParam::FilterType).steps == 6);
    CHECK(std::string(fxParamInfo(FxParam::FilterLfoRate).id) == "filter.lfoRate");
    CHECK(std::string(fxParamInfo(FxParam::FilterLfoDepth).id) == "filter.lfoDepth");
    CHECK(std::string(fxParamInfo(FxParam::FilterLfoShape).id) == "filter.lfoShape");
    CHECK(fxParamInfo(FxParam::FilterLfoRate).max == kDelayTimeSteps - 1);
    CHECK(fxParamInfo(FxParam::FilterLfoRate).steps == kDelayTimeSteps);
    CHECK(fxParamInfo(FxParam::FilterLfoDepth).steps == 0);
    CHECK(fxParamInfo(FxParam::FilterLfoShape).max == 3);
    CHECK(fxParamInfo(FxParam::FilterLfoShape).steps == 4);
    CHECK(fxParamInfo(FxParam::FilterLfoRate).insert == FxInsert::Filter);
}

TEST_CASE("sanitizeFxParam: FilterType rounds into 0..5, LFO steps round, depth clamps", "[FxSettings]")
{
    CHECK(sanitizeFxParam(FxParam::FilterType, 9.0) == Approx(5.0f));
    CHECK(sanitizeFxParam(FxParam::FilterType, 4.4) == Approx(4.0f));
    CHECK(sanitizeFxParam(FxParam::FilterType, -1.0) == Approx(0.0f));
    CHECK(sanitizeFxParam(FxParam::FilterLfoRate, 99.0) == Approx(14.0f));
    CHECK(sanitizeFxParam(FxParam::FilterLfoShape, 3.6) == Approx(3.0f));
    CHECK(sanitizeFxParam(FxParam::FilterLfoDepth, 2.0) == Approx(1.0f));
    CHECK(sanitizeFxParam(FxParam::FilterLfoRate, std::nan("")) == Approx(8.0f));
    CHECK(sanitizeFxParam(FxParam::FilterLfoDepth, std::nan("")) == Approx(0.0f));
    CHECK(sanitizeFxParam(FxParam::FilterLfoShape, std::nan("")) == Approx(0.0f));
}
