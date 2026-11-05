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
    CHECK(sanitizeFxParam(FxParam::FilterType, 7.0) == Approx(2.0f));
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
