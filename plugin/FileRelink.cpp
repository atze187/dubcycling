#include "plugin/FileRelink.h"

namespace dc {

namespace {
std::optional<juce::File> matchIn(const juce::File& dir, const LoopRef& ref)
{
    if (ref.fileName.isEmpty() || ref.hash.isEmpty() || dir == juce::File() || !dir.isDirectory())
        return std::nullopt;
    const auto candidate = dir.getChildFile(ref.fileName);
    if (candidate.existsAsFile() && hashFile(candidate).equalsIgnoreCase(ref.hash))
        return candidate;
    return std::nullopt;
}
} // namespace

std::optional<juce::File> relink(const LoopRef& ref, const std::vector<juce::File>& candidateDirs)
{
    if (ref.path.isNotEmpty())
    {
        const juce::File stored(ref.path);
        if (stored.existsAsFile())
            return stored;
    }
    for (const auto& dir : candidateDirs)
        if (auto f = matchIn(dir, ref))
            return f;
    return std::nullopt;
}

} // namespace dc
