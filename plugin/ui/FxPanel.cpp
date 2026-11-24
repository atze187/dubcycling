#include "plugin/ui/FxPanel.h"
#include <algorithm>
#include <cmath>
#include "plugin/ui/FxStrip.h"
#include "plugin/ui/PanelLook.h"
#include "plugin/ui/RulePanel.h"

namespace dc::ui {

namespace {
using namespace panel;

constexpr int kListW = 130;   // effect list
constexpr int kTitleH = 16;
constexpr int kRowH = 34;     // one knob: name/value line and slider (or buttons)
constexpr int kMargin = 4;
constexpr int kColGap = 8;    // between the two knob columns
constexpr int kReverseW = 56;
constexpr int kChoiceRowH = 16; // one row of mode / shape buttons
constexpr int kChoiceGap = 1;   // between the two rows
constexpr int kChoiceH = 14 + 2 * kChoiceRowH + kChoiceGap; // name line and two button rows

juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }

juce::String paramName(FxParam p)
{
    switch (p)
    {
        case FxParam::ReverbMix: case FxParam::BitcrushMix: case FxParam::FlangerMix: case FxParam::DelayMix: return "Mix";
        case FxParam::ReverbSize: return "Size";
        case FxParam::ReverbDamping: return "Damping";
        case FxParam::FilterType: return "Type";
        case FxParam::FilterCutoff: return "Cutoff";
        case FxParam::FilterResonance: return "Resonance";
        case FxParam::BitcrushBits: return "Bits";
        case FxParam::BitcrushRate: case FxParam::FlangerRate: return "Rate";
        case FxParam::FlangerDepth: return "Depth";
        case FxParam::FlangerFeedback: case FxParam::DelayFeedback: return "Feedback";
        case FxParam::FlangerManual: return "Manual";
        case FxParam::DelayTime: return "Time";
        case FxParam::DelayReverse: return "Reverse";
        case FxParam::FilterLfoRate: return "Rate";
        case FxParam::FilterLfoDepth: return "Depth";
        case FxParam::FilterLfoShape: return "Shape";
    }
    return "";
}

// Button knobs with one button per step: Filter Type (two rows of three) and LFO Shape (two of two).
constexpr int kFilterTypes = 6;
constexpr int kLfoShapes = 4;
const char* const kTypeNames[kFilterTypes] = { "LP", "HP", "BP", "NOTCH", "PEAK", "LP24" };
const char* const kTypeTips[kFilterTypes] = { "Low-pass, 12 dB/oct", "High-pass", "Band-pass", "Notch",
                                              "Peak: boost around the cutoff", "Low-pass, 24 dB/oct" };
const char* const kShapeNames[kLfoShapes] = { "SINE", "TRI", "SAW", "SQR" };
const char* const kShapeTips[kLfoShapes] = { "Sine", "Triangle", "Saw down", "Square" };

bool isChoice(FxParam p) { return p == FxParam::FilterType || p == FxParam::FilterLfoShape; }

int choiceCount(FxParam p) { return p == FxParam::FilterType ? kFilterTypes : kLfoShapes; }

juce::String valueText(FxParam p, double v)
{
    switch (p)
    {
        case FxParam::FilterType: return kTypeNames[juce::jlimit(0, kFilterTypes - 1, juce::roundToInt(v))];
        case FxParam::FilterLfoShape: return kShapeTips[juce::jlimit(0, kLfoShapes - 1, juce::roundToInt(v))];
        case FxParam::FilterLfoRate: return delayTimeName(juce::roundToInt(v));
        case FxParam::FilterCutoff:
            return v >= 1000.0 ? juce::String(v / 1000.0, 1) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
        case FxParam::BitcrushBits: return juce::String(juce::roundToInt(v));
        case FxParam::BitcrushRate: return utf8("\xc3\x97") + juce::String(juce::roundToInt(v));
        case FxParam::FlangerRate: return juce::String(v, 2) + " Hz";
        case FxParam::DelayTime: return delayTimeName(juce::roundToInt(v));
        case FxParam::DelayReverse: return v >= 0.5 ? "On" : "Off";
        default: return juce::String(juce::roundToInt(v * 100.0)) + " %"; // signed for Feedback
    }
}

bool isWhole(FxParam p)
{
    return p == FxParam::BitcrushBits || p == FxParam::BitcrushRate || fxParamInfo(p).steps > 0;
}

double sliderInterval(FxParam p)
{
    if (isWhole(p))
        return 1.0;
    return p == FxParam::FilterCutoff ? 0.0 : 0.01;
}

// Knobs of an insert in the order of the edit view (Mix last where there is one). The Filter's
// first three go into the left column, the LFO's three into the right one.
std::vector<FxParam> editParams(FxInsert i)
{
    switch (i)
    {
        case FxInsert::Bitcrush: return { FxParam::BitcrushBits, FxParam::BitcrushRate, FxParam::BitcrushMix };
        case FxInsert::Filter:
            return { FxParam::FilterType, FxParam::FilterCutoff, FxParam::FilterResonance,
                     FxParam::FilterLfoRate, FxParam::FilterLfoDepth, FxParam::FilterLfoShape };
        case FxInsert::Flanger:
            return { FxParam::FlangerRate, FxParam::FlangerDepth, FxParam::FlangerFeedback, FxParam::FlangerManual,
                     FxParam::FlangerMix };
        case FxInsert::Delay:
            return { FxParam::DelayTime, FxParam::DelayFeedback, FxParam::DelayReverse, FxParam::DelayMix };
        case FxInsert::Reverb: return { FxParam::ReverbSize, FxParam::ReverbDamping, FxParam::ReverbMix };
    }
    return {};
}
} // namespace

