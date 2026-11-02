#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/RuleSet.h"

namespace dc::ui {

// Spreadsheet-style row letters: 0 -> "A", 25 -> "Z", 26 -> "AA", ...
juce::String regionLetters(int index);

// The rules of one Loop-1 region: one row per CURRENT Loop-2 region
//   A  |-------------o-------|  70 %  [GATE]
// (slider 0..100 %, percent value, Gate/One-Shot toggle) and "Stille: x %" for the remainder.
//
// Weights are percent here (0..100), RuleTarget::weight is 0..1. A change that would lift the
// sum above 100 % is capped at the remaining percent; the other rows stay as they are.
//
// Rows whose region does not exist any more (Loop-2 boundaries were removed) are not shown, but
// their targets are kept and published unchanged with every edit of another row; they also keep
// counting against the 100 % budget (the engine would otherwise rescale the visible rows).
// Targets are stored for every row the user touched, also with weight 0, so a row keeps its
// mode while its weight is 0; rows never touched are not stored.
//
// Publishing: setWeight/setMode publish once through onChanged. A slider drag only updates the
// row and the labels locally and publishes once at its end (beginDrag/dragWeight/endDrag are the
// seams the slider callbacks use).
class RulePanel final : public juce::Component
{
public:
    RulePanel();
    ~RulePanel() override;

    // Shows the rule of `region1` for a Loop 2 with `region2Count` regions. `targets` is the stored
    // list (may contain stale entries). No-op if nothing changed or while a drag is in progress.
    void setContext(int region1, int region2Count, const std::vector<RuleTarget>& targets);

    std::function<void(int region1, std::vector<RuleTarget> targets)> onChanged;

    void setWeight(int row, float percent);
    void setMode(int row, PlayMode mode);
    void beginDrag(int row);
    void dragWeight(int row, float percent);
    void endDrag();

    int region1() const { return region1_; }
    int rowCount() const { return static_cast<int>(rows_.size()); }
    juce::String rowLabel(int row) const;
    float weightPercent(int row) const;
    PlayMode mode(int row) const;
    juce::String percentText(int row) const;
    juce::String silenceText() const { return silence_.getText(); }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Row;
    static constexpr int kRowH = 28;

    float otherPercent(int row) const; // everything except this row's own target
    void applyWeight(int row, float percent);
    void updateSilence();
    void publish();
    std::vector<RuleTarget> buildTargets() const;
    void rebuildRows(int count);

    int region1_ = 0;
    std::vector<RuleTarget> stored_; // the full list as last set / published, stale entries included
    std::vector<std::unique_ptr<Row>> rows_;
    int dragRow_ = -1;
    bool dirty_ = false;

    juce::Label title_;
    juce::Label silence_;
    juce::Viewport viewport_;
    juce::Component holder_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RulePanel)
};

} // namespace dc::ui
