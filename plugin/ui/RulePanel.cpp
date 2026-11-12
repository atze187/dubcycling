#include "plugin/ui/RulePanel.h"
#include <algorithm>
#include <cmath>
#include "engine/MixGain.h"
#include "plugin/PluginState.h"
#include "plugin/ui/PanelLook.h"

namespace dc::ui {

namespace {
constexpr float kEps = 0.0005f;

using namespace panel;

const juce::Colour kOutline { 0xff2a2e35 };
const juce::Colour kOneShot { 0xffd69a3c };

juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }

// Percent with 0.01 resolution: removes float noise like 70.00000095 from weight * 100.
float snap(float percent)
{
    if (!std::isfinite(percent))
        return 0.0f;
    return std::round(std::clamp(percent, 0.0f, 100.0f) * 100.0f) / 100.0f;
}

float toPercent(float weight)
{
    return std::isfinite(weight) ? snap(weight * 100.0f) : 0.0f;
}

juce::String percentString(float percent) { return juce::String(juce::roundToInt(percent)) + " %"; }

// Readout of a mix value: the level of the quieter loop, both at full level in the centre.
juce::String mixString(float mix)
{
    const auto g = mixGains(mix);
    const int l1 = juce::roundToInt(g.loop1 * 100.0f);
    const int l2 = juce::roundToInt(g.loop2 * 100.0f);
    if (l2 <= 0)
        return "L1 only";
    if (l1 <= 0)
        return "L2 only";
    if (l2 < 100)
        return "L2 " + juce::String(l2) + " %";
    if (l1 < 100)
        return "L1 " + juce::String(l1) + " %";
    return "L1 | L2";
}

// Columns of a rule row (row bounds without the 2 px top/bottom inset):
//   name | weight slider | percent | GATE/ONE REV | mix slider | mix readout
// The two sliders share what the fixed columns leave, the mix slider gets two fifths of it.
constexpr int kNameW = 60;
constexpr int kPercentW = 34;
constexpr int kModeW = 66;
constexpr int kRevW = 28;
constexpr int kTight = 2; // mode switch to REV (one control group)
constexpr int kMixValueW = 46;
constexpr int kNear = 3;  // slider to its readout
constexpr int kApart = 6; // between column groups

struct RowColumns
{
    juce::Rectangle<int> name, weight, percent, mode, reverse, mix, mixValue;
};

RowColumns rowColumns(juce::Rectangle<int> r)
{
    RowColumns c;
    c.name = r.removeFromLeft(kNameW);
    const int flex = std::max(0, r.getWidth()
                                     - (kNear + kPercentW + kApart + kModeW + kTight + kRevW + kApart + kNear + kMixValueW));
    const int mixW = std::clamp(flex * 2 / 5, 36, 96);
    c.mixValue = r.removeFromRight(kMixValueW);
    r.removeFromRight(kNear);
    c.mix = r.removeFromRight(mixW);
    r.removeFromRight(kApart);
    c.reverse = r.removeFromRight(kRevW);
    r.removeFromRight(kTight);
    c.mode = r.removeFromRight(kModeW);
    r.removeFromRight(kApart);
    c.percent = r.removeFromRight(kPercentW);
    r.removeFromRight(kNear);
    c.weight = r;
    return c;
}

// Two-position switch: GATE | ONE, the active side lit. A click anywhere flips it.
struct ModeSwitch final : public juce::Component, public juce::SettableTooltipClient
{
    ModeSwitch()
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("Gate: plays as long as the Loop 1 region. One-Shot: once, then ends.");
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        const auto cell = [&](juce::Rectangle<float> area, const char* text, bool active, juce::Colour on,
                              juce::Colour onText) {
            g.setColour(active ? on : kWindow);
            g.fillRect(area);
            g.setColour(active ? onText : kDim);
            g.setFont(mono(10.0f));
            g.drawFittedText(text, area.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        };
        auto left = r.removeFromLeft(r.getWidth() * 0.5f);
        cell(left, "GATE", mode == PlayMode::Gate, kEdgeLight, kText);
        cell(r, "ONE", mode == PlayMode::OneShot, juce::Colour(0xff6b4a14), juce::Colour(0xffffd9a0));
        g.setColour(kEdgeLight);
        g.drawRect(getLocalBounds().toFloat().reduced(0.5f), 1.0f);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains(e.getPosition()) && onClick)
            onClick();
    }

