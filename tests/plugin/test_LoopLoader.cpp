#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/LoopLoader.h"

using namespace dc;
using Catch::Matchers::WithinAbs;

namespace {

struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dcloops", "", false);
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

// Writes constant values per channel (channelValues[c]).
void writeConst(juce::AudioFormat& format, const juce::File& file, double sampleRate, int numSamples,
                std::vector<float> channelValues)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream>(file);
    const int numChannels = static_cast<int>(channelValues.size());
    auto writer = format.createWriterFor(out, juce::AudioFormatWriterOptions {}
                                                  .withSampleRate(sampleRate)
                                                  .withNumChannels(numChannels)
                                                  .withBitsPerSample(24));
    REQUIRE(writer != nullptr);
    juce::AudioBuffer<float> buf(numChannels, numSamples);
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < numSamples; ++i)
            buf.setSample(c, i, channelValues[static_cast<size_t>(c)]);
    REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, numSamples));
}

} // namespace

TEST_CASE("LoopLoader decodes stereo wav keeps both channels", "[loopLoader]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    juce::WavAudioFormat wav;
    const auto file = tmp.dir.getChildFile("s.wav");
    writeConst(wav, file, 48000.0, 4800, { 0.5f, -0.5f });
    juce::String error;
    const auto d = LoopLoader::decode(file, error);
    REQUIRE(d != nullptr);
    CHECK(error.isEmpty());
    CHECK(d->sampleRate == 48000.0);
    REQUIRE(d->frames() == 4800);
    CHECK(d->right.size() == 4800);
    CHECK_THAT(d->left[100], WithinAbs(0.5, 0.001));
    CHECK_THAT(d->right[100], WithinAbs(-0.5, 0.001));
}

TEST_CASE("LoopLoader mono file is duplicated to both channels", "[loopLoader]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    juce::WavAudioFormat wav;
    const auto file = tmp.dir.getChildFile("m.wav");
    writeConst(wav, file, 44100.0, 3000, { 0.25f });
    juce::String error;
    const auto d = LoopLoader::decode(file, error);
    REQUIRE(d != nullptr);
    REQUIRE(d->frames() == 3000);
    CHECK(d->left == d->right);
    CHECK_THAT(d->right[10], WithinAbs(0.25, 0.001));
}

TEST_CASE("LoopLoader uses only the first two channels", "[loopLoader]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    juce::WavAudioFormat wav;
    const auto file = tmp.dir.getChildFile("three.wav");
    writeConst(wav, file, 44100.0, 2000, { 0.5f, -0.25f, 0.9f });
    juce::String error;
    const auto d = LoopLoader::decode(file, error);
    REQUIRE(d != nullptr);
    CHECK_THAT(d->left[5], WithinAbs(0.5, 0.001));
    CHECK_THAT(d->right[5], WithinAbs(-0.25, 0.001));
}

TEST_CASE("LoopLoader flac and aiff decode", "[loopLoader]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    juce::AiffAudioFormat aiff;
    juce::FlacAudioFormat flac;
    const std::pair<juce::AudioFormat*, const char*> formats[] = { { &aiff, "a.aiff" }, { &flac, "a.flac" } };
    for (const auto& [format, name] : formats)
    {
        const auto file = tmp.dir.getChildFile(name);
        writeConst(*format, file, 44100.0, 22050, { 0.5f, -0.5f });
        juce::String error;
        const auto d = LoopLoader::decode(file, error);
        INFO(name);
        REQUIRE(d != nullptr);
        CHECK(d->sampleRate == 44100.0);
        REQUIRE(d->frames() == 22050);
        CHECK_THAT(d->left[1000], WithinAbs(0.5, 0.001));
        CHECK_THAT(d->right[1000], WithinAbs(-0.5, 0.001));
    }
}

TEST_CASE("LoopLoader mp3 decode is registered", "[loopLoader]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    if (formats.findFormatForFileExtension(".mp3") == nullptr)
        SKIP("MP3 decoding is not registered by registerBasicFormats on this platform");
    CHECK(formats.findFormatForFileExtension(".mp3") != nullptr);
}

