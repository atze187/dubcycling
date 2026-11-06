#include "plugin/LoopLoader.h"
#include "plugin/PluginState.h"
#include <algorithm>
#include <limits>
#include <new>

namespace dc {

LoopLoader::~LoopLoader()
{
    // Running jobs access lock_ and results_: wait without a timeout (-1).
    pool_.removeAllJobs(true, -1);
}

namespace {

// Opens the file and checks the header against the loader's limits. Sets error and returns
// nullptr when the file would be rejected by decode().
std::unique_ptr<juce::AudioFormatReader> openChecked(const juce::File& file, juce::String& error)
{
    if (!file.existsAsFile())
    {
        error = LoopLoader::kErrorFileNotFound;
        return nullptr;
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    constexpr unsigned int kMaxChannels = 32;
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0 || reader->lengthInSamples <= 0
        || reader->numChannels > kMaxChannels)
    {
        error = "unsupported or damaged file";
        return nullptr;
    }
    // Header values are not trustworthy: bound them before allocating.
    if (static_cast<std::uint64_t>(reader->lengthInSamples) < kMinLoopFrames)
    {
        error = "file too short (under 1000 frames)";
        return nullptr;
    }
    if (static_cast<std::uint64_t>(reader->lengthInSamples) > kMaxLoopFrames
        || reader->lengthInSamples > std::numeric_limits<int>::max())
    {
        error = "file too long (over 10 minutes at 48 kHz)";
        return nullptr;
    }
    return reader;
}

} // namespace

juce::String LoopLoader::probe(const juce::File& file)
{
    juce::String error;
    openChecked(file, error);
    return error;
}

std::shared_ptr<const StereoData> LoopLoader::decode(const juce::File& file, juce::String& error)
{
    auto reader = openChecked(file, error);
    if (reader == nullptr)
        return nullptr;

    std::shared_ptr<StereoData> data;
    try
    {
        const int length = static_cast<int>(reader->lengthInSamples);
        const int channels = static_cast<int>(std::min(reader->numChannels, 2u));
        juce::AudioBuffer<float> buffer(2, length);
        buffer.clear();
        if (!reader->read(buffer.getArrayOfWritePointers(), channels, 0, length))
        {
            error = "unsupported or damaged file";
            return nullptr;
        }
        const float* l = buffer.getReadPointer(0);
        const float* r = channels > 1 ? buffer.getReadPointer(1) : l;

        data = std::make_shared<StereoData>();
        data->sampleRate = reader->sampleRate;
        data->left.assign(l, l + length);
        data->right.assign(r, r + length);
    }
    catch (const std::bad_alloc&)
    {
        error = "unsupported or damaged file";
        return nullptr;
    }
    return data;
}

void LoopLoader::request(int slot, std::uint64_t ticket, const juce::File& file)
{
    pool_.addJob([this, slot, ticket, file] {
        LoopLoadResult r;
        r.slot = slot;
        r.ticket = ticket;
        r.data = decode(file, r.error);
        if (r.data != nullptr)
            r.hash = hashFile(file); // off the message thread (item A6: refresh the stored hash)
        const juce::ScopedLock sl(lock_);
        results_.push_back(std::move(r));
    });
}

std::vector<LoopLoadResult> LoopLoader::takeResults()
{
    const juce::ScopedLock sl(lock_);
    std::vector<LoopLoadResult> out;
    out.swap(results_);
    return out;
}

void LoopLoader::waitForAll()
{
    while (pool_.getNumJobs() > 0)
        juce::Thread::sleep(1);
}

} // namespace dc
