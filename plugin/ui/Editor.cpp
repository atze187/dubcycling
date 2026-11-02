#include "plugin/ui/Editor.h"
#include <algorithm>
#include <cmath>
#include "engine/LoopTiming.h"
#include "engine/TransientDetector.h"

namespace dc::ui {

namespace {
constexpr double kDefaultBpm = 120.0;
constexpr int kHeaderH = 30;
constexpr int kLaneH = 150;
constexpr int kGap = 10;
constexpr int kMargin = 16;

const juce::Colour kWindow { 0xff16181c };
const juce::Colour kText { 0xffd7dbe0 };
const juce::Colour kDim { 0xff9aa1ab };
const juce::Colour kWarn { 0xffe0816a };

juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }

LoopLane::Status laneStatus(DubcyclingProcessor::SlotStatus s)
{
    switch (s)
    {
        case DubcyclingProcessor::SlotStatus::Loading: return LoopLane::Status::Loading;
        case DubcyclingProcessor::SlotStatus::Loaded:  return LoopLane::Status::Loaded;
        case DubcyclingProcessor::SlotStatus::Missing: return LoopLane::Status::Missing;
        case DubcyclingProcessor::SlotStatus::Empty:   break;
    }
    return LoopLane::Status::Empty;
}

const LoopRef& refOf(const PluginState& s, int slot) { return slot == 0 ? s.loop1 : s.loop2; }
LoopRef& refOf(PluginState& s, int slot) { return slot == 0 ? s.loop1 : s.loop2; }

// The slot's file is gone (relink needed), as opposed to a file that exists but failed to decode.
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

Editor::Editor(DubcyclingProcessor& proc) : juce::AudioProcessorEditor(proc), proc_(proc)
{
    for (int i = 0; i < 2; ++i)
    {
        titles_[i].setText("Loop " + juce::String(i + 1), juce::dontSendNotification);
        titles_[i].setFont(juce::FontOptions(16.0f, juce::Font::bold));
        titles_[i].setColour(juce::Label::textColourId, kText);
        addAndMakeVisible(titles_[i]);

        status_[i].setColour(juce::Label::textColourId, kDim);
        addAndMakeVisible(status_[i]);

        barsLabel_[i].setText("Takte", juce::dontSendNotification);
        barsLabel_[i].setColour(juce::Label::textColourId, kDim);
        barsLabel_[i].setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(barsLabel_[i]);

        bars_[i].setSliderStyle(juce::Slider::IncDecButtons);
        bars_[i].setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, kHeaderH - 6);
        bars_[i].setRange(1.0, static_cast<double>(kMaxBars), 1.0);
        bars_[i].setValue(1.0, juce::dontSendNotification);
        bars_[i].valueFromTextFunction = [this, i](const juce::String& text) {
            return static_cast<double>(parseBars(text, static_cast<int>(bars_[i].getValue())));
        };
        bars_[i].onValueChange = [this, i] { barsEdited(i); };
        addAndMakeVisible(bars_[i]);

        load_[i].setButtonText(utf8("Load\xe2\x80\xa6"));
        load_[i].onClick = [this, i] { openChooser(i); };
        addAndMakeVisible(load_[i]);

        suggest_[i].setButtonText("Vorschlag");
        suggest_[i].setTooltip("Grenzen an Transienten vorschlagen (f" + utf8("\xc3\xbc") + "r Drum-Loops)");
        suggest_[i].onClick = [this, i] { suggestRegions(i); };
        suggest_[i].setEnabled(false);
        addAndMakeVisible(suggest_[i]);

        lanes_[i].onRegionsChanged = [this, i](const RegionMap& m) { regionsEdited(i, m); };
        lanes_[i].onFileDropped = [this, i](const juce::File& f) { loadFile(i, f); };
        addAndMakeVisible(lanes_[i]);
    }

    cycle_.setColour(juce::Label::textColourId, kText);
    cycle_.setFont(juce::FontOptions(15.0f));
    addAndMakeVisible(cycle_);

    lanes_[0].onRegionSelected = [this](int) { updateRules(proc_.getPluginState()); };
    rules_.onChanged = [this](int region1, std::vector<RuleTarget> targets) {
        rulesEdited(region1, std::move(targets));
    };
    addAndMakeVisible(rules_);

    setResizable(true, true);
    setResizeLimits(700, 620, 1800, 1400);
    setSize(900, 720);
    refresh();
    startTimerHz(10);
}

Editor::~Editor()
{
    stopTimer();
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
        suggest_[i].setEnabled(lanes_[i].hasData());
        bars_[i].setValue(static_cast<double>(ref.bars), juce::dontSendNotification);

        juce::String text;
        juce::Colour colour = kDim;
        switch (status)
        {
            case DubcyclingProcessor::SlotStatus::Empty:   text = "Leer"; break;
            case DubcyclingProcessor::SlotStatus::Loading: text = utf8("L\xc3\xa4" "dt\xe2\x80\xa6 ") + ref.fileName; break;
            case DubcyclingProcessor::SlotStatus::Loaded:  text = ref.fileName; break;
            case DubcyclingProcessor::SlotStatus::Missing:
                text = (fileIsMissing(status, ref) ? "Datei fehlt: " : "Nicht lesbar: ") + ref.fileName;
                colour = kWarn;
                break;
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
        load_[i].setButtonText(fileIsMissing(status, ref) ? utf8("Neu zuordnen\xe2\x80\xa6") : utf8("Load\xe2\x80\xa6"));
    }
    updateRules(st);
    cycle_.setText("Zyklus: " + juce::String(lcmBars(st.loop1.bars, st.loop2.bars)) + " Takte",
                   juce::dontSendNotification);
}

void Editor::regionsEdited(int slot, const RegionMap& regions)
{
    PluginState st = proc_.getPluginState();
    refOf(st, slot).boundaries = regions.boundaries;
    proc_.applyEdit(st);
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
    rules_.setContext(region1, lanes_[1].regions().regionCount(), targets);
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
    if (pending_[slot].active)
        return;
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
            proc_.loadLoop(slot, file); // keeps regions, rules and bars
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
    proc_.replaceLoop(slot, file, suggestedBarsFor(file, proc_.hostBpm()));
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
}

void Editor::resized()
{
    auto area = getLocalBounds().reduced(kMargin);
    for (int i = 0; i < 2; ++i)
    {
        auto header = area.removeFromTop(kHeaderH);
        titles_[i].setBounds(header.removeFromLeft(80));
        load_[i].setBounds(header.removeFromRight(130).reduced(0, 2));
        header.removeFromRight(4);
        suggest_[i].setBounds(header.removeFromRight(90).reduced(0, 2));
        header.removeFromRight(kGap);
        bars_[i].setBounds(header.removeFromRight(110));
        barsLabel_[i].setBounds(header.removeFromRight(60));
        status_[i].setBounds(header);
        area.removeFromTop(4);
        lanes_[i].setBounds(area.removeFromTop(kLaneH));
        area.removeFromTop(kGap);
    }
    cycle_.setBounds(area.removeFromTop(kHeaderH));
    area.removeFromTop(kGap);
    rules_.setBounds(area);
}

} // namespace dc::ui
