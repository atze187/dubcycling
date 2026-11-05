#include "plugin/ui/Editor.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include "engine/LoopTiming.h"
#include "engine/TransientDetector.h"
#include "plugin/ui/PanelLook.h"

namespace dc::ui {

namespace {
constexpr double kDefaultBpm = 120.0;
constexpr int kHeaderH = 26;
constexpr int kTopH = 24;
constexpr int kGap = 6;
constexpr int kMargin = 8;
constexpr int kLogoW = 88;
constexpr int kCycleW = 130;
constexpr int kFxStripH = 17;     // incl. the 1 px bottom border of the Loop-2 box
constexpr int kFxPanelMinW = 440; // four modules
constexpr int kFxPanelMaxW = 600;
constexpr int kBottomMinH = 190;  // FxPanel: title, switch and four knob rows
constexpr int kMinW = 800;
constexpr int kMinH = 540;

using namespace panel;

juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }

LoopLane::Status laneStatus(DubcyclingProcessor::SlotStatus s)
{
    switch (s)
    {
        case DubcyclingProcessor::SlotStatus::Loading: return LoopLane::Status::Loading;
        case DubcyclingProcessor::SlotStatus::Loaded:  return LoopLane::Status::Loaded;
        case DubcyclingProcessor::SlotStatus::Missing: return LoopLane::Status::Missing;
        case DubcyclingProcessor::SlotStatus::Failed:  return LoopLane::Status::Failed;
        case DubcyclingProcessor::SlotStatus::Empty:   break;
    }
    return LoopLane::Status::Empty;
}

const LoopRef& refOf(const PluginState& s, int slot) { return slot == 0 ? s.loop1 : s.loop2; }
LoopRef& refOf(PluginState& s, int slot) { return slot == 0 ? s.loop1 : s.loop2; }

// The slot's file is gone (relink needed). A file that exists but failed to decode is Failed and
// goes through the replace flow.
bool fileIsMissing(DubcyclingProcessor::SlotStatus status, const LoopRef& ref)
{
    return status == DubcyclingProcessor::SlotStatus::Missing && !juce::File(ref.path).existsAsFile();
}
} // namespace

int suggestedBarsFor(const juce::File& file, double hostBpm)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return 1;
    const double seconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    const double bpm = std::isfinite(hostBpm) && hostBpm > 0.0 ? hostBpm : kDefaultBpm;
    return std::clamp(suggestBars(seconds, bpm, 4.0), 1, kMaxBars);
}

int parseBars(const juce::String& text, int fallback)
{
    const auto t = text.trim();
    if (t.isEmpty() || !t.containsOnly("+-0123456789.,"))
        return fallback;
    const double v = t.replaceCharacter(',', '.').getDoubleValue();
    if (!std::isfinite(v))
        return fallback;
    return static_cast<int>(std::clamp(std::round(v), 1.0, static_cast<double>(kMaxBars)));
}

LoopFileAction decideLoopFileAction(DubcyclingProcessor::SlotStatus status, bool storedPathExists, bool fileReadable,
                                    bool hasRegionsOrRules)
{
    if (!fileReadable)
        return LoopFileAction::Reject;
    if (status == DubcyclingProcessor::SlotStatus::Missing && !storedPathExists)
        return LoopFileAction::Relink;
    return hasRegionsOrRules ? LoopFileAction::ReplaceAfterConfirm : LoopFileAction::ReplaceDirect;
}

namespace {
// Index j such that `longer` with element j removed equals `shorter`; -1 if there is none.
int singleInsertIndex(const std::vector<double>& shorter, const std::vector<double>& longer)
{
    if (longer.size() != shorter.size() + 1)
        return -1;
    std::size_t j = 0;
    while (j < shorter.size() && shorter[j] == longer[j])
        ++j;
    for (std::size_t i = j; i < shorter.size(); ++i)
        if (shorter[i] != longer[i + 1])
            return -1;
    return static_cast<int>(j);
}
} // namespace

