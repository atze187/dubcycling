#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/FxSettings.h"
#include "engine/RegionMap.h"

namespace dc::ui {

// One colour per insert, clearly distinguishable on the dark panel (also used by the effects panel).
juce::Colour fxInsertColour(FxInsert i);

// Strip under the Loop-2 lane: per region four markers (one per insert, filled = on, outline = off),
// laid out over the same x range as the region in the lane above. A region too narrow for four
// markers shows one collective marker instead; clicking that reports nothing (switch in the panel).
// A click only reports (onToggled); the caller decides the new state. Mouse input goes through
// toggleAt (local pixels), which the tests call directly.
class FxStrip final : public juce::Component
{
public:
    FxStrip() = default;

    void setRegions(const RegionMap& regions);
    void setSwitches(const std::vector<std::uint8_t>& masks);
    void setSelectedRegion(int region);

    std::function<void(int region, FxInsert insert)> onToggled;

    bool markerOn(int region, FxInsert insert) const;
    // Individual marker; empty for a collective or non-existing region.
    juce::Rectangle<float> markerBounds(int region, FxInsert insert) const;
    bool isCollective(int region) const;
    juce::Rectangle<float> collectiveBounds(int region) const; // empty unless collective
    // x range that hits a marker: its rect widened to the middle of the gaps and the region edges, so
    // neighbouring columns touch without overlapping ([start, end)). Full strip height; only for a
    // non-collective region.
    juce::Range<float> columnRange(int region, FxInsert insert) const;
    // True and fills region/insert if (x, y) is in a marker's column.
    bool markerAt(float x, float y, int& region, FxInsert& insert) const;
    void toggleAt(float x, float y);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;
        toggleAt(e.position.x, e.position.y);
    }

private:
    float xFor(double phase) const { return static_cast<float>(phase * getWidth()); }
    float markerWidth(int region) const;
    std::uint8_t maskOf(int region) const;

    RegionMap regions_;
    std::vector<std::uint8_t> switches_;
    int selected_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxStrip)
};

} // namespace dc::ui
