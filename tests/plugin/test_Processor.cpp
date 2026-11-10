// DubcyclingProcessor: transactional replace, rejected files, hash refresh, stale loads.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <tuple>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"
#include "plugin/presets/BankFormat.h"

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

namespace {

float peak(const juce::AudioBuffer<float>& b, int from, int to)
{
    float p = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = from; i < to; ++i)
            p = std::max(p, std::abs(b.getSample(c, i)));
    return p;
}

struct MidiFixture : ProcFixture
{
    static constexpr int kBlock = 512;
    MidiFixture()
    {
        proc.replaceLoop(0, wav("loop.wav"), 1);
        proc.waitForLoads();
        REQUIRE(proc.getSlotStatus(0) == Status::Loaded);
        proc.prepareToPlay(44100.0, kBlock);
    }

    juce::AudioBuffer<float> run(juce::MidiBuffer& midi, int n = kBlock)
    {
        juce::AudioBuffer<float> buf(2, n);
        buf.clear();
        proc.processBlock(buf, midi);
        return buf;
    }
    juce::AudioBuffer<float> run(int n = kBlock)
    {
        juce::MidiBuffer m;
        return run(m, n);
    }
};

} // namespace

TEST_CASE("Processor: autoplay output is unchanged by MIDI events", "[processor][midi]")
{
    MidiFixture a, b;
    FixedPlayHead ha, hb;
    juce::AudioPlayHead::PositionInfo info;
    info.setBpm(120.0);
    info.setPpqPosition(0.0);
    info.setIsPlaying(true);
    ha.pos = info;
    hb.pos = info;
    a.proc.setPlayHead(&ha);
    b.proc.setPlayHead(&hb);

    juce::MidiBuffer none, withNote;
    withNote.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 100);
    const auto x = a.run(none);
    const auto y = b.run(withNote);
    REQUIRE(peak(x, 0, MidiFixture::kBlock) > 0.0f); // the comparison is not of two silences
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < MidiFixture::kBlock; ++i)
            REQUIRE(x.getSample(c, i) == y.getSample(c, i));
    a.proc.setPlayHead(nullptr);
    b.proc.setPlayHead(nullptr);
}

TEST_CASE("Processor: midi mode plays only while a note is held (stopped host)", "[processor][midi]")
{
    MidiFixture f;
    CHECK_FALSE(f.proc.isMidiEffect());
    f.proc.setTriggerMode(TriggerMode::Midi);
    CHECK(f.proc.triggerMode() == TriggerMode::Midi);

    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f); // no note: silence

    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 200);
    const auto b = f.run(on);
    CHECK(peak(b, 0, 200) == 0.0f);
    CHECK(peak(b, 250, MidiFixture::kBlock) > 0.0f);
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) > 0.0f); // still held

    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::noteOff(1, 60), 10);
    f.run(off);
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f); // after the cancel fade
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f);
}

TEST_CASE("Processor: midi mode starts at the next bar with a running host", "[processor][midi]")
{
    MidiFixture f;
    f.proc.setTriggerMode(TriggerMode::Midi);
    FixedPlayHead head;
    f.proc.setPlayHead(&head);
    juce::AudioPlayHead::PositionInfo info;
    info.setBpm(120.0);
    info.setIsPlaying(true);
    info.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 });
    info.setPpqPosition(3.99); // the bar line at ppq 4 is 220.5 frames (at 44.1 kHz) into the block
    head.pos = info;

    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    const auto b = f.run(on);
    CHECK(peak(b, 0, 221) == 0.0f);
    CHECK(peak(b, 280, MidiFixture::kBlock) > 0.0f);
    f.proc.setPlayHead(nullptr);
}