// Real MP3 frames: MPEG-1 Layer III, 128 kbit/s, 44.1 kHz, mono, 417 bytes per frame, zero payload
// (= silence). Each frame decodes to 1152 samples.
TEST_CASE("LoopLoader decodes an mp3 file", "[loopLoader]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    if (formats.findFormatForFileExtension(".mp3") == nullptr)
        SKIP("MP3 decoding is not registered by registerBasicFormats on this platform");
    TempDir tmp;
    const auto file = tmp.dir.getChildFile("silence.mp3");
    juce::MemoryBlock data;
    for (int f = 0; f < 20; ++f)
    {
        const unsigned char header[4] = { 0xFF, 0xFB, 0x90, 0xC0 };
        data.append(header, 4);
        const juce::MemoryBlock payload(413, true);
        data.append(payload.getData(), payload.getSize());
    }
    REQUIRE(file.replaceWithData(data.getData(), data.getSize()));
    juce::String error;
    const auto d = LoopLoader::decode(file, error);
    INFO("error: " << error);
    REQUIRE(d != nullptr);
    CHECK(d->frames() >= 1000);
    CHECK(d->right.size() == d->left.size());
}

TEST_CASE("LoopLoader missing file reports error", "[loopLoader]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    juce::String error;
    CHECK(LoopLoader::decode(tmp.dir.getChildFile("none.wav"), error) == nullptr);
    CHECK(error == "file not found");
}

TEST_CASE("LoopLoader unsupported file reports error", "[loopLoader]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    const auto junk = tmp.dir.getChildFile("junk.wav");
    REQUIRE(junk.replaceWithText("not audio at all"));
    juce::String error;
    CHECK(LoopLoader::decode(junk, error) == nullptr);
    CHECK(error == "unsupported or damaged file");
}

TEST_CASE("LoopLoader file shorter than 1000 frames is rejected", "[loopLoader]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    juce::WavAudioFormat wav;
    juce::String error;
    const auto shortFile = tmp.dir.getChildFile("short.wav");
    writeConst(wav, shortFile, 44100.0, 999, { 0.5f, 0.5f });
    CHECK(LoopLoader::decode(shortFile, error) == nullptr);
    CHECK(error.contains("too short"));

    const auto okFile = tmp.dir.getChildFile("ok.wav");
    writeConst(wav, okFile, 44100.0, 1000, { 0.5f, 0.5f });
    error.clear();
    CHECK(LoopLoader::decode(okFile, error) != nullptr);
}

TEST_CASE("LoopLoader file above the frame limit is rejected without allocating", "[loopLoader]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    // Handgebauter WAV-Header: 16 bit mono 48 kHz, data-Chunk behauptet ~1 Mrd. Frames, tatsaechlich 8 Bytes.
    juce::MemoryOutputStream header;
    auto u32 = [&](juce::uint32 v) { header.writeInt(static_cast<int>(v)); };
    auto u16 = [&](juce::uint16 v) { header.writeShort(static_cast<short>(v)); };
    header.write("RIFF", 4);
    u32(0x7FFFFFFF);
    header.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(48000);
    u32(96000);
    u16(2);
    u16(16);
    header.write("data", 4);
    u32(0x7FFFFFF0);
    for (int i = 0; i < 8; ++i)
        header.writeByte(0);
    const auto file = tmp.dir.getChildFile("huge.wav");
    REQUIRE(file.replaceWithData(header.getData(), header.getDataSize()));
    juce::String error;
    CHECK(LoopLoader::decode(file, error) == nullptr);
    CHECK(error.isNotEmpty());
}

TEST_CASE("LoopLoader requests are decoded in the background with their ticket", "[loopLoader]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    juce::WavAudioFormat wav;
    const auto file = tmp.dir.getChildFile("a.wav");
    writeConst(wav, file, 48000.0, 4800, { 0.5f, -0.5f });

    LoopLoader loader;
    loader.request(3, 7, file);
    loader.request(5, 9, tmp.dir.getChildFile("none.wav"));
    loader.waitForAll();
    auto results = loader.takeResults();
    REQUIRE(results.size() == 2);
    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.slot < b.slot; });
    CHECK(results[0].slot == 3);
    CHECK(results[0].ticket == 7);
    CHECK(results[0].data != nullptr);
    CHECK(results[1].slot == 5);
    CHECK(results[1].data == nullptr);
    CHECK(results[1].error == "file not found");
    CHECK(loader.takeResults().empty());
}

TEST_CASE("LoopLoader destroying with queued requests neither crashes nor hangs", "[loopLoader]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    juce::WavAudioFormat wav;
    const auto file = tmp.dir.getChildFile("a.wav");
    writeConst(wav, file, 48000.0, 48000, { 0.5f, 0.5f });
    {
        LoopLoader loader;
        for (int i = 0; i < 20; ++i)
            loader.request(i, static_cast<std::uint64_t>(i), file);
    }
    SUCCEED();
}
