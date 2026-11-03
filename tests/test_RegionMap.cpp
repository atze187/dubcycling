#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <limits>
#include "engine/RegionMap.h"

using Catch::Approx;

TEST_CASE("RegionMap no boundaries is one region", "[RegionMap]")
{
    dc::RegionMap m;
    REQUIRE(m.regionCount() == 1);
    REQUIRE(m.regionAt(0.0) == 0);
    REQUIRE(m.regionAt(0.7) == 0);
    auto b = m.bounds(0);
    REQUIRE(b.first == 0.0);
    REQUIRE(b.second == 1.0);
    auto bad = m.bounds(1);
    REQUIRE(bad.first == 0.0);
    REQUIRE(bad.second == 0.0);
    REQUIRE(m.bounds(-1).second == 0.0);
}

TEST_CASE("RegionMap add keeps sorted order", "[RegionMap]")
{
    dc::RegionMap m;
    REQUIRE(m.addBoundary(0.5));
    REQUIRE(m.addBoundary(0.25));
    REQUIRE(m.addBoundary(0.75));
    REQUIRE(m.regionCount() == 4);
    REQUIRE(m.boundaries == std::vector<double>{0.25, 0.5, 0.75});
    REQUIRE(m.bounds(1).first == Approx(0.25));
    REQUIRE(m.bounds(1).second == Approx(0.5));
    REQUIRE(m.bounds(3).second == 1.0);
}

TEST_CASE("RegionMap add at 0.0 or 1.0 is rejected", "[RegionMap]")
{
    dc::RegionMap m;
    REQUIRE_FALSE(m.addBoundary(0.0));
    REQUIRE_FALSE(m.addBoundary(1.0));
    REQUIRE_FALSE(m.addBoundary(-0.1));
    REQUIRE_FALSE(m.addBoundary(1.5));
    REQUIRE_FALSE(m.addBoundary(std::numeric_limits<double>::quiet_NaN()));
    REQUIRE_FALSE(m.addBoundary(0.001)); // closer than kMinRegion to the start
    REQUIRE_FALSE(m.addBoundary(0.999));
    REQUIRE(m.boundaries.empty());
}

TEST_CASE("RegionMap add closer than kMinRegion to a neighbour is rejected", "[RegionMap]")
{
    dc::RegionMap m;
    REQUIRE(m.addBoundary(0.5));
    REQUIRE_FALSE(m.addBoundary(0.5));
    REQUIRE_FALSE(m.addBoundary(0.5 + dc::kMinRegion * 0.5));
    REQUIRE_FALSE(m.addBoundary(0.5 - dc::kMinRegion * 0.5));
    REQUIRE(m.addBoundary(0.5 + dc::kMinRegion * 2.0));
    REQUIRE(m.boundaries.size() == 2);
}

TEST_CASE("RegionMap move is clamped between neighbours", "[RegionMap]")
{
    dc::RegionMap m;
    m.addBoundary(0.25);
    m.addBoundary(0.5);
    m.addBoundary(0.75);
    REQUIRE(m.moveBoundary(1, 0.6));
    REQUIRE(m.boundaries[1] == Approx(0.6));
    REQUIRE(m.moveBoundary(1, 0.9)); // clamped to next - kMinRegion
    REQUIRE(m.boundaries[1] == Approx(0.75 - dc::kMinRegion));
    REQUIRE(m.moveBoundary(1, 0.0)); // clamped to prev + kMinRegion
    REQUIRE(m.boundaries[1] == Approx(0.25 + dc::kMinRegion));
    REQUIRE(m.moveBoundary(0, -1.0)); // clamped to kMinRegion from 0
    REQUIRE(m.boundaries[0] == Approx(dc::kMinRegion));
    REQUIRE(m.moveBoundary(2, 2.0));
    REQUIRE(m.boundaries[2] == Approx(1.0 - dc::kMinRegion));
    REQUIRE_FALSE(m.moveBoundary(3, 0.5));
    REQUIRE_FALSE(m.moveBoundary(-1, 0.5));
    REQUIRE_FALSE(m.moveBoundary(0, std::numeric_limits<double>::quiet_NaN()));
}

TEST_CASE("RegionMap regionAt returns correct index at boundaries and for 1.0", "[RegionMap]")
{
    dc::RegionMap m;
    m.addBoundary(0.25);
    m.addBoundary(0.5);
    REQUIRE(m.regionAt(0.0) == 0);
    REQUIRE(m.regionAt(0.2499) == 0);
    REQUIRE(m.regionAt(0.25) == 1);
    REQUIRE(m.regionAt(0.4999) == 1);
    REQUIRE(m.regionAt(0.5) == 2);
    REQUIRE(m.regionAt(0.9999) == 2);
    REQUIRE(m.regionAt(1.0) == 0);   // wraps
    REQUIRE(m.regionAt(1.3) == 1);
    REQUIRE(m.regionAt(-0.25) == 2); // wraps to 0.75
    REQUIRE(m.regionAt(std::numeric_limits<double>::quiet_NaN()) == 0);
    REQUIRE(m.regionAt(std::numeric_limits<double>::infinity()) == 0);
}

TEST_CASE("RegionMap remove merges regions", "[RegionMap]")
{
    dc::RegionMap m;
    m.addBoundary(0.25);
    m.addBoundary(0.5);
    REQUIRE(m.regionCount() == 3);
    REQUIRE(m.removeBoundary(0));
    REQUIRE(m.regionCount() == 2);
    REQUIRE(m.bounds(0).second == Approx(0.5));
    REQUIRE_FALSE(m.removeBoundary(1));
    REQUIRE_FALSE(m.removeBoundary(-1));
    REQUIRE(m.removeBoundary(0));
    REQUIRE(m.regionCount() == 1);
}
