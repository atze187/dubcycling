// DubcyclingProcessor: transactional replace, rejected files, hash refresh, stale loads.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <tuple>
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

// ---- Presets: restoreState and the active preset ----

namespace {
PluginState twoLoopState(const juce::File& a, const juce::File& b)
{
    PluginState s;
    s.loop1 = refOfFile(a);
    s.loop2 = refOfFile(b);
    s.loop1.bars = 2;
    s.loop1.boundaries = { 0.25, 0.5 };
    s.loop2.boundaries = { 0.75 };
    s.rules = { RuleEntry { 1, { RuleTarget { 0, 1.0f } } } };
    s.activeBank = "Bank";
    s.activePreset = "Preset";
    return s;
}
} // namespace

TEST_CASE("Processor: restoreState replaces both loops and rules and loads the files", "[processor][presets]")
{
    ProcFixture f;
    f.loadedWithRegionsAndRules(f.wav("old.wav", 8000));
    const auto a = f.wav("a.wav", 16000);
    const auto b = f.wav("b.wav", 24000);
    const auto s = twoLoopState(a, b);

    f.proc.restoreState(s);
    f.proc.waitForLoads();

    CHECK(f.proc.getSlotStatus(0) == Status::Loaded);
    CHECK(f.proc.getSlotStatus(1) == Status::Loaded);
    const auto st = f.proc.getPluginState();
    CHECK(st.loop1.fileName == "a.wav");
    CHECK(st.loop2.fileName == "b.wav");
    CHECK(st.loop1.boundaries == std::vector<double> { 0.25, 0.5 });
    CHECK(st.loop2.boundaries == std::vector<double> { 0.75 });
    CHECK(st.rules.size() == 1);
    CHECK(st.activeBank == "Bank");
    CHECK(st.activePreset == "Preset");
    CHECK(f.frames1() == 16000);
}

TEST_CASE("Processor: restoreState with a missing file marks the slot Missing and keeps regions and rules", "[processor][presets]")
{
    ProcFixture f;
    const auto a = f.wav("a.wav", 16000);
    const auto b = f.wav("b.wav", 24000);
    const auto s = twoLoopState(a, b);
    b.deleteFile();

    f.proc.restoreState(s);
    f.proc.waitForLoads();

    CHECK(f.proc.getSlotStatus(0) == Status::Loaded);
    CHECK(f.proc.getSlotStatus(1) == Status::Missing);
    const auto st = f.proc.getPluginState();
    CHECK(st.loop2.boundaries == std::vector<double> { 0.75 });
    CHECK(st.rules.size() == 1);
}

TEST_CASE("Processor: restoreState with a changed file (hash differs) behaves like setStateInformation", "[processor][presets]")
{
    auto run = [](bool viaRestore) {
        ProcFixture f;
        const auto a = f.wav("a.wav", 16000);
        const auto b = f.wav("b.wav", 24000);
        const auto s = twoLoopState(a, b);
        f.wav("b.wav", 20000); // same name, different content
        if (viaRestore)
            f.proc.restoreState(s);
        else
            f.setState(s);
        f.proc.waitForLoads();
        struct R { Status s0, s1; std::string err; std::vector<double> bounds; std::string path; } r;
        r.s0 = f.proc.getSlotStatus(0);
        r.s1 = f.proc.getSlotStatus(1);
        r.err = f.proc.slotError(1).toStdString();
        r.bounds = f.proc.getPluginState().loop2.boundaries;
        r.path = f.proc.getPluginState().loop2.path.toStdString();
        return std::make_tuple(r.s0, r.s1, r.err, r.bounds, r.path != "");
    };
    CHECK(run(true) == run(false));
}

TEST_CASE("Processor: the active preset survives getStateInformation and setStateInformation", "[processor][presets]")
{
    ProcFixture f;
    f.proc.setActivePreset("Bank", "Preset");
    CHECK(f.proc.getPluginState().activeBank == "Bank");
    CHECK(f.proc.getPluginState().activePreset == "Preset");

    juce::MemoryBlock block;
    f.proc.getStateInformation(block);
    DubcyclingProcessor other;
    other.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK(other.getPluginState().activeBank == "Bank");
    CHECK(other.getPluginState().activePreset == "Preset");
}

TEST_CASE("Processor: applyEdit keeps the active preset", "[processor][presets]")
{
    ProcFixture f;
    f.proc.setActivePreset("Bank", "Preset");
    auto s = f.proc.getPluginState();
    s.activeBank = "Other";
    s.activePreset = "Other";
    s.loop1.boundaries = { 0.5 };
    f.proc.applyEdit(s);
    CHECK(f.proc.getPluginState().activeBank == "Bank");
    CHECK(f.proc.getPluginState().activePreset == "Preset");
    CHECK(f.proc.getPluginState().loop1.boundaries == std::vector<double> { 0.5 });
}

TEST_CASE("Processor: restoreState reduces a hostile fileName to its leaf", "[processor][presets]")
{
    ProcFixture f;
    PluginState s;
    s.loop1.fileName = "../something.wav";
    s.loop2.fileName = "..\\sub\\other.wav";
    f.proc.restoreState(s);
    f.proc.waitForLoads();
    const auto st = f.proc.getPluginState();
    CHECK(st.loop1.fileName == "something.wav");
    CHECK(st.loop2.fileName == "other.wav");
}

