#include "plugin/DubcyclingProcessor.h"
#include "plugin/HostTransport.h"
#include "plugin/FileRelink.h"
#include <algorithm>

namespace dc {

DubcyclingProcessor::DubcyclingProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    startTimerHz(20);
}

DubcyclingProcessor::~DubcyclingProcessor() {
    stopTimer();
}

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


DubcyclingProcessor::SlotStatus DubcyclingProcessor::getSlotStatus(int slot) const {
    if (slot < 0 || slot > 1)
        return SlotStatus::Empty;
    const juce::ScopedLock sl(stateLock_);
    return status_[slot];
}

void DubcyclingProcessor::publish() {
    auto m = std::make_shared<Model>();
    {
        const juce::ScopedLock sl(stateLock_);
        for (int i = 0; i < 2; ++i) {
            const LoopRef& ref = i == 0 ? state_.loop1 : state_.loop2;
            LoopSlot& slot = i == 0 ? m->loop1 : m->loop2;
            slot.data = data_[i];
            slot.bars = ref.bars;
            slot.regions.boundaries = ref.boundaries;
        }
        for (const auto& e : state_.rules)
            m->rules.setTargets(e.region1, e.targets);
    }
    setModel(std::move(m));
}

// Message thread. Caller holds no lock.
void DubcyclingProcessor::startLoad(int slot, const juce::File& file) {
    {
        const juce::ScopedLock sl(stateLock_);
        data_[slot] = nullptr;
        status_[slot] = SlotStatus::Loading;
        ticket_[slot] = nextTicket_++;
    }
    loader_.request(slot, ticket_[slot], file);
}

void DubcyclingProcessor::applyState(const PluginState& s) {
    {
        const juce::ScopedLock sl(stateLock_);
        state_ = s;
        for (int i = 0; i < 2; ++i) {
            data_[i] = nullptr;
            ticket_[i] = nextTicket_++; // invalidates in-flight loads of the previous state
            status_[i] = SlotStatus::Empty;
        }
    }
    for (int i = 0; i < 2; ++i) {
        LoopRef ref;
        {
            const juce::ScopedLock sl(stateLock_);
            ref = refOf(i);
        }
        if (ref.path.isEmpty() && ref.fileName.isEmpty())
            continue;
        if (auto f = relink(ref, sessionDir_)) {
            {
                const juce::ScopedLock sl(stateLock_);
                refOf(i).path = f->getFullPathName(); // remember the new location
            }
            startLoad(i, *f);
        } else {
            const juce::ScopedLock sl(stateLock_);
            status_[i] = SlotStatus::Missing; // regions/rules/bars stay and are saved unchanged
        }
    }
    publish();
}

void DubcyclingProcessor::getStateInformation(juce::MemoryBlock& dest) {
    const juce::ScopedLock sl(stateLock_);
    dest = serialize(state_);
}

void DubcyclingProcessor::setStateInformation(const void* data, int size) {
    if (size <= 0)
        return;
    if (auto s = deserialize(data, static_cast<std::size_t>(size)))
        applyState(*s);
    // damaged or unknown state: keep what we have
}

void DubcyclingProcessor::applyEdit(const PluginState& edited) {
    const auto block = serialize(edited);
    const auto clean = deserialize(block.getData(), block.getSize());
    if (!clean)
        return;
    {
        const juce::ScopedLock sl(stateLock_);
        for (int i = 0; i < 2; ++i) {
            LoopRef& dst = refOf(i);
            const LoopRef& src = i == 0 ? clean->loop1 : clean->loop2;
            dst.bars = src.bars;
            dst.boundaries = src.boundaries;
        }
        state_.rules = clean->rules;
    }
    publish();
}

void DubcyclingProcessor::loadLoop(int slot, const juce::File& file) {
    if (slot < 0 || slot > 1)
        return;
    {
        const juce::ScopedLock sl(stateLock_);
        LoopRef& ref = refOf(slot);
        ref.path = file.getFullPathName();
        ref.fileName = file.getFileName();
        ref.hash = hashFile(file);
    }
    startLoad(slot, file);
    publish();
}

void DubcyclingProcessor::replaceLoop(int slot, const juce::File& file, int bars) {
    if (slot < 0 || slot > 1)
        return;
    {
        const juce::ScopedLock sl(stateLock_);
        LoopRef& ref = refOf(slot);
        ref.path = file.getFullPathName();
        ref.fileName = file.getFileName();
        ref.hash = hashFile(file);
        ref.bars = std::clamp(bars, 1, 1024);
        ref.boundaries.clear();
        state_.rules.clear();
    }
    startLoad(slot, file);
    publish();
}

void DubcyclingProcessor::timerCallback() {
    bool changed = false;
    for (auto& r : loader_.takeResults()) {
        if (r.slot < 0 || r.slot > 1)
            continue;
        const juce::ScopedLock sl(stateLock_);
        if (r.ticket != ticket_[r.slot])
            continue; // stale
        data_[r.slot] = r.data;
        status_[r.slot] = r.data != nullptr ? SlotStatus::Loaded : SlotStatus::Missing;
        changed = true;
    }
    if (changed)
        publish();
    engine_.releaseRetired();
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