TEST_CASE("Processor: all-notes-off and prepareToPlay clear held notes", "[processor][midi]")
{
    MidiFixture f;
    f.proc.setTriggerMode(TriggerMode::Midi);
    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    f.run(on);
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) > 0.0f);

    for (int cc : { 123, 120 })
    {
        juce::MidiBuffer allOff;
        allOff.addEvent(juce::MidiMessage::controllerEvent(1, cc, 0), 0);
        f.run(allOff);
        CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f);
        f.run(on); // play again
        CHECK(peak(f.run(), 0, MidiFixture::kBlock) > 0.0f);
    }

    f.proc.prepareToPlay(44100.0, MidiFixture::kBlock);
    f.run();
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f);

    // a note-on with velocity 0 is a note-off
    f.run(on);
    juce::MidiBuffer zero;
    zero.addEvent(juce::MidiMessage(0x90, 60, 0), 0);
    f.run(zero);
    f.run();
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f);
}

TEST_CASE("Processor: MIDI events in a zero-length block are ignored", "[processor][midi]")
{
    MidiFixture f;
    f.proc.setTriggerMode(TriggerMode::Midi);
    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    f.run(on, 0);
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f); // the note did not gate anything
}

TEST_CASE("Processor: the mode is saved with the project, not with presets", "[processor][midi]")
{
    ProcFixture f;
    CHECK(f.proc.triggerMode() == TriggerMode::Autoplay);
    f.proc.setTriggerMode(TriggerMode::Midi);

    juce::MemoryBlock saved;
    f.proc.getStateInformation(saved);
    CHECK(juce::String::fromUTF8(static_cast<const char*>(saved.getData()), static_cast<int>(saved.getSize()))
              .contains("Trigger=\"midi\""));
    CHECK(deserialize(saved.getData(), saved.getSize()).has_value()); // still a valid state

    DubcyclingProcessor other;
    other.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(other.triggerMode() == TriggerMode::Midi);

    // Autoplay writes no attribute; a state without it (or with garbage) loads as Autoplay.
    f.proc.setTriggerMode(TriggerMode::Autoplay);
    juce::MemoryBlock plain;
    f.proc.getStateInformation(plain);
    CHECK_FALSE(juce::String::fromUTF8(static_cast<const char*>(plain.getData()), static_cast<int>(plain.getSize()))
                    .contains("Trigger"));
    other.setStateInformation(plain.getData(), static_cast<int>(plain.getSize()));
    CHECK(other.triggerMode() == TriggerMode::Autoplay);

    other.setTriggerMode(TriggerMode::Midi);
    auto text = juce::String::fromUTF8(static_cast<const char*>(saved.getData()), static_cast<int>(saved.getSize()))
                    .replace("Trigger=\"midi\"", "Trigger=\"bogus\"");
    other.setStateInformation(text.toRawUTF8(), static_cast<int>(text.getNumBytesAsUTF8()));
    CHECK(other.triggerMode() == TriggerMode::Autoplay);

    // Presets neither carry nor change it.
    other.setTriggerMode(TriggerMode::Midi);
    other.restoreState(PluginState {});
    CHECK(other.triggerMode() == TriggerMode::Midi);
    other.setTriggerMode(TriggerMode::Autoplay);
    other.restoreState(PluginState {});
    CHECK(other.triggerMode() == TriggerMode::Autoplay);

    Bank bank;
    bank.presets.push_back(Preset { "p", PluginState {} });
    CHECK_FALSE(bankToJson(bank).containsIgnoreCase("trigger"));
}

// ---- MIDI trigger: no click at a stop ------------------------------------------------------------

namespace {

// A loop that is periodic over its length (a 50 Hz cosine at 8 kHz, whole cycles): no jump in the
// signal at its own wrap, and at the crest on every bar line, so a stop there would cut at full level.
juce::File cosWav(const juce::File& dir, const juce::String& name, int frames = 16000)
{
    const auto file = dir.getChildFile(name);
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream>(file);
    auto writer = format.createWriterFor(out, juce::AudioFormatWriterOptions {}
                                                  .withSampleRate(8000)
                                                  .withNumChannels(2)
                                                  .withBitsPerSample(24));
    REQUIRE(writer != nullptr);
    juce::AudioBuffer<float> buf(2, frames);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c)
            buf.setSample(c, i, 0.5f * std::cos(juce::MathConstants<float>::twoPi * 50.0f
                                                * static_cast<float>(i) / 8000.0f));
    REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, frames));
    return file;
}

