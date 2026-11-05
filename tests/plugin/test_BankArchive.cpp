// BankArchive: zip export of a bank and the safe import of a zip from someone else.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <set>
#include <tuple>
#include <juce_audio_formats/juce_audio_formats.h>
#include "plugin/PluginState.h"
#include "plugin/presets/BankArchive.h"
#include "plugin/presets/BankFormat.h"
#include "plugin/presets/BankStore.h"

using namespace dc;
using Error = BankStore::Error;

namespace {

struct ArchiveFixture
{
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dcarchive", "", false);
    juce::File loops = dir.getChildFile("loops");
    BankStore store { dir.getChildFile("Presets") };
    BankStore other { dir.getChildFile("Other") }; // a second machine's preset folder, not created yet

    ArchiveFixture() { loops.createDirectory(); }
    ~ArchiveFixture() { dir.deleteRecursively(); }

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

    static PluginState state(const juce::File& f1, const juce::File& f2)
    {
        PluginState s;
        for (auto [l, f, bars] : { std::tuple { &s.loop1, f1, 2 }, std::tuple { &s.loop2, f2, 4 } })
        {
            l->path = f.getFullPathName();
            l->fileName = f.getFileName();
            l->hash = hashFile(f);
            l->bars = bars;
            l->boundaries = { 0.5 };
        }
        s.rules.push_back({ 0, { { 1, 1.0f, PlayMode::Gate } } });
        return s;
    }

    // Bank "Dub" with presets "One" (a, b) and "Two" (c, b) plus an unused sample in samples/.
    void makeBank()
    {
        REQUIRE(store.createBank("Dub").ok());
        const auto a = wav("a.wav", 0.01f);
        const auto b = wav("b.wav", 0.02f);
        const auto c = wav("c.wav", 0.03f);
        REQUIRE(store.savePreset("Dub", "One", state(a, b), false).ok());
        REQUIRE(store.savePreset("Dub", "Two", state(c, b), false).ok());
        wav(store.bankDir("Dub").getChildFile("samples").getChildFile("unused.wav"), 0.05f);
    }

    juce::File zip(const juce::String& name) const { return dir.getChildFile(name); }

    // Everything below `root`, relative, with '/'.
    static std::set<juce::String> tree(const juce::File& root)
    {
        std::set<juce::String> out;
        for (auto& f : root.findChildFiles(juce::File::findFilesAndDirectories, true))
            out.insert(f.getRelativePathFrom(root).replaceCharacter('\\', '/'));
        return out;
    }
};

std::set<juce::String> entryNames(const juce::File& zipFile)
{
    juce::ZipFile zip(zipFile);
    std::set<juce::String> names;
    for (int i = 0; i < zip.getNumEntries(); ++i)
        names.insert(zip.getEntry(i)->filename);
    return names;
}

// A zip with exactly these entries (name, content); names are stored as given, unchecked.
juce::File handZip(const juce::File& file, const std::vector<std::pair<juce::String, juce::String>>& entries)
{
    juce::ZipFile::Builder builder;
    for (auto& [name, content] : entries)
        builder.addEntry(std::make_unique<juce::MemoryInputStream>(content.toRawUTF8(), content.getNumBytesAsUTF8(), true), 0,
                         name, juce::Time::getCurrentTime());
    file.deleteFile();
    juce::FileOutputStream out(file);
    REQUIRE(out.openedOk());
    REQUIRE(builder.writeToStream(out, nullptr));
    return file;
}

// Changes the uncompressed size that the central directory declares for `entry`.
void patchDeclaredSize(const juce::File& zipFile, const juce::String& entry, juce::uint32 declared)
{
    juce::MemoryBlock data;
    REQUIRE(zipFile.loadFileAsData(data));
    auto* bytes = static_cast<juce::uint8*>(data.getData());
    bool patched = false;
    for (size_t i = 0; i + 46 <= data.getSize(); ++i)
    {
        if (juce::ByteOrder::littleEndianInt(bytes + i) != 0x02014b50)
            continue;
        const auto nameLen = juce::ByteOrder::littleEndianShort(bytes + i + 28);
        if (juce::String::fromUTF8(reinterpret_cast<const char*>(bytes + i + 46), nameLen) != entry)
            continue;
        for (int b = 0; b < 4; ++b)
            bytes[i + 24 + static_cast<size_t>(b)] = static_cast<juce::uint8>(declared >> (8 * b));
        patched = true;
    }
    REQUIRE(patched);
    REQUIRE(zipFile.replaceWithData(data.getData(), data.getSize()));
}

juce::String emptyBankJson() { return bankToJson({}); }

// bank.json whose one preset uses `path` for loop1.
juce::String bankJsonWithLoop(const juce::String& path)
{
    Bank bank;
    bank.presets.push_back({ "P", {} });
    bank.presets.back().state.loop1.path = path;
    bank.presets.back().state.loop1.fileName = "a.wav";
    bank.presets.back().state.loop1.bars = 2;
    return bankToJson(bank);
}

} // namespace

