#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include "engine/StereoData.h"

namespace dc {

// Annahme: zehn Minuten bei 48 kHz.
constexpr std::uint64_t kMaxLoopFrames = 48000ull * 600;
constexpr std::uint64_t kMinLoopFrames = 1000;

struct LoopLoadResult
{
    int slot = 0;
    std::uint64_t ticket = 0;
    std::shared_ptr<const StereoData> data; // nullptr bei Fehler
    juce::String error;
};

// Dekodiert Audiodateien zu StereoData (Mono wird auf beide Kanaele dupliziert,
// bei mehr als zwei Kanaelen zaehlen nur die ersten zwei) auf einem eigenen Thread.
class LoopLoader
{
public:
    LoopLoader() = default;
    ~LoopLoader();

    // Dekodiert synchron. Bei Fehler nullptr und error ("file not found",
    // "unsupported or damaged file", "file too short ...", "file too long ...").
    static std::shared_ptr<const StereoData> decode(const juce::File& file, juce::String& error);

    void request(int slot, std::uint64_t ticket, const juce::File& file);
    std::vector<LoopLoadResult> takeResults();
    // Blockiert, bis alle Auftraege fertig sind (Tests, Aufraeumen).
    void waitForAll();

private:
    juce::ThreadPool pool_ { juce::ThreadPoolOptions {}.withNumberOfThreads(1).withThreadName("Dubcycling loops") };
    juce::CriticalSection lock_;
    std::vector<LoopLoadResult> results_;

    JUCE_DECLARE_NON_COPYABLE(LoopLoader)
};

} // namespace dc
