// BankStore: bank folders, saving presets with sample handling, loading presets.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <juce_audio_formats/juce_audio_formats.h>
#include "plugin/PluginState.h"
#include "plugin/presets/BankFormat.h"
#include "plugin/presets/BankStore.h"

using namespace dc;
using Error = BankStore::Error;

namespace {

struct StoreFixture
{
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dcbank", "", false);
    juce::File loops = dir.getChildFile("loops");
    BankStore store { dir.getChildFile("Presets") };

    StoreFixture() { loops.createDirectory(); }
    ~StoreFixture() { dir.deleteRecursively(); }

    // Small WAV; a different `freq` gives different content (and hash).
    juce::File wav(const juce::File& file, float freq = 0.01f)
    {
        file.getParentDirectory().createDirectory();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream>(file);
        auto writer = format.createWriterFor(out, juce::AudioFormatWriterOptions {}
                                                      .withSampleRate(8000)
                                                      .withNumChannels(1)
                                                      .withBitsPerSample(16));
        REQUIRE(writer != nullptr);
        const int frames = 800;
        juce::AudioBuffer<float> buf(1, frames);
        for (int i = 0; i < frames; ++i)
            buf.setSample(0, i, 0.5f * std::sin(static_cast<float>(i) * freq));
        REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, frames));
        return file;
    }

    juce::File wav(const juce::String& name, float freq = 0.01f) { return wav(loops.getChildFile(name), freq); }

    static LoopRef ref(const juce::File& file, int bars = 2)
    {
        LoopRef l;
        l.path = file.getFullPathName();
        l.fileName = file.getFileName();
        l.hash = hashFile(file);
        l.bars = bars;
        l.boundaries = { 0.5 };
        return l;
    }

    static PluginState state(const juce::File& f1, const juce::File& f2)
    {
        PluginState s;
        s.loop1 = ref(f1);
        s.loop2 = ref(f2, 4);
        s.rules.push_back({ 0, { { 1, 1.0f, PlayMode::Gate } } });
        return s;
    }

    juce::File bankJson(const juce::String& bank) const { return store.bankDir(bank).getChildFile("bank.json"); }
    juce::File samples(const juce::String& bank) const { return store.bankDir(bank).getChildFile("samples"); }
    int sampleCount(const juce::String& bank) const { return samples(bank).getNumberOfChildFiles(juce::File::findFiles); }

    std::vector<juce::String> presetNames(const juce::String& bank) const
    {
        std::vector<juce::String> names;
        if (auto b = store.readBank(bank).bank)
            for (auto& p : b->presets)
                names.push_back(p.name);
        return names;
    }

    Preset storedPreset(const juce::String& bank, const juce::String& name) const
    {
        auto b = store.readBank(bank).bank;
        REQUIRE(b.has_value());
        for (auto& p : b->presets)
            if (p.name == name)
                return p;
        FAIL("preset not found: " << name);
        return {};
    }
};

} // namespace

TEST_CASE("BankStore: createBank makes folder, samples and an empty bank.json")
{
    StoreFixture f;
    const auto r = f.store.createBank("Dub One");
    REQUIRE(r.ok());
    CHECK(r.error == Error::None);
    CHECK(f.store.bankDir("Dub One") == f.store.root().getChildFile("Dub One"));
    CHECK(f.store.bankDir("Dub One").isDirectory());
    CHECK(f.samples("Dub One").isDirectory());
    REQUIRE(f.bankJson("Dub One").existsAsFile());
    const auto parsed = f.store.readBank("Dub One");
    REQUIRE(parsed.bank.has_value());
    CHECK(parsed.bank->presets.empty());
    CHECK(f.store.listBanks() == juce::StringArray { "Dub One" });
}

TEST_CASE("BankStore: createBank rejects a bad or taken name")
{
    StoreFixture f;
    REQUIRE(f.store.createBank("Dub").ok());

    auto r = f.store.createBank("a/b");
    CHECK(r.error == Error::InvalidName);
    CHECK(r.message.isNotEmpty());
    CHECK(f.store.createBank("").error == Error::InvalidName);
    CHECK(f.store.createBank("con").error == Error::InvalidName);

    r = f.store.createBank("dub");
    CHECK(r.error == Error::NameTaken);
    CHECK(r.message.isNotEmpty());

    // A folder without bank.json is not a bank, but its name is still taken.
    f.store.bankDir("Folder").createDirectory();
    CHECK(f.store.createBank("Folder").error == Error::NameTaken);
    CHECK_FALSE(f.bankJson("Folder").exists());
}

