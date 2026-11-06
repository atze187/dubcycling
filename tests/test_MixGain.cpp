#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <limits>
#include "engine/MixGain.h"

using Catch::Approx;

TEST_CASE("MixGain: the centre keeps both loops at full level", "[MixGain]")
{
    const auto g = dc::mixGains(0.5f);
    REQUIRE(g.loop1 == Approx(1.0f));
    REQUIRE(g.loop2 == Approx(1.0f));
}

TEST_CASE("MixGain: the ends solo one loop", "[MixGain]")
{
    const auto a = dc::mixGains(0.0f);
    REQUIRE(a.loop1 == Approx(1.0f));
    REQUIRE(a.loop2 == Approx(0.0f));
    const auto b = dc::mixGains(1.0f);
    REQUIRE(b.loop1 == Approx(0.0f));
    REQUIRE(b.loop2 == Approx(1.0f));
}

TEST_CASE("MixGain: left of centre only loop 2 gets quieter, right of centre only loop 1", "[MixGain]")
{
    const auto l = dc::mixGains(0.25f);
    REQUIRE(l.loop1 == Approx(1.0f));
    REQUIRE(l.loop2 == Approx(0.5f));
    const auto r = dc::mixGains(0.75f);
    REQUIRE(r.loop1 == Approx(0.5f));
    REQUIRE(r.loop2 == Approx(1.0f));
}

TEST_CASE("MixGain: NaN, infinity and out-of-range values are sanitized", "[MixGain]")
{
    const float inf = std::numeric_limits<float>::infinity();
    REQUIRE(dc::sanitizeMix(std::numeric_limits<float>::quiet_NaN()) == 0.5f);
    REQUIRE(dc::sanitizeMix(inf) == 0.5f);
    REQUIRE(dc::sanitizeMix(-inf) == 0.5f);
    REQUIRE(dc::sanitizeMix(-3.0f) == 0.0f);
    REQUIRE(dc::sanitizeMix(7.0f) == 1.0f);
    REQUIRE(dc::sanitizeMix(0.3f) == 0.3f);
    REQUIRE(dc::mixGains(std::numeric_limits<float>::quiet_NaN()).loop2 == Approx(1.0f));
}
