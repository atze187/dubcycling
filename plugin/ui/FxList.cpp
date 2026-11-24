#include "plugin/ui/FxList.h"
#include <algorithm>
#include <cmath>
#include "plugin/ui/FxStrip.h"
#include "plugin/ui/PanelLook.h"

namespace dc::ui {

namespace {
using namespace panel;

constexpr int kRowGap = 2;      // between rows
constexpr int kHandleW = 14;
constexpr int kMarkerW = 4;
constexpr int kSwitchW = 30;
constexpr int kSwitchH = 14;
constexpr int kSwitchInset = 4; // from the row's right edge
constexpr float kDragThreshold = 4.0f; // a body press becomes a drag after this many pixels
} // namespace

const char* fxInsertName(FxInsert i)
{
    switch (i)
    {
        case FxInsert::Bitcrush: return "BITCRUSH";
        case FxInsert::Filter: return "FILTER";
        case FxInsert::Flanger: return "FLANGER";
        case FxInsert::Reverb: return "REVERB";
        case FxInsert::Delay: return "DELAY";
    }
    return "";
}

FxList::FxList()
{
    setInterceptsMouseClicks(true, false);
}

void FxList::setContext(const FxOrder& order, int selectedInsert, std::uint8_t maskOfSelectedRegion, bool switchesEnabled)
{
    if (!isDragging())
        order_ = sanitizeFxOrder(order);
    selected_ = static_cast<FxInsert>(std::clamp(selectedInsert, 0, kFxInserts - 1));
    mask_ = maskOfSelectedRegion;
    enabled_ = switchesEnabled;
    repaint();
}

void FxList::select(FxInsert i)
{
    if (i == selected_)
        return;
    selected_ = i;
    repaint();
}

FxInsert FxList::insertAt(int position) const
{
    return order_[static_cast<std::size_t>(std::clamp(position, 0, kFxInserts - 1))];
}

int FxList::positionOf(FxInsert i) const
{
    for (int k = 0; k < kFxInserts; ++k)
        if (order_[static_cast<std::size_t>(k)] == i)
            return k;
    return 0;
}

int FxList::rowHeight() const { return std::clamp(getHeight() / kFxInserts, 18, 28); }

juce::Rectangle<int> FxList::rowBounds(int position) const
{
    if (position < 0 || position >= kFxInserts)
        return {};
    const int h = rowHeight();
    return { 0, top() + position * h, getWidth(), h - kRowGap };
}

juce::Rectangle<int> FxList::handleBounds(int position) const
{
    const auto row = rowBounds(position);
    return row.isEmpty() ? row : row.withWidth(kHandleW);
}

juce::Rectangle<int> FxList::switchBounds(FxInsert i) const
{
    const auto row = rowBounds(positionOf(i));
    return { row.getRight() - kSwitchInset - kSwitchW, row.getCentreY() - kSwitchH / 2, kSwitchW, kSwitchH };
}

int FxList::positionAt(float y) const
{
    if (y < static_cast<float>(top()))
        return -1;
    const int k = static_cast<int>(std::floor((y - static_cast<float>(top())) / static_cast<float>(rowHeight())));
    return k >= 0 && k < kFxInserts ? k : -1;
}

int FxList::slotFor(float y) const
{
    const float s = (y - static_cast<float>(top())) / static_cast<float>(rowHeight());
    return std::clamp(static_cast<int>(std::lround(s)), 0, kFxInserts);
}

void FxList::dragRow(int fromPosition, int toPosition)
{
    if (fromPosition < 0 || fromPosition >= kFxInserts || toPosition < 0 || toPosition >= kFxInserts
        || fromPosition == toPosition)
        return;
    auto o = order_;
    const auto moved = o[static_cast<std::size_t>(fromPosition)];
    if (fromPosition < toPosition)
        std::rotate(o.begin() + fromPosition, o.begin() + fromPosition + 1, o.begin() + toPosition + 1);
    else
        std::rotate(o.begin() + toPosition, o.begin() + fromPosition, o.begin() + fromPosition + 1);
    jassert(o[static_cast<std::size_t>(toPosition)] == moved);
    juce::ignoreUnused(moved);
    order_ = sanitizeFxOrder(o);
    repaint();
    if (onOrderChanged)
        onOrderChanged(order_);
}

void FxList::pressAt(juce::Point<float> p)
{
    dragFrom_ = -1;
    dragActive_ = false;
    const int k = positionAt(p.y);
    if (k < 0 || p.x < 0.0f || p.x >= static_cast<float>(getWidth()))
        return;
    const auto ins = insertAt(k);
    if (switchBounds(ins).toFloat().contains(p))
    {
        if (!enabled_)
            return;
        const bool on = !switchOn(ins);
        const auto bit = static_cast<std::uint8_t>(1u << static_cast<int>(ins));
        mask_ = on ? static_cast<std::uint8_t>(mask_ | bit) : static_cast<std::uint8_t>(mask_ & ~bit);
        repaint();
        if (onSwitched)
            onSwitched(ins, on);
        return;
    }
    dragFrom_ = k;
    pressY_ = p.y;
    cursorY_ = p.y;
    grabOffset_ = p.y - static_cast<float>(rowBounds(k).getY());
    slot_ = slotFor(p.y);
    if (handleBounds(k).toFloat().contains(p))
    {
        dragActive_ = true; // the handle drags at once and does not select
        repaint();
        return;
    }
    if (ins != selected_)
    {
        selected_ = ins;
        repaint();
        if (onSelected)
            onSelected(ins);
    }
}

void FxList::dragTo(juce::Point<float> p)
{
    if (dragFrom_ < 0)
        return;
    if (!dragActive_ && std::abs(p.y - pressY_) < kDragThreshold)
        return;
    dragActive_ = true;
    cursorY_ = p.y;
    slot_ = slotFor(p.y);
    repaint();
}

void FxList::release()
{
    const bool wasDrag = isDragging();
    const int from = dragFrom_;
    const int slot = slot_;
    dragFrom_ = -1;
    dragActive_ = false;
    slot_ = -1;
    if (!wasDrag)
        return;
    repaint();
    dragRow(from, slot > from ? slot - 1 : slot); // the one publish of the drag
}

juce::String FxList::getTooltip()
{
    const auto m = getMouseXYRelative().toFloat();
    const int k = positionAt(m.y);
    if (k < 0)
        return {};
    const auto ins = insertAt(k);
    const juce::String name = fxInsertName(ins);
    if (switchBounds(ins).toFloat().contains(m))
        return enabled_ ? name + " for " + regionText_ + " on/off" : juce::String("Select a Loop 2 region first");
    if (handleBounds(k).toFloat().contains(m))
        return "Drag to move " + name + " in the effect chain";
    return "Show the " + name + " settings (drag to reorder)";
}

void FxList::mouseMove(const juce::MouseEvent& e)
{
    const int k = positionAt(e.position.y);
    if (k >= 0 && handleBounds(k).toFloat().contains(e.position))
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else if (k >= 0 && enabled_ && switchBounds(insertAt(k)).toFloat().contains(e.position))
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void FxList::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    pressAt(e.position);
}

void FxList::mouseDrag(const juce::MouseEvent& e) { dragTo(e.position); }

void FxList::mouseUp(const juce::MouseEvent&) { release(); }

void FxList::paintRow(juce::Graphics& g, int position, juce::Rectangle<int> r, bool lifted) const
{
    const auto ins = insertAt(position);
    const auto colour = fxInsertColour(ins);
    const bool sel = ins == selected_;
    g.setColour(lifted ? kButton.brighter(0.08f) : sel ? kButton : kWave);
    g.fillRect(r);
    g.setColour(lifted ? kAccent : sel ? kEdgeLight : kEdge);
    g.drawRect(r, 1);

    // Handle: three short lines.
    const auto h = r.withWidth(kHandleW).toFloat();
    g.setColour(lifted ? kText : kDim);
    for (int l = -1; l <= 1; ++l)
        g.fillRect(juce::Rectangle<float>(h.getCentreX() - 3.0f, h.getCentreY() + static_cast<float>(l) * 3.0f - 0.5f,
                                          6.0f, 1.0f));

    auto rest = r.withTrimmedLeft(kHandleW);
    g.setColour(colour);
    g.fillRect(rest.removeFromLeft(kMarkerW).reduced(0, 4));
    rest.removeFromLeft(5);

    const auto sw = switchBounds(ins).withY(r.getCentreY() - kSwitchH / 2);
    rest.setRight(sw.getX() - 2);
    g.setFont(mono(10.0f, sel || lifted));
    g.setColour(sel || lifted ? kText : kDim);
    g.drawFittedText(fxInsertName(ins), rest, juce::Justification::centredLeft, 1, 0.8f);

    // Switch: filled in the insert colour when on, outlined when off, greyed without a region.
    const auto s = sw.toFloat();
    const bool on = switchOn(ins);
    if (!enabled_)
    {
        g.setColour(kEdge);
        g.drawRect(s, 1.0f);
        g.setColour(kEdgeLight);
    }
    else if (on)
    {
        g.setColour(colour);
        g.fillRect(s);
        g.setColour(kLcd);
    }
    else
    {
        g.setColour(kLcd);
        g.fillRect(s);
        g.setColour(kEdgeLight);
        g.drawRect(s, 1.0f);
        g.setColour(kDim);
    }
    g.setFont(mono(9.0f, true));
    g.drawText(on ? "ON" : "OFF", sw, juce::Justification::centred, false);
}

void FxList::paint(juce::Graphics& g)
{
    const int dragged = draggedPosition();
    for (int k = 0; k < kFxInserts; ++k)
    {
        if (k == dragged)
        {
            // The gap the lifted row came from.
            const auto r = rowBounds(k);
            g.setColour(kLcd);
            g.fillRect(r);
            g.setColour(kEdge);
            g.drawRect(r, 1);
            continue;
        }
        paintRow(g, k, rowBounds(k), false);
    }
    if (dragged < 0)
        return;

    // The lifted row follows the cursor, with a shadow.
    auto lifted = rowBounds(dragged);
    const int y = juce::roundToInt(cursorY_ - grabOffset_);
    lifted.setY(std::clamp(y, 0, std::max(0, getHeight() - lifted.getHeight())));
    lifted = lifted.reduced(2, 0).translated(2, 0);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRect(lifted.translated(2, 3));
    paintRow(g, dragged, lifted, true);

    // Insertion line in the gap the row would drop into, on top so the lifted row never hides it.
    const int h = rowHeight();
    const int lineY = std::clamp(top() + slot_ * h - kRowGap / 2 - 1, 0, std::max(0, getHeight() - 2));
    g.setColour(kAccent);
    g.fillRect(0, lineY, getWidth(), 2);
    g.fillRect(0, lineY - 2, 2, 6);
    g.fillRect(getWidth() - 2, lineY - 2, 2, 6);
}

} // namespace dc::ui