TEST_CASE("Archive: export packs bank.json and only used samples under one top folder")
{
    ArchiveFixture f;
    f.makeBank();
    const auto target = f.zip("Dub.zip");

    const auto r = exportBank(f.store, "Dub", target);
    REQUIRE(r.ok());
    CHECK(r.message.isEmpty());
    REQUIRE(target.existsAsFile());
    CHECK(entryNames(target) == std::set<juce::String> { "Dub/bank.json", "Dub/samples/a.wav", "Dub/samples/b.wav", "Dub/samples/c.wav" });
    CHECK(f.store.bankDir("Dub").getChildFile("samples").getChildFile("unused.wav").existsAsFile());
    // No temp file next to the zip.
    CHECK(f.dir.getNumberOfChildFiles(juce::File::findFiles) == 1);
}

TEST_CASE("Archive: export with a missing sample fails and writes no zip")
{
    ArchiveFixture f;
    f.makeBank();
    REQUIRE(f.store.bankDir("Dub").getChildFile("samples").getChildFile("c.wav").deleteFile());
    const auto target = f.zip("Dub.zip");

    const auto r = exportBank(f.store, "Dub", target);
    CHECK(r.error == Error::SampleMissing);
    CHECK(r.message.contains("c.wav"));
    CHECK_FALSE(target.exists());
    CHECK(f.dir.getNumberOfChildFiles(juce::File::findFiles) == 0);

    SECTION("an existing zip at the target stays as it was")
    {
        REQUIRE(target.replaceWithText("old"));
        CHECK(exportBank(f.store, "Dub", target).error == Error::SampleMissing);
        CHECK(target.loadFileAsString() == "old");
    }
    SECTION("a bank that does not exist")
    {
        CHECK(exportBank(f.store, "Nope", target).error == Error::BankMissing);
        CHECK_FALSE(target.exists());
    }
}

TEST_CASE("Archive: export refuses sample names that import would reject")
{
    ArchiveFixture f;
    f.makeBank();
    const auto target = f.zip("Dub.zip");
    auto bank = f.store.readBank("Dub").bank;
    REQUIRE(bank.has_value());
    const auto samples = f.store.bankDir("Dub").getChildFile("samples");

    SECTION("not portable")
    {
        // Such files cannot be created on Windows (con.wav would even open the console device),
        // so only the stored path names them: the name is refused before the file is looked up.
        for (const juce::String name : { "take?.wav", "con.wav", "end." })
        {
            INFO("name " << name);
            bank->presets[0].state.loop1.path = "samples/" + name;
            REQUIRE(f.store.bankDir("Dub").getChildFile("bank.json").replaceWithText(bankToJson(*bank)));
            const auto r = exportBank(f.store, "Dub", target);
            CHECK(r.error == Error::SampleNameInvalid);
            CHECK(r.message.contains(name));
            CHECK_FALSE(target.exists());
            CHECK(f.dir.getNumberOfChildFiles(juce::File::findFiles) == 0);
        }
    }
    SECTION("two spellings of one name")
    {
        // On a case-insensitive file system A.wav is a.wav; elsewhere it is a second file.
        if (!samples.getChildFile("A.wav").exists())
            REQUIRE(samples.getChildFile("a.wav").copyFileTo(samples.getChildFile("A.wav")));
        bank->presets[1].state.loop1.path = "samples/A.wav";
        REQUIRE(f.store.bankDir("Dub").getChildFile("bank.json").replaceWithText(bankToJson(*bank)));
        const auto r = exportBank(f.store, "Dub", target);
        CHECK(r.error == Error::SampleNameInvalid);
        CHECK(r.message.contains("A.wav"));
        CHECK_FALSE(target.exists());
        CHECK(f.dir.getNumberOfChildFiles(juce::File::findFiles) == 0);
    }
}

