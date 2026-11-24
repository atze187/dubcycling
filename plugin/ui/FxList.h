#pragma once
#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/FxSettings.h"

namespace dc::ui {

// Display name of an insert (BITCRUSH, FILTER, FLANGER, DELAY, REVERB).
const char* fxInsertName(FxInsert i);

// The effect chain as a list: one row per insert in chain order. A row has a drag handle, a marker in
// the insert's colour, the name and the on/off switch of the insert for the selected Loop-2 region
// (greyed and inert without a valid region). A click on a row selects it; dragging a row (by the
// handle, or the row after a few pixels) shows it lifted with an insertion line and changes the order
// only on the drop: onOrderChanged fires once per completed drag, never during it (each committed
// order change costs a short effect mute in the engine). Dragging works without a region.
//
// Mouse input goes through pressAt / dragTo / release (local pixels), which the tests call directly.
class FxList final : public juce::Component, public juce::SettableTooltipClient
{
public:
    FxList();

    // order: shown as is (invalid -> default); ignored while a drag runs. selectedInsert: int of the
    // FxInsert to highlight. mask: switch bits of the selected region.
    void setContext(const FxOrder& order, int selectedInsert, std::uint8_t maskOfSelectedRegion, bool switchesEnabled);
    // Text for the switch tooltips, e.g. "region C".
    void setRegionText(const juce::String& text) { regionText_ = text; }

    std::function<void(FxInsert)> onSelected;
    std::function<void(FxInsert, bool on)> onSwitched;
    std::function<void(const FxOrder&)> onOrderChanged;

    const FxOrder& order() const { return order_; }
    FxInsert insertAt(int position) const;
    int positionOf(FxInsert i) const;
    FxInsert selected() const { return selected_; }
    void select(FxInsert i); // no callback
    bool switchOn(FxInsert i) const { return enabled_ && ((mask_ >> static_cast<int>(i)) & 1) != 0; }
    bool switchesEnabled() const { return enabled_; }

    // Layout seams (local pixels).
    juce::Rectangle<int> rowBounds(int position) const;
    juce::Rectangle<int> switchBounds(FxInsert i) const;
    juce::Rectangle<int> handleBounds(int position) const;

    // Completes a drag: the row at fromPosition ends up at toPosition (both 0..4). Publishes the new
    // full permutation once if it changed.
    void dragRow(int fromPosition, int toPosition);

    // Mouse seams.
    void pressAt(juce::Point<float> p);
    void dragTo(juce::Point<float> p);
    void release();
    bool isDragging() const { return dragFrom_ >= 0 && dragActive_; }
    int draggedPosition() const { return isDragging() ? dragFrom_ : -1; }
    // Gap the dragged row would drop into: 0 = before the first row ... kFxInserts = after the last;
    // -1 when no drag runs.
    int insertionSlot() const { return isDragging() ? slot_ : -1; }

    juce::String getTooltip() override;
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    int rowHeight() const;
    int top() const { return 0; }
    int positionAt(float y) const; // -1 outside the rows
    int slotFor(float y) const;
    void paintRow(juce::Graphics& g, int position, juce::Rectangle<int> r, bool lifted) const;

    FxOrder order_ = defaultFxOrder();
    FxInsert selected_ = FxInsert::Bitcrush;
    std::uint8_t mask_ = 0;
    bool enabled_ = false;
    juce::String regionText_;

    // press / drag state
    int dragFrom_ = -1;      // row pressed for a possible drag
    bool dragActive_ = false; // the press became a drag
    float pressY_ = 0.0f, grabOffset_ = 0.0f, cursorY_ = 0.0f;
    int slot_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxList)
};

} // namespace dc::ui
