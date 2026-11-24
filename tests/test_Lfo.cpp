#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include "engine/fx/Lfo.h"

using namespace dc::fx;

TEST_CASE("Lfo: every shape peaks at phase 0 and has the documented values at 0, 0.25, 0.5, 0.75", "[Lfo]")
{
    struct Row { LfoShape shape; float v[4]; };
    const Row rows[] = {
        {LfoShape::Sine, {1.0f, 0.0f, -1.0f, 0.0f}},
        {LfoShape::Triangle, {1.0f, 0.0f, -1.0f, 0.0f}},
        {LfoShape::SawDown, {1.0f, 0.5f, 0.0f, -0.5f}},
        {LfoShape::Square, {1.0f, 1.0f, -1.0f, -1.0f}},
    };
    const double phases[4] = {0.0, 0.25, 0.5, 0.75};
    for (const auto& row : rows)
    {
        INFO("shape " << static_cast<int>(row.shape));
        for (int i = 0; i < 4; ++i)
        {
            INFO("phase " << phases[i]);
            CHECK(std::abs(lfoValue(row.shape, phases[i]) - row.v[i]) < 1e-6f);
        }
        // Phase 0 is the maximum: no phase on a fine grid is above it.
        for (int k = 0; k < 1000; ++k)
        {
            const float v = lfoValue(row.shape, k / 1000.0);
            REQUIRE(v <= lfoValue(row.shape, 0.0));
            REQUIRE(v >= -1.0f);
        }
    }
}

TEST_CASE("lfoPhase: wraps, handles negative beats and invalid input", "[Lfo]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(lfoPhase(0.0, 1.0) == 0.0);
    CHECK(lfoPhase(0.25, 1.0) == 0.25);
    CHECK(lfoPhase(4.5, 1.0) == 0.5);
    CHECK(lfoPhase(3.0, 2.0) == 0.5);
    CHECK(std::abs(lfoPhase(1.0, 4.0 / 3.0) - 0.75) < 1e-12);
    CHECK(lfoPhase(-0.25, 1.0) == 0.75);
    CHECK(lfoPhase(-4.0, 1.0) == 0.0);
    CHECK(lfoPhase(-1.0, 4.0) == 0.75);
    CHECK(lfoPhase(nan, 1.0) == 0.0);
    CHECK(lfoPhase(inf, 1.0) == 0.0);
    CHECK(lfoPhase(1.5, nan) == 0.0);
    CHECK(lfoPhase(3.0, 0.0) == 0.0);
    CHECK(lfoPhase(3.0, -1.0) == 0.0);
    CHECK(lfoPhase(1.0, 1e-320) == 0.0); // beats / cycle overflows to infinity
    // Always in [0, 1), also just below a cycle boundary.
    for (double b : {-1e-17, -1e-9, 0.9999999999999999, 1e9 + 0.3, -1e9 - 0.3})
    {
        INFO("beats " << b);
        const double p = lfoPhase(b, 1.0);
        CHECK(p >= 0.0);
        CHECK(p < 1.0);
    }
}