std::vector<RuleEntry> remapRulesForBoundaryEdit(std::vector<RuleEntry> rules, int slot,
                                                 const std::vector<double>& before,
                                                 const std::vector<double>& after)
{
    // shift(index) -> new index, or -1 to drop.
    std::function<int(int)> shift;
    if (const int k = singleInsertIndex(before, after); k >= 0)
        shift = [k](int i) { return i > k ? i + 1 : i; }; // boundary k splits old region k
    else if (const int k2 = singleInsertIndex(after, before); k2 >= 0)
        shift = [k2](int i) { return i == k2 + 1 ? -1 : (i > k2 + 1 ? i - 1 : i); }; // k2, k2+1 merge
    else
        return rules;

    if (slot == 0)
    {
        std::vector<RuleEntry> out;
        out.reserve(rules.size());
        for (auto& e : rules)
            if (const int r = shift(e.region1); r >= 0)
            {
                e.region1 = r;
                out.push_back(std::move(e));
            }
        return out;
    }
    for (auto& e : rules)
    {
        std::vector<RuleTarget> kept;
        kept.reserve(e.targets.size());
        for (auto t : e.targets)
            if (const int r = shift(t.region2); r >= 0)
            {
                t.region2 = r;
                kept.push_back(t);
            }
        e.targets = std::move(kept);
    }
    return rules;
}

std::vector<juce::String> remapLabelsForBoundaryEdit(std::vector<juce::String> labels,
                                                     const std::vector<double>& before,
                                                     const std::vector<double>& after)
{
    if (const int k = singleInsertIndex(before, after); k >= 0)
    {
        if (labels.size() > static_cast<std::size_t>(k) + 1)
            labels.insert(labels.begin() + k + 1, juce::String());
    }
    else if (const int k2 = singleInsertIndex(after, before); k2 >= 0)
    {
        if (labels.size() > static_cast<std::size_t>(k2) + 1)
            labels.erase(labels.begin() + k2 + 1);
    }
    while (!labels.empty() && labels.back().isEmpty())
        labels.pop_back();
    return labels;
}

std::vector<std::uint8_t> remapFxSwitchesForBoundaryEdit(std::vector<std::uint8_t> masks,
                                                         const std::vector<double>& before,
                                                         const std::vector<double>& after)
{
    if (const int k = singleInsertIndex(before, after); k >= 0)
    {
        if (masks.size() > static_cast<std::size_t>(k))
            masks.insert(masks.begin() + k + 1, masks[static_cast<std::size_t>(k)]);
    }
    else if (const int k2 = singleInsertIndex(after, before); k2 >= 0)
    {
        if (masks.size() > static_cast<std::size_t>(k2) + 1)
            masks.erase(masks.begin() + k2 + 1);
    }
    while (!masks.empty() && masks.back() == 0)
        masks.pop_back();
    return masks;
}