TEST_CASE("BankStore: savePreset copies both loops into samples and stores relative paths")
{
    StoreFixture f;
    REQUIRE(f.store.createBank("B").ok());
    const auto a = f.wav("a.wav", 0.01f);
    const auto b = f.wav("b.wav", 0.02f);
    const auto hashA = hashFile(a);

    REQUIRE(f.store.savePreset("B", "First", StoreFixture::state(a, b), false).ok());

    CHECK(f.samples("B").getChildFile("a.wav").existsAsFile());
    CHECK(f.samples("B").getChildFile("b.wav").existsAsFile());
    CHECK(a.existsAsFile());
    CHECK(b.existsAsFile());
    CHECK(hashFile(a) == hashA);

    const auto json = f.bankJson("B").loadFileAsString();
    CHECK(json.contains("samples/a.wav"));
    CHECK(json.contains("samples/b.wav"));
    CHECK_FALSE(json.contains(f.loops.getFullPathName().replace("\\", "/")));
    CHECK_FALSE(json.contains(f.dir.getFileName()));

    const auto p = f.storedPreset("B", "First");
    CHECK(p.state.loop1.path == "samples/a.wav");
    CHECK(p.state.loop1.fileName == "a.wav");
    CHECK(p.state.loop1.hash == hashA);
    CHECK(p.state.loop1.bars == 2);
    CHECK(p.state.loop2.bars == 4);
    CHECK(p.state.rules.size() == 1);
}

TEST_CASE("BankStore: a loop already inside samples is not copied again")
{
    StoreFixture f;
    REQUIRE(f.store.createBank("B").ok());
    REQUIRE(f.store.savePreset("B", "First", StoreFixture::state(f.wav("a.wav", 0.01f), f.wav("b.wav", 0.02f)), false).ok());
    REQUIRE(f.sampleCount("B") == 2);

    const auto loaded = f.store.loadPreset("B", "First");
    REQUIRE(loaded.result.ok());
    REQUIRE(f.store.savePreset("B", "Second", loaded.state, false).ok());

    CHECK(f.sampleCount("B") == 2);
    CHECK(f.storedPreset("B", "Second").state.loop1.path == "samples/a.wav");
    CHECK(f.storedPreset("B", "Second").state.loop2.path == "samples/b.wav");
}

TEST_CASE("BankStore: same file name with a different hash gets a counter")
{
    StoreFixture f;
    REQUIRE(f.store.createBank("B").ok());
    const auto first = f.wav(f.dir.getChildFile("one").getChildFile("kick.wav"), 0.01f);
    const auto other = f.wav(f.dir.getChildFile("two").getChildFile("kick.wav"), 0.03f);
    const auto same = f.wav(f.dir.getChildFile("three").getChildFile("kick.wav"), 0.01f);
    const auto empty = PluginState {};

    PluginState s1 = empty;
    s1.loop1 = StoreFixture::ref(first);
    REQUIRE(f.store.savePreset("B", "P1", s1, false).ok());
    const auto storedFirst = f.samples("B").getChildFile("kick.wav");
    const auto firstHash = hashFile(storedFirst);
    CHECK(firstHash == hashFile(first));

    PluginState s2 = empty;
    s2.loop1 = StoreFixture::ref(other);
    REQUIRE(f.store.savePreset("B", "P2", s2, false).ok());
    const auto storedSecond = f.samples("B").getChildFile("kick (2).wav");
    REQUIRE(storedSecond.existsAsFile());
    CHECK(hashFile(storedSecond) == hashFile(other));
    CHECK(hashFile(storedFirst) == firstHash);
    CHECK(f.storedPreset("B", "P2").state.loop1.path == "samples/kick (2).wav");
    CHECK(f.storedPreset("B", "P2").state.loop1.hash == hashFile(other));

    PluginState s3 = empty;
    s3.loop1 = StoreFixture::ref(same);
    REQUIRE(f.store.savePreset("B", "P3", s3, false).ok());
    CHECK(f.sampleCount("B") == 2);
    CHECK(f.storedPreset("B", "P3").state.loop1.path == "samples/kick.wav");

    // Same content as the counted copy: that copy is reused, no third file.
    PluginState s4 = empty;
    s4.loop1 = StoreFixture::ref(f.wav(f.dir.getChildFile("four").getChildFile("kick.wav"), 0.03f));
    REQUIRE(f.store.savePreset("B", "P4", s4, false).ok());
    CHECK(f.sampleCount("B") == 2);
    CHECK(f.storedPreset("B", "P4").state.loop1.path == "samples/kick (2).wav");
}