// Largest sample-to-sample jump in x[from, end).
float maxStep(const std::vector<float>& x, size_t from)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t>(from, 1); i < x.size(); ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

float peakOf(const std::vector<float>& x, size_t from, size_t to)
{
    float p = 0.0f;
    for (size_t i = from; i < std::min(to, x.size()); ++i)
        p = std::max(p, std::abs(x[i]));
    return p;
}

// MIDI mode, 44.1 kHz, 512-frame blocks, a playhead that runs continuously (or reports stopped).
struct StopFixture : ProcFixture
{
    static constexpr int kBlock = 512;
    static constexpr double kRate = 44100.0;
    FixedPlayHead head;
    bool running = false;
    double ppq = 0.0;
    std::vector<float> out; // left channel of every block so far

    explicit StopFixture(bool withFx)
    {
        // Loop 1 is 2 bars long: the stop on the bar line at ppq 4 is not at Loop 1's own wrap.
        proc.replaceLoop(0, cosWav(dir, "l1.wav", 32000), 2);
        if (withFx)
            proc.replaceLoop(1, cosWav(dir, "l2.wav"), 1);
        proc.waitForLoads();
        REQUIRE(proc.getSlotStatus(0) == Status::Loaded);
        if (withFx)
        {
            REQUIRE(proc.getSlotStatus(1) == Status::Loaded);
            auto s = proc.getPluginState();
            s.rules = { RuleEntry { 0, { RuleTarget { 0, 1.0f } } } };
            s.fx.setSwitch(0, FxInsert::Reverb, true);
            s.fx.params.set(FxParam::ReverbMix, 1.0);
            proc.applyEdit(s);
        }
        proc.prepareToPlay(kRate, kBlock);
        proc.setTriggerMode(TriggerMode::Midi);
        proc.setPlayHead(&head);
    }
    ~StopFixture() { proc.setPlayHead(nullptr); }

    void block(juce::MidiBuffer& midi)
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setBpm(120.0);
        info.setIsPlaying(running);
        info.setPpqPosition(ppq);
        info.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 });
        head.pos = info;
        juce::AudioBuffer<float> buf(2, kBlock);
        buf.clear();
        proc.processBlock(buf, midi);
        out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
        if (running)
            ppq += kBlock * 120.0 / (60.0 * kRate);
    }
    void block()
    {
        juce::MidiBuffer none;
        block(none);
    }
    static juce::MidiBuffer note(bool on, int frame)
    {
        juce::MidiBuffer m;
        m.addEvent(on ? juce::MidiMessage::noteOn(1, 60, 1.0f) : juce::MidiMessage::noteOff(1, 60), frame);
        return m;
    }
};

constexpr float kMaxStep = 0.01f; // a 50 Hz cosine at 0.5 moves at most 0.0036 per sample

} // namespace

TEST_CASE("Processor: a stopped-host stop fades out instead of cutting", "[processor][midi]")
{
    StopFixture f(false);
    auto on = StopFixture::note(true, 0);
    f.block(on);
    for (int b = 0; b < 3; ++b)
        f.block();
    auto off = StopFixture::note(false, 300);
    f.block(off);
    const size_t stop = 4 * StopFixture::kBlock + 300;
    for (int b = 0; b < 3; ++b)
        f.block();
    REQUIRE(peakOf(f.out, stop - 900, stop) > 0.1f); // a hard cut here would be a click
    CHECK(maxStep(f.out, StopFixture::kBlock) < kMaxStep);
    CHECK(peakOf(f.out, stop + 300, f.out.size()) == 0.0f); // silent after the ~5 ms fade
}

TEST_CASE("Processor: a stop on a running host's bar line fades out instead of cutting", "[processor][midi]")
{
    StopFixture f(false);
    f.running = true;
    auto on = StopFixture::note(true, 0);
    f.block(on); // the bar line at ppq 0 is frame 0: starts at once
    for (int b = 0; b < 9; ++b)
        f.block();
    auto off = StopFixture::note(false, 0);
    f.block(off); // stops at ppq 4 = frame 88200
    while (f.out.size() < 88200 + 4 * StopFixture::kBlock)
        f.block();
    REQUIRE(peakOf(f.out, 88200 - 900, 88200) > 0.1f);
    CHECK(maxStep(f.out, StopFixture::kBlock) < kMaxStep);
    CHECK(peakOf(f.out, 88200 + 300, f.out.size()) == 0.0f);
}