    PlayMode mode = PlayMode::Gate;
    std::function<void()> onClick;
};

// On/off switch REV in the style of the mode switch's cells: lit while the row plays backwards.
// The toggle state only shows the row's flag; a click asks the panel to flip it (onClick).
struct ReverseButton final : public juce::Button
{
    ReverseButton() : juce::Button("REV")
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("Play this Loop 2 region backwards");
        setClickingTogglesState(false);
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const bool on = getToggleState();
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        auto fill = on ? juce::Colour(0xff1d3d57) : kWindow;
        if (highlighted || down)
            fill = fill.brighter(0.15f);
        g.setColour(fill);
        g.fillRect(r);
        g.setColour(on ? kAccent : kDim);
        g.setFont(mono(10.0f));
        g.drawFittedText(getButtonText(), getLocalBounds(), juce::Justification::centred, 1, 0.8f);
        g.setColour(on ? kAccent.withAlpha(0.7f) : kEdgeLight);
        g.drawRect(r, 1.0f);
    }
};

bool same(const std::vector<RuleTarget>& a, const std::vector<RuleTarget>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].region2 != b[i].region2 || a[i].weight != b[i].weight || a[i].mode != b[i].mode
            || sanitizeMix(a[i].mix) != sanitizeMix(b[i].mix) || a[i].reverse != b[i].reverse)
            return false;
    return true;
}
} // namespace

juce::String regionLetters(int index)
{
    juce::String s;
    for (int n = std::max(index, 0) + 1; n > 0; n = (n - 1) / 26)
        s = juce::String::charToString(static_cast<juce::juce_wchar>('A' + (n - 1) % 26)) + s;
    return s;
}

juce::String regionLabel(const std::vector<juce::String>& labels, int index)
{
    if (index >= 0 && static_cast<std::size_t>(index) < labels.size() && labels[static_cast<std::size_t>(index)].isNotEmpty())
        return labels[static_cast<std::size_t>(index)];
    return regionLetters(index);
}

