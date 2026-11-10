#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/Engine.h"
#include "engine/MidiTrigger.h"
#include "plugin/LoopLoader.h"
#include "plugin/PluginState.h"
#include "plugin/SlotTracker.h"

namespace dc {

class DubcyclingProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    DubcyclingProcessor();
    ~DubcyclingProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // Host bypass: silence, but the MIDI trigger still sees every note (a note-off while bypassed
    // must not leave the plugin playing afterwards) and its clock runs on.
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dubcycling"; }
    bool acceptsMidi() const override { return true; } // notes gate the output in MIDI trigger mode; ignored in Autoplay
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- Loop slots and persisted state ----
    // Threading: setStateInformation, the editor's edits/loads and the timer run on the message
    // thread; getStateInformation may be called from another host thread. state_, the slot tracker
    // and model_ are guarded by stateLock_; file IO (hashing) is done outside the lock.
    using SlotStatus = dc::SlotStatus;
    // Loading also while a replaceLoop is staged (the old loop still plays until it has decoded).
    SlotStatus getSlotStatus(int slot) const;
    // Failed: the loader's error text (e.g. "unsupported or damaged file"); empty otherwise.
    juce::String slotError(int slot) const;
    // File name of a staged replaceLoop that is still decoding; empty if none.
    juce::String pendingReplaceFileName(int slot) const;
    // The slot's last replaceLoop that failed to decode (id 0 = none; ids increase).
    SlotTracker::ReplaceFailure replaceFailure(int slot) const;
    // The persisted state (what getStateInformation writes). Message thread.
    PluginState getPluginState() const { const juce::ScopedLock sl(stateLock_); return state_; }
    // Replaces regions/rules/bars (e.g. from the editor) and republishes the model. Boundaries and
    // rules are sanitized like on load. Loop files are not touched.
    void applyEdit(const PluginState& edited);
    // Applies a complete state (e.g. a loaded preset): relink, hash check and async load exactly like
    // setStateInformation. Each loop's fileName is reduced to its leaf first (an imported bank could
    // carry "../x.wav"). Message thread.
    void restoreState(const PluginState& s);
    // Remembers which preset is loaded (saved with the state). No publish. Message thread.
    void setActivePreset(const juce::String& bank, const juce::String& preset);
    // Relink: assigns a file to the slot and KEEPS regions, rules and bars (use for the "file missing"
    // case or a re-exported version of the same loop). Recomputes path/name/hash, loads async.
    // A file that does not exist or cannot be read changes nothing and returns false.
    bool loadLoop(int slot, const juce::File& file);
    // Loads a different loop. Transactional: the replacement is staged and decoded first; only when
    // it decodes, the slot gets the new reference and bars and DROPS its regions and all rules (their
    // region indices no longer mean anything). If decoding fails nothing changes and the failure is
    // reported via replaceFailure(). A file that does not exist or cannot be read returns false.
    bool replaceLoop(int slot, const juce::File& file, int bars);
    // Optional folder to search for moved files (see relink()); empty = none. Message thread.
    // Moved files (name AND hash match) are searched in: the folder of the other slot's file (if it
    // exists), the folder of the last file loaded or relinked in this session (memory only, not
    // persisted), and this folder. After a slot is (re)linked or loaded, a Missing other slot is
    // searched again (both loops moved together are found once one of them is relinked).
    void setSessionDir(const juce::File& dir) { sessionDir_ = dir; }
    // Last tempo the host reported in processBlock; 0 = none yet. Any thread.
    // Trigger mode (see engine/MidiTrigger.h). Any thread; applied at the start of the next processBlock.
    // Saved with the project (getStateInformation), NOT with presets or banks.
    void setTriggerMode(TriggerMode m) { triggerMode_.store(m, std::memory_order_relaxed); }
    TriggerMode triggerMode() const { return triggerMode_.load(std::memory_order_relaxed); }
    double hostBpm() const { return hostBpm_.load(std::memory_order_relaxed); }
    // Numerator of the last time signature the host reported in processBlock (1..64); 4 before. Any thread.
    int hostBeatsPerBar() const { return hostBeatsPerBar_.load(std::memory_order_relaxed); }
    // How long before a Loop-1 region entry its draw is made and shown (see Engine::Playhead::teaser).
    static constexpr double kTeaserLookaheadSec = 0.15;
    // Audition of one region (slot 0 = Loop 1, 1 = Loop 2), see Engine::requestPreview. Any thread.
    void previewRegion(int slot, int region) { engine_.requestPreview(slot, region); }
    // Loop positions at the end of the last block, for the editor's playheads. Any thread.
    Engine::Playhead playhead() const { return engine_.playhead(); }

    // ---- Pass slots (saved passes, see Engine) ----
    // True when the engine has a finished, non-empty pass that was not saved yet, a slot is free and
    // no replay is requested. Message thread.
    bool canSave() const;
    // Stores the last finished pass in the lowest free slot (through applyEdit, so the model is
    // republished seamlessly). False when !canSave(). Message thread.
    bool savePass();
    // Empties one slot (out of range: nothing) or all of them. A replay request that only named the
    // cleared slot(s) is reset to off. Message thread.
    void clearPass(int slot);
    void clearAllPasses();
    bool passFilled(int slot) const;
    // Replay request (0 off, 1 chain, 2 + i slot i; see Engine::setReplay). Any thread.
    void setReplayRequest(int request) { engine_.setReplay(request); }
    int replayRequest() const { return engine_.replayRequest(); }
    int replaySlot() const { return engine_.replaySlot(); }

