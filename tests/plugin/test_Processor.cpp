// DubcyclingProcessor: transactional replace, rejected files, hash refresh, stale loads.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"

using namespace dc;
using Status = DubcyclingProcessor::SlotStatus;

namespace {

struct ProcFixture
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dcproc", "", false);
    DubcyclingProcessor proc;

    ProcFixture() { dir.createDirectory(); }
    ~ProcFixture() { dir.deleteRecursively(); }

    juce::File wav(const juce::String& name, int frames = 16000)
    {
        const auto file = dir.getChildFile(name);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream>(file);
        auto writer = format.createWriterFor(out, juce::AudioFormatWriterOptions {}
                                                      .withSampleRate(8000)
                                                      .withNumChannels(2)
                                                      .withBitsPerSample(16));
        REQUIRE(writer != nullptr);
        juce::AudioBuffer<float> buf(2, frames);
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < 2; ++c)
                buf.setSample(c, i, 0.5f * std::sin(static_cast<float>(i) * 0.01f));
        REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, frames));
        return file;
    }

    juce::File broken(const juce::String& name)
    {
        const auto file = dir.getChildFile(name);
        file.replaceWithText("this is not audio");
        return file;
    }

    // Slot 0 loaded from `file`, with boundaries and one rule.
    void loadedWithRegionsAndRules(const juce::File& file)
    {
        proc.replaceLoop(0, file, 2);
        proc.waitForLoads();
        REQUIRE(proc.getSlotStatus(0) == Status::Loaded);
        auto s = proc.getPluginState();
        s.loop1.boundaries = { 0.25, 0.5 };
        s.rules = { RuleEntry { 1, { RuleTarget { 0, 1.0f } } } };
        proc.applyEdit(s);
    }

    void setState(const PluginState& s)
    {
        const auto block = serialize(s);
        proc.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    }

    std::size_t frames1() const
    {
        const auto m = proc.getModel();
        return m != nullptr && m->loop1.data != nullptr ? m->loop1.data->frames() : 0;
    }
};

} // namespace

TEST_CASE("Processor: replaceLoop keeps the old state until the new loop is decoded", "[processor][hardening]")
{
    ProcFixture f;
    f.loadedWithRegionsAndRules(f.wav("good.wav"));
    const auto before = serialize(f.proc.getPluginState());

    f.proc.replaceLoop(0, f.wav("other.wav", 32000), 4);
    CHECK(serialize(f.proc.getPluginState()) == before); // staged, nothing applied yet
    CHECK(f.frames1() == 16000);                          // the old loop keeps playing

    f.proc.waitForLoads();
    const auto after = f.proc.getPluginState();
    CHECK(after.loop1.fileName == "other.wav");
    CHECK(after.loop1.bars == 4);
    CHECK(after.loop1.boundaries.empty());
    CHECK(after.rules.empty());
    CHECK(f.proc.getSlotStatus(0) == Status::Loaded);
    CHECK(f.frames1() == 32000);
}

TEST_CASE("Processor: replaceLoop with an undecodable file keeps regions, rules and the reference", "[processor][hardening]")
{
    ProcFixture f;
    f.loadedWithRegionsAndRules(f.wav("good.wav"));
    const auto before = serialize(f.proc.getPluginState());

    CHECK(f.proc.replaceFailure(0).id == 0);
    CHECK(f.proc.replaceLoop(0, f.broken("broken.wav"), 4)); // readable, so it is staged
    f.proc.waitForLoads();

    CHECK(serialize(f.proc.getPluginState()) == before);
    CHECK(f.proc.getSlotStatus(0) == Status::Loaded);
    CHECK(f.frames1() == 16000);
    const auto failure = f.proc.replaceFailure(0);
    CHECK(failure.id > 0);
    CHECK(failure.fileName == "broken.wav");
    CHECK(failure.error == "unsupported or damaged file");
}