struct FxPanel::Knob final
{
    FxParam p {};
    juce::Label name, value;
    juce::Slider slider;                                   // not for the choice knobs / DelayReverse
    std::array<juce::TextButton, kFilterTypes> choices;    // FilterType (6), FilterLfoShape (first 4)
    juce::TextButton toggle;                               // DelayReverse only
    bool updating = false;
    bool dimmed = false;

    void setVisible(bool v)
    {
        name.setVisible(v);
        if (isChoice(p))
        {
            for (int b = 0; b < choiceCount(p); ++b)
                choices[static_cast<std::size_t>(b)].setVisible(v);
        }
        else if (p == FxParam::DelayReverse)
        {
            toggle.setVisible(v);
        }
        else
        {
            value.setVisible(v);
            slider.setVisible(v);
        }
    }

    void clearBounds()
    {
        name.setBounds({});
        value.setBounds({});
        slider.setBounds({});
        toggle.setBounds({});
        for (auto& b : choices)
            b.setBounds({});
    }
};

FxPanel::FxPanel()
{
    listTitle_.setText("CHAIN", juce::dontSendNotification);
    listTitle_.setFont(mono(10.0f, true));
    listTitle_.setColour(juce::Label::textColourId, kDim);
    listTitle_.setBorderSize(juce::BorderSize<int>(0));
    listTitle_.setJustificationType(juce::Justification::centredLeft);
    listTitle_.setTooltip("Effect order: drag a row to move it");
    addAndMakeVisible(listTitle_);

    regionTitle_.setFont(mono(10.0f));
    regionTitle_.setColour(juce::Label::textColourId, kDim);
    regionTitle_.setBorderSize(juce::BorderSize<int>(0));
    regionTitle_.setJustificationType(juce::Justification::centredRight);
    regionTitle_.setTooltip("The on/off switches act on this Loop 2 region");
    addAndMakeVisible(regionTitle_);

    editTitle_.setFont(mono(11.0f, true));
    editTitle_.setBorderSize(juce::BorderSize<int>(0, 8, 0, 0));
    editTitle_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(editTitle_);

    lfoTitle_.setText("LFO", juce::dontSendNotification);
    lfoTitle_.setFont(mono(10.0f, true));
    lfoTitle_.setColour(juce::Label::textColourId, fxInsertColour(FxInsert::Filter));
    lfoTitle_.setBorderSize(juce::BorderSize<int>(0));
    lfoTitle_.setJustificationType(juce::Justification::centredLeft);
    lfoTitle_.setTooltip("Tempo-synced LFO on the cutoff");
    addChildComponent(lfoTitle_);

    list_.onSelected = [this](FxInsert i) { selectInsert(i); };
    list_.onSwitched = [this](FxInsert i, bool on) {
        if (!valid_)
            return;
        const auto bit = static_cast<std::uint8_t>(1u << static_cast<int>(i));
        mask_ = on ? static_cast<std::uint8_t>(mask_ | bit) : static_cast<std::uint8_t>(mask_ & ~bit);
        userSwitched(i, on);
    };
    list_.onOrderChanged = [this](const FxOrder& o) {
        order_ = o;
        if (onOrderChanged)
            onOrderChanged(o);
    };
    addAndMakeVisible(list_);

    for (int i = 0; i < kFxParamCount; ++i)
    {
        const auto p = static_cast<FxParam>(i);
        const auto& info = fxParamInfo(p);
        auto k = std::make_unique<Knob>();
        k->p = p;
        const auto colour = fxInsertColour(info.insert);

        k->name.setText(paramName(p), juce::dontSendNotification);
        k->name.setFont(mono(10.0f));
        k->name.setColour(juce::Label::textColourId, kDim);
        k->name.setBorderSize(juce::BorderSize<int>(0));
        addAndMakeVisible(k->name);

        if (isChoice(p))
        {
            const bool type = p == FxParam::FilterType;
            for (int b = 0; b < choiceCount(p); ++b)
            {
                auto& btn = k->choices[static_cast<std::size_t>(b)];
                btn.setButtonText(type ? kTypeNames[b] : kShapeNames[b]);
                btn.setTooltip(type ? kTypeTips[b] : juce::String("LFO shape: ") + kShapeTips[b]);
                btn.setClickingTogglesState(false);
                btn.setColour(juce::TextButton::buttonOnColourId, colour.darker(0.5f));
                btn.setColour(juce::TextButton::textColourOnId, kText);
                btn.onClick = [this, p, b] { setParam(p, b); };
                addAndMakeVisible(btn);
            }
        }
        else if (p == FxParam::DelayReverse)
        {
            auto& t = k->toggle;
            t.setClickingTogglesState(true);
            t.setColour(juce::TextButton::buttonOnColourId, colour.darker(0.5f));
            t.setColour(juce::TextButton::textColourOnId, kText);
            t.setTooltip("Play every echo backwards");
            t.onClick = [this, raw = k.get()] {
                if (!raw->updating)
                    setParam(FxParam::DelayReverse, raw->toggle.getToggleState() ? 1.0 : 0.0);
            };
            addAndMakeVisible(t);
        }
        else
        {
            k->value.setFont(mono(10.0f));
            k->value.setJustificationType(juce::Justification::centredRight);
            k->value.setColour(juce::Label::textColourId, kAmber);
            k->value.setBorderSize(juce::BorderSize<int>(0));
            addAndMakeVisible(k->value);

            auto& s = k->slider;
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
            s.setRange(info.min, info.max, sliderInterval(p));
            if (info.logarithmic)
                s.setSkewFactorFromMidPoint(std::sqrt(info.min * info.max));
            s.setDoubleClickReturnValue(true, info.def);
            s.setColour(juce::Slider::trackColourId, colour);
            s.setColour(juce::Slider::thumbColourId, kText);
            s.setColour(juce::Slider::backgroundColourId, kEdge);
            if (p == FxParam::FlangerManual)
                s.setTooltip("Fixed sweep position, used when Rate is 0");
            auto* raw = k.get();
            s.onDragStart = [this, p] { beginDrag(p); };
            s.onValueChange = [this, raw, p] {
                if (raw->updating)
                    return;
                apply(p, raw->slider.getValue());
                if (!dragging_ && dirty_) // wheel / keyboard / double-click: no drag around it
                    publish();
            };
            s.onDragEnd = [this] { endDrag(); };
            addAndMakeVisible(s);
        }
        knobs_[static_cast<std::size_t>(i)] = std::move(k);
    }
    for (int i = 0; i < kFxParamCount; ++i)
        show(static_cast<FxParam>(i));
    setContext(0, 0, {}, FxSettings {});
    selectionSet_ = false; // the editor's first setContext picks the initial selection
    updateVisibility();
}