Editor::Editor(DubcyclingProcessor& proc, juce::File presetsRoot)
    : juce::AudioProcessorEditor(proc), proc_(proc), store_(std::move(presetsRoot)), presetBar_(proc, store_)
{
    setLookAndFeel(&look_);
    addAndMakeVisible(presetBar_);

    logo_.setText("DUBCYCLING", juce::dontSendNotification);
    logo_.setFont(mono(12.0f, true));
    logo_.setColour(juce::Label::textColourId, kText);
    addAndMakeVisible(logo_);
    bpmCaption_.setText("BPM", juce::dontSendNotification);
    bpmCaption_.setFont(mono(10.0f));
    bpmCaption_.setColour(juce::Label::textColourId, kDim);
    bpmCaption_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(bpmCaption_);
    styleLcd(bpm_, kGreen);
    addAndMakeVisible(bpm_);

    for (int i = 0; i < 2; ++i)
    {
        titles_[i].setText("L" + juce::String(i + 1), juce::dontSendNotification);
        titles_[i].setFont(mono(11.0f, true));
        titles_[i].setJustificationType(juce::Justification::centred);
        titles_[i].setColour(juce::Label::textColourId, kText);
        titles_[i].setColour(juce::Label::backgroundColourId, kEdge);
        addAndMakeVisible(titles_[i]);

        styleLcd(status_[i], kAmber, juce::Justification::centredLeft);
        addAndMakeVisible(status_[i]);

        barsLabel_[i].setText("TAKTE", juce::dontSendNotification);
        barsLabel_[i].setFont(mono(10.0f));
        barsLabel_[i].setColour(juce::Label::textColourId, kDim);
        barsLabel_[i].setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(barsLabel_[i]);

        bars_[i].setSliderStyle(juce::Slider::IncDecButtons);
        bars_[i].setTextBoxStyle(juce::Slider::TextBoxLeft, false, 34, kHeaderH - 6);
        bars_[i].setRange(1.0, static_cast<double>(kMaxBars), 1.0);
        bars_[i].setValue(1.0, juce::dontSendNotification);
        bars_[i].valueFromTextFunction = [this, i](const juce::String& text) {
            return static_cast<double>(parseBars(text, static_cast<int>(bars_[i].getValue())));
        };
        bars_[i].onValueChange = [this, i] { barsEdited(i); };
        addAndMakeVisible(bars_[i]);

        load_[i].setButtonText("LOAD");
        load_[i].setTooltip("Audiodatei laden (wav, mp3, flac, aif)");
        load_[i].onClick = [this, i] { openChooser(i); };
        addAndMakeVisible(load_[i]);

        suggest_[i].setButtonText("AUTO");
        suggest_[i].setTooltip("Grenzen an Transienten vorschlagen (f" + utf8("\xc3\xbc") + "r Drum-Loops)");
        suggest_[i].onClick = [this, i] { suggestRegions(i); };
        suggest_[i].setEnabled(false);
        addAndMakeVisible(suggest_[i]);

        lanes_[i].onRegionsChanged = [this, i](const RegionMap& m) { regionsEdited(i, m); };
        lanes_[i].onFileDropped = [this, i](const juce::File& f) { loadFile(i, f); };
        lanes_[i].onRegionPreview = [this, i](int region) { proc_.previewRegion(i, region); };
        lanes_[i].onLabelEdited =[this, i](int region, juce::String text) { labelEdited(i, region, text); };
        addAndMakeVisible(lanes_[i]);
    }

    styleLcd(cycle_, kAmber);
    addAndMakeVisible(cycle_);

    lanes_[0].onRegionSelected = [this](int) { updateRules(proc_.getPluginState()); };
    rules_.onChanged = [this](int region1, std::vector<RuleTarget> targets) {
        rulesEdited(region1, std::move(targets));
    };
    rules_.onLabelEdited = [this](int region, juce::String text) { labelEdited(1, region, text); };
    addAndMakeVisible(rules_);

    // A strip click selects the region in the lane and flips that switch; the panel switches the
    // selected region (it reports the region it was given).
    lanes_[1].onRegionSelected = [this](int) { updateFx(proc_.getPluginState()); };
    fxStrip_.onToggled = [this](int region, FxInsert insert) {
        lanes_[1].setSelectedRegion(region);
        switchEdited(region, insert, !proc_.getPluginState().fx.switchOn(region, insert));
    };
    fxPanel_.onSwitchChanged = [this](int region, FxInsert insert, bool on) { switchEdited(region, insert, on); };
    fxPanel_.onParamsChanged = [this](const FxParams& params) { paramsEdited(params); };
    addAndMakeVisible(fxStrip_);
    addAndMakeVisible(fxPanel_);

    setResizable(true, true);
    setResizeLimits(kMinW, kMinH, 1800, 1400);
    setSize(900, 600);
    for (int i = 0; i < 2; ++i)
        seenFailure_[i] = proc_.replaceFailure(i).id; // only failures from now on are shown
    refresh();
    startTimerHz(60);
}

