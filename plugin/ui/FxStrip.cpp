#include "plugin/ui/FxStrip.h"
#include <algorithm>
#include "plugin/ui/PanelLook.h"

namespace dc::ui {

namespace {
constexpr float kInset = 1.0f;     // between region edge and first marker
constexpr float kGap = 1.0f;       // between markers
constexpr float kMinMarker = 4.0f; // narrower markers: collective marker instead
constexpr float kPadY = 3.0f;
} // namespace

juce::Colour fxInsertColour(FxInsert i)
{
    switch (i)
    {
        case FxInsert::Bitcrush: return juce::Colour(0xffffb347); // amber
        case FxInsert::Filter:   return juce::Colour(0xff5aa9e6); // blue
        case FxInsert::Flanger:  return juce::Colour(0xff7fe3a0); // green
        case FxInsert::Reverb:   return juce::Colour(0xffc58af0); // violet
    }
    return panel::kDim;
}

void FxStrip::setRegions(const RegionMap& regions)
{
    if (regions.boundaries == regions_.boundaries)
        return;
    regions_ = regions;
    selected_ = std::clamp(selected_, 0, std::max(0, regions_.regionCount() - 1));
    repaint();
}

void FxStrip::setLive(bool live)
{
    if (live == live_)
        return;
    live_ = live;
    repaint();
}

void FxStrip::setSwitches(const std::vector<std::uint8_t>& masks)
{
    if (masks == switches_)
        return;
    switches_ = masks;
    repaint();
}

void FxStrip::setSelectedRegion(int region)
{
    const int r = std::clamp(region, 0, std::max(0, regions_.regionCount() - 1));
    if (r == selected_)
        return;
    selected_ = r;
    repaint();
}

std::uint8_t FxStrip::maskOf(int region) const
{
    return region >= 0 && region < static_cast<int>(switches_.size()) ? switches_[static_cast<std::size_t>(region)] : 0;
}

bool FxStrip::markerOn(int region, FxInsert insert) const
{
    return (maskOf(region) & (1 << static_cast<int>(insert))) != 0;
}

float FxStrip::markerWidth(int region) const
{
    if (region < 0 || region >= count())
        return 0.0f;
    const auto [lo, hi] = regions_.bounds(region);
    const float w = xFor(hi) - xFor(lo);
    return (w - 2.0f * kInset - (kFxInserts - 1) * kGap) / kFxInserts;
}

bool FxStrip::isCollective(int region) const
{
    return region >= 0 && region < count() && markerWidth(region) < kMinMarker;
}

juce::Rectangle<float> FxStrip::markerBounds(int region, FxInsert insert) const
{
    if (region < 0 || region >= count() || isCollective(region))
        return {};
    const float mw = markerWidth(region);
    const float x0 = xFor(regions_.bounds(region).first) + kInset;
    const int k = static_cast<int>(insert);
    return { x0 + static_cast<float>(k) * (mw + kGap), kPadY, mw, static_cast<float>(getHeight()) - 2.0f * kPadY };
}

juce::Rectangle<float> FxStrip::collectiveBounds(int region) const
{
    if (!isCollective(region))
        return {};
    const auto [lo, hi] = regions_.bounds(region);
    const float x0 = xFor(lo), x1 = xFor(hi);
    const float w = std::max(0.0f, x1 - x0 - 2.0f * kInset);
    return { x0 + kInset, kPadY, w, static_cast<float>(getHeight()) - 2.0f * kPadY };
}

juce::Range<float> FxStrip::columnRange(int region, FxInsert insert) const
{
    // Contiguous columns: the marker widened to the middle of the gap (the region edge at the ends).
    const auto [lo, hi] = regions_.bounds(region);
    const int k = static_cast<int>(insert);
    const auto m = markerBounds(region, insert);
    const float left = k == 0 ? xFor(lo) : m.getX() - kGap * 0.5f;
    const float right = k == kFxInserts - 1 ? xFor(hi) : m.getRight() + kGap * 0.5f;
    return { left, right };
}

bool FxStrip::markerAt(float x, float y, int& region, FxInsert& insert) const
{
    if (y < 0.0f || y >= static_cast<float>(getHeight()))
        return false;
    for (int r = 0; r < count(); ++r)
    {
        if (isCollective(r))
            continue;
        for (int k = 0; k < kFxInserts; ++k)
        {
            const auto col = columnRange(r, static_cast<FxInsert>(k));
            if (x >= col.getStart() && x < col.getEnd())
            {
                region = r;
                insert = static_cast<FxInsert>(k);
                return true;
            }
        }
    }
    return false;
}

void FxStrip::toggleAt(float x, float y)
{
    int region = 0;
    FxInsert insert = FxInsert::Bitcrush;
    if (markerAt(x, y, region, insert) && onToggled)
        onToggled(region, insert);
}

void FxStrip::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(panel::kWave);
    g.fillRect(bounds);

    if (selected_ >= 0 && selected_ < count())
    {
        const auto [lo, hi] = regions_.bounds(selected_);
        g.setColour(panel::kAccent.withAlpha(0.14f));
        g.fillRect(juce::Rectangle<float>(xFor(lo), 0.0f, xFor(hi) - xFor(lo), bounds.getHeight()));
    }

    for (int r = 0; r < count(); ++r)
    {
        if (isCollective(r))
        {
            int first = -1;
            for (int k = 0; k < kFxInserts && first < 0; ++k)
                if (markerOn(r, static_cast<FxInsert>(k)))
                    first = k;
            g.setColour(first >= 0 ? fxInsertColour(static_cast<FxInsert>(first)) : panel::kEdge);
            g.fillRect(collectiveBounds(r));
            continue;
        }
        for (int k = 0; k < kFxInserts; ++k)
        {
            const auto insert = static_cast<FxInsert>(k);
            const auto rect = markerBounds(r, insert);
            g.setColour(fxInsertColour(insert));
            if (markerOn(r, insert))
                g.fillRect(rect);
            else
                g.drawRect(rect, 1.0f);
        }
    }

    g.setColour(panel::kEdge);
    g.drawRect(bounds, 1.0f);
}

} // namespace dc::ui