TEST_CASE("Processor: an undecodable file at the stored path is Failed with the loader's error", "[processor][hardening]")
{
    ProcFixture f;
    const auto bad = f.broken("damaged.wav");
    PluginState s;
    s.loop1.path = bad.getFullPathName();
    s.loop1.fileName = bad.getFileName();
    s.loop1.boundaries = { 0.5 };
    s.rules = { RuleEntry { 0, { RuleTarget { 1, 1.0f } } } };
    f.setState(s);
    f.proc.waitForLoads();
    CHECK(f.proc.getSlotStatus(0) == Status::Failed);
    CHECK(f.proc.slotError(0) == "unsupported or damaged file");
    const auto after = f.proc.getPluginState();
    CHECK(after.loop1.boundaries == std::vector<double> { 0.5 }); // kept and saved unchanged
    CHECK(after.rules.size() == 1);

    PluginState gone;
    gone.loop2.path = f.dir.getChildFile("gone.wav").getFullPathName();
    gone.loop2.fileName = "gone.wav";
    f.setState(gone);
    CHECK(f.proc.getSlotStatus(1) == Status::Missing);
    CHECK(f.proc.slotError(1).isEmpty());
}

TEST_CASE("Processor: loadLoop and replaceLoop with a missing file change nothing", "[processor][hardening]")
{
    ProcFixture f;
    f.loadedWithRegionsAndRules(f.wav("good.wav"));
    const auto before = serialize(f.proc.getPluginState());
    const auto absent = f.dir.getChildFile("nothere.wav");

    CHECK_FALSE(f.proc.loadLoop(0, absent));
    f.proc.waitForLoads();
    CHECK(serialize(f.proc.getPluginState()) == before);
    CHECK(f.proc.getSlotStatus(0) == Status::Loaded);

    CHECK_FALSE(f.proc.replaceLoop(0, absent, 3));
    f.proc.waitForLoads();
    CHECK(serialize(f.proc.getPluginState()) == before);
    CHECK(f.proc.getSlotStatus(0) == Status::Loaded);
    CHECK(f.frames1() == 16000);
}

TEST_CASE("Processor: a load from the stored path refreshes a stale hash", "[processor][hardening]")
{
    ProcFixture f;
    const auto file = f.wav("reexported.wav");
    PluginState s;
    s.loop1.path = file.getFullPathName();
    s.loop1.fileName = file.getFileName();
    s.loop1.hash = "00"; // the file was re-exported in place since the session was saved
    s.loop1.boundaries = { 0.5 };
    f.setState(s);
    f.proc.waitForLoads();

    REQUIRE(f.proc.getSlotStatus(0) == Status::Loaded);
    const auto after = f.proc.getPluginState();
    CHECK(after.loop1.hash.equalsIgnoreCase(hashFile(file)));
    CHECK(after.loop1.boundaries == std::vector<double> { 0.5 });
}

TEST_CASE("Processor: a second setStateInformation drops the first one's in-flight load", "[processor][hardening]")
{
    ProcFixture f;
    const auto a = f.wav("a.wav", 16000);
    const auto b = f.wav("b.wav", 24000);
    PluginState sa, sb;
    sa.loop1.path = a.getFullPathName();
    sa.loop1.fileName = a.getFileName();
    sb.loop1.path = b.getFullPathName();
    sb.loop1.fileName = b.getFileName();

    f.setState(sa);
    f.setState(sb);
    f.proc.waitForLoads();
    CHECK(f.proc.getSlotStatus(0) == Status::Loaded);
    CHECK(f.proc.getPluginState().loop1.fileName == "b.wav");
    CHECK(f.frames1() == 24000);
}

// --- F2: automatic relink of moved files ----------------------------------------------------------
namespace {
LoopRef refOfFile(const juce::File& f)
{
    LoopRef r;
    r.path = f.getFullPathName();
    r.fileName = f.getFileName();
    r.hash = hashFile(f);
    r.boundaries = { 0.5 };
    return r;
}

// Moves `f` into `folder` (created if needed) and returns the new file.
juce::File moveInto(const juce::File& f, const juce::File& folder)
{
    folder.createDirectory();
    const auto moved = folder.getChildFile(f.getFileName());
    REQUIRE(f.moveFileTo(moved));
    return moved;
}
} // namespace