TEST_CASE("Archive: export then import into another root gives a working bank")
{
    ArchiveFixture f;
    f.makeBank();
    const auto target = f.zip("Dub.zip");
    REQUIRE(exportBank(f.store, "Dub", target).ok());

    juce::String imported;
    const auto r = importBank(f.other, target, imported);
    REQUIRE(r.ok());
    CHECK(imported == "Dub");
    CHECK(f.other.listBanks() == juce::StringArray { "Dub" });
    CHECK_FALSE(f.other.bankDir("Dub").getChildFile("samples").getChildFile("unused.wav").exists());
    // Only the bank folder, no leftovers.
    CHECK(f.other.root().getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 1);

    for (const juce::String name : { "One", "Two" })
    {
        const auto original = f.store.readBank("Dub").bank;
        REQUIRE(original.has_value());
        const auto& stored = name == "One" ? original->presets[0] : original->presets[1];

        const auto loaded = f.other.loadPreset("Dub", name);
        REQUIRE(loaded.result.ok());
        CHECK(juce::File(loaded.state.loop1.path).existsAsFile());
        CHECK(juce::File(loaded.state.loop2.path).existsAsFile());
        CHECK(juce::File(loaded.state.loop1.path).isAChildOf(f.other.root()));
        CHECK(hashFile(juce::File(loaded.state.loop1.path)) == loaded.state.loop1.hash);
        CHECK(presetFingerprint(loaded.state) == presetFingerprint(stored.state));
    }
}

TEST_CASE("Archive: import into an existing name creates \"Name (2)\" and leaves the original untouched")
{
    ArchiveFixture f;
    f.makeBank();
    const auto target = f.zip("Dub.zip");
    REQUIRE(exportBank(f.store, "Dub", target).ok());
    const auto before = ArchiveFixture::tree(f.store.bankDir("Dub"));
    const auto json = f.store.bankDir("Dub").getChildFile("bank.json").loadFileAsString();

    juce::String imported;
    REQUIRE(importBank(f.store, target, imported).ok());
    CHECK(imported == "Dub (2)");
    REQUIRE(importBank(f.store, target, imported).ok());
    CHECK(imported == "Dub (3)");

    CHECK(f.store.listBanks() == juce::StringArray { "Dub", "Dub (2)", "Dub (3)" });
    CHECK(ArchiveFixture::tree(f.store.bankDir("Dub")) == before);
    CHECK(f.store.bankDir("Dub").getChildFile("bank.json").loadFileAsString() == json);
    CHECK(f.store.loadPreset("Dub (2)", "One").result.ok());

    SECTION("a folder without bank.json also takes the name")
    {
        f.store.bankDir("Dub (4)").createDirectory();
        REQUIRE(importBank(f.store, target, imported).ok());
        CHECK(imported == "Dub (5)");
        CHECK(f.store.bankDir("Dub (4)").getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0);
    }
}

TEST_CASE("Archive: entries escaping the folder are rejected and nothing is created")
{
    ArchiveFixture f;
    const auto json = emptyBankJson();
    const juce::StringArray evil { "../evil.txt", "/abs.txt", "C:/x.txt", "bank/../../x", "B/samples/../../x.wav",
                                   "B\\..\\x.wav", "B/samples\\a.wav", "B/./bank.json", "B//samples/a.wav" };
    for (auto& name : evil)
    {
        INFO("entry " << name);
        const auto zipFile = handZip(f.zip("evil.zip"), { { "B/bank.json", json }, { name, "x" } });
        juce::String imported = "unchanged";
        const auto r = importBank(f.store, zipFile, imported);
        CHECK(r.error == Error::ArchiveInvalid);
        CHECK(r.message.isNotEmpty());
        CHECK(imported == "unchanged");
        CHECK_FALSE(f.store.root().exists());
        CHECK_FALSE(f.dir.getChildFile("evil.txt").exists());
        CHECK_FALSE(f.dir.getChildFile("x").exists());
        CHECK_FALSE(f.dir.getChildFile("x.wav").exists());
    }
}

