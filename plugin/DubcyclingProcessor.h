#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/Engine.h"
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

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dubcycling"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
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
    double hostBpm() const { return hostBpm_.load(std::memory_order_relaxed); }
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DubcyclingProcessor)
};

}  // namespace dc
