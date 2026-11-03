#include "plugin/HostTransport.h"

namespace dc {

TransportInfo toTransportInfo(const juce::Optional<juce::AudioPlayHead::PositionInfo>& position)
{
    TransportInfo t;
    if (!position.hasValue())
        return t;
    const auto ppq = position->getPpqPosition();
    const auto bpm = position->getBpm();
    const auto ts = position->getTimeSignature();
    if (!bpm.hasValue())
        return t;
    if (ppq.hasValue()) {
        t.ppq = *ppq;
    } else {
        // Fallback: assumes a constant tempo since the project start; a tempo-automated
        // project drifts with this.
        const auto seconds = position->getTimeInSeconds();
        if (!seconds.hasValue())
            return t;
        t.ppq = *seconds * *bpm / 60.0;
    }
    t.bpm = *bpm;
    t.tsNum = ts.hasValue() ? ts->numerator : 4;
    t.tsDen = ts.hasValue() ? ts->denominator : 4;
    t.playing = position->getIsPlaying();
    return t;
}

} // namespace dc
