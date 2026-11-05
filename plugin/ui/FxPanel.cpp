#include "plugin/ui/FxPanel.h"
#include <algorithm>
#include <cmath>
#include "plugin/ui/FxStrip.h"
#include "plugin/ui/PanelLook.h"
#include "plugin/ui/RulePanel.h"

namespace dc::ui {

namespace {
using namespace panel;

constexpr int kTitleH = 18;
constexpr int kSwitchH = 20;
constexpr int kRowH = 34;
constexpr int kMargin = 4;

juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }

const char* insertTitle(FxInsert i)
{
    switch (i)
    {
        case FxInsert::Bitcrush: return "BITCRUSH";
        case FxInsert::Filter: return "FILTER";
        case FxInsert::Flanger: return "FLANGER";
        case FxInsert::Reverb: return "REVERB";
    }
    return "";
}

juce::String paramName(FxParam p)
{
    switch (p)
    {
        case FxParam::ReverbMix: case FxParam::BitcrushMix: case FxParam::FlangerMix: return "Mix";
        case FxParam::ReverbSize: return utf8("Gr\xc3\xb6\xc3\x9f" "e");
        case FxParam::ReverbDamping: return utf8("D\xc3\xa4mpfung");
        case FxParam::FilterType: return "Typ";
        case FxParam::FilterCutoff: return "Cutoff";
        case FxParam::FilterResonance: return "Resonanz";
        case FxParam::BitcrushBits: return "Bits";
        case FxParam::BitcrushRate: case FxParam::FlangerRate: return "Rate";
        case FxParam::FlangerDepth: return "Tiefe";
        case FxParam::FlangerFeedback: return "Feedback";
    }
    return "";
}

juce::String valueText(FxParam p, double v)
{
    switch (p)
    {
        case FxParam::FilterType:
        {
            static const char* const names[] = { "TP", "HP", "BP" };
            return names[juce::jlimit(0, 2, juce::roundToInt(v))];
        }
        case FxParam::FilterCutoff:
            return v >= 1000.0 ? juce::String(v / 1000.0, 1) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
        case FxParam::BitcrushBits: return juce::String(juce::roundToInt(v));
        case FxParam::BitcrushRate: return utf8("\xc3\x97") + juce::String(juce::roundToInt(v));
        case FxParam::FlangerRate: return juce::String(v, 2) + " Hz";
        default: return juce::String(juce::roundToInt(v * 100.0)) + " %"; // signed for Feedback
    }
}

bool isWhole(FxParam p) { return p == FxParam::BitcrushBits || p == FxParam::BitcrushRate; }

double sliderInterval(FxParam p)
{
    if (isWhole(p))
        return 1.0;
    return p == FxParam::FilterCutoff ? 0.0 : 0.01;
}
} // namespace

struct FxPanel::Knob final
{
    FxParam p {};
    juce::Label name, value;
    juce::Slider slider;                         // unused for FilterType
    std::array<juce::TextButton, 3> typeButtons; // FilterType only
    bool updating = false;
};

struct FxPanel::Module final
{
    FxInsert insert {};
    juce::Label title;
    juce::ToggleButton on;
    std::vector<FxParam> params;
};

