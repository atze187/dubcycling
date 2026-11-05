#pragma once
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/FxSettings.h"

namespace dc::ui {

// The four insert modules in chain order (Bitcrush, Filter, Flanger, Reverb), side by side. Each
// module has a title in the insert's colour, an on/off switch for the selected Loop-2 region and
// the knobs of the parameter table (shown with their values). A double-click on a slider resets it
// to its default.
//
// The parameter set is one for all regions: every change publishes the WHOLE FxParams once through
// onParamsChanged. A slider drag only follows locally and publishes once at its end (beginDrag /
// dragParam / endDrag are the seams the slider callbacks use); setContext leaves a drag alone.
class FxPanel final : public juce::Component
{
public:
    FxPanel();
    ~FxPanel() override;

    // Shows the switches of `region2` (of `regionCount` Loop-2 regions) and all knob values.
    // Without a valid region the switches are disabled. No-op for the knobs while a drag runs.
    void setContext(int region2, int regionCount, const std::vector<juce::String>& labels, const FxSettings& fx);

    std::function<void(const FxParams&)> onParamsChanged;
    std::function<void(int region2, FxInsert insert, bool on)> onSwitchChanged;

    // Like a user action: sanitized, shown, published once.
    void setParam(FxParam p, double value);
    void resetParam(FxParam p); // default value, like a double-click
    void beginDrag(FxParam p);
    void dragParam(FxParam p, double value); // local only
    void endDrag();
    void setSwitch(FxInsert i, bool on); // no-op while the switches are disabled

    double param(FxParam p) const { return static_cast<double>(params_.get(p)); }
    juce::String paramText(FxParam p) const;
    bool switchOn(FxInsert i) const;
    bool switchEnabled(FxInsert i) const;
    juce::String switchText(FxInsert i) const;

    // Layout seams: the area of a module's switch and of a knob (name, value and slider / buttons).
    juce::Rectangle<int> switchBounds(FxInsert i) const;
    juce::Rectangle<int> paramBounds(FxParam p) const;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Module;
    struct Knob;
    Knob& knob(FxParam p) { return *knobs_[static_cast<std::size_t>(p)]; }
    const Knob& knob(FxParam p) const { return *knobs_[static_cast<std::size_t>(p)]; }
    void apply(FxParam p, double value);
    void show(FxParam p);
    void publish();
    void userSwitched(FxInsert i, bool on);

    FxParams params_;
    int region2_ = 0;
    int regionCount_ = 0;
    bool dragging_ = false;
    bool dirty_ = false;

    std::array<std::unique_ptr<Module>, kFxInserts> modules_;
    std::array<std::unique_ptr<Knob>, kFxParamCount> knobs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxPanel)
};

} // namespace dc::ui
