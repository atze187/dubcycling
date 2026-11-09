#include "plugin/DubcyclingProcessor.h"
#include "plugin/HostTransport.h"
#include "plugin/FileRelink.h"
#include "plugin/ui/Editor.h"
#include <algorithm>
#include <array>

namespace dc {

DubcyclingProcessor::DubcyclingProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    engine_.setTeaserLookahead(kTeaserLookaheadSec);
    for (auto& b : boundNotes_)
        b.store(-1, std::memory_order_relaxed);
    startTimerHz(20);
}

DubcyclingProcessor::~DubcyclingProcessor() {
    stopTimer();
}

void DubcyclingProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine_.prepare(sampleRate, samplesPerBlock);
    trigger_.prepare(sampleRate); // clears held notes and any armed start/stop
}

void DubcyclingProcessor::releaseResources() {
    trigger_.reset();
}

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
        m->fx = state_.fx;
        m->passes = state_.passes;
    }
    unsigned filled = 0;
    for (int i = 0; i < kPassSlots; ++i)
        if (!m->passes[static_cast<size_t>(i)].empty())
            filled |= 1u << i;
    setModel(std::move(m));
    filledMask_.store(filled, std::memory_order_relaxed);
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
        bool empty = false;
        {
            const juce::ScopedLock sl(stateLock_);
            empty = refOf(i).path.isEmpty() && refOf(i).fileName.isEmpty();
        }
        if (!empty)
            tryRelink(i); // a success also retries the other slot if it is Missing
    }
    engine_.setReplay(0); // a loaded state (project or preset) brings its own passes: replay off
    // A pass finished before the load belongs to the previous state: only passes finished after
    // the load can be saved into this one.
    Pass last;
    if (std::uint32_t id = 0; engine_.lastPass(last, id))
        lastSavedId_.store(id, std::memory_order_relaxed);
    publish();
}

std::vector<juce::File> DubcyclingProcessor::relinkCandidates(int slot) const {
    std::vector<juce::File> dirs;
    auto add = [&dirs](const juce::File& d) {
        if (d != juce::File() && std::find(dirs.begin(), dirs.end(), d) == dirs.end())
            dirs.push_back(d);
    };
    juce::String otherPath;
    {
        const juce::ScopedLock sl(stateLock_);
        otherPath = (slot == 0 ? state_.loop2 : state_.loop1).path;
    }
    if (otherPath.isNotEmpty() && juce::File(otherPath).existsAsFile())
        add(juce::File(otherPath).getParentDirectory()); // project loops usually live together
    add(lastGoodDir_);
    add(sessionDir_);
    return dirs;
}

bool DubcyclingProcessor::tryRelink(int slot) {
    LoopRef ref;
    {
        const juce::ScopedLock sl(stateLock_);
        ref = refOf(slot);
    }
    const auto f = relink(ref, relinkCandidates(slot)); // file IO outside the lock
    if (!f) {
        const juce::ScopedLock sl(stateLock_);
        slots_.setMissing(slot); // regions/rules/bars stay and are saved unchanged
        return false;
    }
    {
        const juce::ScopedLock sl(stateLock_);
        refOf(slot).path = f->getFullPathName(); // remember the new location
    }
    lastGoodDir_ = f->getParentDirectory();
    startLoad(slot, *f);
    relinkOtherIfMissing(slot);
    return true;
}

bool DubcyclingProcessor::relinkOtherIfMissing(int slot) {
    const int other = slot == 0 ? 1 : 0;
    {
        const juce::ScopedLock sl(stateLock_);
        if (slots_.status(other) != SlotStatus::Missing)
            return false;
    }
    return tryRelink(other);
}

void DubcyclingProcessor::getStateInformation(juce::MemoryBlock& dest) {
    {
        const juce::ScopedLock sl(stateLock_);
        dest = serialize(state_);
    }
    // The trigger mode and the note bindings belong to this instance, not to PluginState (so not to
    // presets/banks): they are optional root attributes next to the serialized state. Absent =
    // Autoplay / unbound.
    juce::String attrs;
    if (triggerMode() == TriggerMode::Midi)
        attrs << " Trigger=\"midi\"";
    attrs << bindingAttributes();
    if (attrs.isNotEmpty()) {
        const auto text = juce::String::fromUTF8(static_cast<const char*>(dest.getData()),
                                                 static_cast<int>(dest.getSize()));
        const int at = text.indexOf("<DubcyclingState");
        if (at >= 0) {
            const auto withAttrs = text.replaceSection(at + 16, 0, attrs);
            dest.replaceAll(withAttrs.toRawUTF8(), withAttrs.getNumBytesAsUTF8());
        }
    }
}

