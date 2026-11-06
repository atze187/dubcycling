#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include "engine/StereoData.h"

namespace dc {

// Assumption: ten minutes at 48 kHz.
constexpr std::uint64_t kMaxLoopFrames = 48000ull * 600;
constexpr std::uint64_t kMinLoopFrames = 1000;

struct LoopLoadResult
{
    int slot = 0;
    std::uint64_t ticket = 0;
    std::shared_ptr<const StereoData> data; // nullptr on error
    juce::String error;
    juce::String hash; // SHA-256 (hashFile) of the decoded file; empty on error
};

// Decodes audio files to StereoData (mono is duplicated to both channels,
// with more than two channels only the first two count) on its own thread.
class LoopLoader
{
public:
    static constexpr const char* kErrorFileNotFound = "file not found";

    LoopLoader() = default;
    ~LoopLoader();

    // Decodes synchronously. On failure nullptr and error ("file not found",
    // "unsupported or damaged file", "file too short ...", "file too long ...").
    static std::shared_ptr<const StereoData> decode(const juce::File& file, juce::String& error);
    // Header-only check with the same rules and error texts as decode(); empty = would load.
    static juce::String probe(const juce::File& file);

    void request(int slot, std::uint64_t ticket, const juce::File& file);
    std::vector<LoopLoadResult> takeResults();
    // Blocks until all jobs are done (tests, cleanup).
    void waitForAll();

private:
    juce::ThreadPool pool_ { juce::ThreadPoolOptions {}.withNumberOfThreads(1).withThreadName("Dubcycling loops") };
    juce::CriticalSection lock_;
    std::vector<LoopLoadResult> results_;

    JUCE_DECLARE_NON_COPYABLE(LoopLoader)
};

} // namespace dc
