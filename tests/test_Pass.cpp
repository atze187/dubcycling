#include <catch2/catch_test_macros.hpp>

#include "engine/Pass.h"

#include <cmath>
#include <limits>

using namespace dc;

TEST_CASE("sanitizePass drops invalid events, clamps and sorts", "[pass]")
{
    Pass p;
    p.events.push_back({ 0.5, false, 2, PlayMode::Gate });
    p.events.push_back({ std::numeric_limits<double>::quiet_NaN(), false, 1, PlayMode::Gate });
    p.events.push_back({ 0.25, false, -1, PlayMode::Gate }); // Start without region
    p.events.push_back({ 1.5, true, -1, PlayMode::Gate });   // Stop, clamped
    p.events.push_back({ -0.5, false, 3, PlayMode::OneShot });  // clamped to 0

    const Pass s = sanitizePass(p);
    REQUIRE(s.events.size() == 3);
    CHECK(s.events[0].phase == 0.0);
    CHECK(s.events[0].region2 == 3);
    CHECK(s.events[1].phase == 0.5);
    CHECK(s.events[2].phase == 1.0);
    CHECK(s.events[2].stop);
}

TEST_CASE("sanitizePass caps at kMaxPassEvents", "[pass]")
{
    Pass p;
    for (int i = 0; i < kMaxPassEvents + 10; ++i)
        p.events.push_back({ 0.001 * (i % 100), false, 0, PlayMode::Gate });
    CHECK(static_cast<int>(sanitizePass(p).events.size()) == kMaxPassEvents);
}

TEST_CASE("PassRecorder basics", "[pass]")
{
    PassRecorder r;
    Pass out;
    std::uint32_t id = 0;

    SECTION("read before any finish is false")
    {
        CHECK_FALSE(r.read(out, id));
    }
    SECTION("add before begin is ignored")
    {
        r.add(0.1, false, 0, PlayMode::Gate);
        CHECK_FALSE(r.isOpen());
        r.finish();
        CHECK_FALSE(r.read(out, id));
    }
    SECTION("begin/add/finish publishes with increasing ids")
    {
        r.begin();
        CHECK(r.isOpen());
        r.add(0.1, false, 2, PlayMode::OneShot);
        r.add(0.6, true, -1, PlayMode::Gate);
        r.finish();
        CHECK_FALSE(r.isOpen());
        REQUIRE(r.read(out, id));
        CHECK(id == 1);
        REQUIRE(out.events.size() == 2);
        CHECK(out.events[0].phase == 0.1);
        CHECK(out.events[0].region2 == 2);
        CHECK(out.events[0].mode == PlayMode::OneShot);
        CHECK(out.events[1].stop);

        r.begin();
        r.add(0.3, false, 1, PlayMode::Gate);
        r.finish();
        REQUIRE(r.read(out, id));
        CHECK(id == 2);
        CHECK(out.events.size() == 1);
    }
    SECTION("abandon publishes nothing")
    {
        r.begin();
        r.add(0.1, false, 0, PlayMode::Gate);
        r.abandon();
        CHECK_FALSE(r.isOpen());
        r.finish();
        CHECK_FALSE(r.read(out, id));
    }
    SECTION("overflow invalidates the pass")
    {
        r.begin();
        for (int i = 0; i < kMaxPassEvents + 1; ++i)
            r.add(0.5, false, 0, PlayMode::Gate);
        r.finish();
        CHECK_FALSE(r.read(out, id));
    }
    SECTION("begin twice discards the first pass")
    {
        r.begin();
        r.add(0.1, false, 0, PlayMode::Gate);
        r.begin();
        r.add(0.9, false, 4, PlayMode::Gate);
        r.finish();
        REQUIRE(r.read(out, id));
        REQUIRE(out.events.size() == 1);
        CHECK(out.events[0].phase == 0.9);
        CHECK(id == 1);
    }
}
