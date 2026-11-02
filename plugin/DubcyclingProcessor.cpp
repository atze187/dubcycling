#include "plugin/DubcyclingProcessor.h"

namespace dc {

DubcyclingProcessor::DubcyclingProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void DubcyclingProcessor::prepareToPlay(double, int) {}

void DubcyclingProcessor::releaseResources() {}

bool DubcyclingProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet().isDisabled();
}

void DubcyclingProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

juce::AudioProcessorEditor* DubcyclingProcessor::createEditor() {
    return new juce::GenericAudioProcessorEditor(*this);
}

}  // namespace dc

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new dc::DubcyclingProcessor();
}
