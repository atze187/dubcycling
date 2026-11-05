#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"
#include "plugin/ui/LoopLane.h"
#include "plugin/presets/BankStore.h"
#include "plugin/ui/FxPanel.h"
#include "plugin/ui/FxStrip.h"
#include "plugin/ui/PanelLook.h"
#include "plugin/ui/PresetBar.h"
#include "plugin/ui/RulePanel.h"

namespace dc::ui {

constexpr int kMaxBars = 256;
constexpr int kSuggestMaxBoundaries = 15; // "Vorschlag": at most this many boundaries per lane

// Bars suggestion for a new loop file: suggestBars(length, hostBpm or 120 when unknown, 4 beats).
// 1 when the file cannot be read.
int suggestedBarsFor(const juce::File& file, double hostBpm);

// Bars typed into the field: a number is rounded and clamped to 1..kMaxBars; anything that is
// not a number gives `fallback` (the current value).
int parseBars(const juce::String& text, int fallback);

// What a file dropped on / chosen for a slot does.
enum class LoopFileAction
{
    Reject,              // not loadable: nothing changes
    Relink,              // the slot's file is gone (Missing): loadLoop, keeps regions, rules and bars.
                         // A Failed slot (file there but unreadable) is replaced, not relinked.
    ReplaceAfterConfirm, // replaceLoop would drop regions/rules: ask first
    ReplaceDirect        // replaceLoop, nothing to lose
};

// storedPathExists: the slot's stored file is still there. fileReadable: the new file passes
// LoopLoader::probe. hasRegionsOrRules: the slot has boundaries or there are any rules
// (replaceLoop drops all rules).
LoopFileAction decideLoopFileAction(DubcyclingProcessor::SlotStatus status, bool storedPathExists, bool fileReadable,
                                    bool hasRegionsOrRules);

// Rules are keyed by region index, so adding or removing one boundary of a lane shifts them.
// `slot` 0 = Loop 1 (RuleEntry::region1), 1 = Loop 2 (RuleTarget::region2); `before`/`after` are the
// lane's boundaries before and after the edit. Insert of a boundary splitting old region k: indices
// > k get +1 (k keeps its rule, the new region k+1 starts without one). Removal of the boundary
// between old regions k and k+1: k keeps its rule (the merged region keeps the left rule), k+1 is
// dropped, indices > k+1 get -1. Stale indices (beyond the old region count) shift the same way,
// so they stay stale. A drag (same count), a count change other than one, or a diff that is not a
// single insert/removal returns the rules unchanged.
std::vector<RuleEntry> remapRulesForBoundaryEdit(std::vector<RuleEntry> rules, int slot,
                                                 const std::vector<double>& before,
                                                 const std::vector<double>& after);

// Region names are keyed by region index like the rules, with the same rule: a new boundary splits
// old region k into k and k+1 (k keeps its name, the new region k+1 has none, later names shift
// up); removing the boundary between k and k+1 keeps the left name and drops k+1's. Any other
// change (a drag, several boundaries at once) returns the names unchanged. No trailing empty entries.
std::vector<juce::String> remapLabelsForBoundaryEdit(std::vector<juce::String> labels,
                                                     const std::vector<double>& before,
                                                     const std::vector<double>& after);

// Insert switches are keyed by Loop-2 region index (FxSettings::switches): a new boundary splitting
// old region k gives the new region k+1 the mask of k (later masks shift up), so the split region
// keeps its sound on both halves; removing the boundary between k and k+1 keeps k's mask and drops
// k+1's (later masks shift down). Any other change (a drag, several boundaries at once) returns the
// masks unchanged. No trailing zeros; a list shorter than the split index stays as it is.
std::vector<std::uint8_t> remapFxSwitchesForBoundaryEdit(std::vector<std::uint8_t> masks,
                                                         const std::vector<double>& before,
                                                         const std::vector<double>& after);

// Title and text of the "replace loop?" confirmation (slot 0 = Loop 1).
juce::String replaceConfirmTitle(int slot);
juce::String replaceConfirmMessage();

// Title and text of the "replace the boundaries with the suggestion?" confirmation.
juce::String suggestConfirmTitle(int slot);
juce::String suggestConfirmMessage();

// Edit view: top row with logo, preset bar and BPM (the cycle readout only when it fits);
// Loop 1 lane on top, Loop 2 below, each with bars field, "Load..." button and slot
// status; the insert strip right under the Loop-2 lane; the combined cycle length; at the bottom
// the rule panel for the selected Loop-1 region (left) and the effects panel for the selected
// Loop-2 region (right). A 10 Hz timer polls the processor and refreshes lanes and panels; edits go
// through processor.applyEdit. A switch toggle (strip or panel) is heard at once: the engine
// re-evaluates the sounding voice's mask on the model swap.
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    // presetsRoot: folder of the preset banks (tests pass a temporary one).
    explicit Editor(DubcyclingProcessor& proc, juce::File presetsRoot = BankStore::defaultRoot());
    ~Editor() override;