TEST_CASE("Processor: effect tails ring out after a MIDI stop, without a click", "[processor][midi][fx]")
{
    StopFixture f(true);
    auto on = StopFixture::note(true, 0);
    f.block(on);
    for (int b = 0; b < 9; ++b)
        f.block();
    auto off = StopFixture::note(false, 300);
    f.block(off);
    const size_t stop = 10 * StopFixture::kBlock + 300;
    for (int b = 0; b < 10; ++b)
        f.block();
    REQUIRE(peakOf(f.out, stop - 900, stop) > 0.05f);
    CHECK(maxStep(f.out, 2 * StopFixture::kBlock) < kMaxStep);
    CHECK(peakOf(f.out, stop + 600, f.out.size()) > 1e-4f); // the reverb still rings
}

// ---- MIDI trigger: host bypass ------------------------------------------------------------------

TEST_CASE("Processor: a note-off while bypassed is not lost", "[processor][midi]")
{
    MidiFixture f;
    f.proc.setTriggerMode(TriggerMode::Midi);
    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    f.run(on);
    REQUIRE(peak(f.run(), 0, MidiFixture::kBlock) > 0.0f);

    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::noteOff(1, 60), 10);
    juce::AudioBuffer<float> buf(2, MidiFixture::kBlock);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < MidiFixture::kBlock; ++i)
            buf.setSample(c, i, 0.25f); // garbage in: bypass must still output silence
    f.proc.processBlockBypassed(buf, off);
    CHECK(peak(buf, 0, MidiFixture::kBlock) == 0.0f);

    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f); // the note is released
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f);
}

// ---- Pass slots: Save, Clear, replay control, MIDI bindings and learn ---------------------------

namespace {

Pass onePass(int region2)
{
    Pass p;
    p.events = { PassEvent { 0.0, false, region2, PlayMode::Gate } };
    return p;
}

// Both loops loaded (one bar each), Loop-1 region 0 always starts Loop-2 region 0, so every finished
// pass has one event. Autoplay with a running host at 120 bpm, 44.1 kHz, 512-frame blocks.
struct PassFixture : ProcFixture
{
    static constexpr int kBlock = 512;
    static constexpr double kRate = 44100.0;
    FixedPlayHead head;
    double ppq = 0.0;

    PassFixture()
    {
        proc.replaceLoop(0, wav("l1.wav"), 1);
        proc.replaceLoop(1, wav("l2.wav"), 1);
        proc.waitForLoads();
        REQUIRE(proc.getSlotStatus(0) == Status::Loaded);
        REQUIRE(proc.getSlotStatus(1) == Status::Loaded);
        auto s = proc.getPluginState();
        s.rules = { RuleEntry { 0, { RuleTarget { 0, 1.0f } } } };
        proc.applyEdit(s);
        proc.prepareToPlay(kRate, kBlock);
        proc.setPlayHead(&head);
    }
    ~PassFixture() { proc.setPlayHead(nullptr); }

    void block(juce::MidiBuffer& midi)
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setBpm(120.0);
        info.setIsPlaying(true);
        info.setPpqPosition(ppq);
        info.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 });
        head.pos = info;
        juce::AudioBuffer<float> buf(2, kBlock);
        buf.clear();
        proc.processBlock(buf, midi);
        ppq += kBlock * 120.0 / (60.0 * kRate);
    }
    void block()
    {
        juce::MidiBuffer none;
        block(none);
    }
    // Plays on until the next bar line has been passed.
    void bar()
    {
        const double next = std::floor(ppq / 4.0 + 1e-9) * 4.0 + 4.0;
        while (ppq <= next)
            block();
    }
    void fill(std::initializer_list<int> slots)
    {
        auto s = proc.getPluginState();
        for (int i : slots)
            s.passes[static_cast<size_t>(i)] = onePass(0);
        proc.applyEdit(s);
    }
};

