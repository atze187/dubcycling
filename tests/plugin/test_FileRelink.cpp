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

TEST_CASE("FileRelink finds moved file by name and hash in a candidate folder")
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
    auto r = relink(ref, { session });
    REQUIRE(r.has_value());
    CHECK(*r == moved);
}

TEST_CASE("FileRelink searches the candidate folders in order and skips unusable ones")
{
    TempDir t;
    auto orig = t.dir.getChildFile("old").getChildFile("loop.wav");
    orig.create();
    orig.replaceWithText("content");
    auto ref = refFor(orig);
    orig.deleteFile();
    auto empty = t.dir.getChildFile("empty");
    empty.createDirectory();
    auto first = t.dir.getChildFile("first");
    auto second = t.dir.getChildFile("second");
    first.createDirectory();
    second.createDirectory();
    first.getChildFile("loop.wav").replaceWithText("content");
    second.getChildFile("loop.wav").replaceWithText("content");
    const auto notAFolder = t.dir.getChildFile("file.txt");
    notAFolder.replaceWithText("x");
    auto r = relink(ref, { juce::File(), t.dir.getChildFile("absent"), notAFolder, empty, first, second });
    REQUIRE(r.has_value());
    CHECK(*r == first.getChildFile("loop.wav"));
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
    CHECK_FALSE(relink(ref, { session }).has_value());
}

TEST_CASE("FileRelink returns nullopt when nothing matches")
{
    TempDir t;
    LoopRef ref;
    ref.path = t.dir.getChildFile("nowhere").getChildFile("x.wav").getFullPathName();
    ref.fileName = "x.wav";
    ref.hash = "deadbeef";
    CHECK_FALSE(relink(ref, { t.dir }).has_value());
    CHECK_FALSE(relink(ref, {}).has_value());
    CHECK_FALSE(relink(LoopRef {}, {}).has_value());
}