FxPanel::~FxPanel() = default;

juce::Button& FxPanel::reverseButton() { return knob(FxParam::DelayReverse).toggle; }

juce::Button& FxPanel::modeButton(int i)
{
    return knob(FxParam::FilterType).choices[static_cast<std::size_t>(juce::jlimit(0, kFilterTypes - 1, i))];
}

juce::Button& FxPanel::shapeButton(int i)
{
    return knob(FxParam::FilterLfoShape).choices[static_cast<std::size_t>(juce::jlimit(0, kLfoShapes - 1, i))];
}

juce::Slider& FxPanel::slider(FxParam p) { return knob(p).slider; }

void FxPanel::show(FxParam p)
{
    auto& k = knob(p);
    const double v = param(p);
    if (isChoice(p))
    {
        for (int b = 0; b < choiceCount(p); ++b)
            k.choices[static_cast<std::size_t>(b)].setToggleState(juce::roundToInt(v) == b, juce::dontSendNotification);
        return;
    }
    if (p == FxParam::DelayReverse)
    {
        k.updating = true;
        k.toggle.setToggleState(v >= 0.5, juce::dontSendNotification);
        k.toggle.setButtonText(v >= 0.5 ? "ON" : "OFF");
        k.updating = false;
        return;
    }
    k.updating = true;
    k.slider.setValue(v, juce::dontSendNotification);
    k.updating = false;
    k.value.setText(valueText(p, v), juce::dontSendNotification);
    if (p == FxParam::FlangerRate)
        updateDimming();
}

