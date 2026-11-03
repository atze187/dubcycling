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
    // Play position as a phase over the whole loop (0..1); anything outside (e.g. -1) hides it.
    // Repaints only the strip the line moved across.
    void setPlayhead(double phase);
    double playhead() const { return playhead_; }

    const RegionMap& regions() const { return regions_; }
    int selectedRegion() const { return selected_; }
    bool hasData() const { return data_ != nullptr && data_->frames() > 0; }
    bool isDragging() const { return dragging_ >= 0; }
    // Shown instead of the waveform; empty when there is data.
    juce::String hintText() const;

    std::function<void(const RegionMap&)> onRegionsChanged;
    std::function<void(int)> onRegionSelected;
    std::function<void(const juce::File&)> onFileDropped;

    // Click: grabs the nearest boundary within the hit tolerance, otherwise selects the region under x.
    void pressAt(float x);
    // Moves a grabbed boundary (clamped by RegionMap::moveBoundary), keeping the offset between
    // cursor and boundary from the press. Only a local preview: nothing is published while dragging.
    void dragTo(float x);
    // Ends a drag; fires onRegionsChanged once if the boundary actually moved.
    void release();
    // On a boundary: removes it; elsewhere: adds a boundary at x.
    void doubleClickAt(float x);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override { pressAt(e.position.x); }
    void mouseDrag(const juce::MouseEvent& e) override { dragTo(e.position.x); }
    void mouseUp(const juce::MouseEvent&) override { release(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override { doubleClickAt(e.position.x); }
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
    void changed();
    void cancelDrag(); // ends a drag without publishing and restores the regions from the press
    void setDropHighlight(bool on);

    std::shared_ptr<const StereoData> data_;
    Status status_ = Status::Empty;
    RegionMap regions_;
    int selected_ = 0;
    int dragging_ = -1;
    float grabOffset_ = 0.0f; // boundary x minus cursor x at the press
    RegionMap dragStart_;     // regions at the press, to detect a real move and to cancel
    bool dropHighlight_ = false;
    double playhead_ = -1.0; // -1 = hidden
    std::vector<PeakColumn> peaks_;
    const StereoData* peaksData_ = nullptr;
    int peaksWidth_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopLane)
};

} // namespace dc::ui