juce::MidiBuffer noteOn(int note, int channel = 1)
{
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::noteOn(channel, note, 1.0f), 0);
    return m;
}

juce::MidiBuffer noteOff(int note, int channel = 1)
{
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::noteOff(channel, note), 0);
    return m;
}

// Binds `note` to `target` through learn (one block with the note-on, one with its note-off).
void learn(MidiFixture& f, int target, int note)
{
    f.proc.armLearn(target);
    auto on = noteOn(note);
    f.run(on);
    auto off = noteOff(note);
    f.run(off);
}

juce::String stateText(DubcyclingProcessor& proc)
{
    juce::MemoryBlock block;
    proc.getStateInformation(block);
    return juce::String::fromUTF8(static_cast<const char*>(block.getData()), static_cast<int>(block.getSize()));
}

void setStateText(DubcyclingProcessor& proc, const juce::String& text)
{
    proc.setStateInformation(text.toRawUTF8(), static_cast<int>(text.getNumBytesAsUTF8()));
}

} // namespace

TEST_CASE("Processor: the default state has no passes, no bindings and replay off", "[processor][passes]")
{
    ProcFixture f;
    CHECK(f.proc.replayRequest() == 0);
    CHECK(f.proc.replaySlot() == -1);
    CHECK_FALSE(f.proc.canSave());
    CHECK_FALSE(f.proc.savePass());
    CHECK(f.proc.learnArmed() == -1);
    for (int t = 0; t < DubcyclingProcessor::kBindTargets; ++t)
        CHECK(f.proc.boundNote(t) == -1);
    for (int i = 0; i < kPassSlots; ++i)
        CHECK_FALSE(f.proc.passFilled(i));
    if (const auto m = f.proc.getModel()) // nothing published yet is silence, i.e. no passes either
        for (const auto& p : m->passes)
            CHECK(p.empty());
    CHECK_FALSE(stateText(f.proc).contains("Bind"));
    CHECK_FALSE(stateText(f.proc).contains("Passes"));
}

TEST_CASE("Processor: savePass stores each finished pass once in the lowest free slot", "[processor][passes]")
{
    PassFixture f;
    CHECK_FALSE(f.proc.savePass()); // nothing finished yet
    f.bar();
    f.bar(); // two full passes have played
    REQUIRE(f.proc.canSave());
    CHECK(f.proc.savePass());
    CHECK(f.proc.passFilled(0));
    CHECK_FALSE(f.proc.passFilled(1));
    const auto saved = f.proc.getPluginState().passes[0];
    REQUIRE(saved.events.size() == 1);
    CHECK(saved.events[0].region2 == 0);
    CHECK(f.proc.getModel()->passes[0] == saved);

    CHECK_FALSE(f.proc.canSave()); // the same pass again
    CHECK_FALSE(f.proc.savePass());
    CHECK_FALSE(f.proc.passFilled(1));

    f.bar(); // a new pass
    CHECK(f.proc.savePass());
    CHECK(f.proc.passFilled(1));
    CHECK(f.proc.getModel()->passes[1] == f.proc.getPluginState().passes[1]);
}

TEST_CASE("Processor: a pass finished before a state load cannot be saved into the loaded state", "[processor][passes]")
{
    PassFixture f;
    f.bar();
    f.bar();
    REQUIRE(f.proc.canSave());

    f.proc.restoreState(f.proc.getPluginState()); // e.g. a preset load
    f.proc.waitForLoads();
    CHECK_FALSE(f.proc.canSave());
    CHECK_FALSE(f.proc.savePass());
    CHECK_FALSE(f.proc.passFilled(0));

    f.bar(); // the pass running at the load is not complete
    f.bar(); // a pass finished after the load can be saved
    CHECK(f.proc.canSave());
    CHECK(f.proc.savePass());
    CHECK(f.proc.passFilled(0));

    f.bar();
    REQUIRE(f.proc.canSave());
    setStateText(f.proc, stateText(f.proc)); // a project load does the same
    f.proc.waitForLoads();
    CHECK_FALSE(f.proc.canSave());
    CHECK_FALSE(f.proc.savePass());
}

