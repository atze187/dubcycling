// Shared test helpers for editor tests that need loaded loops.
#pragma once
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <juce_audio_formats/juce_audio_formats.h>
#include "plugin/DubcyclingProcessor.h"

namespace dc::testing {

// Loads a short WAV into both slots: the rule panel only shows weights once both loops exist.
// Must be created before the state is edited (replaceLoop resets regions and rules).
struct LoopPair
{
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dcpair", "", false);

    explicit LoopPair(DubcyclingProcessor& proc)
    {
        dir.createDirectory();
        const auto file = dir.getChildFile("loop.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream>(file);
        auto writer = wav.createWriterFor(out, juce::AudioFormatWriterOptions {}
                                                   .withSampleRate(8000)
                                                   .withNumChannels(2)
                                                   .withBitsPerSample(16));
        REQUIRE(writer != nullptr);
        juce::AudioBuffer<float> buf(2, 16000);
        buf.clear();
        REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, 16000));
        writer.reset();
        REQUIRE(proc.replaceLoop(0, file, 2));
        REQUIRE(proc.replaceLoop(1, file, 2));
        proc.waitForLoads();
    }
    ~LoopPair() { dir.deleteRecursively(); }
};

} // namespace dc::testing