void DubcyclingProcessor::setStateInformation(const void* data, int size) {
    if (size <= 0)
        return;
    if (auto s = deserialize(data, static_cast<std::size_t>(size))) {
        // deserialize succeeded, so the text is well-formed XML; unknown or missing value = Autoplay
        bool midi = false;
        const auto text = juce::String::fromUTF8(static_cast<const char*>(data), size);
        if (auto xml = juce::XmlDocument::parse(text)) {
            midi = xml->getStringAttribute("Trigger") == "midi";
            readBindings(*xml);
        }
        setTriggerMode(midi ? TriggerMode::Midi : TriggerMode::Autoplay);
        applyState(*s);
    }
    // damaged or unknown state: keep what we have
}

namespace {
juce::String bindingAttributeName(int target) {
    if (target == DubcyclingProcessor::kTargetSave)
        return "BindSave";
    if (target == DubcyclingProcessor::kTargetPlay)
        return "BindPlay";
    return "BindSlot" + juce::String(target);
}
} // namespace

juce::String DubcyclingProcessor::bindingAttributes() const {
    juce::String attrs;
    for (int t = 0; t < kBindTargets; ++t) {
        const int note = boundNote(t);
        if (note >= 0)
            attrs << " " << bindingAttributeName(t) << "=\"" << note << "\"";
    }
    return attrs;
}

void DubcyclingProcessor::readBindings(const juce::XmlElement& root) {
    std::array<bool, 128> taken {};
    for (int t = 0; t < kBindTargets; ++t) {
        const auto v = root.getStringAttribute(bindingAttributeName(t)).trim();
        int note = -1;
        if (v.isNotEmpty() && v.length() <= 3 && v.containsOnly("0123456789")) {
            const int n = v.getIntValue();
            if (n >= 0 && n < 128 && !taken[static_cast<size_t>(n)]) {
                taken[static_cast<size_t>(n)] = true; // a duplicate keeps the first target
                note = n;
            }
        }
        boundNotes_[static_cast<size_t>(t)].store(note, std::memory_order_relaxed);
    }
    bindingsVersion_.fetch_add(1, std::memory_order_relaxed);
    // A note held now that is bound from here on would never get its note-off to the gate: the
    // next block releases every bound note (as a learn does).
    releaseBoundNotes_.store(true, std::memory_order_relaxed);
}

void DubcyclingProcessor::armLearn(int target) {
    learnTarget_.store(target >= 0 && target < kBindTargets ? target : -1, std::memory_order_relaxed);
}

int DubcyclingProcessor::boundNote(int target) const {
    return target >= 0 && target < kBindTargets ? boundNotes_[static_cast<size_t>(target)].load(std::memory_order_relaxed)
                                                : -1;
}

void DubcyclingProcessor::clearBinding(int target) {
    if (target < 0 || target >= kBindTargets)
        return;
    boundNotes_[static_cast<size_t>(target)].store(-1, std::memory_order_relaxed);
    bindingsVersion_.fetch_add(1, std::memory_order_relaxed);
}

int DubcyclingProcessor::targetOfNote(int note) const {
    for (int t = 0; t < kBindTargets; ++t)
        if (boundNotes_[static_cast<size_t>(t)].load(std::memory_order_relaxed) == note)
            return t;
    return -1;
}

void DubcyclingProcessor::bindNote(int target, int note) {
    for (int t = 0; t < kBindTargets; ++t) {
        int expected = note;
        if (t != target) // one note, one target: move it
            boundNotes_[static_cast<size_t>(t)].compare_exchange_strong(expected, -1, std::memory_order_relaxed);
    }
    boundNotes_[static_cast<size_t>(target)].store(note, std::memory_order_relaxed);
    bindingsVersion_.fetch_add(1, std::memory_order_relaxed);
}

// ---- Pass slots ----

bool DubcyclingProcessor::passFilled(int slot) const {
    if (slot < 0 || slot >= kPassSlots)
        return false;
    const juce::ScopedLock sl(stateLock_);
    return !state_.passes[static_cast<size_t>(slot)].empty();
}