FxPanel::FxPanel()
{
    for (int i = 0; i < kFxInserts; ++i)
    {
        auto m = std::make_unique<Module>();
        m->insert = static_cast<FxInsert>(i);
        m->title.setText(insertTitle(m->insert), juce::dontSendNotification);
        m->title.setFont(mono(11.0f, true));
        m->title.setColour(juce::Label::textColourId, fxInsertColour(m->insert));
        addAndMakeVisible(m->title);
        m->on.setColour(juce::ToggleButton::textColourId, kText);
        m->on.setColour(juce::ToggleButton::tickColourId, fxInsertColour(m->insert));
        m->on.setColour(juce::ToggleButton::tickDisabledColourId, kDim);
        m->on.onClick = [this, ins = m->insert] {
            userSwitched(ins, modules_[static_cast<std::size_t>(ins)]->on.getToggleState());
        };
        addAndMakeVisible(m->on);
        modules_[static_cast<std::size_t>(i)] = std::move(m);
    }

    for (int i = 0; i < kFxParamCount; ++i)
    {
        const auto p = static_cast<FxParam>(i);
        const auto& info = fxParamInfo(p);
        auto k = std::make_unique<Knob>();
        k->p = p;
        const auto colour = fxInsertColour(info.insert);
        modules_[static_cast<std::size_t>(info.insert)]->params.push_back(p);

        k->name.setText(paramName(p), juce::dontSendNotification);
        k->name.setFont(mono(10.0f));
        k->name.setColour(juce::Label::textColourId, kDim);
        k->name.setBorderSize(juce::BorderSize<int>(0));
        addAndMakeVisible(k->name);

        if (p == FxParam::FilterType)
        {
            for (int b = 0; b < 3; ++b)
            {
                auto& btn = k->typeButtons[static_cast<std::size_t>(b)];
                btn.setButtonText(valueText(p, b));
                btn.setClickingTogglesState(false);
                btn.setColour(juce::TextButton::buttonOnColourId, colour.darker(0.5f));
                btn.setColour(juce::TextButton::textColourOnId, kText);
                btn.onClick = [this, b] { setParam(FxParam::FilterType, b); };
                addAndMakeVisible(btn);
            }
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
}

FxPanel::~FxPanel() = default;

void FxPanel::show(FxParam p)
{
    auto& k = knob(p);
    const double v = param(p);
    if (p == FxParam::FilterType)
    {
        for (int b = 0; b < 3; ++b)
            k.typeButtons[static_cast<std::size_t>(b)].setToggleState(juce::roundToInt(v) == b,
                                                                       juce::dontSendNotification);
        return;
    }
    k.updating = true;
    k.slider.setValue(v, juce::dontSendNotification);
    k.updating = false;
    k.value.setText(valueText(p, v), juce::dontSendNotification);
}

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

bool FxPanel::switchEnabled(FxInsert i) const { return modules_[static_cast<std::size_t>(i)]->on.isEnabled(); }

bool FxPanel::switchOn(FxInsert i) const { return modules_[static_cast<std::size_t>(i)]->on.getToggleState(); }

juce::String FxPanel::switchText(FxInsert i) const
{
    return modules_[static_cast<std::size_t>(i)]->on.getButtonText();
}

void FxPanel::userSwitched(FxInsert i, bool on)
{
    if (!switchEnabled(i))
        return;
    if (onSwitchChanged)
        onSwitchChanged(region2_, i, on);
}

void FxPanel::setSwitch(FxInsert i, bool on)
{
    if (!switchEnabled(i))
        return;
    modules_[static_cast<std::size_t>(i)]->on.setToggleState(on, juce::dontSendNotification);
    userSwitched(i, on);
}

void FxPanel::setContext(int region2, int regionCount, const std::vector<juce::String>& labels, const FxSettings& fx)
{
    region2_ = region2;
    regionCount_ = std::max(regionCount, 0);
    const bool valid = region2_ >= 0 && region2_ < regionCount_;
    for (int i = 0; i < kFxInserts; ++i)
    {
        const auto ins = static_cast<FxInsert>(i);
        auto& on = modules_[static_cast<std::size_t>(i)]->on;
        on.setEnabled(valid);
        on.setToggleState(valid && fx.switchOn(region2_, ins), juce::dontSendNotification);
        on.setButtonText(valid ? "Region " + regionLabel(labels, region2_) : juce::String("keine Region"));
        on.setTooltip(valid ? juce::String(insertTitle(ins)) + utf8(" f\xc3\xbcr Region ") + regionLabel(labels, region2_)
                                  + " an/aus"
                            : utf8("Erst eine Loop-2-Region w\xc3\xa4hlen"));
    }
    if (dragging_)
        return; // the editor's poll must not pull the slider out of the user's hand
    params_ = fx.params;
    dirty_ = false;
    for (int i = 0; i < kFxParamCount; ++i)
        show(static_cast<FxParam>(i));
}

juce::Rectangle<int> FxPanel::switchBounds(FxInsert i) const
{
    return modules_[static_cast<std::size_t>(i)]->on.getBounds();
}

juce::Rectangle<int> FxPanel::paramBounds(FxParam p) const
{
    const auto& k = knob(p);
    if (p == FxParam::FilterType)
        return k.name.getBounds().getUnion(k.typeButtons[0].getBounds()).getUnion(k.typeButtons[2].getBounds());
    return k.name.getBounds().getUnion(k.value.getBounds()).getUnion(k.slider.getBounds());
}

void FxPanel::paint(juce::Graphics& g)
{
    g.fillAll(kStrip);
    g.setColour(kEdge);
    g.drawRect(getLocalBounds(), 1);
    const float colW = static_cast<float>(getWidth()) / kFxInserts;
    for (int i = 1; i < kFxInserts; ++i)
        g.drawVerticalLine(juce::roundToInt(colW * static_cast<float>(i)), 4.0f, static_cast<float>(getHeight() - 4));
}

void FxPanel::resized()
{
    const int colW = getWidth() / kFxInserts;
    for (int i = 0; i < kFxInserts; ++i)
    {
        auto& m = *modules_[static_cast<std::size_t>(i)];
        auto col = juce::Rectangle<int>(i * colW, 0, colW, getHeight()).reduced(kMargin + 2, kMargin);
        m.title.setBounds(col.removeFromTop(kTitleH));
        m.on.setBounds(col.removeFromTop(kSwitchH));
        col.removeFromTop(2);
        for (const auto p : m.params)
        {
            auto row = col.removeFromTop(kRowH);
            auto& k = knob(p);
            auto line = row.removeFromTop(14);
            if (p == FxParam::FilterType)
            {
                k.name.setBounds(line);
                auto bt = row.removeFromTop(18);
                const int bw = bt.getWidth() / 3;
                for (auto& btn : k.typeButtons)
                    btn.setBounds(bt.removeFromLeft(bw).reduced(1, 0));
                continue;
            }
            k.value.setBounds(line.removeFromRight(line.getWidth() * 3 / 5));
            k.name.setBounds(line);
            k.slider.setBounds(row.removeFromTop(14));
        }
    }
}

} // namespace dc::ui
