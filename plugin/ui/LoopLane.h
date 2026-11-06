#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/RegionMap.h"
#include "engine/StereoData.h"

namespace dc::ui {

struct PeakColumn
{
    float min = 0.0f;
    float max = 0.0f;
};

// Min/max per pixel column of the mean of both channels, clamped to -1..1. Non-finite samples are
// skipped. Empty input or columns <= 0 give an empty vector; channels of different length use the
// shorter one.
std::vector<PeakColumn> computePeaks(const std::vector<float>& left, const std::vector<float>& right, int columns);

// Rounds every boundary to the nearest multiple of `step` (a grid step in phase), drops values that
// land on 0 or 1 or outside, then sorts, spaces and de-duplicates them like sanitizeBoundaries.
// step <= 0 (no grid) returns the input unchanged.
std::vector<double> snapBoundariesToGrid(const std::vector<double>& boundaries, double step);

// True for the file types the lanes and the "Load..." button accept (wav, mp3, flac, aif, aiff).
bool isAcceptedAudioFile(const juce::File& file);

// One loop: waveform, region boundaries as vertical lines, the selected region highlighted, and
// a hint text instead of the waveform while there is no data. Mouse input goes through
// pressAt/dragTo/release/doubleClickAt (local x in pixels), which the tests call directly.
class LoopLane final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    enum class Status { Empty, Loading, Loaded, Missing, Failed };

    LoopLane() = default;

    void setData(std::shared_ptr<const StereoData> data);
    void setStatus(Status s);
    // Takes over the boundaries (ignored while a boundary is being dragged); the selection is
    // clamped into the new region count.
    void setRegions(const RegionMap& regions);
    void setSelectedRegion(int region);
    // Custom region names by index (see LoopRef::labels); missing or empty = the default letter.
    void setLabels(const std::vector<juce::String>& labels);
    // Fired when an inline edit is committed: `text` is sanitized, empty when the user typed nothing
    // or the default letter (= remove the custom name).
    std::function<void(int region, juce::String text)> onLabelEdited;
    // Region whose name is drawn under the point, or -1 (names of very narrow regions are not drawn).
    int labelAt(float x, float y) const;
    // Opens the inline editor over a region's name; false if the lane has no data or no such region.
    // A running edit is committed first. Enter or leaving the field commits, Escape cancels.
    bool beginLabelEdit(int region);
    void endLabelEdit(bool commit);
    bool isEditingLabel() const { return labelEditor_ != nullptr; }
    juce::TextEditor* labelEditor() { return labelEditor_.get(); }
    // The region that is sounding (a Loop-2 voice); -1 = none. Drawn as a tint.
    void setActiveRegion(int region);
    int activeRegion() const { return activeRegion_; }
    // The coming draw, shown before it takes effect: `what` is a region (>= 0, it fades in as a tint
    // with a bar that fills up), kSilence (the lane dims and says so) or kNone; progress runs 0..1 up
    // to the moment it takes effect (values outside are clamped).
    static constexpr int kNone = -1;
    static constexpr int kSilence = -2;
    void setTeaser(int what, double progress);
    int teaser() const { return teaser_; }
    double teaserProgress() const { return teaserProgress_; }
    // Play position as a phase over the whole loop (0..1); anything outside (e.g. -1) hides it.
    // Repaints only the strip the line moved across.
    void setPlayhead(double phase);
    double playhead() const { return playhead_; }

    // Beat grid over the whole loop: `bars` bars of `beatsPerBar` beats. Either <= 0 = no grid.
    void setGrid(int bars, int beatsPerBar);
    // Snap step in phase: one beat while beats are at least 4 px apart, else one bar while bars are,
    // else 0 (no grid: no data, no valid bars/beats, or too fine for the lane width).
    double gridStep() const;
    // Nearest grid point to `phase`, clamped to 0..1; unchanged when gridStep() is 0.
    double snapPhase(double phase) const;
    // What paint draws: beat lines (with stronger bar lines), or only bar lines when beats are too dense.
    bool beatLinesShown() const;
    bool barLinesShown() const;

