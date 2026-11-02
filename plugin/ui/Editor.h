#pragma once
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"
#include "plugin/ui/LoopLane.h"

namespace dc::ui {

constexpr int kMaxBars = 256;

// Bars suggestion for a new loop file: suggestBars(length, hostBpm or 120 when unknown, 4 beats).
// 1 when the file cannot be read.
int suggestedBarsFor(const juce::File& file, double hostBpm);

// Edit view: Loop 1 lane on top, Loop 2 below, each with bars field, "Load..." button and slot
// status; the combined cycle length; a placeholder for the rule panel (Task 11). A 10 Hz timer
// polls the processor and refreshes the lanes; edits go through processor.applyEdit.
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(DubcyclingProcessor& proc);
    ~Editor() override;

    // Selected region of Loop 1 (for the rule panel).
    int selectedRegion1() const { return lanes_[0].selectedRegion(); }
    juce::String cycleText() const { return cycle_.getText(); }
    const LoopLane& lane(int slot) const { return lanes_[slot == 0 ? 0 : 1]; }
    // Pulls state, slot status and loop data from the processor (the timer calls it).
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override { refresh(); }
    void regionsEdited(int slot, const RegionMap& regions);
    void barsEdited(int slot);
    void fileChosen(int slot, const juce::File& file);
    void openChooser(int slot);

    DubcyclingProcessor& proc_;
    LoopLane lanes_[2];
    juce::Label titles_[2];
    juce::Label status_[2];
    juce::Label barsLabel_[2];
    juce::Slider bars_[2];
    juce::TextButton load_[2];
    juce::Label cycle_;
    juce::Label rulesPlaceholder_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};

} // namespace dc::ui
