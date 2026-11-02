#include "plugin/DubcyclingProcessor.h"
#include "plugin/HostTransport.h"

namespace dc {

DubcyclingProcessor::DubcyclingProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void DubcyclingProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine_.prepare(sampleRate, samplesPerBlock);
}

void DubcyclingProcessor::releaseResources() {}

bool DubcyclingProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet().isDisabled();
}

void DubcyclingProcessor::setModel(std::shared_ptr<const Model> model) {
    model_ = model;
    engine_.setModel(std::move(model));
}

void DubcyclingProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2) {
        buffer.clear();
        return;
    }
    auto* head = getPlayHead();
    const TransportInfo t = toTransportInfo(head != nullptr ? head->getPosition()
                                                            : juce::Optional<juce::AudioPlayHead::PositionInfo> {});
    engine_.process(t, buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, n);
}

juce::AudioProcessorEditor* DubcyclingProcessor::createEditor() {
    return new juce::GenericAudioProcessorEditor(*this);
}

}  // namespace dc

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new dc::DubcyclingProcessor();
}