TEST_CASE("Processor: a free slot is required, and Save is off while a replay is requested", "[processor][passes]")
{
    PassFixture f;
    f.fill({ 0, 1, 2, 3, 4, 5, 6, 7 });
    f.bar();
    f.bar();
    CHECK_FALSE(f.proc.canSave());
    CHECK_FALSE(f.proc.savePass());

    f.proc.clearPass(5);
    CHECK_FALSE(f.proc.passFilled(5));
    CHECK(f.proc.canSave());
    f.proc.setReplayRequest(1);
    CHECK(f.proc.replayRequest() == 1);
    CHECK_FALSE(f.proc.canSave());
    CHECK_FALSE(f.proc.savePass());
    f.proc.setReplayRequest(0);
    CHECK(f.proc.savePass());
    CHECK(f.proc.passFilled(5)); // the lowest free slot
}

TEST_CASE("Processor: an empty finished pass cannot be saved", "[processor][passes]")
{
    PassFixture f;
    auto s = f.proc.getPluginState();
    s.rules.clear(); // nothing is drawn: the pass has no events
    f.proc.applyEdit(s);
    f.bar();
    f.bar();
    CHECK_FALSE(f.proc.canSave());
    CHECK_FALSE(f.proc.savePass());
}

TEST_CASE("Processor: clearPass and clearAllPasses empty the slots in state and model", "[processor][passes]")
{
    PassFixture f;
    f.fill({ 0, 2, 3 });
    CHECK(f.proc.passFilled(2));
    CHECK(f.proc.getModel()->passes[2] == onePass(0));
    f.proc.clearPass(2);
    CHECK_FALSE(f.proc.passFilled(2));
    CHECK(f.proc.getModel()->passes[2].empty());
    CHECK(f.proc.passFilled(0));
    f.proc.clearPass(-1); // out of range: nothing
    f.proc.clearPass(8);
    f.proc.clearAllPasses();
    for (int i = 0; i < kPassSlots; ++i)
    {
        CHECK_FALSE(f.proc.passFilled(i));
        CHECK(f.proc.getModel()->passes[static_cast<size_t>(i)].empty());
    }
    CHECK(f.proc.getPluginState().loop1.fileName == "l1.wav"); // nothing else changed
    CHECK(f.proc.getPluginState().rules.size() == 1);
}

TEST_CASE("Processor: learn binds the next note-on, which never gates, and moves a bound note", "[processor][passes][learn]")
{
    MidiFixture f;
    f.proc.setTriggerMode(TriggerMode::Midi);
    const int v0 = f.proc.bindingsVersion();
    f.proc.armLearn(3);
    CHECK(f.proc.learnArmed() == 3);
    auto on = noteOn(60);
    CHECK(peak(f.run(on), 0, MidiFixture::kBlock) == 0.0f); // consumed, no gate
    CHECK(f.proc.boundNote(3) == 60);
    CHECK(f.proc.learnArmed() == -1);
    CHECK(f.proc.bindingsVersion() != v0);
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f);
    auto off = noteOff(60);
    f.run(off);

    CHECK(peak(f.run(on), 0, MidiFixture::kBlock) == 0.0f); // bound: never gates
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f);
    f.run(off);

    auto on61 = noteOn(61); // others gate as before
    f.run(on61);
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) > 0.0f);
    auto off61 = noteOff(61);
    f.run(off61);
    f.run();
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f);

    learn(f, 5, 60); // one note, one target
    CHECK(f.proc.boundNote(5) == 60);
    CHECK(f.proc.boundNote(3) == -1);

    f.proc.armLearn(7);
    f.proc.armLearn(-1); // cancel
    CHECK(f.proc.learnArmed() == -1);
    f.run(on61);
    CHECK(f.proc.boundNote(7) == -1);
    f.run(off61);

    const int v1 = f.proc.bindingsVersion();
    f.proc.clearBinding(5);
    CHECK(f.proc.boundNote(5) == -1);
    CHECK(f.proc.bindingsVersion() != v1);
}