    // ---- MIDI note bindings (per instance: saved with the project, NOT with presets or banks) ----
    // Targets 0..7 are the pass slots (a bound note requests that slot's replay, ignored while it is
    // empty), kTargetSave saves the last pass (on the message thread), kTargetPlay toggles replay
    // (off -> chain while a slot is filled, any replay -> off). One note per target and one target
    // per note. Bound notes (on and off) never reach the MIDI gate; after bindings are loaded with a
    // state, the next block releases every bound note in the gate. Bound notes also act while the
    // host bypasses the plugin. Any thread (atomics only).
    static constexpr int kTargetSave = 8;
    static constexpr int kTargetPlay = 9;
    static constexpr int kBindTargets = 10;
    // The next note-on binds to `target` (moved if bound elsewhere) and is consumed; -1 cancels.
    void armLearn(int target);
    int learnArmed() const { return learnTarget_.load(std::memory_order_relaxed); }
    int boundNote(int target) const;
    void clearBinding(int target);
    // Increments whenever a binding changes (for the UI to refresh).
    int bindingsVersion() const { return bindingsVersion_.load(std::memory_order_relaxed); }
#ifdef DC_TEST_HOOKS
    // Test hook (only compiled into dc_plugin_tests, which defines DC_TEST_HOOKS): blocks until
    // pending loads are decoded and takes their results. Message thread.
    void waitForLoads() { loader_.waitForAll(); timerCallback(); }
#endif


    // Publishes a new model to the audio thread (lock-free handoff inside Engine::setModel).
    // Call from the message thread (or any non-audio thread); nullptr = silence.
    void setModel(std::shared_ptr<const Model> model);
    // The last published model. Message thread.
    std::shared_ptr<const Model> getModel() const { const juce::ScopedLock sl(stateLock_); return model_; }

private:
    void timerCallback() override;
    // Audio thread: reads the transport and the block's gate events and plans the trigger segments.
    int planTrigger(juce::MidiBuffer& midi, int n, std::array<TriggerSegment, MidiTrigger::kMaxSegments>& segments);
    void startLoad(int slot, const juce::File& file);
    void publish();
    LoopRef& refOf(int slot) { return slot == 0 ? state_.loop1 : state_.loop2; }
    void applyState(const PluginState& s);
    // Folders relink() searches for the slot's moved file (see setSessionDir). Message thread.
    std::vector<juce::File> relinkCandidates(int slot) const;
    // Finds the slot's file (relink); found: new path, starts the load, remembers its folder and
    // retries a Missing other slot. Not found: Missing (regions/rules/bars kept). Caller publishes.
    bool tryRelink(int slot);
    // tryRelink of the other slot if it is Missing; true if it was found.
    bool relinkOtherIfMissing(int slot);

    Engine engine_;
    std::shared_ptr<const Model> model_; // last published model; the audio thread reads via engine_
    PluginState state_;                  // source of truth for getStateInformation
    SlotTracker slots_;                  // status, data, tickets, staged replacements
    juce::File sessionDir_;
    juce::File lastGoodDir_; // folder of the last file loaded or relinked (message thread, memory only)
    mutable juce::CriticalSection stateLock_; // guards state_, slots_ and model_
    LoopLoader loader_;
    std::atomic<double> hostBpm_ { 0.0 };
    std::atomic<int> hostBeatsPerBar_ { 4 };
    std::atomic<TriggerMode> triggerMode_ { TriggerMode::Autoplay };
    MidiTrigger trigger_; // audio thread only

    // Pass slots and bindings.
    // Bit i set: pass slot i is filled in the last published model (read on the audio thread).
    std::atomic<unsigned> filledMask_ { 0 };
    std::atomic<std::uint32_t> lastSavedId_ { 0 }; // id of the last pass savePass stored (or the last before a state load)
    std::atomic<bool> releaseBoundNotes_ { false }; // set by readBindings; the next planTrigger releases bound notes
    std::atomic<bool> saveRequested_ { false }; // a bound Save note; handled in timerCallback
    std::atomic<int> learnTarget_ { -1 };
    std::array<std::atomic<int>, kBindTargets> boundNotes_ {}; // -1 = unbound (set in the constructor)
    std::atomic<int> bindingsVersion_ { 0 };
    // Audio thread: a note-on with a learn armed binds (Learned); a bound note triggers its target
    // (Bound); both are consumed. Anything else gates (Gate).
    enum class NoteRoute { Gate, Bound, Learned };
    NoteRoute routeNoteOn(int note);
    int targetOfNote(int note) const;
    void bindNote(int target, int note);
    // Root attributes of the project state (outside PluginState).
    juce::String bindingAttributes() const;
    void readBindings(const juce::XmlElement& root);
    // Clears slots and resets the replay request if it named only cleared slots. Message thread.
    void clearPasses(unsigned mask);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DubcyclingProcessor)
};

}  // namespace dc
