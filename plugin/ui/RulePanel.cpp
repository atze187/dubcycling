#include "plugin/ui/RulePanel.h"
#include <algorithm>
#include <cmath>

namespace dc::ui {

namespace {
constexpr float kEps = 0.0005f;

const juce::Colour kText { 0xffd7dbe0 };
const juce::Colour kDim { 0xff9aa1ab };
const juce::Colour kOutline { 0xff3a3f47 };
const juce::Colour kAccent { 0xff5aa9e6 };
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
juce::String modeText(PlayMode m) { return m == PlayMode::Gate ? "GATE" : "ONESHOT"; }

bool same(const std::vector<RuleTarget>& a, const std::vector<RuleTarget>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].region2 != b[i].region2 || a[i].weight != b[i].weight || a[i].mode != b[i].mode)
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

struct RulePanel::Row final : public juce::Component
{
    Row(RulePanel& owner, int index) : owner_(owner), index_(index)
    {
        letter.setText(regionLetters(index), juce::dontSendNotification);
        letter.setTooltip("Loop 2, Region " + juce::String(index + 1));
        letter.setColour(juce::Label::textColourId, kText);
        letter.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        addAndMakeVisible(letter);

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

        value.setJustificationType(juce::Justification::centredRight);
        value.setColour(juce::Label::textColourId, kText);
        addAndMakeVisible(value);

        toggle.onClick = [this] {
            owner_.setMode(index_, mode == PlayMode::Gate ? PlayMode::OneShot : PlayMode::Gate);
        };
        addAndMakeVisible(toggle);
        show();
    }

    // Brings slider, value label and toggle in line with percent / mode.
    void show()
    {
        updating = true;
        slider.setValue(static_cast<double>(percent), juce::dontSendNotification);
        updating = false;
        value.setText(percentString(percent), juce::dontSendNotification);
        toggle.setButtonText(modeText(mode));
        toggle.setColour(juce::TextButton::buttonColourId, mode == PlayMode::Gate ? kOutline : kOneShot.darker(0.4f));
        toggle.setColour(juce::TextButton::textColourOffId, mode == PlayMode::Gate ? kDim : kText);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(0, 2);
        letter.setBounds(r.removeFromLeft(34));
        toggle.setBounds(r.removeFromRight(86));
        r.removeFromRight(8);
        value.setBounds(r.removeFromRight(48));
        slider.setBounds(r);
    }

    RulePanel& owner_;
    int index_;
    juce::Label letter, value;
    juce::Slider slider;
    juce::TextButton toggle;
    float percent = 0.0f;
    PlayMode mode = PlayMode::Gate;
    bool touched = false; // edited since the last setContext/publish and not yet in stored_
    bool updating = false;
};

RulePanel::RulePanel()
{
    title_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, kText);
    addAndMakeVisible(title_);

    silence_.setColour(juce::Label::textColourId, kDim);
    silence_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(silence_);

    viewport_.setViewedComponent(&holder_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    setContext(0, 0, {});
}

RulePanel::~RulePanel()
{
    viewport_.setViewedComponent(nullptr, false);
}

juce::String RulePanel::rowLabel(int row) const
{
    return row >= 0 && row < rowCount() ? regionLetters(row) : juce::String();
}

float RulePanel::weightPercent(int row) const
{
    return row >= 0 && row < rowCount() ? rows_[static_cast<std::size_t>(row)]->percent : 0.0f;
}

PlayMode RulePanel::mode(int row) const
{
    return row >= 0 && row < rowCount() ? rows_[static_cast<std::size_t>(row)]->mode : PlayMode::Gate;
}

juce::String RulePanel::percentText(int row) const
{
    return row >= 0 && row < rowCount() ? rows_[static_cast<std::size_t>(row)]->value.getText() : juce::String();
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
        row.touched = false;
        for (const auto& t : stored_)
            if (t.region2 == i)
            {
                row.percent = toPercent(t.weight);
                row.mode = t.mode;
                break; // duplicates beyond the first count as "other" targets
            }
        row.show();
    }
    title_.setText(utf8("Regeln f\xc3\xbcr Loop-1-Region ") + juce::String(region1_ + 1), juce::dontSendNotification);
    updateSilence();
}

float RulePanel::otherPercent(int row) const
{
    float sum = 0.0f;
    for (int i = 0; i < rowCount(); ++i)
        if (i != row)
            sum += rows_[static_cast<std::size_t>(i)]->percent;
    // Stored targets that no row shows: stale regions and duplicates of a shown region.
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

void RulePanel::updateSilence()
{
    const float rest = std::max(0.0f, snap(100.0f - otherPercent(-1)));
    silence_.setText("Stille: " + juce::String(juce::roundToInt(rest)) + " %", juce::dontSendNotification);
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
            }
        }
        out.push_back(n); // stale and duplicate entries pass through untouched
    }
    for (int i = 0; i < rowCount(); ++i)
    {
        const auto& r = *rows_[static_cast<std::size_t>(i)];
        if (!seen[static_cast<std::size_t>(i)] && r.touched)
            out.push_back(RuleTarget { i, r.percent / 100.0f, r.mode });
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
    g.setColour(kOutline);
    g.drawRect(getLocalBounds(), 1);
}

void RulePanel::resized()
{
    auto r = getLocalBounds().reduced(8, 6);
    auto top = r.removeFromTop(24);
    silence_.setBounds(top.removeFromRight(150));
    title_.setBounds(top);
    r.removeFromTop(2);
    viewport_.setBounds(r);
    const int w = viewport_.getMaximumVisibleWidth();
    holder_.setSize(std::max(w, 100), std::max(rowCount() * kRowH, 1));
    for (int i = 0; i < rowCount(); ++i)
        rows_[static_cast<std::size_t>(i)]->setBounds(0, i * kRowH, holder_.getWidth(), kRowH);
}

} // namespace dc::ui