TEST_CASE("Processor: fx edits are published in the model", "[processor][fx]")
{
    ProcFixture f;
    auto s = f.proc.getPluginState();
    s.loop2.boundaries = { 0.5 };
    s.fx.params.set(FxParam::ReverbMix, 0.8);
    s.fx.params.set(FxParam::FilterCutoff, 500.0);
    s.fx.setSwitch(0, FxInsert::Reverb, true);
    s.fx.setSwitch(1, FxInsert::Filter, true);
    s.fx.setSwitch(5, FxInsert::Filter, true); // beyond the 2 regions: trimmed
    f.proc.applyEdit(s);
    FxSettings expected = s.fx;
    expected.trimTo(2);
    const auto m = f.proc.getModel();
    REQUIRE(m != nullptr);
    CHECK(m->fx == expected);
    CHECK(m->fx.maskFor(1) == 2);
    CHECK(f.proc.getPluginState().fx == expected);
}

TEST_CASE("Processor: replacing Loop 2 clears the switches but keeps the parameters", "[processor][fx]")
{
    ProcFixture f;
    f.proc.replaceLoop(1, f.wav("l2.wav"), 2);
    f.proc.waitForLoads();
    auto s = f.proc.getPluginState();
    s.loop2.boundaries = { 0.5 };
    s.fx.params.set(FxParam::ReverbMix, 0.8);
    s.fx.setSwitch(1, FxInsert::Reverb, true);
    f.proc.applyEdit(s);
    REQUIRE(f.proc.getModel()->fx.maskFor(1) == 8);

    f.proc.replaceLoop(1, f.wav("l2b.wav", 32000), 4);
    f.proc.waitForLoads();
    const auto after = f.proc.getPluginState();
    CHECK(after.fx.switches.empty());
    CHECK(after.fx.params.get(FxParam::ReverbMix) == 0.8f);
    CHECK(f.proc.getModel()->fx.switches.empty());
    CHECK(f.proc.getModel()->fx.params.get(FxParam::ReverbMix) == 0.8f);
}

TEST_CASE("Processor: an old state without fx plays like before", "[processor][fx]")
{
    ProcFixture f;
    const juce::String xml = "<DubcyclingState version=\"1\"><Loop1 bars=\"1\"/><Loop2 bars=\"1\" boundaries=\"0.5\"/></DubcyclingState>";
    f.proc.setStateInformation(xml.toRawUTF8(), static_cast<int>(xml.getNumBytesAsUTF8()));
    const auto m = f.proc.getModel();
    REQUIRE(m != nullptr);
    CHECK(m->fx.switches.empty());
    CHECK(m->fx == FxSettings {});
}

TEST_CASE("Processor: the mix is published in the model", "[processor][mix]")
{
    ProcFixture f;
    auto s = f.proc.getPluginState();
    s.loop1.boundaries = { 0.5 };
    s.rules = { RuleEntry { 0, { RuleTarget { 0, 1.0f, PlayMode::Gate, 0.3f } } } };
    f.proc.applyEdit(s);
    const auto m = f.proc.getModel();
    REQUIRE(m != nullptr);
    REQUIRE(m->rules.targets(0).size() == 1);
    CHECK(m->rules.targets(0)[0].mix == 0.3f);
    CHECK(f.proc.getPluginState().rules[0].targets[0].mix == 0.3f);
}

namespace {
struct FixedPlayHead : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> pos;
    juce::Optional<PositionInfo> getPosition() const override { return pos; }
};

void runBlock(DubcyclingProcessor& proc, FixedPlayHead& head, int numerator)
{
    juce::AudioPlayHead::PositionInfo info;
    info.setBpm(120.0);
    info.setPpqPosition(0.0);
    info.setTimeSignature(juce::AudioPlayHead::TimeSignature { numerator, 4 });
    head.pos = info;
    juce::AudioBuffer<float> buf(2, 64);
    buf.clear();
    juce::MidiBuffer midi;
    proc.processBlock(buf, midi);
}
} // namespace

TEST_CASE("Processor: hostBeatsPerBar defaults to 4 and follows the host time signature", "[processor][grid]")
{
    ProcFixture f;
    f.proc.prepareToPlay(44100.0, 64);
    FixedPlayHead head;
    f.proc.setPlayHead(&head);
    CHECK(f.proc.hostBeatsPerBar() == 4);
    runBlock(f.proc, head, 7);
    CHECK(f.proc.hostBeatsPerBar() == 7);
    runBlock(f.proc, head, 0); // out of range: keeps the last value
    CHECK(f.proc.hostBeatsPerBar() == 7);
    runBlock(f.proc, head, 65);
    CHECK(f.proc.hostBeatsPerBar() == 7);
    runBlock(f.proc, head, 64);
    CHECK(f.proc.hostBeatsPerBar() == 64);
    f.proc.setPlayHead(nullptr);
}