Editor::~Editor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void Editor::refresh()
{
    const PluginState st = proc_.getPluginState();
    const auto model = proc_.getModel();
    for (int i = 0; i < 2; ++i)
    {
        const LoopRef& ref = refOf(st, i);
        const auto status = proc_.getSlotStatus(i);
        lanes_[i].setStatus(laneStatus(status));
        lanes_[i].setData(model != nullptr ? (i == 0 ? model->loop1.data : model->loop2.data) : nullptr);
        RegionMap regions;
        regions.boundaries = ref.boundaries;
        lanes_[i].setRegions(regions);
        lanes_[i].setLabels(ref.labels);
        suggest_[i].setEnabled(status == DubcyclingProcessor::SlotStatus::Loaded && lanes_[i].hasData());
        bars_[i].setValue(static_cast<double>(ref.bars), juce::dontSendNotification);

        // A staged replace that failed to decode: the slot is unchanged, say so once.
        const auto failure = proc_.replaceFailure(i);
        if (failure.id > seenFailure_[i])
        {
            seenFailure_[i] = failure.id;
            notice_[i] = "Nicht geladen: " + failure.fileName + " (" + failure.error + ")";
            noticeUntil_[i] = juce::Time::getMillisecondCounter() + 5000;
        }

        juce::String text;
        juce::Colour colour = kAmber;
        juce::Colour led = kGreen;
        switch (status)
        {
            case DubcyclingProcessor::SlotStatus::Empty:   text = "Leer"; led = kDim; break;
            case DubcyclingProcessor::SlotStatus::Loading:
            {
                const auto pending = proc_.pendingReplaceFileName(i);
                text = utf8("L\xc3\xa4" "dt\xe2\x80\xa6 ") + (pending.isNotEmpty() ? pending : ref.fileName);
                led = kAmber;
                break;
            }
            case DubcyclingProcessor::SlotStatus::Loaded:  text = ref.fileName; break;
            case DubcyclingProcessor::SlotStatus::Missing:
                text = "Datei fehlt: " + ref.fileName;
                colour = kWarn;
                led = kWarn;
                break;
            case DubcyclingProcessor::SlotStatus::Failed:
                text = "Nicht lesbar: " + proc_.slotError(i);
                colour = kWarn;
                led = kWarn;
                break;
        }
        if (led != ledColour_[i])
        {
            ledColour_[i] = led;
            repaint(ledBounds_[i].getSmallestIntegerContainer().expanded(2));
        }
        if (noticeUntil_[i] != 0 && juce::Time::getMillisecondCounter() < noticeUntil_[i])
        {
            text = notice_[i];
            colour = kWarn;
        }
        else
        {
            noticeUntil_[i] = 0;
        }
        status_[i].setText(text, juce::dontSendNotification);
        status_[i].setColour(juce::Label::textColourId, colour);
        const bool relink = fileIsMissing(status, ref);
        load_[i].setButtonText(relink ? "RELINK" : "LOAD");
        load_[i].setTooltip(relink ? "Datei neu zuordnen" : "Audiodatei laden (wav, mp3, flac, aif)");
    }
    const double bpm = proc_.hostBpm();
    bpm_.setText(std::isfinite(bpm) && bpm > 0.0 ? juce::String(bpm, 1) : juce::String("--"),
                 juce::dontSendNotification);
    rules_.setLabels(st.loop1.labels, st.loop2.labels);
    updateRules(st);
    updateFx(st);
    cycle_.setText("Zyklus: " + juce::String(lcmBars(st.loop1.bars, st.loop2.bars)) + " Takte",
                   juce::dontSendNotification);
    presetBar_.refresh();
}