TEST_CASE("Archive: a zip with two top-level folders, no bank.json, or a foreign folder is rejected")
{
    ArchiveFixture f;
    const auto json = emptyBankJson();
    const std::vector<std::vector<std::pair<juce::String, juce::String>>> cases {
        { { "A/bank.json", json }, { "B/bank.json", json } },
        { { "A/bank.json", json }, { "B/samples/a.wav", "x" } },
        { { "A/samples/a.wav", "x" } },
        { { "A/bank.json", json }, { "A/extra/readme.txt", "x" } },
        { { "A/bank.json", json }, { "A/samples/sub/a.wav", "x" } },
        { { "__MACOSX/A/bank.json", json } },
        { { "A/bank.json", json }, { ".DS_Store", "x" } },
        { { "A/bank.json", json }, { "A/samples/con.wav", "x" } },
        { { "A/bank.json", json }, { "A/samples/a.wav", "x" }, { "A/samples/A.wav", "y" } },
        { { "A/bank.json", json }, { "A/bank.json", json } },
        { { "bank.json", json } },
        { { "con/bank.json", json } },
    };
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        INFO("case " << i);
        const auto zipFile = handZip(f.zip("bad.zip"), cases[i]);
        juce::String imported;
        const auto r = importBank(f.store, zipFile, imported);
        CHECK(r.error == Error::ArchiveInvalid);
        CHECK(r.message.isNotEmpty());
        CHECK_FALSE(f.store.root().exists());
    }

    SECTION("not a zip at all")
    {
        const auto junk = f.zip("junk.zip");
        REQUIRE(junk.replaceWithText("no zip"));
        juce::String imported;
        CHECK(importBank(f.store, junk, imported).error == Error::ArchiveInvalid);
        CHECK(importBank(f.store, f.zip("missing.zip"), imported).error == Error::ArchiveInvalid);
        CHECK_FALSE(f.store.root().exists());
    }
    SECTION("folder entries for the bank and samples are fine")
    {
        const auto zipFile = handZip(f.zip("dirs.zip"), { { "A/", "" }, { "A/samples/", "" }, { "A/bank.json", json } });
        juce::String imported;
        REQUIRE(importBank(f.store, zipFile, imported).ok());
        CHECK(imported == "A");
        CHECK(f.store.listBanks() == juce::StringArray { "A" });
    }
}

TEST_CASE("Archive: a too-new bank.json is rejected")
{
    ArchiveFixture f;
    REQUIRE(f.store.createBank("Keep").ok());
    const auto zipFile = handZip(f.zip("new.zip"), { { "A/bank.json", "{\"version\": 99, \"presets\": []}" } });
    juce::String imported;
    const auto r = importBank(f.store, zipFile, imported);
    CHECK(r.error == Error::BankTooNew);
    CHECK(r.message.isNotEmpty());
    CHECK(f.store.listBanks() == juce::StringArray { "Keep" });

    const auto damaged = handZip(f.zip("damaged.zip"), { { "A/bank.json", "{ not json" } });
    CHECK(importBank(f.store, damaged, imported).error == Error::BankUnreadable);
    CHECK(f.store.listBanks() == juce::StringArray { "Keep" });
}