void FxPanel::updateDimming()
{
    auto& k = knob(FxParam::FlangerManual);
    k.dimmed = params_.get(FxParam::FlangerRate) > 0.0f;
    k.name.setColour(juce::Label::textColourId, k.dimmed ? kEdgeLight : kDim);
    k.value.setColour(juce::Label::textColourId, k.dimmed ? kAmber.withAlpha(0.4f) : kAmber);
    k.slider.setAlpha(k.dimmed ? 0.45f : 1.0f); // still editable
}

bool FxPanel::paramDimmed(FxParam p) const { return knob(p).dimmed; }

void FxPanel::apply(FxParam p, double value)
{
    double v = sanitizeFxParam(p, value);
    if (isWhole(p))
        v = std::round(v);
    if (static_cast<float>(v) != params_.get(p))
    {
        params_.set(p, v);
        dirty_ = true;
    }
    show(p); // also pulls a clamped slider back
}

void FxPanel::publish()
{
    dirty_ = false;
    if (onParamsChanged)
        onParamsChanged(params_);
}

void FxPanel::setParam(FxParam p, double value)
{
    apply(p, value);
    if (dirty_)
        publish();
}

void FxPanel::resetParam(FxParam p) { setParam(p, fxParamInfo(p).def); }

void FxPanel::beginDrag(FxParam) { dragging_ = true; }

void FxPanel::dragParam(FxParam p, double value) { apply(p, value); }

void FxPanel::endDrag()
{
    dragging_ = false;
    if (dirty_)
        publish();
}

juce::String FxPanel::paramText(FxParam p) const { return valueText(p, param(p)); }

bool FxPanel::switchEnabled(FxInsert) const { return valid_; }

bool FxPanel::switchOn(FxInsert i) const { return list_.switchOn(i); }

juce::String FxPanel::switchText(FxInsert) const { return switchText_; }

void FxPanel::userSwitched(FxInsert i, bool on)
{
    if (!valid_)
        return;
    if (onSwitchChanged)
        onSwitchChanged(region2_, i, on);
}

void FxPanel::setSwitch(FxInsert i, bool on)
{
    if (!valid_)
        return;
    const auto bit = static_cast<std::uint8_t>(1u << static_cast<int>(i));
    mask_ = on ? static_cast<std::uint8_t>(mask_ | bit) : static_cast<std::uint8_t>(mask_ & ~bit);
    updateList();
    userSwitched(i, on);
}

void FxPanel::updateList() { list_.setContext(order_, static_cast<int>(selected_), mask_, valid_); }

void FxPanel::selectInsert(FxInsert i)
{
    selectionSet_ = true;
    const bool changed = i != selected_;
    selected_ = i;
    list_.select(i);
    if (changed)
        updateVisibility();
}

