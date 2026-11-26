#pragma once
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/FxSettings.h"
#include "plugin/ui/FxList.h"

namespace dc::ui {

// The effects in two parts: on the left the effect list (FxList: chain order, drag to reorder, the
// on/off switches of the selected Loop-2 region), on the right the edit view of the effect selected
// in the list: its knobs (name, value, slider; Filter Type and LFO Shape as buttons, Delay Reverse as
// a toggle). The Filter's view has two columns: mode, Cutoff, Resonance; the LFO's Rate, Depth, Shape.
// A double-click on a slider resets it to its default. The selection is UI state only: initially the
// first effect in the order of the first setContext.
//
// The parameter set is one for all regions: every change publishes the WHOLE FxParams once through
// onParamsChanged. A slider drag only follows locally and publishes once at its end (beginDrag /
// dragParam / endDrag are the seams the slider callbacks use); setContext leaves a drag alone.
// A completed list drag publishes the new order once through onOrderChanged.
class FxPanel final : public juce::Component
{
public:
    FxPanel();
    ~FxPanel() override;

    // Shows the switches of `region2` (of `regionCount` Loop-2 regions), the order and all knob
    // values. Without a valid region the switches are disabled. No-op for the knobs while a drag runs.
    void setContext(int region2, int regionCount, const std::vector<juce::String>& labels, const FxSettings& fx);

    std::function<void(const FxParams&)> onParamsChanged;
    std::function<void(int region2, FxInsert insert, bool on)> onSwitchChanged;
    std::function<void(const FxOrder&)> onOrderChanged;

    // Like a user action: sanitized, shown, published once.
    void setParam(FxParam p, double value);
    void resetParam(FxParam p); // default value, like a double-click
    void beginDrag(FxParam p);
    void dragParam(FxParam p, double value); // local only
    void endDrag();
    void setSwitch(FxInsert i, bool on); // no-op while the switches are disabled

    // The effect shown in the edit view (also highlighted in the list).
    void selectInsert(FxInsert i);
    FxInsert selectedInsert() const { return selected_; }

    double param(FxParam p) const { return static_cast<double>(params_.get(p)); }
    juce::String paramText(FxParam p) const;
    // Shown dimmed: has no audible effect at the current settings (Flanger Manual unless Rate is 0).
    bool paramDimmed(FxParam p) const;
    bool switchOn(FxInsert i) const;
    bool switchEnabled(FxInsert i) const;
    juce::String switchText(FxInsert i) const; // "Region C" / "no region"

    // Layout seams: the list switch of an insert and a knob (name, value and slider / buttons), in
    // panel coordinates. paramBounds is empty for knobs of effects not shown in the edit view.
    juce::Rectangle<int> switchBounds(FxInsert i) const;
    juce::Rectangle<int> paramBounds(FxParam p) const;

    FxList& list() { return list_; }
    const FxList& list() const { return list_; }
    juce::Button& reverseButton();
    juce::Button& modeButton(int i);  // Filter Type 0..5: LP HP BP NOTCH PEAK LP24
    juce::Button& shapeButton(int i); // Filter LFO Shape 0..3: SINE TRI SAW SQR
    juce::Slider& slider(FxParam p);  // not used for button knobs

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Knob;
    Knob& knob(FxParam p) { return *knobs_[static_cast<std::size_t>(p)]; }
    const Knob& knob(FxParam p) const { return *knobs_[static_cast<std::size_t>(p)]; }
    void apply(FxParam p, double value);
    void show(FxParam p);
    void publish();
    void userSwitched(FxInsert i, bool on);
    void updateList();
    void updateVisibility();
    void updateDimming();
    void layoutFilter(juce::Rectangle<int> area);
    void layoutKnob(FxParam p, juce::Rectangle<int> row);

    FxParams params_;
    FxOrder order_ = defaultFxOrder();
    std::uint8_t mask_ = 0;
    int region2_ = 0;
    int regionCount_ = 0;
    bool valid_ = false;
    juce::String switchText_ = "no region";
    FxInsert selected_ = FxInsert::Bitcrush;
    bool selectionSet_ = false;
    bool dragging_ = false;
    bool dirty_ = false;

    FxList list_;
    juce::Label listTitle_, regionTitle_, editTitle_;
    juce::Label lfoTitle_; // caption of the Filter's LFO column
    std::array<std::unique_ptr<Knob>, kFxParamCount> knobs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxPanel)
};

} // namespace dc::ui