TEST_CASE("Archive: a zip over the entry or size limit is rejected")
{
    ArchiveFixture f;
    const auto json = emptyBankJson();
    juce::String imported;

    SECTION("defaults")
    {
        CHECK(kMaxArchiveEntries == 2048);
        CHECK(kMaxArchiveBytes == juce::int64(1024) * 1024 * 1024);
    }
    SECTION("entries")
    {
        std::vector<std::pair<juce::String, juce::String>> entries { { "A/bank.json", json } };
        for (int i = 0; i < 4; ++i)
            entries.push_back({ "A/samples/s" + juce::String(i) + ".wav", "x" });
        const auto zipFile = handZip(f.zip("many.zip"), entries);
        CHECK(importBank(f.store, zipFile, imported, { 4, kMaxArchiveBytes }).error == Error::ArchiveTooLarge);
        CHECK_FALSE(f.store.root().exists());
        REQUIRE(importBank(f.store, zipFile, imported, { 5, kMaxArchiveBytes }).ok());
    }
    SECTION("bytes")
    {
        const auto big = juce::String::repeatedString("x", 1000);
        const auto zipFile = handZip(f.zip("big.zip"), { { "A/bank.json", json }, { "A/samples/a.wav", big } });
        const auto total = static_cast<juce::int64>(json.getNumBytesAsUTF8()) + 1000;
        const auto r = importBank(f.store, zipFile, imported, { kMaxArchiveEntries, total - 1 });
        CHECK(r.error == Error::ArchiveTooLarge);
        CHECK(r.message.isNotEmpty());
        CHECK_FALSE(f.store.root().exists());
        REQUIRE(importBank(f.store, zipFile, imported, { kMaxArchiveEntries, total }).ok());
    }
    SECTION("bank.json has its own size limit")
    {
        CHECK(kMaxBankJsonBytes == juce::int64(4) * 1024 * 1024);
        const auto bigJson = "{\"version\": 1, \"presets\": [], \"pad\": \"" + juce::String::repeatedString("x", 2000) + "\"}";
        const auto zipFile = handZip(f.zip("bigjson.zip"), { { "A/bank.json", bigJson } });
        const auto size = static_cast<juce::int64>(bigJson.getNumBytesAsUTF8());
        const auto r = importBank(f.store, zipFile, imported, { kMaxArchiveEntries, kMaxArchiveBytes, size - 1 });
        CHECK(r.error == Error::ArchiveTooLarge);
        CHECK(r.message.contains("bank.json"));
        CHECK_FALSE(f.store.root().exists());
        REQUIRE(importBank(f.store, zipFile, imported, { kMaxArchiveEntries, kMaxArchiveBytes, size }).ok());
    }
    SECTION("a deeply nested bank.json under the size limit is damaged, not a crash")
    {
        const auto zipFile = handZip(f.zip("deep.zip"), { { "A/bank.json", juce::String::repeatedString("[", 100000) } });
        CHECK(importBank(f.store, zipFile, imported).error == Error::BankUnreadable);
        CHECK(f.store.root().getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0);
    }
    SECTION("a declared size over the default limit")
    {
        const auto zipFile = handZip(f.zip("bomb.zip"), { { "A/bank.json", json }, { "A/samples/a.wav", "x" } });
        patchDeclaredSize(zipFile, "A/samples/a.wav", 0xfffffff0u);
        CHECK(importBank(f.store, zipFile, imported).error == Error::ArchiveTooLarge);
        CHECK_FALSE(f.store.root().exists());
    }
}

TEST_CASE("Archive: an entry with more data than it declares is rejected")
{
    ArchiveFixture f;
    REQUIRE(f.store.createBank("Keep").ok());
    const auto zipFile = handZip(f.zip("lie.zip"), { { "A/bank.json", emptyBankJson() },
                                                     { "A/samples/a.wav", juce::String::repeatedString("x", 5000) } });
    patchDeclaredSize(zipFile, "A/samples/a.wav", 10);
    juce::String imported;
    CHECK(importBank(f.store, zipFile, imported).error == Error::ArchiveInvalid);
    CHECK(f.store.root().getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 1);
}

TEST_CASE("Archive: a bank.json whose loop is not in the archive's samples is rejected")
{
    ArchiveFixture f;
    REQUIRE(f.store.createBank("Keep").ok());
    for (const juce::String path : { "../x.wav", "samples/missing.wav", "samples/../a.wav", "C:/a.wav", "samples\\a.wav", "a.wav" })
    {
        INFO("path " << path);
        const auto zipFile = handZip(f.zip("loop.zip"), { { "A/bank.json", bankJsonWithLoop(path) }, { "A/samples/a.wav", "x" } });
        juce::String imported;
        const auto r = importBank(f.store, zipFile, imported);
        CHECK(r.error == Error::ArchiveInvalid);
        CHECK(r.message.contains(path));
        CHECK(f.store.listBanks() == juce::StringArray { "Keep" });
        CHECK(f.store.root().getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 1);
    }

    const auto good = handZip(f.zip("good.zip"), { { "A/bank.json", bankJsonWithLoop("samples/a.wav") }, { "A/samples/a.wav", "x" } });
    juce::String imported;
    REQUIRE(importBank(f.store, good, imported).ok());
    CHECK(imported == "A");
}