void FxPanel::updateVisibility()
{
    for (int i = 0; i < kFxParamCount; ++i)
    {
        auto& k = *knobs_[static_cast<std::size_t>(i)];
        k.setVisible(fxParamInfo(k.p).insert == selected_);
    }
    lfoTitle_.setVisible(selected_ == FxInsert::Filter);
    editTitle_.setText(fxInsertName(selected_), juce::dontSendNotification);
    editTitle_.setColour(juce::Label::textColourId, fxInsertColour(selected_));
    resized();
    repaint();
}

void FxPanel::setContext(int region2, int regionCount, const std::vector<juce::String>& labels, const FxSettings& fx)
{
    region2_ = region2;
    regionCount_ = std::max(regionCount, 0);
    valid_ = region2_ >= 0 && region2_ < regionCount_;
    mask_ = valid_ ? fx.maskFor(region2_) : 0;
    const auto label = valid_ ? regionLabel(labels, region2_) : juce::String();
    switchText_ = valid_ ? "Region " + label : juce::String("no region");
    regionTitle_.setText(valid_ ? "REGION " + label : juce::String("NO REGION"), juce::dontSendNotification);
    regionTitle_.setColour(juce::Label::textColourId, valid_ ? kText : kDim);
    list_.setRegionText(valid_ ? "region " + label : juce::String());
    if (!list_.isDragging())
        order_ = sanitizeFxOrder(fx.order);
    if (!selectionSet_)
    {
        selectionSet_ = true;
        selected_ = order_[0];
        updateVisibility();
    }
    updateList();
    if (dragging_)
        return; // the editor's poll must not pull the slider out of the user's hand
    params_ = fx.params;
    dirty_ = false;
    for (int i = 0; i < kFxParamCount; ++i)
        show(static_cast<FxParam>(i));
}

juce::Rectangle<int> FxPanel::switchBounds(FxInsert i) const
{
    return list_.switchBounds(i) + list_.getPosition();
}

juce::Rectangle<int> FxPanel::paramBounds(FxParam p) const
{
    const auto& k = knob(p);
    if (!k.name.isVisible())
        return {};
    if (isChoice(p))
    {
        auto r = k.name.getBounds();
        for (int b = 0; b < choiceCount(p); ++b)
            r = r.getUnion(k.choices[static_cast<std::size_t>(b)].getBounds());
        return r;
    }
    if (p == FxParam::DelayReverse)
        return k.name.getBounds().getUnion(k.toggle.getBounds());
    return k.name.getBounds().getUnion(k.value.getBounds()).getUnion(k.slider.getBounds());
}

void FxPanel::paint(juce::Graphics& g)
{
    g.fillAll(kStrip);
    g.setColour(kEdge);
    g.drawRect(getLocalBounds(), 1);
    // Separator between list and edit view.
    const int x = list_.getRight() + kMargin;
    g.drawVerticalLine(x, static_cast<float>(kMargin), static_cast<float>(getHeight() - kMargin));
    // Colour bar in front of the edit view's title.
    const auto t = editTitle_.getBounds();
    g.setColour(fxInsertColour(selected_));
    g.fillRect(t.getX(), t.getY() + 3, 3, t.getHeight() - 6);
    g.setColour(kEdge);
    g.drawHorizontalLine(t.getBottom() + 1, static_cast<float>(t.getX()), static_cast<float>(t.getRight()));
    // The LFO caption runs out in a thin line over its column.
    if (lfoTitle_.isVisible() && !lfoTitle_.getBounds().isEmpty())
    {
        const auto c = lfoTitle_.getBounds();
        const int textW = juce::GlyphArrangement::getStringWidthInt(lfoTitle_.getFont(), lfoTitle_.getText());
        g.drawHorizontalLine(c.getCentreY(), static_cast<float>(c.getX() + textW + 6), static_cast<float>(c.getRight()));
    }
}

