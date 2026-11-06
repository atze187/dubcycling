#include <catch2/catch_test_macros.hpp>
#include <type_traits>
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

TEST_CASE("Sequencer entry without rule is not a draw", "[Sequencer]")
{
    dc::RuleSet rs;
    rs.setTargets(1, { T(2, 1.0f, PlayMode::OneShot) });
    dc::Sequencer s;
    int calls = 0;
    s.setRandom([&calls] { ++calls; return 0.1f; });
    s.onRegion1(1, rs);
    auto c = s.onRegion1(4, rs);
    REQUIRE(c.type == Type::None);
    REQUIRE(s.active()); // a running one-shot keeps playing
    REQUIRE(calls == 1); // no random draw for a rule-less region
}

TEST_CASE("Sequencer rule with zero total weight is a silence draw", "[Sequencer]")
{
    dc::RuleSet rs;
    rs.setTargets(1, { T(2, 1.0f, PlayMode::OneShot) });
    rs.setTargets(2, { T(0, 0.0f) });
    dc::Sequencer s;
    s.setRandom(seq({ 0.1f }));
    s.onRegion1(1, rs);
    REQUIRE(s.onRegion1(2, rs).type == Type::Stop);
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

TEST_CASE("Sequencer clamps a random value of exactly 1 below 1 before drawing", "[Sequencer][hardening]")
{
    dc::RuleSet rs;
    rs.setTargets(0, { T(4, 1.0f) });
    dc::Sequencer s;
    s.setRandom([] { return 1.0f; }); // uniform_real_distribution<float> can return exactly 1.0f
    const auto c = s.onRegion1(0, rs);
    CHECK(c.type == Type::Start);
    CHECK(c.region2 == 4);
}

TEST_CASE("Sequencer is not copyable or movable (audio-thread state, own RNG)", "[Sequencer][hardening]")
{
    CHECK_FALSE(std::is_copy_constructible_v<dc::Sequencer>);
    CHECK_FALSE(std::is_copy_assignable_v<dc::Sequencer>);
    CHECK_FALSE(std::is_move_constructible_v<dc::Sequencer>);
    CHECK_FALSE(std::is_move_assignable_v<dc::Sequencer>);
    dc::RuleSet rs;
    rs.setTargets(0, { T(1, 1.0f) });
    dc::Sequencer s; // default RNG
    for (int i = 0; i < 100; ++i)
        CHECK(s.onRegion1(0, rs).type == Type::Start);
}

TEST_CASE("Sequencer: the start command carries the mix of the drawn target and the triggering region", "[Sequencer]")
{
    dc::RuleSet rs;
    dc::RuleTarget t = T(2, 1.0f);
    t.mix = 0.8f;
    rs.setTargets(3, { t });
    dc::Sequencer s;
    s.setRandom(seq({ 0.1f }));
    const auto c = s.onRegion1(3, rs);
    REQUIRE(c.type == Type::Start);
    REQUIRE(c.mix == 0.8f);
    REQUIRE(c.region1 == 3);
    REQUIRE(dc::Sequencer::Command{}.mix == 0.5f);
    REQUIRE(dc::Sequencer::Command{}.region1 == -1);
}

TEST_CASE("Sequencer: a draw carries the mix", "[Sequencer]")
{
    dc::RuleSet rs;
    dc::RuleTarget t = T(2, 1.0f);
    t.mix = 0.2f;
    rs.setTargets(1, { t });
    dc::Sequencer s;
    s.setRandom(seq({ 0.1f }));
    const auto d = s.draw(1, rs);
    REQUIRE(d.type == dc::Sequencer::Draw::Type::Target);
    REQUIRE(d.mix == 0.2f);
    const auto c = s.apply(1, d);
    REQUIRE(c.mix == 0.2f);
    REQUIRE(c.region1 == 1);
}