    const RegionMap& regions() const { return regions_; }
    int selectedRegion() const { return selected_; }
    bool hasData() const { return data_ != nullptr && data_->frames() > 0; }
    bool isDragging() const { return dragging_ >= 0; }
    // Shown instead of the waveform; empty when there is data.
    juce::String hintText() const;

    std::function<void(const RegionMap&)> onRegionsChanged;
    std::function<void(int)> onRegionSelected;
    // A right-click on a region: audition it (selects and moves nothing). Left clicks stay silent.
    std::function<void(int)> onRegionPreview;
    std::function<void(const juce::File&)> onFileDropped;

    // Click: grabs the nearest boundary within the hit tolerance, otherwise selects the region under x.
    void pressAt(float x);
    // Right-click: fires onRegionPreview for the region under x (a boundary counts as the region it lies in).
    void auditionAt(float x);
    // Moves a grabbed boundary (clamped by RegionMap::moveBoundary), keeping the offset between
    // cursor and boundary from the press. Only a local preview: nothing is published while dragging.
    // The new position snaps to the grid (see gridStep) unless `free`; the one-argument form snaps.
    void dragTo(float x) { dragTo(x, false); }
    void dragTo(float x, bool free);
    // Ends a drag; fires onRegionsChanged once if the boundary actually moved.
    void release();
    // On a boundary: removes it; elsewhere: adds a boundary at x, snapped to the grid unless `free`.
    void doubleClickAt(float x) { doubleClickAt(x, false); }
    void doubleClickAt(float x, bool free);
    // Same, but a double-click on a region's name edits the name instead.
    void doubleClickAt(float x, float y) { doubleClickAt(x, y, false); }
    void doubleClickAt(float x, float y, bool free);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            auditionAt(e.position.x);
        else
            pressAt(e.position.x);
    }
    void mouseDrag(const juce::MouseEvent& e) override { dragTo(e.position.x, e.mods.isShiftDown()); }
    void mouseUp(const juce::MouseEvent&) override { release(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        doubleClickAt(e.position.x, e.position.y, e.mods.isShiftDown());
    }
    void mouseMove(const juce::MouseEvent& e) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { setDropHighlight(true); }
    void fileDragExit(const juce::StringArray&) override { setDropHighlight(false); }

private:
    bool interactive() const { return hasData() && getWidth() > 0; }
    double phaseAt(float x) const { return static_cast<double>(x) / getWidth(); }
    float xFor(double phase) const { return static_cast<float>(phase * getWidth()); }
    int boundaryAt(float x) const; // nearest boundary within the hit tolerance, or -1
    double beatPixels() const;     // distance of two beats in pixels, 0 without a grid
    void changed();
    void cancelDrag(); // ends a drag without publishing and restores the regions from the press
    void setDropHighlight(bool on);

    std::shared_ptr<const StereoData> data_;
    Status status_ = Status::Empty;
    RegionMap regions_;
    int selected_ = 0;
    int gridBars_ = 0;
    int gridBeatsPerBar_ = 0;
    int dragging_ = -1;
    float grabOffset_ = 0.0f; // boundary x minus cursor x at the press
    RegionMap dragStart_;     // regions at the press, to detect a real move and to cancel
    juce::Rectangle<float> labelRect(int region) const; // empty when the name is not drawn
    std::vector<juce::String> labels_;
    std::unique_ptr<juce::TextEditor> labelEditor_;
    std::unique_ptr<juce::TextEditor> retiredEditor_; // finished editor, freed later (it may still be on the stack)
    int editingRegion_ = -1;
    bool dropHighlight_ = false;
    double playhead_ = -1.0; // -1 = hidden
    int activeRegion_ = -1;
    int teaser_ = -1;
    double teaserProgress_ = 0.0;
    std::vector<PeakColumn> peaks_;
    const StereoData* peaksData_ = nullptr;
    int peaksWidth_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopLane)
};

} // namespace dc::ui
