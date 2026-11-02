#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include "plugin/FileRelink.h"
#include "plugin/PluginState.h"

using namespace dc;

namespace {
struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dcrelink", "", false);
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

LoopRef refFor(const juce::File& f)
{
    LoopRef r;
    r.path = f.getFullPathName();
    r.fileName = f.getFileName();
    r.hash = hashFile(f);
    r.bars = 2;
    r.boundaries = { 0.5 };
    return r;
}
}

TEST_CASE("FileRelink finds file at stored path")
{
    TempDir t;
    auto f = t.dir.getChildFile("loop.wav");
    f.replaceWithText("content");
    auto r = relink(refFor(f), {});
    REQUIRE(r.has_value());
    CHECK(*r == f);
}

TEST_CASE("FileRelink finds moved file by name and hash in session dir")
{
    TempDir t;
    auto orig = t.dir.getChildFile("old").getChildFile("loop.wav");
    orig.create();
    orig.replaceWithText("content");
    auto ref = refFor(orig);
    auto session = t.dir.getChildFile("session");
    session.createDirectory();
    auto moved = session.getChildFile("loop.wav");
    orig.moveFileTo(moved);
    auto r = relink(ref, session);
    REQUIRE(r.has_value());
    CHECK(*r == moved);
}

TEST_CASE("FileRelink finds moved file in the folder of the stored path")
{
    TempDir t;
    auto orig = t.dir.getChildFile("loop.wav");
    orig.replaceWithText("content");
    auto ref = refFor(orig);
    orig.deleteFile();
    ref.path = t.dir.getChildFile("renamed_away.wav").getFullPathName();
    t.dir.getChildFile("loop.wav").replaceWithText("content");
    auto r = relink(ref, {});
    REQUIRE(r.has_value());
    CHECK(r->getFileName() == "loop.wav");
}

TEST_CASE("FileRelink does not accept a file with the same name but different hash")
{
    TempDir t;
    auto orig = t.dir.getChildFile("old").getChildFile("loop.wav");
    orig.create();
    orig.replaceWithText("content");
    auto ref = refFor(orig);
    orig.deleteFile();
    auto session = t.dir.getChildFile("session");
    session.createDirectory();
    session.getChildFile("loop.wav").replaceWithText("different content");
    CHECK_FALSE(relink(ref, session).has_value());
}

TEST_CASE("FileRelink returns nullopt when nothing matches")
{
    TempDir t;
    LoopRef ref;
    ref.path = t.dir.getChildFile("nowhere").getChildFile("x.wav").getFullPathName();
    ref.fileName = "x.wav";
    ref.hash = "deadbeef";
    CHECK_FALSE(relink(ref, t.dir).has_value());
    CHECK_FALSE(relink(ref, {}).has_value());
    CHECK_FALSE(relink(LoopRef {}, {}).has_value());
}