struct RulePanel::Row final : public juce::Component
{
    Row(RulePanel& owner, int index) : owner_(owner), index_(index)
    {
        setComponentID("row" + juce::String(index));
        letter.setComponentID("name");
        letter.setText(regionLabel(owner.labels2_, index), juce::dontSendNotification);
        letter.setTooltip("Loop 2, Region " + juce::String(index + 1) + utf8(" \xe2\x80\x93 double-click to rename"));
        letter.setColour(juce::Label::textColourId, kText);
        letter.setColour(juce::Label::textWhenEditingColourId, kAmber);
        letter.setColour(juce::Label::backgroundWhenEditingColourId, kLcd);
        letter.setFont(mono(12.0f, true));
        letter.setMinimumHorizontalScale(0.8f);
        letter.setEditable(false, true, false);
        letter.onEditorShow = [this] {
            if (auto* ed = letter.getCurrentTextEditor())
                ed->setInputRestrictions(kMaxLabelLength);
        };
        letter.onTextChange = [this] {
            const auto text = sanitizeLabel(letter.getText());
            letter.setText(text.isNotEmpty() ? text : regionLetters(index_), juce::dontSendNotification);
            if (owner_.onLabelEdited)
                owner_.onLabelEdited(index_, text);
        };
        addAndMakeVisible(letter);

        slider.setComponentID("weight");
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        slider.setRange(0.0, 100.0, 1.0);
        slider.setColour(juce::Slider::trackColourId, kAccent);
        slider.setColour(juce::Slider::thumbColourId, kText);
        slider.setColour(juce::Slider::backgroundColourId, kOutline);
        slider.onDragStart = [this] { owner_.beginDrag(index_); };
        slider.onValueChange = [this] {
            if (updating)
                return;
            owner_.applyWeight(index_, static_cast<float>(slider.getValue()));
            if (owner_.dragRow_ < 0 && owner_.dirty_) // wheel / keyboard: no drag around it
                owner_.publish();
        };
        slider.onDragEnd = [this] { owner_.endDrag(); };
        addAndMakeVisible(slider);

        value.setComponentID("percent");
        value.setJustificationType(juce::Justification::centredRight);
        value.setBorderSize(juce::BorderSize<int>(0, 1, 0, 1));
        value.setMinimumHorizontalScale(0.8f);
        value.setColour(juce::Label::textColourId, kText);
        value.setFont(mono(11.0f));
        addAndMakeVisible(value);

        toggle.setComponentID("mode");
        toggle.onClick = [this] {
            owner_.setMode(index_, mode == PlayMode::Gate ? PlayMode::OneShot : PlayMode::Gate);
        };
        addAndMakeVisible(toggle);

        reverseButton.setComponentID("reverse");
        reverseButton.onClick = [this] { owner_.setReverse(index_, !reverse); };
        addAndMakeVisible(reverseButton);

        mixSlider.setComponentID("mix");
        mixSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        mixSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        mixSlider.setRange(0.0, 1.0, 0.01);
        mixSlider.setDoubleClickReturnValue(true, 0.5);
        mixSlider.getProperties().set(PanelLook::kBipolar, true);
        mixSlider.setColour(juce::Slider::trackColourId, kGreen);
        mixSlider.setColour(juce::Slider::thumbColourId, kText);
        mixSlider.setColour(juce::Slider::backgroundColourId, kOutline);
        mixSlider.setTooltip("Mix Loop 1 / Loop 2: left lowers Loop 2, right lowers Loop 1. Double-click: centre");
        mixSlider.onDragStart = [this] { owner_.beginMixDrag(index_); };
        mixSlider.onValueChange = [this] {
            if (updating)
                return;
            owner_.applyMix(index_, static_cast<float>(mixSlider.getValue()));
            if (owner_.dragRow_ < 0 && owner_.dirty_) // wheel / keyboard: no drag around it
                owner_.publish();
        };
        mixSlider.onDragEnd = [this] { owner_.endMixDrag(); };
        addAndMakeVisible(mixSlider);

        mixValue.setComponentID("mixValue");
        mixValue.setJustificationType(juce::Justification::centredLeft);
        mixValue.setBorderSize(juce::BorderSize<int>(0, 1, 0, 1));
        mixValue.setMinimumHorizontalScale(0.8f);
        mixValue.setColour(juce::Label::textColourId, kText);
        mixValue.setFont(mono(10.0f));
        addAndMakeVisible(mixValue);
        show();
    }