TEST_CASE("Processor: relinking loop 1 manually also finds loop 2 moved into the same folder", "[processor][relink]")
{
    ProcFixture f;
    f.dir.getChildFile("old").createDirectory();
    const auto a = f.wav("old/a.wav", 16000);
    const auto b = f.wav("old/b.wav", 24000);
    PluginState s;
    s.loop1 = refOfFile(a);
    s.loop2 = refOfFile(b);
    const auto newDir = f.dir.getChildFile("new");
    const auto a2 = moveInto(a, newDir);
    const auto b2 = moveInto(b, newDir);
    f.setState(s);
    f.proc.waitForLoads();
    REQUIRE(f.proc.getSlotStatus(0) == Status::Missing);
    REQUIRE(f.proc.getSlotStatus(1) == Status::Missing);

    REQUIRE(f.proc.loadLoop(0, a2));
    f.proc.waitForLoads();
    CHECK(f.proc.getSlotStatus(0) == Status::Loaded);
    CHECK(f.proc.getSlotStatus(1) == Status::Loaded);
    const auto st = f.proc.getPluginState();
    CHECK(juce::File(st.loop2.path) == b2);
    CHECK(st.loop2.boundaries == std::vector<double> { 0.5 }); // relink keeps regions
}

TEST_CASE("Processor: a moved loop is found in the folder of the other slot's file", "[processor][relink]")
{
    for (int movedSlot : { 0, 1 })
    {
        ProcFixture f;
        f.dir.getChildFile("old").createDirectory();
        f.dir.getChildFile("project").createDirectory();
        // The moved loop now sits next to the other slot's (unmoved) file.
        const auto moved = f.wav("old/moved.wav", 16000);
        const auto stays = f.wav("project/stays.wav", 24000);
        PluginState s;
        (movedSlot == 0 ? s.loop1 : s.loop2) = refOfFile(moved);
        (movedSlot == 0 ? s.loop2 : s.loop1) = refOfFile(stays);
        const auto now = moveInto(moved, f.dir.getChildFile("project"));
        f.setState(s);
        f.proc.waitForLoads();
        CHECK(f.proc.getSlotStatus(0) == Status::Loaded);
        CHECK(f.proc.getSlotStatus(1) == Status::Loaded);
        const auto st = f.proc.getPluginState();
        CHECK(juce::File((movedSlot == 0 ? st.loop1 : st.loop2).path) == now);
    }
}

TEST_CASE("Processor: a same-named file with another hash in a candidate folder is not accepted", "[processor][relink]")
{
    ProcFixture f;
    f.dir.getChildFile("old").createDirectory();
    f.dir.getChildFile("project").createDirectory();
    const auto moved = f.wav("old/moved.wav", 16000);
    const auto stays = f.wav("project/stays.wav", 24000);
    PluginState s;
    s.loop1 = refOfFile(moved);
    s.loop2 = refOfFile(stays);
    moved.deleteFile();
    f.wav("project/moved.wav", 20000); // same name, different content
    f.setState(s);
    f.proc.waitForLoads();
    CHECK(f.proc.getSlotStatus(0) == Status::Missing);
    CHECK(f.proc.getSlotStatus(1) == Status::Loaded);
    CHECK(f.proc.getPluginState().loop1.path == moved.getFullPathName()); // reference unchanged
}

TEST_CASE("Processor: the folder of the last loaded file is searched for a moved loop", "[processor][relink]")
{
    ProcFixture f;
    f.dir.getChildFile("old").createDirectory();
    f.dir.getChildFile("library").createDirectory();
    const auto loop = f.wav("old/loop.wav", 16000);
    PluginState s;
    s.loop2 = refOfFile(loop); // slot 0 empty: no other-slot folder to search
    moveInto(loop, f.dir.getChildFile("library"));

    // Something was loaded from the library before (this session).
    REQUIRE(f.proc.replaceLoop(0, f.wav("library/other.wav", 24000), 1));
    f.proc.waitForLoads();
    f.setState(s);
    f.proc.waitForLoads();
    CHECK(f.proc.getSlotStatus(1) == Status::Loaded);
    CHECK(juce::File(f.proc.getPluginState().loop2.path) == f.dir.getChildFile("library").getChildFile("loop.wav"));
}