void FxPanel::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    auto left = area.removeFromLeft(std::min(kListW, area.getWidth()));
    auto head = left.removeFromTop(kTitleH);
    listTitle_.setBounds(head.removeFromLeft(40));
    regionTitle_.setBounds(head);
    left.removeFromTop(2);
    list_.setBounds(left);
    area.removeFromLeft(2 * kMargin + 1); // separator in the middle
    editTitle_.setBounds(area.removeFromTop(kTitleH));
    area.removeFromTop(4);

    for (auto& k : knobs_)
        k->clearBounds();

    lfoTitle_.setBounds({});
    if (selected_ == FxInsert::Filter)
    {
        layoutFilter(area);
        return;
    }

    // One column while the knobs fit under each other, else two (Flanger at the minimum height).
    const auto params = editParams(selected_);
    const int n = static_cast<int>(params.size());
    const int maxRows = std::max(1, area.getHeight() / kRowH);
    const int cols = n > maxRows ? 2 : 1;
    const int rows = (n + cols - 1) / cols;
    const int colW = (area.getWidth() - (cols - 1) * kColGap) / cols;
    for (int idx = 0; idx < n; ++idx)
    {
        const int c = idx / rows, r = idx % rows;
        layoutKnob(params[static_cast<std::size_t>(idx)],
                   { area.getX() + c * (colW + kColGap), area.getY() + r * kRowH, colW, kRowH - 2 });
    }
}

// Two columns: on the left the mode (two rows of buttons), Cutoff and Resonance; on the right, under
// the "LFO" caption, Rate, Depth and the shape (two rows of buttons). The left column is a little wider
// for "NOTCH"; Depth lines up with Cutoff, Shape with Resonance.
void FxPanel::layoutFilter(juce::Rectangle<int> area)
{
    const int leftW = (area.getWidth() - kColGap) * 11 / 20;
    auto left = area.withWidth(leftW);
    auto right = area.withTrimmedLeft(leftW + kColGap);
    const int blockH = kChoiceH + 3; // the mode block, and the caption plus Rate beside it

    layoutKnob(FxParam::FilterType, left.withHeight(kChoiceH));
    layoutKnob(FxParam::FilterCutoff, { left.getX(), left.getY() + blockH, leftW, kRowH - 2 });
    layoutKnob(FxParam::FilterResonance, { left.getX(), left.getY() + blockH + kRowH, leftW, kRowH - 2 });

    lfoTitle_.setBounds(right.withHeight(14));
    layoutKnob(FxParam::FilterLfoRate, { right.getX(), right.getY() + blockH - kRowH, right.getWidth(), kRowH - 2 });
    layoutKnob(FxParam::FilterLfoDepth, { right.getX(), right.getY() + blockH, right.getWidth(), kRowH - 2 });
    layoutKnob(FxParam::FilterLfoShape,
               { right.getX(), right.getY() + blockH + kRowH, right.getWidth(), kChoiceH });
}

// One knob in `row`: the name line (14 px) and below it the slider, the toggle or the button rows.
void FxPanel::layoutKnob(FxParam p, juce::Rectangle<int> row)
{
    auto& k = knob(p);
    auto line = row.removeFromTop(14);
    if (isChoice(p))
    {
        k.name.setBounds(line);
        const int n = choiceCount(p);
        const int perRow = (n + 1) / 2;
        for (int r = 0; r < 2; ++r)
        {
            row.removeFromTop(r == 0 ? 0 : kChoiceGap);
            auto bt = row.removeFromTop(kChoiceRowH);
            const int bw = bt.getWidth() / perRow;
            for (int c = 0; c < perRow && r * perRow + c < n; ++c)
                k.choices[static_cast<std::size_t>(r * perRow + c)].setBounds(bt.removeFromLeft(bw).reduced(1, 0));
        }
        return;
    }
    if (p == FxParam::DelayReverse)
    {
        k.name.setBounds(line);
        k.toggle.setBounds(row.removeFromTop(18).removeFromLeft(std::min(kReverseW, row.getWidth())));
        return;
    }
    // The name gets the width of its text, the value the rest, right-aligned.
    const int nameW = std::min(line.getWidth(),
                               juce::GlyphArrangement::getStringWidthInt(k.name.getFont(), k.name.getText()) + 3);
    k.name.setBounds(line.removeFromLeft(nameW));
    k.value.setBounds(line);
    k.slider.setBounds(row.removeFromTop(14));
}

} // namespace dc::ui
