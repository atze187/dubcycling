#pragma once

#include <atomic>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/Engine.h"
#include "plugin/LoopLoader.h"
#include "plugin/PluginState.h"

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

    // ---- Loop slots and persisted state (message thread) ----
    enum class SlotStatus { Empty, Loading, Loaded, Missing };
    SlotStatus getSlotStatus(int slot) const;
    // The persisted state (what getStateInformation writes). Message thread.
    PluginState getPluginState() const { const juce::ScopedLock sl(stateLock_); return state_; }
    // Replaces regions/rules/bars (e.g. from the editor) and republishes the model. Boundaries and
    // rules are sanitized like on load. Loop files are not touched.
    void applyEdit(const PluginState& edited);
    // Relink: assigns a file to the slot and KEEPS regions, rules and bars (use for the "file missing"
    // case or a re-exported version of the same loop). Recomputes path/name/hash, loads async.
    void loadLoop(int slot, const juce::File& file);
    // Loads a different loop: sets bars, DROPS that slot's regions and all rules (their region
    // indices no longer mean anything), loads async.
    void replaceLoop(int slot, const juce::File& file, int bars);
    // Optional folder to search for moved files (see relink()); empty = none.
    void setSessionDir(const juce::File& dir) { sessionDir_ = dir; }
    // Last tempo the host reported in processBlock; 0 = none yet. Any thread.
    double hostBpm() const { return hostBpm_.load(std::memory_order_relaxed); }
    // Test seam: blocks until pending loads are decoded and takes their results. Message thread.
    void waitForLoads() { loader_.waitForAll(); timerCallback(); }


    // Publishes a new model to the audio thread (lock-free handoff inside Engine::setModel).
    // Call from the message thread (or any non-audio thread); nullptr = silence.
    void setModel(std::shared_ptr<const Model> model);
    // The last published model. Message thread only.
    std::shared_ptr<const Model> getModel() const { return model_; }

private:
    void timerCallback() override;
    void startLoad(int slot, const juce::File& file);
    void publish();
    LoopRef& refOf(int slot) { return slot == 0 ? state_.loop1 : state_.loop2; }
    void applyState(const PluginState& s);

    Engine engine_;
    std::shared_ptr<const Model> model_; // message-thread copy; the audio thread reads via engine_
    PluginState state_;                  // message thread; source of truth for getStateInformation
    std::shared_ptr<const StereoData> data_[2];
    SlotStatus status_[2] { SlotStatus::Empty, SlotStatus::Empty };
    std::uint64_t ticket_[2] { 0, 0 };
    std::uint64_t nextTicket_ = 1;
    juce::File sessionDir_;
    mutable juce::CriticalSection stateLock_; // guards state_ (hosts may call getStateInformation off the message thread)
    LoopLoader loader_;
    std::atomic<double> hostBpm_ { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DubcyclingProcessor)
};

}  // namespace dc
