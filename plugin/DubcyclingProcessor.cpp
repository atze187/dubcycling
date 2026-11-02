#include "plugin/DubcyclingProcessor.h"
#include "plugin/HostTransport.h"
#include "plugin/FileRelink.h"
#include "plugin/ui/Editor.h"
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
    {
        const juce::ScopedLock sl(stateLock_);
        model_ = model;
    }
    engine_.setModel(std::move(model)); // has its own lock; never called with stateLock_ held
}


DubcyclingProcessor::SlotStatus DubcyclingProcessor::getSlotStatus(int slot) const {
    const juce::ScopedLock sl(stateLock_);
    return slots_.status(slot);
}

juce::String DubcyclingProcessor::slotError(int slot) const {
    const juce::ScopedLock sl(stateLock_);
    return slots_.error(slot);
}

juce::String DubcyclingProcessor::pendingReplaceFileName(int slot) const {
    const juce::ScopedLock sl(stateLock_);
    return slots_.pendingFileName(slot);
}

SlotTracker::ReplaceFailure DubcyclingProcessor::replaceFailure(int slot) const {
    const juce::ScopedLock sl(stateLock_);
    return slots_.replaceFailure(slot);
}

void DubcyclingProcessor::publish() {
    auto m = std::make_shared<Model>();
    {
        const juce::ScopedLock sl(stateLock_);
        for (int i = 0; i < 2; ++i) {
            const LoopRef& ref = i == 0 ? state_.loop1 : state_.loop2;
            LoopSlot& slot = i == 0 ? m->loop1 : m->loop2;
            slot.data = slots_.data(i);
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
    std::uint64_t ticket = 0;
    {
        const juce::ScopedLock sl(stateLock_);
        ticket = slots_.beginLoad(slot);
    }
    loader_.request(slot, ticket, file);
}

void DubcyclingProcessor::applyState(const PluginState& s) {
    {
        const juce::ScopedLock sl(stateLock_);
        state_ = s;
        slots_.resetAll(); // invalidates in-flight loads and staged replacements of the previous state
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
            slots_.setMissing(i); // regions/rules/bars stay and are saved unchanged
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

bool DubcyclingProcessor::loadLoop(int slot, const juce::File& file) {
    if (slot < 0 || slot > 1)
        return false;
    const juce::String hash = hashFile(file); // file IO outside the lock
    if (hash.isEmpty())
        return false; // missing or unreadable: keep the reference (it could never relink otherwise)
    {
        const juce::ScopedLock sl(stateLock_);
        LoopRef& ref = refOf(slot);
        ref.path = file.getFullPathName();
        ref.fileName = file.getFileName();
        ref.hash = hash;
    }
    startLoad(slot, file);
    publish();
    return true;
}

bool DubcyclingProcessor::replaceLoop(int slot, const juce::File& file, int bars) {
    if (slot < 0 || slot > 1)
        return false;
    const juce::String hash = hashFile(file); // file IO outside the lock
    if (hash.isEmpty())
        return false;
    LoopRef next;
    next.path = file.getFullPathName();
    next.fileName = file.getFileName();
    next.hash = hash;
    next.bars = std::clamp(bars, 1, 1024);
    std::uint64_t ticket = 0;
    {
        const juce::ScopedLock sl(stateLock_);
        ticket = slots_.beginReplace(slot, std::move(next)); // applied in timerCallback once decoded
    }
    loader_.request(slot, ticket, file);
    return true;
}

void DubcyclingProcessor::timerCallback() {
    bool changed = false;
    for (auto& r : loader_.takeResults()) {
        const juce::ScopedLock sl(stateLock_);
        changed = slots_.apply(r, state_) || changed;
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
    if (t.bpm > 0.0)
        hostBpm_.store(t.bpm, std::memory_order_relaxed);
    engine_.process(t, buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, n);
}

juce::AudioProcessorEditor* DubcyclingProcessor::createEditor() {
    return new ui::Editor(*this);
}

}  // namespace dc

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new dc::DubcyclingProcessor();
}