TEST_CASE("Archive: a failed import leaves no temporary folder behind")
{
    ArchiveFixture f;
    f.makeBank();
    const auto before = ArchiveFixture::tree(f.store.root());

    const std::vector<std::pair<juce::String, juce::String>> failing[] = {
        { { "A/bank.json", "{\"version\": 99, \"presets\": []}" }, { "A/samples/a.wav", "x" } },
        { { "A/bank.json", "{ not json" }, { "A/samples/a.wav", "x" } },
        { { "A/bank.json", bankJsonWithLoop("samples/missing.wav") }, { "A/samples/a.wav", "x" } },
    };
    for (auto& entries : failing)
    {
        const auto zipFile = handZip(f.zip("fail.zip"), entries);
        juce::String imported;
        CHECK_FALSE(importBank(f.store, zipFile, imported).ok());
        CHECK(ArchiveFixture::tree(f.store.root()) == before);
    }
}

TEST_CASE("Archive: import skips system files and __MACOSX, but still rejects unsafe names among them")
{
    ArchiveFixture f;
    const auto zipFile = handZip(f.zip("mac.zip"), {
                                                       { "B/", "" },
                                                       { "B/bank.json", bankJsonWithLoop("samples/a.wav") },
                                                       { "B/samples/a.wav", "x" },
                                                       { "B/.DS_Store", "ds" },
                                                       { "B/Thumbs.db", "th" },
                                                       { "B/desktop.ini", "ini" },
                                                       { "B/._bank.json", "rsrc" },
                                                       { "B/samples/._x.wav", "rsrc" },
                                                       { "__MACOSX/", "" },
                                                       { "__MACOSX/B/._bank.json", "rsrc" },
                                                       { "__MACOSX/B/samples/._a.wav", "rsrc" },
                                                   });
    juce::String imported;
    const auto r = importBank(f.store, zipFile, imported);
    REQUIRE(r.message == "");
    REQUIRE(r.ok());
    CHECK(imported == "B");
    CHECK(f.store.listBanks() == juce::StringArray { "B" });
    CHECK(ArchiveFixture::tree(f.store.root()) == std::set<juce::String> { "B", "B/bank.json", "B/samples", "B/samples/a.wav" });
    CHECK(f.store.loadPreset("B", "P").result.ok());

    const juce::StringArray hostile { "__MACOSX/../evil.txt", "__MACOSX/B/a:b", "__MACOSX\\x", "B/._../x", "B/../.DS_Store",
                                      "B/samples/._x/../../x.wav", "__MACOSX//B/._a" };
    for (auto& name : hostile)
    {
        INFO("entry " << name);
        const auto bad = handZip(f.zip("evil.zip"), { { "C/bank.json", emptyBankJson() }, { name, "x" } });
        juce::String none = "unchanged";
        const auto rejected = importBank(f.store, bad, none);
        CHECK(rejected.error == Error::ArchiveInvalid);
        CHECK(none == "unchanged");
        CHECK(f.store.listBanks() == juce::StringArray { "B" });
        CHECK_FALSE(f.dir.getChildFile("evil.txt").exists());
    }
}

TEST_CASE("Archive: export refuses a bank folder name that import would reject")
{
    ArchiveFixture f;
    const auto target = f.zip("bad.zip");
    for (const juce::String name : { "a:b", "con", "end.", "x?" })
    {
        INFO("bank " << name);
        const auto r = exportBank(f.store, name, target);
        CHECK(r.error == Error::InvalidName);
        CHECK(r.message == bankNameProblem(name, {}));
        CHECK_FALSE(target.exists());
    }
}