void Editor::updatePlayheads()
{
    const auto head = proc_.playhead();
    lanes_[0].setPlayhead(head.phase1);
    lanes_[1].setPlayhead(head.phase2);
    lanes_[1].setActiveRegion(head.region2);
    // The draw for the next Loop-1 entry is already made: its Loop-2 region (or silence) builds up
    // over the lookahead window and takes effect when the countdown ends.
    if (head.teaser != Engine::kNoTeaser && head.teaserWindow > 0.0f)
        lanes_[1].setTeaser(head.teaser == Engine::kTeaserSilence ? LoopLane::kSilence : head.teaser,
                            1.0 - static_cast<double>(head.teaserEta) / static_cast<double>(head.teaserWindow));
    else
        lanes_[1].setTeaser(LoopLane::kNone, 0.0);
}

void Editor::regionsEdited(int slot, const RegionMap& regions)
{
    PluginState st = proc_.getPluginState();
    // Rules move with their regions; boundaries and rules go out in one applyEdit (one model swap).
    st.rules = remapRulesForBoundaryEdit(std::move(st.rules), slot, refOf(st, slot).boundaries, regions.boundaries);
    refOf(st, slot).labels = remapLabelsForBoundaryEdit(std::move(refOf(st, slot).labels),
                                                        refOf(st, slot).boundaries, regions.boundaries);
    if (slot == 1) // insert switches are per Loop-2 region
        st.fx.switches = remapFxSwitchesForBoundaryEdit(std::move(st.fx.switches), st.loop2.boundaries,
                                                        regions.boundaries);
    refOf(st, slot).boundaries = regions.boundaries;
    proc_.applyEdit(st);
    if (slot == 1)
        updateFx(proc_.getPluginState()); // the strip follows the lane at once, not on the next poll
}

void Editor::updateFx(const PluginState& st)
{
    const auto& regions = lanes_[1].regions();
    const int selected = lanes_[1].selectedRegion();
    fxStrip_.setRegions(regions);
    fxStrip_.setSwitches(st.fx.switches);
    fxStrip_.setSelectedRegion(selected);
    fxPanel_.setContext(selected, regions.regionCount(), st.loop2.labels, st.fx);
}

void Editor::switchEdited(int region2, FxInsert insert, bool on)
{
    PluginState st = proc_.getPluginState();
    st.fx.setSwitch(region2, insert, on);
    proc_.applyEdit(st);
    refresh();
}

void Editor::paramsEdited(const FxParams& params)
{
    PluginState st = proc_.getPluginState();
    st.fx.params = params;
    proc_.applyEdit(st);
}

void Editor::labelEdited(int slot, int region, const juce::String& text)
{
    if (region < 0 || region >= kMaxRegion1)
        return;
    PluginState st = proc_.getPluginState();
    auto& labels = refOf(st, slot).labels;
    const auto index = static_cast<std::size_t>(region);
    if (labels.size() <= index)
    {
        if (text.isEmpty())
            return;
        labels.resize(index + 1);
    }
    labels[index] = sanitizeLabel(text);
    while (!labels.empty() && labels.back().isEmpty())
        labels.pop_back();
    proc_.applyEdit(st);
    refresh();
}

void Editor::updateRules(const PluginState& st)
{
    // The lane clamps its selection into the current region count, so a Loop-1 region that was
    // removed has already fallen back to the last valid one here.
    const int region1 = lanes_[0].selectedRegion();
    std::vector<RuleTarget> targets;
    for (const auto& e : st.rules)
        if (e.region1 == region1)
        {
            targets = e.targets;
            break;
        }
    // Nothing to weight until both loops exist: no rows (and so no sliders) for an empty slot.
    const bool bothLoops = proc_.getSlotStatus(0) != DubcyclingProcessor::SlotStatus::Empty
                        && proc_.getSlotStatus(1) != DubcyclingProcessor::SlotStatus::Empty;
    rules_.setContext(region1, bothLoops ? lanes_[1].regions().regionCount() : 0, targets);
}