bool DubcyclingProcessor::canSave() const {
    if (engine_.replayRequest() != 0)
        return false;
    Pass p;
    std::uint32_t id = 0;
    if (!engine_.lastPass(p, id) || p.empty() || id == lastSavedId_.load(std::memory_order_relaxed))
        return false;
    const juce::ScopedLock sl(stateLock_);
    return std::any_of(state_.passes.begin(), state_.passes.end(), [](const Pass& s) { return s.empty(); });
}

bool DubcyclingProcessor::savePass() {
    if (engine_.replayRequest() != 0)
        return false;
    Pass p;
    std::uint32_t id = 0;
    if (!engine_.lastPass(p, id) || p.empty() || id == lastSavedId_.load(std::memory_order_relaxed))
        return false;
    PluginState s = getPluginState();
    const auto free = std::find_if(s.passes.begin(), s.passes.end(), [](const Pass& x) { return x.empty(); });
    if (free == s.passes.end())
        return false;
    *free = sanitizePass(std::move(p));
    if (free->empty())
        return false;
    applyEdit(s);
    lastSavedId_.store(id, std::memory_order_relaxed);
    return true;
}

void DubcyclingProcessor::clearPass(int slot) {
    if (slot >= 0 && slot < kPassSlots)
        clearPasses(1u << slot);
}

void DubcyclingProcessor::clearAllPasses() {
    clearPasses((1u << kPassSlots) - 1u);
}

void DubcyclingProcessor::clearPasses(unsigned mask) {
    PluginState s = getPluginState();
    unsigned left = 0;
    for (int i = 0; i < kPassSlots; ++i) {
        auto& p = s.passes[static_cast<size_t>(i)];
        if ((mask >> i) & 1u)
            p = Pass {};
        if (!p.empty())
            left |= 1u << i;
    }
    applyEdit(s);
    // A request for a cleared slot, or the chain with nothing left, would only play random: off.
    const int req = engine_.replayRequest();
    if ((req >= 2 && ((left >> (req - 2)) & 1u) == 0) || (req == 1 && left == 0))
        engine_.setReplay(0);
}

// Audio thread.
DubcyclingProcessor::NoteRoute DubcyclingProcessor::routeNoteOn(int note) {
    int learn = learnTarget_.load(std::memory_order_relaxed);
    if (learn >= 0 && learnTarget_.compare_exchange_strong(learn, -1, std::memory_order_relaxed)) {
        bindNote(learn, note);
        return NoteRoute::Learned;
    }
    const int t = targetOfNote(note);
    if (t < 0)
        return NoteRoute::Gate;
    if (t < kPassSlots) {
        if ((filledMask_.load(std::memory_order_relaxed) >> t) & 1u) // ignored for an empty slot
            engine_.setReplay(2 + t);
    } else if (t == kTargetPlay) {
        // Off -> chain only with something to play (else Save would be blocked for nothing).
        if (engine_.replayRequest() != 0)
            engine_.setReplay(0);
        else if (filledMask_.load(std::memory_order_relaxed) != 0)
            engine_.setReplay(1);
    } else if (t == kTargetSave) {
        saveRequested_.store(true, std::memory_order_relaxed);
    }
    return NoteRoute::Bound;
}

void DubcyclingProcessor::restoreState(const PluginState& s) {
    PluginState clean = s;
    for (LoopRef* ref : { &clean.loop1, &clean.loop2 }) {
        const int cut = std::max(ref->fileName.lastIndexOfChar('/'), ref->fileName.lastIndexOfChar('\\'));
        if (cut >= 0)
            ref->fileName = ref->fileName.substring(cut + 1);
    }
    applyState(clean);
}