TEST_CASE("Processor: learning a held note does not leave the gate stuck", "[processor][passes][learn]")
{
    MidiFixture f;
    f.proc.setTriggerMode(TriggerMode::Midi);
    auto on = noteOn(60);
    f.run(on);
    REQUIRE(peak(f.run(), 0, MidiFixture::kBlock) > 0.0f); // held, plays

    f.proc.armLearn(2);
    auto again = noteOn(60, 2); // the same note from another channel binds it
    f.run(again);
    CHECK(f.proc.boundNote(2) == 60);
    f.run();
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f); // the held gate was released
    auto off = noteOff(60);
    f.run(off); // the bound note-off is consumed
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f);

    auto on61 = noteOn(61);
    f.run(on61);
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) > 0.0f);
    auto off61 = noteOff(61);
    f.run(off61);
    f.run();
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f); // no held note left over
}

TEST_CASE("Processor: bound slot and Play notes set the replay request", "[processor][passes][learn]")
{
    MidiFixture f;
    auto s = f.proc.getPluginState();
    s.passes[1] = onePass(0);
    f.proc.applyEdit(s);
    learn(f, 1, 62);
    learn(f, 4, 63); // slot 4 is empty
    learn(f, DubcyclingProcessor::kTargetPlay, 64);
    CHECK(f.proc.boundNote(DubcyclingProcessor::kTargetPlay) == 64);

    auto empty = noteOn(63);
    f.run(empty);
    CHECK(f.proc.replayRequest() == 0); // ignored for an empty slot
    auto slot1 = noteOn(62);
    f.run(slot1);
    CHECK(f.proc.replayRequest() == 2 + 1);

    auto play = noteOn(64);
    auto playOff = noteOff(64);
    f.run(play); // any replay -> off
    CHECK(f.proc.replayRequest() == 0);
    f.run(playOff);
    f.run(play); // off -> chain
    CHECK(f.proc.replayRequest() == 1);
    f.run(playOff);
    f.run(play);
    CHECK(f.proc.replayRequest() == 0);
}

TEST_CASE("Processor: a bound Play note does not turn replay on while every slot is empty", "[processor][passes][learn]")
{
    PassFixture f;
    f.proc.armLearn(DubcyclingProcessor::kTargetPlay);
    auto on = noteOn(64);
    auto off = noteOff(64);
    f.block(on);
    f.block(off);
    REQUIRE(f.proc.boundNote(DubcyclingProcessor::kTargetPlay) == 64);
    f.bar();
    f.bar();
    REQUIRE(f.proc.canSave());
    f.block(on);
    f.block(off);
    CHECK(f.proc.replayRequest() == 0); // nothing to play
    CHECK(f.proc.canSave());            // and Save stays available

    f.proc.setReplayRequest(1); // any replay -> off still works
    f.block(on);
    CHECK(f.proc.replayRequest() == 0);
}

TEST_CASE("Processor: a held note that a state load binds does not leave the gate stuck", "[processor][passes][learn]")
{
    MidiFixture f;
    f.proc.setTriggerMode(TriggerMode::Midi);
    auto on = noteOn(60, 3);
    f.run(on);
    REQUIRE(peak(f.run(), 0, MidiFixture::kBlock) > 0.0f); // held, plays

    // A project whose bindings put note 60 on slot 1: its note-off will never reach the gate.
    const auto text = stateText(f.proc).replace("<DubcyclingState", "<DubcyclingState BindSlot1=\"60\"");
    setStateText(f.proc, text);
    f.proc.waitForLoads();
    REQUIRE(f.proc.boundNote(1) == 60);
    REQUIRE(f.proc.triggerMode() == TriggerMode::Midi);
    f.run();
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) == 0.0f); // the held gate was released

    auto on61 = noteOn(61); // other notes still gate
    f.run(on61);
    CHECK(peak(f.run(), 0, MidiFixture::kBlock) > 0.0f);
}