void Editor::rulesEdited(int region1, std::vector<RuleTarget> targets)
{
    PluginState st = proc_.getPluginState();
    bool found = false;
    for (auto& e : st.rules)
        if (e.region1 == region1)
        {
            e.targets = std::move(targets);
            found = true;
            break;
        }
    if (!found)
        st.rules.push_back(RuleEntry { region1, std::move(targets) });
    proc_.applyEdit(st);
}

void Editor::barsEdited(int slot)
{
    PluginState st = proc_.getPluginState();
    refOf(st, slot).bars = static_cast<int>(bars_[slot].getValue());
    proc_.applyEdit(st);
    refresh();
}

juce::String replaceConfirmTitle(int slot)
{
    return "Loop " + juce::String(slot == 0 ? 1 : 2) + " ersetzen?";
}

juce::String replaceConfirmMessage()
{
    return utf8("Dessen Regionen und alle Regeln (auch die zwischen Loop 1 und Loop 2) werden gel\xc3\xb6scht.");
}

juce::String suggestConfirmTitle(int slot)
{
    return "Regionen von Loop " + juce::String(slot == 0 ? 1 : 2) + " ersetzen?";
}

juce::String suggestConfirmMessage()
{
    return utf8("Die vorhandenen Grenzen werden durch den Vorschlag ersetzt. Regeln bleiben erhalten, "
                "k\xc3\xb6nnen danach aber auf nicht mehr vorhandene Regionen zeigen.");
}

void Editor::suggestRegions(int slot)
{
    slot = slot == 0 ? 0 : 1;
    if (pending_[slot].active || proc_.getSlotStatus(slot) != DubcyclingProcessor::SlotStatus::Loaded)
        return; // e.g. a staged replace: the data shown is about to go away
    const auto model = proc_.getModel();
    const auto data = model == nullptr ? nullptr : (slot == 0 ? model->loop1.data : model->loop2.data);
    if (data == nullptr || data->frames() == 0)
        return;
    const std::vector<double> found = suggestBoundaries(*data, kSuggestMaxBoundaries);
    if (found.empty())
    {
        showNotice(slot, "Keine Transienten gefunden");
        return;
    }
    const PluginState st = proc_.getPluginState();
    auto commit = [this, slot, found] {
        PluginState now = proc_.getPluginState();
        refOf(now, slot).boundaries = found;
        proc_.applyEdit(now);
        refresh();
    };
    if (refOf(st, slot).boundaries.empty())
    {
        commit();
        return;
    }

    pending_[slot].active = true;
    pending_[slot].state = serialize(st);
    juce::Component::SafePointer<Editor> safe(this);
    auto answer = [safe, slot, commit](bool ok) {
        if (safe == nullptr || !safe->pending_[slot].active)
            return;
        const juce::MemoryBlock asked = safe->pending_[slot].state;
        safe->pending_[slot] = {};
        if (!ok)
            return;
        // The answer only covers the state the user was asked about; never ask twice.
        if (serialize(safe->proc_.getPluginState()) == asked)
            commit();
        else
            safe->showNotice(slot, "Nicht ersetzt: Loop " + juce::String(slot + 1) + " wurde inzwischen ge"
                                       + utf8("\xc3\xa4") + "ndert");
    };
    if (confirmReplace)
    {
        confirmReplace(slot, std::move(answer));
        return;
    }
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel(juce::MessageBoxIconType::WarningIcon,
                                                                suggestConfirmTitle(slot), suggestConfirmMessage(),
                                                                "Ersetzen", "Abbrechen", this);
    juce::AlertWindow::showAsync(options, [answer](int result) { answer(result == 1); });
}

void Editor::loadFile(int slot, const juce::File& file)
{
    slot = slot == 0 ? 0 : 1;
    if (pending_[slot].active)
        return; // one open confirmation per slot: further drops are ignored until it is answered
    handleFile(slot, file, nullptr);
}

