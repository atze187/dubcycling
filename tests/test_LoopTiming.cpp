#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <limits>
#include "engine/LoopTiming.h"

using Catch::Approx;

TEST_CASE("LoopTiming beatsPerBar", "[LoopTiming]")
{
    REQUIRE(dc::beatsPerBar(4, 4) == Approx(4.0));
    REQUIRE(dc::beatsPerBar(6, 8) == Approx(3.0));
    REQUIRE(dc::beatsPerBar(0, 4) == Approx(4.0));
    REQUIRE(dc::beatsPerBar(4, 0) == Approx(4.0));
    REQUIRE(dc::beatsPerBar(-3, 4) == Approx(4.0));
}

TEST_CASE("LoopTiming suggestBars", "[LoopTiming]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    REQUIRE(dc::suggestBars(8.0, 120, 4) == 4);
    REQUIRE(dc::suggestBars(0.01, 120, 4) == 1);
    REQUIRE(dc::suggestBars(8.0, 0, 4) == 1);
    REQUIRE(dc::suggestBars(8.0, nan, 4) == 1);
    REQUIRE(dc::suggestBars(nan, 120, 4) == 1);
    REQUIRE(dc::suggestBars(inf, 120, 4) == 1);
    REQUIRE(dc::suggestBars(8.0, 120, 0) == 1);
    REQUIRE(dc::suggestBars(-1.0, 120, 4) == 1);
    REQUIRE(dc::suggestBars(6.0, 120, 3) == 4); // 12 beats / 3
}

TEST_CASE("LoopTiming lcmBars", "[LoopTiming]")
{
    REQUIRE(dc::lcmBars(3, 4) == 12);
    REQUIRE(dc::lcmBars(4, 4) == 4);
    REQUIRE(dc::lcmBars(0, 4) == 4);
    REQUIRE(dc::lcmBars(-2, 3) == 3);
}

TEST_CASE("LoopTiming sourceStep", "[LoopTiming]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(dc::sourceStep(192000, 8.0, 120, 48000) == Approx(1.0));
    REQUIRE(dc::sourceStep(96000, 8.0, 120, 48000) == Approx(0.5));
    REQUIRE(dc::sourceStep(192000, 8.0, 1200, 48000) == Approx(4.0));
    REQUIRE(dc::sourceStep(192000, 8.0, 1.0, 48000) == Approx(0.25));
    REQUIRE(dc::sourceStep(192000, 8.0, 0, 48000) == 0.0);
    REQUIRE(dc::sourceStep(192000, 8.0, nan, 48000) == 0.0);
    REQUIRE(dc::sourceStep(192000, 8.0, 120, 0) == 0.0);
    REQUIRE(dc::sourceStep(192000, 8.0, 120, nan) == 0.0);
    REQUIRE(dc::sourceStep(0, 8.0, 120, 48000) == 0.0);
    REQUIRE(dc::sourceStep(192000, 0.0, 120, 48000) == 0.0);
    REQUIRE(dc::sourceStep(192000, nan, 120, 48000) == 0.0);
}