    // Selected region of Loop 1 (for the rule panel).
    int selectedRegion1() const { return lanes_[0].selectedRegion(); }
    juce::String cycleText() const { return cycle_.getText(); }
    juce::String statusText(int slot) const { return status_[slot == 0 ? 0 : 1].getText(); }
    const LoopLane& lane(int slot) const { return lanes_[slot == 0 ? 0 : 1]; }
    LoopLane& lane(int slot) { return lanes_[slot == 0 ? 0 : 1]; }
    RulePanel& rulePanel() { return rules_; }
    FxStrip& fxStrip() { return fxStrip_; }
    FxPanel& fxPanel() { return fxPanel_; }
    PresetBar& presetBar() { return presetBar_; }
    juce::Slider& barsField(int slot) { return bars_[slot == 0 ? 0 : 1]; }
    // Pulls state, slot status and loop data from the processor (the timer calls it).
    void refresh();

    // A file dropped on a lane or picked with "Load...": rejects, relinks or replaces per
    // decideLoopFileAction. While a confirmation for the slot is open, further files for that
    // slot are ignored. On OK the decision is re-run; a replace only happens if the state still
    // equals the one the user was asked about.
    void loadFile(int slot, const juce::File& file);
    bool isConfirmationPending(int slot) const { return pending_[slot == 0 ? 0 : 1].active; }

    // "Vorschlag": replaces the slot's boundaries with suggestBoundaries(data, kSuggestMaxBoundaries),
    // committed with one applyEdit. Rules are kept (entries for regions that no longer exist show
    // up as stale in the rule panel). No-op unless the slot is Loaded (the button is enabled only
    // then), with no transients found, or while a confirmation for the slot is open. When the slot already has boundaries the user is asked
    // first (same hook as below); the answer only counts if the slot is unchanged since the question.
    void suggestRegions(int slot);
    juce::TextButton& suggestButton(int slot) { return suggest_[slot == 0 ? 0 : 1]; }

    // Asks whether to replace a loop whose regions/rules would be lost (or, for "Vorschlag", whether
    // to overwrite the boundaries) and calls answer(true) to go ahead. Defaults to an async
    // AlertWindow (replaceConfirmTitle/Message or suggestConfirmTitle/Message); tests replace it.
    std::function<void(int slot, std::function<void(bool)> answer)> confirmReplace;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // 60 Hz: playheads and teaser every tick, the full refresh (state, status, rules) every sixth.
    void timerCallback() override
    {
        updatePlayheads();
        if (++tick_ % 6 == 0)
            refresh();
    }
    void updatePlayheads();
    int tick_ = 0;
    // New boundaries from a lane; the rules are remapped (remapRulesForBoundaryEdit) in the same applyEdit.
    void regionsEdited(int slot, const RegionMap& regions);
    // A region name was edited in a lane or in the rule panel; empty text removes the custom name.
    void labelEdited(int slot, int region, const juce::String& text);
    // Shows the rule of the selected Loop-1 region for the current Loop-2 region count.
    void updateRules(const PluginState& st);
    // Replaces the entry for region1 (other entries, stale ones included, stay), then applyEdit.
    void rulesEdited(int region1, std::vector<RuleTarget> targets);
    // Strip and panel show fx for the current Loop-2 regions and the lane's selected region.
    void updateFx(const PluginState& st);
    // One insert switch of a Loop-2 region (strip click or panel switch), then applyEdit.
    void switchEdited(int region2, FxInsert insert, bool on);
    void paramsEdited(const FxParams& params);
    void barsEdited(int slot);
    // confirmedState: serialized state the user confirmed a replace for, or nullptr (not asked yet).
    void handleFile(int slot, const juce::File& file, const juce::MemoryBlock* confirmedState);
    void askReplace(int slot, const juce::File& file, const juce::MemoryBlock& askedState);
    void replace(int slot, const juce::File& file);
    void openChooser(int slot);
    void showNotice(int slot, const juce::String& text);

    PanelLook look_; // declared first: destroyed after every child that uses it
    DubcyclingProcessor& proc_;
    BankStore store_; // declared before presetBar_, which uses it
    PresetBar presetBar_;
    LoopLane lanes_[2];
    juce::Label titles_[2];
    juce::Label status_[2];
    juce::Label barsLabel_[2];
    juce::Slider bars_[2];
    juce::TextButton load_[2];
    juce::TextButton suggest_[2];
    juce::Label logo_;
    juce::Label bpmCaption_;
    juce::Label bpm_;
    juce::Label cycle_;
    juce::Rectangle<int> stripBounds_[2];
    juce::Rectangle<float> ledBounds_[2];
    juce::Colour ledColour_[2];
    RulePanel rules_;
    FxStrip fxStrip_;
    FxPanel fxPanel_;
    juce::TooltipWindow tooltips_ { this };
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String notice_[2];         // short message shown instead of the status (e.g. rejected file)
    juce::uint32 noticeUntil_[2] {}; // Time::getMillisecondCounter() when the notice expires
    std::uint64_t seenFailure_[2] {}; // id of the last replace failure shown (proc.replaceFailure)
    struct PendingConfirm
    {
        bool active = false;
        juce::MemoryBlock state; // serialized PluginState when the question was asked
    };
    PendingConfirm pending_[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};

} // namespace dc::ui
