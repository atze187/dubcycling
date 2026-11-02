#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <limits>
#include <random>
#include "engine/RuleSet.h"

using Catch::Approx;

namespace {
dc::RuleTarget T(int r, float w, dc::PlayMode m = dc::PlayMode::Gate) { return { r, w, m }; }
}

TEST_CASE("RuleSet no rule draws nothing", "[RuleSet]")
{
    dc::RuleSet rs;
    REQUIRE(rs.targets(0).empty());
    REQUIRE(rs.targets(-3).empty());
    REQUIRE(rs.targets(100000).empty());
    REQUIRE_FALSE(rs.draw(0, 0.0f).has_value());
    REQUIRE_FALSE(rs.draw(5, 0.5f).has_value());
    rs.setTargets(-1, { T(1, 1.0f) }); // ignored
    REQUIRE_FALSE(rs.draw(-1, 0.1f).has_value());
}

TEST_CASE("RuleSet weights 0.5/0.3 draw first for u=0.2, second for u=0.6, nothing for u=0.85", "[RuleSet]")
{
    dc::RuleSet rs;
    rs.setTargets(1, { T(2, 0.5f, dc::PlayMode::Gate), T(3, 0.3f, dc::PlayMode::OneShot) });
    auto a = rs.draw(1, 0.2f);
    REQUIRE(a.has_value());
    REQUIRE(a->region2 == 2);
    REQUIRE(a->mode == dc::PlayMode::Gate);
    auto b = rs.draw(1, 0.6f);
    REQUIRE(b.has_value());
    REQUIRE(b->region2 == 3);
    REQUIRE(b->mode == dc::PlayMode::OneShot);
    REQUIRE_FALSE(rs.draw(1, 0.85f).has_value());
    REQUIRE_FALSE(rs.draw(0, 0.2f).has_value()); // other region has no rule
}

TEST_CASE("RuleSet weights summing to 1.4 are scaled so the sum is 1", "[RuleSet]")
{
    dc::RuleSet rs;
    rs.setTargets(0, { T(1, 0.7f), T(2, 0.7f) });
    const auto& t = rs.targets(0);
    REQUIRE(t.size() == 2);
    REQUIRE(t[0].weight + t[1].weight == Approx(1.0f));
    REQUIRE(t[0].weight == Approx(0.5f));
    REQUIRE(rs.draw(0, 0.49f)->region2 == 1);
    REQUIRE(rs.draw(0, 0.51f)->region2 == 2);
    REQUIRE(rs.draw(0, 0.999f).has_value());

    rs.setTargets(0, { T(1, 5.0f) }); // clamped to 1
    REQUIRE(rs.targets(0)[0].weight == Approx(1.0f));
}

TEST_CASE("RuleSet negative or NaN weight becomes 0", "[RuleSet]")
{
    dc::RuleSet rs;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    rs.setTargets(0, { T(1, -0.5f), T(2, nan), T(3, 0.4f) });
    const auto& t = rs.targets(0);
    REQUIRE(t[0].weight == 0.0f);
    REQUIRE(t[1].weight == 0.0f);
    REQUIRE(t[2].weight == Approx(0.4f));
    REQUIRE(rs.draw(0, 0.1f)->region2 == 3);
    REQUIRE_FALSE(rs.draw(0, 0.5f).has_value());
}

TEST_CASE("RuleSet u = 0 picks the first non-zero target", "[RuleSet]")
{
    dc::RuleSet rs;
    rs.setTargets(0, { T(1, 0.0f), T(2, 0.5f), T(3, 0.5f) });
    REQUIRE(rs.draw(0, 0.0f)->region2 == 2);
    REQUIRE_FALSE(rs.draw(0, 1.0f).has_value());
    REQUIRE_FALSE(rs.draw(0, -0.1f).has_value());
    REQUIRE_FALSE(rs.draw(0, std::numeric_limits<float>::quiet_NaN()).has_value());
}

TEST_CASE("RuleSet statistical: 10000 draws with 0.7/0.2 are within 3 percent", "[RuleSet]")
{
    dc::RuleSet rs;
    rs.setTargets(0, { T(1, 0.7f), T(2, 0.2f) });
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    int c1 = 0, c2 = 0, none = 0;
    const int n = 10000;
    for (int i = 0; i < n; ++i)
    {
        float u = dist(rng);
        if (u >= 1.0f) u = 0.0f;
        auto r = rs.draw(0, u);
        if (!r) ++none;
        else if (r->region2 == 1) ++c1;
        else ++c2;
    }
    REQUIRE(std::abs(c1 / double(n) - 0.7) < 0.03);
    REQUIRE(std::abs(c2 / double(n) - 0.2) < 0.03);
    REQUIRE(std::abs(none / double(n) - 0.1) < 0.03);
}