TEST_CASE("BankStore: save without overwrite refuses an existing name; with overwrite keeps its position")
{
    StoreFixture f;
    REQUIRE(f.store.createBank("B").ok());
    const auto a = f.wav("a.wav", 0.01f);
    const auto b = f.wav("b.wav", 0.02f);
    const auto c = f.wav("c.wav", 0.04f);
    REQUIRE(f.store.savePreset("B", "One", StoreFixture::state(a, b), false).ok());
    REQUIRE(f.store.savePreset("B", "Two", StoreFixture::state(a, b), false).ok());
    REQUIRE(f.store.savePreset("B", "Three", StoreFixture::state(a, b), false).ok());

    const auto before = f.bankJson("B").loadFileAsString();
    const auto refused = f.store.savePreset("B", "two", StoreFixture::state(c, b), false);
    CHECK(refused.error == Error::NameTaken);
    CHECK(refused.message.isNotEmpty());
    CHECK(f.bankJson("B").loadFileAsString() == before);

    CHECK(f.store.savePreset("B", "  ", StoreFixture::state(a, b), false).error == Error::InvalidName);
    CHECK(f.store.savePreset("B", juce::String::repeatedString("x", kMaxPresetNameLength + 1), StoreFixture::state(a, b), false).error
          == Error::InvalidName);

    REQUIRE(f.store.savePreset("B", "Two", StoreFixture::state(c, b), true).ok());
    CHECK(f.presetNames("B") == std::vector<juce::String> { "One", "Two", "Three" });
    CHECK(f.storedPreset("B", "Two").state.loop1.path == "samples/c.wav");
    CHECK(f.storedPreset("B", "One").state.loop1.path == "samples/a.wav");

    // Overwrite of a name that does not exist yet appends it.
    REQUIRE(f.store.savePreset("B", " Four ", StoreFixture::state(a, b), true).ok());
    CHECK(f.presetNames("B") == std::vector<juce::String> { "One", "Two", "Three", "Four" });
}

TEST_CASE("BankStore: saving never removes a sample from samples")
{
    StoreFixture f;
    REQUIRE(f.store.createBank("B").ok());
    const auto a = f.wav("a.wav", 0.01f);
    const auto b = f.wav("b.wav", 0.02f);
    const auto c = f.wav("c.wav", 0.04f);
    REQUIRE(f.store.savePreset("B", "P", StoreFixture::state(a, b), false).ok());
    REQUIRE(f.store.savePreset("B", "P", StoreFixture::state(c, b), true).ok());

    CHECK(f.samples("B").getChildFile("a.wav").existsAsFile());
    CHECK(f.samples("B").getChildFile("c.wav").existsAsFile());
    CHECK(f.sampleCount("B") == 3);
    CHECK(f.storedPreset("B", "P").state.loop1.path == "samples/c.wav");
}

TEST_CASE("BankStore: a missing loop file refuses the save with SampleMissing and writes nothing")
{
    StoreFixture f;
    REQUIRE(f.store.createBank("B").ok());
    const auto a = f.wav("a.wav", 0.01f);
    const auto gone = f.loops.getChildFile("gone.wav");
    const auto before = f.bankJson("B").loadFileAsString();

    const auto r = f.store.savePreset("B", "P", StoreFixture::state(a, gone), false);
    CHECK(r.error == Error::SampleMissing);
    CHECK(r.message.contains("gone.wav"));
    CHECK(f.bankJson("B").loadFileAsString() == before);
    CHECK(f.sampleCount("B") == 0);
    CHECK_FALSE(f.bankJson("B").getSiblingFile("bank.json.tmp").exists());

    SECTION("an empty slot is saved as empty")
    {
        PluginState s;
        s.loop1 = StoreFixture::ref(a);
        REQUIRE(f.store.savePreset("B", "Half", s, false).ok());
        const auto p = f.storedPreset("B", "Half");
        CHECK(p.state.loop1.path == "samples/a.wav");
        CHECK(p.state.loop2.path.isEmpty());
        CHECK(p.state.loop2.hash.isEmpty());
        CHECK(f.sampleCount("B") == 1);

        const auto loaded = f.store.loadPreset("B", "Half");
        REQUIRE(loaded.result.ok());
        CHECK(loaded.state.loop2.path.isEmpty());
    }
}

