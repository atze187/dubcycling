// SlotTracker: ticket/status bookkeeping and the transactional replace, without a MessageManager.
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include "plugin/SlotTracker.h"

using namespace dc;

namespace {

std::shared_ptr<const StereoData> frames(std::size_t n)
{
    auto d = std::make_shared<StereoData>();
    d->sampleRate = 8000.0;
    d->left.assign(n, 0.0f);
    d->right.assign(n, 0.0f);
    return d;
}

LoopLoadResult ok(int slot, std::uint64_t ticket, std::shared_ptr<const StereoData> d, juce::String hash = "abc")
{
    LoopLoadResult r;
    r.slot = slot;
    r.ticket = ticket;
    r.data = std::move(d);
    r.hash = std::move(hash);
    return r;
}

LoopLoadResult fail(int slot, std::uint64_t ticket, juce::String error)
{
    LoopLoadResult r;
    r.slot = slot;
    r.ticket = ticket;
    r.error = std::move(error);
    return r;
}

PluginState withWork()
{
    PluginState s;
    s.loop1 = { "C:/a.wav", "a.wav", "aaa", 2, { 0.25, 0.5 } };
    s.loop2 = { "C:/b.wav", "b.wav", "bbb", 1, { 0.5 } };
    s.rules = { RuleEntry { 1, { RuleTarget { 0, 1.0f } } } };
    return s;
}

LoopRef newRef()
{
    return { "C:/new.wav", "new.wav", "nnn", 4, {} };
}

} // namespace

TEST_CASE("SlotTracker: stale ticket results are dropped", "[SlotTracker]")
{
    SlotTracker t;
    PluginState s = withWork();
    const auto first = t.beginLoad(0);
    const auto second = t.beginLoad(0);
    CHECK(first != second);
    CHECK_FALSE(t.apply(ok(0, first, frames(1000)), s));
    CHECK(t.status(0) == SlotStatus::Loading);
    CHECK(t.data(0) == nullptr);
    const auto d = frames(2000);
    CHECK(t.apply(ok(0, second, d), s));
    CHECK(t.status(0) == SlotStatus::Loaded);
    CHECK(t.data(0) == d);
    CHECK_FALSE(t.apply(ok(7, second, d), s)); // invalid slot
}

TEST_CASE("SlotTracker: resetAll invalidates in-flight loads and staged replaces", "[SlotTracker]")
{
    SlotTracker t;
    PluginState s = withWork();
    const auto load = t.beginLoad(0);
    const auto repl = t.beginReplace(1, newRef());
    t.resetAll();
    CHECK(t.status(0) == SlotStatus::Empty);
    CHECK(t.status(1) == SlotStatus::Empty);
    CHECK_FALSE(t.apply(ok(0, load, frames(1000)), s));
    CHECK_FALSE(t.apply(ok(1, repl, frames(1000)), s));
    CHECK(t.status(0) == SlotStatus::Empty);
    CHECK(t.status(1) == SlotStatus::Empty);
    CHECK(serialize(s) == serialize(withWork()));
}

TEST_CASE("SlotTracker: Missing, Failed and Loaded transitions", "[SlotTracker]")
{
    SlotTracker t;
    PluginState s = withWork();
    t.setMissing(0);
    CHECK(t.status(0) == SlotStatus::Missing);

    auto ticket = t.beginLoad(0);
    CHECK(t.status(0) == SlotStatus::Loading);
    CHECK(t.apply(fail(0, ticket, "unsupported or damaged file"), s));
    CHECK(t.status(0) == SlotStatus::Failed);
    CHECK(t.error(0) == "unsupported or damaged file");
    CHECK(t.data(0) == nullptr);

    ticket = t.beginLoad(0);
    CHECK(t.apply(fail(0, ticket, LoopLoader::kErrorFileNotFound), s));
    CHECK(t.status(0) == SlotStatus::Missing); // gone between relink and decode

    ticket = t.beginLoad(0);
    CHECK(t.apply(ok(0, ticket, frames(1000), "fresh"), s));
    CHECK(t.status(0) == SlotStatus::Loaded);
    CHECK(t.error(0).isEmpty());
    CHECK(s.loop1.hash == "fresh"); // stored hash refreshed after a successful load
    CHECK(s.loop1.boundaries == std::vector<double> { 0.25, 0.5 });
    CHECK(s.rules.size() == 1);
}