    // Brings sliders, readouts and toggle in line with percent / mode / mix.
    void show()
    {
        updating = true;
        slider.setValue(static_cast<double>(percent), juce::dontSendNotification);
        mixSlider.setValue(static_cast<double>(mix), juce::dontSendNotification);
        updating = false;
        value.setText(percentString(percent), juce::dontSendNotification);
        mixValue.setText(mixString(mix), juce::dontSendNotification);
        toggle.mode = mode;
        toggle.repaint();
        reverseButton.setToggleState(reverse, juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        if (index_ > 0)
        {
            g.setColour(juce::Colour(0xff20242a));
            g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
        }
    }

    void resized() override
    {
        const auto c = rowColumns(getLocalBounds().reduced(0, 2));
        letter.setBounds(c.name);
        slider.setBounds(c.weight);
        value.setBounds(c.percent);
        toggle.setBounds(c.mode);
        reverseButton.setBounds(c.reverse);
        mixSlider.setBounds(c.mix);
        mixValue.setBounds(c.mixValue);
    }

    RulePanel& owner_;
    int index_;
    juce::Label letter, value, mixValue;
    juce::Slider slider, mixSlider;
    ModeSwitch toggle;
    ReverseButton reverseButton;
    float percent = 0.0f;
    PlayMode mode = PlayMode::Gate;
    float mix = 0.5f;
    bool reverse = false;
    bool touched = false; // edited since the last setContext/publish and not yet in stored_
    bool updating = false;
};

RulePanel::RulePanel()
{
    title_.setFont(mono(11.0f, true));
    title_.setColour(juce::Label::textColourId, kText);
    addAndMakeVisible(title_);

    styleLcd(silence_, kAmber);
    staleHint_.setFont(mono(11.0f));
    staleHint_.setColour(juce::Label::textColourId, kOneShot);
    staleHint_.setJustificationType(juce::Justification::centredRight);
    addChildComponent(staleHint_);
    staleReset_.setButtonText("Reset");
    staleReset_.setTooltip("Removes the rules on regions that no longer exist");
    staleReset_.onClick = [this] { resetStale(); };
    addChildComponent(staleReset_);
    addAndMakeVisible(silence_);

    viewport_.setViewedComponent(&holder_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(8);
    addAndMakeVisible(viewport_);
    setContext(0, 0, {});
}

RulePanel::~RulePanel()
{
    viewport_.setViewedComponent(nullptr, false);
}

juce::String RulePanel::rowLabel(int row) const
{
    return row >= 0 && row < rowCount() ? regionLabel(labels2_, row) : juce::String();
}

void RulePanel::setLabels(const std::vector<juce::String>& loop1Labels, const std::vector<juce::String>& loop2Labels)
{
    if (loop1Labels == labels1_ && loop2Labels == labels2_)
        return;
    labels1_ = loop1Labels;
    labels2_ = loop2Labels;
    for (int i = 0; i < rowCount(); ++i)
    {
        auto& letter = rows_[static_cast<std::size_t>(i)]->letter;
        if (!letter.isBeingEdited())
            letter.setText(regionLabel(labels2_, i), juce::dontSendNotification);
    }
    updateTitle();
}

void RulePanel::updateTitle()
{
    title_.setText(utf8("Rules for Loop 1 \xe2\x80\x93 ") + regionLabel(labels1_, region1_),
                   juce::dontSendNotification);
}

float RulePanel::weightPercent(int row) const
{
    return row >= 0 && row < rowCount() ? rows_[static_cast<std::size_t>(row)]->percent : 0.0f;
}

PlayMode RulePanel::mode(int row) const
{
    return row >= 0 && row < rowCount() ? rows_[static_cast<std::size_t>(row)]->mode : PlayMode::Gate;
}

bool RulePanel::reverse(int row) const
{
    return row >= 0 && row < rowCount() && rows_[static_cast<std::size_t>(row)]->reverse;
}

juce::String RulePanel::percentText(int row) const
{
    return row >= 0 && row < rowCount() ? rows_[static_cast<std::size_t>(row)]->value.getText() : juce::String();
}

float RulePanel::mix(int row) const
{
    return row >= 0 && row < rowCount() ? rows_[static_cast<std::size_t>(row)]->mix : 0.5f;
}

juce::String RulePanel::mixText(int row) const
{
    return row >= 0 && row < rowCount() ? rows_[static_cast<std::size_t>(row)]->mixValue.getText() : juce::String();
}

void RulePanel::rebuildRows(int count)
{
    rows_.clear();
    holder_.removeAllChildren();
    for (int i = 0; i < count; ++i)
    {
        rows_.push_back(std::make_unique<Row>(*this, i));
        holder_.addAndMakeVisible(*rows_.back());
    }
    resized();
}

void RulePanel::setContext(int region1, int region2Count, const std::vector<RuleTarget>& targets)
{
    if (dragRow_ >= 0)
        return; // the editor's poll must not pull the slider out of the user's hand
    region1 = std::max(region1, 0);
    region2Count = std::max(region2Count, 0);
    const bool sameCount = region2Count == rowCount();
    if (sameCount && region1 == region1_ && same(targets, stored_) && !dirty_)
    {
        bool touched = false;
        for (const auto& r : rows_)
            touched = touched || r->touched;
        if (!touched)
            return;
    }

    region1_ = region1;
    stored_ = targets;
    dirty_ = false;
    if (!sameCount)
        rebuildRows(region2Count);
    for (int i = 0; i < rowCount(); ++i)
    {
        auto& row = *rows_[static_cast<std::size_t>(i)];
        row.percent = 0.0f;
        row.mode = PlayMode::Gate;
        row.mix = 0.5f;
        row.reverse = false;
        row.touched = false;
        for (const auto& t : stored_)
            if (t.region2 == i)
            {
                row.percent = toPercent(t.weight);
                row.mode = t.mode;
                row.mix = sanitizeMix(t.mix);
                row.reverse = t.reverse;
                break; // duplicates beyond the first count as "other" targets
            }
        row.show();
    }
    updateTitle();
    updateSilence();
}

float RulePanel::otherPercent(int row) const
{
    float sum = stalePercent();
    for (int i = 0; i < rowCount(); ++i)
        if (i != row)
            sum += rows_[static_cast<std::size_t>(i)]->percent;
    return sum;
}

// Stored targets that no row shows: stale regions and duplicates of a shown region.
float RulePanel::stalePercent() const
{
    float sum = 0.0f;
    std::vector<bool> seen(static_cast<std::size_t>(rowCount()), false);
    for (const auto& t : stored_)
    {
        const bool shown = t.region2 >= 0 && t.region2 < rowCount() && !seen[static_cast<std::size_t>(t.region2)];
        if (shown)
            seen[static_cast<std::size_t>(t.region2)] = true;
        else
            sum += toPercent(t.weight);
    }
    return sum;
}

void RulePanel::resetStale()
{
    if (dragRow_ >= 0)
        return;
    std::vector<RuleTarget> kept;
    std::vector<bool> seen(static_cast<std::size_t>(rowCount()), false);
    for (const auto& t : stored_)
        if (t.region2 >= 0 && t.region2 < rowCount() && !seen[static_cast<std::size_t>(t.region2)])
        {
            seen[static_cast<std::size_t>(t.region2)] = true;
            kept.push_back(t);
        }
    if (kept.size() == stored_.size())
        return;
    stored_ = std::move(kept);
    publish();
    updateSilence();
}

void RulePanel::updateSilence()
{
    const float rest = std::max(0.0f, snap(100.0f - otherPercent(-1)));
    silence_.setText("Silence: " + juce::String(juce::roundToInt(rest)) + " %", juce::dontSendNotification);
    bool stale = false;
    std::vector<bool> seen(static_cast<std::size_t>(rowCount()), false);
    for (const auto& t : stored_)
    {
        const bool shown = t.region2 >= 0 && t.region2 < rowCount() && !seen[static_cast<std::size_t>(t.region2)];
        if (shown)
            seen[static_cast<std::size_t>(t.region2)] = true;
        else
            stale = true;
    }
    staleHint_.setText(juce::String(juce::roundToInt(stalePercent())) + " % of it on removed regions",
                       juce::dontSendNotification);
    if (stale != staleHint_.isVisible())
    {
        staleHint_.setVisible(stale);
        staleReset_.setVisible(stale);
        resized();
    }
}

void RulePanel::applyWeight(int row, float percent)
{
    if (row < 0 || row >= rowCount())
        return;
    auto& r = *rows_[static_cast<std::size_t>(row)];
    float p = snap(percent); // NaN -> 0, clamped to 0..100
    p = std::min(p, std::max(0.0f, snap(100.0f - otherPercent(row))));
    if (std::abs(p - r.percent) > kEps)
    {
        r.percent = p;
        r.touched = true;
        dirty_ = true;
    }
    r.show(); // also pulls a capped slider back to the cap
    updateSilence();
}

void RulePanel::setWeight(int row, float percent)
{
    applyWeight(row, percent);
    if (dirty_)
        publish();
}

void RulePanel::setMode(int row, PlayMode newMode)
{
    if (row < 0 || row >= rowCount())
        return;
    auto& r = *rows_[static_cast<std::size_t>(row)];
    if (r.mode == newMode)
        return;
    r.mode = newMode;
    r.touched = true;
    dirty_ = true;
    r.show();
    publish();
}

void RulePanel::setReverse(int row, bool on)
{
    if (row < 0 || row >= rowCount())
        return;
    auto& r = *rows_[static_cast<std::size_t>(row)];
    if (r.reverse == on)
        return;
    r.reverse = on;
    r.touched = true;
    dirty_ = true;
    r.show();
    publish();
}

void RulePanel::applyMix(int row, float newMix)
{
    if (row < 0 || row >= rowCount())
        return;
    auto& r = *rows_[static_cast<std::size_t>(row)];
    const float m = sanitizeMix(newMix);
    if (m != r.mix)
    {
        r.mix = m;
        r.touched = true;
        dirty_ = true;
    }
    r.show();
}

void RulePanel::setMix(int row, float newMix)
{
    applyMix(row, newMix);
    if (dirty_)
        publish();
}

void RulePanel::beginMixDrag(int row) { beginDrag(row); }

void RulePanel::dragMix(int row, float newMix)
{
    applyMix(row, newMix); // local only, as dragWeight
}

void RulePanel::endMixDrag() { endDrag(); }

void RulePanel::beginDrag(int row)
{
    if (row >= 0 && row < rowCount())
        dragRow_ = row;
}

void RulePanel::dragWeight(int row, float percent)
{
    applyWeight(row, percent); // local only: labels and slider follow, nothing is published
}

void RulePanel::endDrag()
{
    dragRow_ = -1;
    if (dirty_)
        publish();
}

std::vector<RuleTarget> RulePanel::buildTargets() const
{
    std::vector<RuleTarget> out;
    std::vector<bool> seen(static_cast<std::size_t>(rowCount()), false);
    for (const auto& t : stored_)
    {
        RuleTarget n = t;
        if (t.region2 >= 0 && t.region2 < rowCount() && !seen[static_cast<std::size_t>(t.region2)])
        {
            seen[static_cast<std::size_t>(t.region2)] = true;
            const auto& r = *rows_[static_cast<std::size_t>(t.region2)];
            if (r.touched)
            {
                n.weight = r.percent / 100.0f;
                n.mode = r.mode;
                n.mix = r.mix;
                n.reverse = r.reverse;
            }
        }
        out.push_back(n); // stale and duplicate entries pass through untouched
    }
    for (int i = 0; i < rowCount(); ++i)
    {
        const auto& r = *rows_[static_cast<std::size_t>(i)];
        if (!seen[static_cast<std::size_t>(i)] && r.touched)
            out.push_back(RuleTarget { i, r.percent / 100.0f, r.mode, r.mix, r.reverse });
    }
    return out;
}

void RulePanel::publish()
{
    dirty_ = false;
    auto targets = buildTargets();
    stored_ = targets;
    for (auto& r : rows_)
        r->touched = false;
    if (onChanged)
        onChanged(region1_, std::move(targets));
}

void RulePanel::paint(juce::Graphics& g)
{
    g.fillAll(kStrip);
    g.setColour(kOutline);
    g.drawRect(getLocalBounds(), 1);
    g.drawHorizontalLine(kHeaderH + 3, 1.0f, static_cast<float>(getWidth() - 1));
    if (rowCount() == 0)
        return;
    // Column captions, aligned with the rows' columns.
    const auto v = viewport_.getBounds();
    const auto c = rowColumns(juce::Rectangle<int>(v.getX(), 0, holder_.getWidth(), kRowH).reduced(0, 2));
    const int y = v.getY() - kCaptionH;
    g.setColour(kDim);
    g.setFont(mono(9.0f));
    const auto caption = [&](juce::Rectangle<int> col, const char* text) {
        g.drawFittedText(text, col.withY(y).withHeight(kCaptionH), juce::Justification::centredLeft, 1, 0.8f);
    };
    caption(c.weight, "WEIGHT");
    caption(c.mode, "MODE");
    caption(c.mix, "MIX");
}

void RulePanel::resized()
{
    auto r = getLocalBounds().reduced(6, 3);
    auto top = r.removeFromTop(kHeaderH);
    silence_.setBounds(top.removeFromRight(112).reduced(0, 2));
    title_.setBounds(top);
    r.removeFromTop(3);
    if (staleHint_.isVisible())
    {
        auto line = r.removeFromTop(22);
        staleReset_.setBounds(line.removeFromRight(110).reduced(0, 1));
        line.removeFromRight(8);
        staleHint_.setBounds(line);
    }
    r.removeFromTop(2);
    if (rowCount() > 0)
        r.removeFromTop(kCaptionH);
    viewport_.setBounds(r);
    // The vertical scrollbar takes its width from the rows when they do not fit; reserve it up front.
    const bool scrolls = rowCount() * kRowH > viewport_.getMaximumVisibleHeight();
    const int w = viewport_.getMaximumVisibleWidth() - (scrolls ? viewport_.getScrollBarThickness() : 0);
    holder_.setSize(std::max(w, 100), std::max(rowCount() * kRowH, 1));
    for (int i = 0; i < rowCount(); ++i)
        rows_[static_cast<std::size_t>(i)]->setBounds(0, i * kRowH, holder_.getWidth(), kRowH);
}

} // namespace dc::ui