TEST_CASE("BankStore: a damaged or too-new bank.json is not overwritten")
{
    StoreFixture f;
    REQUIRE(f.store.createBank("B").ok());
    const auto a = f.wav("a.wav", 0.01f);
    const auto b = f.wav("b.wav", 0.02f);

    SECTION("damaged")
    {
        const juce::String text = "{ this is not json";
        REQUIRE(f.bankJson("B").replaceWithText(text));
        const auto r = f.store.savePreset("B", "P", StoreFixture::state(a, b), false);
        CHECK(r.error == Error::BankUnreadable);
        CHECK(r.message.isNotEmpty());
        CHECK(f.bankJson("B").loadFileAsString() == text);
        CHECK(f.store.loadPreset("B", "P").result.error == Error::BankUnreadable);
    }
    SECTION("too new")
    {
        const juce::String text = "{\"version\": 99, \"presets\": []}";
        REQUIRE(f.bankJson("B").replaceWithText(text));
        const auto r = f.store.savePreset("B", "P", StoreFixture::state(a, b), false);
        CHECK(r.error == Error::BankTooNew);
        CHECK(r.message.isNotEmpty());
        CHECK(f.bankJson("B").loadFileAsString() == text);
        CHECK(f.store.loadPreset("B", "P").result.error == Error::BankTooNew);
    }
    SECTION("missing bank")
    {
        CHECK(f.store.savePreset("Nope", "P", StoreFixture::state(a, b), false).error == Error::BankMissing);
        CHECK(f.store.loadPreset("Nope", "P").result.error == Error::BankMissing);
        CHECK_FALSE(f.store.bankDir("Nope").exists());
    }
}

TEST_CASE("BankStore: loadPreset gives absolute paths that exist and the stored hash")
{
    StoreFixture f;
    REQUIRE(f.store.createBank("B").ok());
    const auto a = f.wav("a.wav", 0.01f);
    const auto b = f.wav("b.wav", 0.02f);
    REQUIRE(f.store.savePreset("B", "P", StoreFixture::state(a, b), false).ok());
    const auto stored = f.storedPreset("B", "P");

    const auto loaded = f.store.loadPreset("B", "p");
    REQUIRE(loaded.result.ok());
    const juce::File l1(loaded.state.loop1.path);
    const juce::File l2(loaded.state.loop2.path);
    CHECK(juce::File::isAbsolutePath(loaded.state.loop1.path));
    CHECK(l1 == f.samples("B").getChildFile("a.wav"));
    CHECK(l2 == f.samples("B").getChildFile("b.wav"));
    CHECK(l1.existsAsFile());
    CHECK(l2.existsAsFile());
    CHECK(loaded.state.loop1.hash == stored.state.loop1.hash);
    CHECK(loaded.state.loop1.hash == hashFile(l1));
    CHECK(loaded.state.loop2.hash == hashFile(l2));
    CHECK(loaded.state.loop2.bars == 4);
    CHECK(loaded.state.rules.size() == 1);
    CHECK(loaded.state.activeBank == "B");
    CHECK(loaded.state.activePreset == "P");

    const auto missing = f.store.loadPreset("B", "Unknown");
    CHECK(missing.result.error == Error::PresetMissing);
    CHECK(missing.result.message.isNotEmpty());
}

TEST_CASE("BankStore: listBanks is alphabetical and ignores folders without bank.json")
{
    StoreFixture f;
    CHECK(f.store.listBanks().isEmpty());
    REQUIRE(f.store.createBank("beta").ok());
    REQUIRE(f.store.createBank("Alpha").ok());
    REQUIRE(f.store.createBank("Gamma").ok());
    f.store.root().getChildFile("Delta").createDirectory();
    f.store.root().getChildFile("loose.json").replaceWithText("{}");

    CHECK(f.store.listBanks() == juce::StringArray { "Alpha", "beta", "Gamma" });
}

TEST_CASE("BankStore: save re-reads bank.json right before writing")
{
    StoreFixture f;
    REQUIRE(f.store.createBank("B").ok());
    const auto a = f.wav("a.wav", 0.01f);
    const auto b = f.wav("b.wav", 0.02f);
    REQUIRE(f.store.savePreset("B", "Mine", StoreFixture::state(a, b), false).ok());

    // Another instance adds a preset behind our back.
    auto bank = f.store.readBank("B").bank;
    REQUIRE(bank.has_value());
    Preset other;
    other.name = "Theirs";
    bank->presets.push_back(other);
    REQUIRE(f.bankJson("B").replaceWithText(bankToJson(*bank)));

    REQUIRE(f.store.savePreset("B", "Mine Too", StoreFixture::state(a, b), false).ok());
    CHECK(f.presetNames("B") == std::vector<juce::String> { "Mine", "Theirs", "Mine Too" });
}
