#include <catch2/catch_test_macros.hpp>
#include <vector>
#include "engine/Sequencer.h"

using dc::PlayMode;
using Type = dc::Sequencer::Command::Type;

namespace {
dc::RuleTarget T(int r, float w, PlayMode m = PlayMode::Gate) { return { r, w, m }; }

// Fixed sequence of draws; repeats the last value when exhausted.
std::function<float()> seq(std::vector<float> v)
{
    return [v, i = std::size_t(0)]() mutable { float x = v[i < v.size() ? i : v.size() - 1]; ++i; return x; };
}
}

TEST_CASE("Sequencer entry with rule starts target", "[Sequencer]")
{
    dc::RuleSet rs;
    rs.setTargets(1, { T(2, 0.5f, PlayMode::OneShot) });
    dc::Sequencer s;
    s.setRandom(seq({ 0.1f }));
    auto c = s.onRegion1(1, rs);
    REQUIRE(c.type == Type::Start);
    REQUIRE(c.region2 == 2);
    REQUIRE(c.mode == PlayMode::OneShot);
    REQUIRE(s.active());
}

TEST_CASE("Sequencer entry without rule stops voice", "[Sequencer]")
{
    dc::RuleSet rs;
    rs.setTargets(1, { T(2, 1.0f) });
    dc::Sequencer s;
    s.setRandom(seq({ 0.1f }));
    s.onRegion1(1, rs);
    auto c = s.onRegion1(4, rs);
    REQUIRE(c.type == Type::Stop);
    REQUIRE_FALSE(s.active());
}

TEST_CASE("Sequencer silence draw stops voice", "[Sequencer]")
{
    dc::RuleSet rs;
    rs.setTargets(1, { T(2, 0.5f) });
    dc::Sequencer s;
    s.setRandom(seq({ 0.9f }));
    REQUIRE(s.onRegion1(1, rs).type == Type::Stop); // even when idle
    s.setRandom(seq({ 0.1f, 0.9f }));
    REQUIRE(s.onRegion1(1, rs).type == Type::Start);
    REQUIRE(s.onRegion1(1, rs).type == Type::Stop);
    REQUIRE_FALSE(s.active());
}

TEST_CASE("Sequencer new entry cancels a running one-shot and starts the new target", "[Sequencer]")
{
    dc::RuleSet rs;
    rs.setTargets(1, { T(2, 1.0f, PlayMode::OneShot) });
    rs.setTargets(2, { T(5, 1.0f, PlayMode::Gate) });
    dc::Sequencer s;
    s.setRandom(seq({ 0.0f }));
    s.onRegion1(1, rs);
    auto c = s.onRegion1(2, rs);
    REQUIRE(c.type == Type::Start);
    REQUIRE(c.region2 == 5);
    REQUIRE(c.mode == PlayMode::Gate);
    REQUIRE(s.shouldGateStop(3));
    REQUIRE_FALSE(s.shouldGateStop(2));
}

TEST_CASE("Sequencer re-entering the same region draws again (wrap)", "[Sequencer]")
{
    dc::RuleSet rs;
    rs.setTargets(1, { T(2, 0.4f), T(3, 0.4f) });
    dc::Sequencer s;
    s.setRandom(seq({ 0.1f, 0.6f }));
    REQUIRE(s.onRegion1(1, rs).region2 == 2);
    auto c = s.onRegion1(1, rs);
    REQUIRE(c.type == Type::Start);
    REQUIRE(c.region2 == 3);
}

TEST_CASE("Sequencer gate stops when region 1 changes", "[Sequencer]")
{
    dc::RuleSet rs;
    rs.setTargets(1, { T(2, 1.0f, PlayMode::Gate) });
    dc::Sequencer s;
    s.setRandom(seq({ 0.0f }));
    REQUIRE_FALSE(s.shouldGateStop(1)); // idle
    s.onRegion1(1, rs);
    REQUIRE_FALSE(s.shouldGateStop(1));
    REQUIRE(s.shouldGateStop(2));
    REQUIRE(s.shouldGateStop(-1));
}

TEST_CASE("Sequencer one-shot does not stop when region 1 changes", "[Sequencer]")
{
    dc::RuleSet rs;
    rs.setTargets(1, { T(2, 1.0f, PlayMode::OneShot) });
    dc::Sequencer s;
    s.setRandom(seq({ 0.0f }));
    s.onRegion1(1, rs);
    REQUIRE_FALSE(s.shouldGateStop(2));
    s.onVoiceEnded();
    REQUIRE_FALSE(s.active());
}

TEST_CASE("Sequencer reset idles and default random works", "[Sequencer]")
{
    dc::RuleSet rs;
    rs.setTargets(1, { T(2, 1.0f) });
    dc::Sequencer s; // default mt19937: weight 1 always starts
    for (int i = 0; i < 50; ++i)
        REQUIRE(s.onRegion1(1, rs).type == Type::Start);
    s.reset();
    REQUIRE_FALSE(s.active());
    REQUIRE_FALSE(s.shouldGateStop(9));
    REQUIRE(dc::kCancelFadeMs == 5);
}