TEST_CASE("SlotTracker: a failed replace keeps the old state and reports the failure", "[SlotTracker]")
{
    SlotTracker t;
    PluginState s = withWork();
    const auto old = frames(1000);
    REQUIRE(t.apply(ok(0, t.beginLoad(0), old, "aaa"), s));
    const auto before = serialize(s);
    CHECK(t.replaceFailure(0).id == 0);

    const auto ticket = t.beginReplace(0, newRef());
    CHECK(t.status(0) == SlotStatus::Loading); // shown as loading while staged
    CHECK(t.pendingFileName(0) == "new.wav");
    CHECK(t.data(0) == old);                   // the old loop keeps playing
    CHECK(serialize(s) == before);

    CHECK_FALSE(t.apply(fail(0, ticket, "unsupported or damaged file"), s));
    CHECK(serialize(s) == before);
    CHECK(t.status(0) == SlotStatus::Loaded);
    CHECK(t.data(0) == old);
    CHECK(t.pendingFileName(0).isEmpty());
    const auto f = t.replaceFailure(0);
    CHECK(f.id > 0);
    CHECK(f.fileName == "new.wav");
    CHECK(f.error == "unsupported or damaged file");
}

TEST_CASE("SlotTracker: a successful replace applies reference, bars and drops regions and rules", "[SlotTracker]")
{
    SlotTracker t;
    PluginState s = withWork();
    REQUIRE(t.apply(ok(0, t.beginLoad(0), frames(1000)), s));
    const auto ticket = t.beginReplace(0, newRef());
    const auto d = frames(3000);
    CHECK(t.apply(ok(0, ticket, d, "decoded"), s));
    CHECK(t.status(0) == SlotStatus::Loaded);
    CHECK(t.data(0) == d);
    CHECK(s.loop1.path == "C:/new.wav");
    CHECK(s.loop1.fileName == "new.wav");
    CHECK(s.loop1.hash == "decoded");
    CHECK(s.loop1.bars == 4);
    CHECK(s.loop1.boundaries.empty());
    CHECK(s.rules.empty());
    CHECK(s.loop2.boundaries == std::vector<double> { 0.5 }); // the other slot is untouched
    CHECK(t.replaceFailure(0).id == 0);
}

TEST_CASE("SlotTracker: a newer load or replace supersedes a staged replace", "[SlotTracker]")
{
    SlotTracker t;
    PluginState s = withWork();
    const auto before = serialize(s);
    const auto repl = t.beginReplace(0, newRef());
    const auto load = t.beginLoad(0); // e.g. a relink of the slot's own file
    CHECK_FALSE(t.apply(ok(0, repl, frames(1000)), s));
    CHECK(serialize(s) == before);
    CHECK(t.apply(ok(0, load, frames(2000), "aaa"), s));
    CHECK(t.data(0)->frames() == 2000);

    const auto r1 = t.beginReplace(0, newRef());
    LoopRef second = newRef();
    second.fileName = "second.wav";
    const auto r2 = t.beginReplace(0, second);
    CHECK_FALSE(t.apply(ok(0, r1, frames(500)), s));
    CHECK(t.apply(ok(0, r2, frames(600)), s));
    CHECK(s.loop1.fileName == "second.wav");
}

TEST_CASE("SlotTracker: the slot's own load keeps running while a replace is staged", "[SlotTracker]")
{
    SlotTracker t;
    PluginState s = withWork();
    const auto load = t.beginLoad(0);
    const auto repl = t.beginReplace(0, newRef());
    CHECK(t.apply(ok(0, load, frames(1000), "aaa"), s)); // the old file finished decoding
    CHECK(t.status(0) == SlotStatus::Loading);            // still staged
    CHECK_FALSE(t.apply(fail(0, repl, "unsupported or damaged file"), s));
    CHECK(t.status(0) == SlotStatus::Loaded);
    CHECK(t.data(0)->frames() == 1000);
}

TEST_CASE("SlotTracker: replacing slot 1 clears the fx switches and keeps the parameters", "[SlotTracker][fx]")
{
    SlotTracker t;
    PluginState s = withWork();
    s.fx.params.set(FxParam::ReverbMix, 0.9);
    s.fx.setSwitch(1, FxInsert::Reverb, true);
    REQUIRE(t.apply(ok(1, t.beginLoad(1), frames(1000)), s));
    CHECK(s.fx.maskFor(1) == 8); // a plain load keeps them
    REQUIRE(t.apply(ok(1, t.beginReplace(1, newRef()), frames(3000)), s));
    CHECK(s.fx.switches.empty());
    CHECK(s.fx.params.get(FxParam::ReverbMix) == 0.9f);
    s.fx.setSwitch(1, FxInsert::Reverb, true);
    REQUIRE(t.apply(ok(0, t.beginReplace(0, newRef()), frames(3000)), s));
    CHECK(s.fx.maskFor(1) == 8); // replacing Loop 1 leaves Loop-2 switches alone
}