void Editor::handleFile(int slot, const juce::File& file, const juce::MemoryBlock* confirmedState)
{
    const PluginState st = proc_.getPluginState();
    const LoopRef& ref = refOf(st, slot);
    const juce::String error = LoopLoader::probe(file);
    const auto action = decideLoopFileAction(proc_.getSlotStatus(slot), juce::File(ref.path).existsAsFile(),
                                             error.isEmpty(), !ref.boundaries.empty() || !st.rules.empty());
    switch (action)
    {
        case LoopFileAction::Reject:
            showNotice(slot, "Nicht geladen: " + file.getFileName() + " (" + error + ")");
            return;
        case LoopFileAction::Relink:
            if (!proc_.loadLoop(slot, file)) // keeps regions, rules and bars
            {
                showNotice(slot, "Nicht geladen: " + file.getFileName());
                return;
            }
            noticeUntil_[slot] = 0;
            refresh();
            return;
        case LoopFileAction::ReplaceDirect:
            replace(slot, file);
            return;
        case LoopFileAction::ReplaceAfterConfirm:
            if (confirmedState != nullptr)
            {
                // The answer only covers the state the user was asked about; never ask twice.
                if (serialize(st) == *confirmedState)
                    replace(slot, file);
                else
                    showNotice(slot, "Nicht ersetzt: Loop " + juce::String(slot + 1) + " wurde inzwischen ge"
                                         + utf8("\xc3\xa4") + "ndert");
                return;
            }
            askReplace(slot, file, serialize(st));
            return;
    }
}

void Editor::askReplace(int slot, const juce::File& file, const juce::MemoryBlock& askedState)
{
    pending_[slot].active = true;
    pending_[slot].state = askedState;
    juce::Component::SafePointer<Editor> safe(this);
    auto answer = [safe, slot, file](bool ok) {
        if (safe == nullptr || !safe->pending_[slot].active)
            return;
        const juce::MemoryBlock asked = safe->pending_[slot].state;
        safe->pending_[slot] = {};
        if (ok)
            safe->handleFile(slot, file, &asked); // re-validates file and slot before replacing
    };
    if (confirmReplace)
    {
        confirmReplace(slot, std::move(answer));
        return;
    }
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel(juce::MessageBoxIconType::WarningIcon,
                                                                replaceConfirmTitle(slot), replaceConfirmMessage(),
                                                                "Ersetzen", "Abbrechen", this);
    juce::AlertWindow::showAsync(options, [answer](int result) { answer(result == 1); });
}

void Editor::replace(int slot, const juce::File& file)
{
    if (!proc_.replaceLoop(slot, file, suggestedBarsFor(file, proc_.hostBpm())))
    {
        showNotice(slot, "Nicht geladen: " + file.getFileName());
        return;
    }
    noticeUntil_[slot] = 0;
    refresh();
}

void Editor::showNotice(int slot, const juce::String& text)
{
    notice_[slot] = text;
    noticeUntil_[slot] = juce::Time::getMillisecondCounter() + 5000;
    refresh();
}

void Editor::openChooser(int slot)
{
    const auto ref = refOf(proc_.getPluginState(), slot);
    const juce::File start = ref.path.isNotEmpty() ? juce::File(ref.path).getParentDirectory() : juce::File();
    chooser_ = std::make_unique<juce::FileChooser>("Loop " + juce::String(slot + 1), start,
                                                   "*.wav;*.mp3;*.flac;*.aif;*.aiff");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (isAcceptedAudioFile(file))
                                  loadFile(slot, file);
                          });
}