TEST_CASE("Processor: a bound Save note saves on the message thread", "[processor][passes][learn]")
{
    PassFixture f;
    f.proc.armLearn(DubcyclingProcessor::kTargetSave);
    auto on = noteOn(65);
    f.block(on);
    CHECK(f.proc.boundNote(DubcyclingProcessor::kTargetSave) == 65);
    f.bar();
    f.bar();
    f.proc.waitForLoads(); // runs the timer
    CHECK_FALSE(f.proc.passFilled(0)); // learning did not save
    f.block(on);
    f.proc.waitForLoads();
    CHECK(f.proc.passFilled(0));
}

TEST_CASE("Processor: bindings are saved with the project, not with presets", "[processor][passes][learn]")
{
    MidiFixture f;
    learn(f, 0, 60);
    learn(f, 7, 67);
    learn(f, DubcyclingProcessor::kTargetSave, 70);
    learn(f, DubcyclingProcessor::kTargetPlay, 71);
    const auto text = stateText(f.proc);
    CHECK(deserialize(text.toRawUTF8(), text.getNumBytesAsUTF8()).has_value());

    DubcyclingProcessor other;
    setStateText(other, text);
    CHECK(other.boundNote(0) == 60);
    CHECK(other.boundNote(7) == 67);
    CHECK(other.boundNote(DubcyclingProcessor::kTargetSave) == 70);
    CHECK(other.boundNote(DubcyclingProcessor::kTargetPlay) == 71);
    CHECK(other.boundNote(3) == -1);

    // Presets neither carry nor change them.
    other.restoreState(PluginState {});
    CHECK(other.boundNote(0) == 60);
    CHECK(other.boundNote(DubcyclingProcessor::kTargetPlay) == 71);
    Bank bank;
    bank.presets.push_back(Preset { "p", f.proc.getPluginState() });
    CHECK_FALSE(bankToJson(bank).containsIgnoreCase("bind"));

    // Together with the trigger mode.
    f.proc.setTriggerMode(TriggerMode::Midi);
    setStateText(other, stateText(f.proc));
    CHECK(other.triggerMode() == TriggerMode::Midi);
    CHECK(other.boundNote(7) == 67);

    // Invalid values are unbound, a duplicate note keeps the first target, a missing one is unbound.
    auto bad = text.replace("=\"60\"", "=\"200\"").replace("=\"67\"", "=\"x\"").replace("=\"71\"", "=\"70\"");
    setStateText(other, bad);
    CHECK(other.boundNote(0) == -1);
    CHECK(other.boundNote(7) == -1);
    CHECK(other.boundNote(DubcyclingProcessor::kTargetSave) == 70);
    CHECK(other.boundNote(DubcyclingProcessor::kTargetPlay) == -1);
    setStateText(other, text.replace("Bind", "Xind"));
    for (int t = 0; t < DubcyclingProcessor::kBindTargets; ++t)
        CHECK(other.boundNote(t) == -1);
}

TEST_CASE("Processor: loading a preset with passes fills the model and resets replay", "[processor][passes][presets]")
{
    ProcFixture f;
    f.proc.setReplayRequest(2 + 3);
    PluginState s;
    s.passes[3] = onePass(1);
    s.passes[6] = onePass(2);
    f.proc.restoreState(s);
    CHECK(f.proc.replayRequest() == 0);
    CHECK(f.proc.passFilled(3));
    CHECK(f.proc.passFilled(6));
    CHECK_FALSE(f.proc.passFilled(0));
    const auto m = f.proc.getModel();
    REQUIRE(m != nullptr);
    CHECK(m->passes[3] == onePass(1));
    CHECK(m->passes[6] == onePass(2));

    // A project load does the same.
    DubcyclingProcessor other;
    other.setReplayRequest(1);
    juce::MemoryBlock block;
    f.proc.getStateInformation(block);
    other.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK(other.replayRequest() == 0);
    CHECK(other.getModel()->passes[6] == onePass(2));

    // An edit carries the passes too.
    auto e = f.proc.getPluginState();
    e.passes[3] = Pass {};
    e.passes[0] = onePass(0);
    f.proc.applyEdit(e);
    CHECK_FALSE(f.proc.passFilled(3));
    CHECK(f.proc.passFilled(0));
    CHECK(f.proc.getModel()->passes[0] == onePass(0));
}