void DubcyclingProcessor::setActivePreset(const juce::String& bank, const juce::String& preset) {
    const juce::ScopedLock sl(stateLock_);
    state_.activeBank = bank;
    state_.activePreset = preset;
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
            dst.labels = src.labels;
        }
        state_.rules = clean->rules;
        state_.fx = clean->fx;
        state_.passes = clean->passes;
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
    lastGoodDir_ = file.getParentDirectory();
    startLoad(slot, file);
    relinkOtherIfMissing(slot); // e.g. both loops moved together: their new folder is known now
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
    lastGoodDir_ = file.getParentDirectory();
    if (relinkOtherIfMissing(slot))
        publish();
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
    if (saveRequested_.exchange(false, std::memory_order_relaxed))
        savePass(); // a bound Save note (audio thread); nothing happens if there is nothing to save
    engine_.releaseRetired();
}
int DubcyclingProcessor::planTrigger(juce::MidiBuffer& midi, int n,
                                     std::array<TriggerSegment, MidiTrigger::kMaxSegments>& segments) {
    auto* head = getPlayHead();
    const TransportInfo t = toTransportInfo(head != nullptr ? head->getPosition()
                                                            : juce::Optional<juce::AudioPlayHead::PositionInfo> {});
    if (t.bpm > 0.0)
    {
        hostBpm_.store(t.bpm, std::memory_order_relaxed);
        if (t.tsNum >= 1 && t.tsNum <= 64)
            hostBeatsPerBar_.store(t.tsNum, std::memory_order_relaxed);
    }
    trigger_.setMode(triggerMode_.load(std::memory_order_relaxed));

    // Gate-relevant MIDI events, in frame order (MidiBuffer iterates sorted). Fixed storage, extras ignored.
    constexpr int kMaxGateEvents = 512;
    std::array<GateEvent, kMaxGateEvents> events;
    int numEvents = 0;
    if (releaseBoundNotes_.exchange(false, std::memory_order_relaxed)) {
        // Bindings changed by a state load: release every bound note on all channels at frame 0.
        for (int t = 0; t < kBindTargets; ++t) {
            const int note = boundNotes_[static_cast<std::size_t>(t)].load(std::memory_order_relaxed);
            if (note < 0)
                continue;
            GateEvent e;
            e.frame = 0;
            e.kind = GateEvent::Kind::NoteOff;
            e.note = note;
            for (int ch = 0; ch < 16 && numEvents < kMaxGateEvents; ++ch) {
                e.channel = ch;
                events[static_cast<std::size_t>(numEvents++)] = e;
            }
        }
    }
    {
        for (const auto meta : midi) {
            if (numEvents >= kMaxGateEvents)
                break;
            const auto msg = meta.getMessage();
            GateEvent e;
            // n == 0: no frame to put an event on (the trigger then only takes its note-offs)
            e.frame = n > 0 ? std::clamp(meta.samplePosition, 0, n - 1) : 0;
            e.channel = std::clamp(msg.getChannel() - 1, 0, 15);
            if (msg.isNoteOn(false)) {
                const NoteRoute route = routeNoteOn(msg.getNoteNumber());
                if (route != NoteRoute::Gate) {
                    if (route == NoteRoute::Learned) {
                        // Just learned: the note never reaches the gate from now on, so release it
                        // on every channel in case it is held (the gate would stick otherwise).
                        e.kind = GateEvent::Kind::NoteOff;
                        e.note = msg.getNoteNumber();
                        for (int ch = 0; ch < 16 && numEvents < kMaxGateEvents; ++ch) {
                            e.channel = ch;
                            events[static_cast<std::size_t>(numEvents++)] = e;
                        }
                    }
                    continue; // a control note, not a gate event
                }
                e.kind = GateEvent::Kind::NoteOn;
                e.note = msg.getNoteNumber();
            } else if (msg.isNoteOff(true)) { // also note-on with velocity 0
                if (targetOfNote(msg.getNoteNumber()) >= 0)
                    continue; // bound notes never reach the gate
                e.kind = GateEvent::Kind::NoteOff;
                e.note = msg.getNoteNumber();
            } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
                e.kind = GateEvent::Kind::AllOff;
            } else {
                continue;
            }
            events[static_cast<std::size_t>(numEvents++)] = e;
        }
    }

    return trigger_.plan(t, events.data(), numEvents, n, segments);
}

void DubcyclingProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    std::array<TriggerSegment, MidiTrigger::kMaxSegments> segments;
    planTrigger(midi, buffer.getNumSamples(), segments); // held notes, arms and clock stay exact
    buffer.clear();
}

void DubcyclingProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2) {
        buffer.clear();
        return;
    }
    std::array<TriggerSegment, MidiTrigger::kMaxSegments> segments;
    const int numSegments = planTrigger(midi, n, segments);
    float* l = buffer.getWritePointer(0);
    float* r = buffer.getWritePointer(1);
    for (int i = 0; i < numSegments; ++i) {
        const auto& seg = segments[static_cast<std::size_t>(i)];
        engine_.process(seg.t, l + seg.frame, r + seg.frame, seg.length);
    }
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
