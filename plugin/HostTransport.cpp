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
    if (!ppq.hasValue() || !bpm.hasValue() || !ts.hasValue())
        return t;
    t.ppq = *ppq;
    t.bpm = *bpm;
    t.tsNum = ts->numerator;
    t.tsDen = ts->denominator;
    t.playing = position->getIsPlaying();
    return t;
}

} // namespace dc