void Editor::paint(juce::Graphics& g)
{
    g.fillAll(kWindow);
    for (int i = 0; i < 2; ++i)
    {
        const auto strip = stripBounds_[i];
        g.setColour(kStrip);
        g.fillRect(strip);
        g.setColour(kEdge);
        g.drawRect(strip, 1);
        g.drawHorizontalLine(strip.getY() + kHeaderH, static_cast<float>(strip.getX()),
                             static_cast<float>(strip.getRight()));
        g.setColour(ledColour_[i]);
        g.fillEllipse(ledBounds_[i]);
    }
    g.setColour(kEdge);
    g.drawHorizontalLine(kMargin + kTopH + 3, static_cast<float>(kMargin), static_cast<float>(getWidth() - kMargin));
}

void Editor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);

    // Top row: logo, preset bar, BPM on the right. The cycle readout sits left of BPM only when the
    // preset bar keeps its full width; otherwise it is hidden (cycleText() still has the text).
    auto top = area.removeFromTop(kTopH);
    logo_.setBounds(top.removeFromLeft(kLogoW));
    top.removeFromLeft(kGap * 2);
    bpm_.setBounds(top.removeFromRight(52));
    top.removeFromRight(kGap);
    bpmCaption_.setBounds(top.removeFromRight(34));
    top.removeFromRight(kGap * 2);
    const bool cycleFits = top.getWidth() >= PresetBar::kPreferredWidth + kGap * 2 + kCycleW;
    cycle_.setVisible(cycleFits);
    if (cycleFits)
    {
        cycle_.setBounds(top.removeFromRight(kCycleW));
        top.removeFromRight(kGap * 2);
    }
    presetBar_.setBounds(top);
    area.removeFromTop(kGap + 3);

    // Lanes and the bottom row (rule panel, effects panel) share what is left: the bottom row gets
    // about a third (at least what the effects panel needs), the rest goes to Loop 1 (58 %) and
    // Loop 2 (42 %). The insert strip sits inside the Loop-2 box, right under the lane.
    const int shared = area.getHeight() - 2 * kHeaderH - 2 * kGap - kFxStripH;
    const int rulesH = std::clamp(shared * 38 / 100, kBottomMinH, 320);
    const int laneSpace = std::max(shared - rulesH, 80);
    const int lane0H = std::max(laneSpace * 58 / 100, 40);
    const int laneH[2] = { lane0H, std::max(laneSpace - lane0H, 40) };

    for (int i = 0; i < 2; ++i)
    {
        auto strip = area.removeFromTop(kHeaderH + laneH[i] + (i == 1 ? kFxStripH : 0));
        stripBounds_[i] = strip;
        auto header = strip.removeFromTop(kHeaderH).reduced(6, 3);
        ledBounds_[i] = juce::Rectangle<float>(7.0f, 7.0f)
                            .withCentre({ static_cast<float>(header.getX()) + 3.5f, header.getCentreY() + 0.0f });
        header.removeFromLeft(14);
        titles_[i].setBounds(header.removeFromLeft(28));
        header.removeFromLeft(kGap);
        load_[i].setBounds(header.removeFromRight(58));
        header.removeFromRight(4);
        suggest_[i].setBounds(header.removeFromRight(46));
        header.removeFromRight(kGap);
        bars_[i].setBounds(header.removeFromRight(84));
        barsLabel_[i].setBounds(header.removeFromRight(46));
        header.removeFromRight(kGap);
        status_[i].setBounds(header);
        if (i == 1)
        {
            // Same x range as the lane above: the markers line up with the regions.
            lanes_[i].setBounds(strip.removeFromTop(laneH[i]).reduced(1, 0));
            fxStrip_.setBounds(strip.reduced(1, 0).withTrimmedBottom(1));
        }
        else
        {
            lanes_[i].setBounds(strip.reduced(1, 0).withTrimmedBottom(1));
        }
        area.removeFromTop(kGap);
    }
    const int fxW = std::clamp(area.getWidth() / 2, kFxPanelMinW, kFxPanelMaxW);
    fxPanel_.setBounds(area.removeFromRight(fxW));
    area.removeFromRight(kGap);
    rules_.setBounds(area);
}

} // namespace dc::ui
