#include "plugin/ui/LoopLane.h"
#include <algorithm>
#include <cmath>
#include "plugin/PluginState.h"
#include "plugin/ui/PanelLook.h"
#include "plugin/ui/RulePanel.h"

namespace dc::ui {

namespace {
constexpr float kHit = 5.0f; // hit tolerance of a boundary, in pixels left and right
constexpr float kCorner = 0.0f;
constexpr float kLabelFont = 10.0f;
constexpr float kLabelCharW = 6.2f; // monospace advance at kLabelFont, a little generous
constexpr double kMinGridPx = 4.0;  // grid lines closer than this are not drawn and do not snap

const juce::Colour kBackground { 0xff101215 };
const juce::Colour kOutline { 0xff2a2e35 };
const juce::Colour kWave { 0xff6f94b3 };
const juce::Colour kSelected { 0x245aa9e6 };
const juce::Colour kBoundary { 0xffe8c26a };
const juce::Colour kHint { 0xff7b828d };
const juce::Colour kBeatLine = panel::kEdgeLight; // faint, but visible over the waveform
const juce::Colour kBarLine { 0xff5a626d };         // clearly stronger, still far below the boundaries
} // namespace

std::vector<PeakColumn> computePeaks(const std::vector<float>& left, const std::vector<float>& right, int columns)
{
    std::vector<PeakColumn> out;
    const std::size_t n = std::min(left.size(), right.size());
    if (n == 0 || columns <= 0)
        return out;
    const auto cols = static_cast<std::size_t>(columns);
    out.resize(cols);
    for (std::size_t c = 0; c < cols; ++c)
    {
        const std::size_t from = std::min(n - 1, n * c / cols);
        const std::size_t to = std::max(from + 1, std::min(n, n * (c + 1) / cols));
        float lo = 1.0f, hi = -1.0f;
        bool any = false;
        for (std::size_t i = from; i < to; ++i)
        {
            const float mean = 0.5f * (left[i] + right[i]);
            if (!std::isfinite(mean))
                continue;
            const float v = std::clamp(mean, -1.0f, 1.0f);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            any = true;
        }
        out[c] = any ? PeakColumn { lo, hi } : PeakColumn {};
    }
    return out;
}

std::vector<double> snapBoundariesToGrid(const std::vector<double>& boundaries, double step)
{
    if (!(step > 0.0))
        return boundaries;
    std::vector<double> snapped;
    snapped.reserve(boundaries.size());
    for (double b : boundaries)
    {
        const double s = std::round(b / step) * step;
        if (s > 0.0 && s < 1.0)
            snapped.push_back(s);
    }
    std::sort(snapped.begin(), snapped.end()); // which of two too-close values survives: the left one
    return sanitizeBoundaries(snapped);
}

bool isAcceptedAudioFile(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aif" || ext == ".aiff";
}

void LoopLane::setData(std::shared_ptr<const StereoData> data)
{
    if (data == data_)
        return;
    cancelDrag();
    endLabelEdit(false);
    data_ = std::move(data);
    peaks_.clear();
    peaksData_ = nullptr;
    repaint();
}

void LoopLane::setStatus(Status s)
{
    if (s == status_)
        return;
    status_ = s;
    repaint();
}

void LoopLane::setRegions(const RegionMap& regions)
{
    if (isDragging() || regions.boundaries == regions_.boundaries)
        return;
    endLabelEdit(false); // its region may be gone
    regions_ = regions;
    selected_ = std::clamp(selected_, 0, regions_.regionCount() - 1);
    repaint();
}

void LoopLane::setSelectedRegion(int region)
{
    const int r = std::clamp(region, 0, regions_.regionCount() - 1);
    if (r == selected_)
        return;
    selected_ = r;
    repaint();
}

void LoopLane::setLabels(const std::vector<juce::String>& labels)
{
    if (labels == labels_)
        return;
    labels_ = labels;
    repaint();
}

juce::Rectangle<float> LoopLane::labelRect(int region) const
{
    if (!hasData() || region < 0 || region >= regions_.regionCount())
        return {};
    const auto [lo, hi] = regions_.bounds(region);
    const float width = xFor(hi) - xFor(lo) - 8.0f;
    if (width < 12.0f)
        return {};
    const float wanted = 6.0f + kLabelCharW * static_cast<float>(regionLabel(labels_, region).length());
    return { xFor(lo) + 4.0f, static_cast<float>(getHeight()) - 14.0f, std::min(wanted, width), 12.0f };
}

int LoopLane::labelAt(float x, float y) const
{
    for (int r = 0; r < regions_.regionCount(); ++r)
        if (labelRect(r).contains(x, y))
            return r;
    return -1;
}

bool LoopLane::beginLabelEdit(int region)
{
    endLabelEdit(true);
    if (!interactive() || region < 0 || region >= regions_.regionCount())
        return false;
    editingRegion_ = region;
    labelEditor_ = std::make_unique<juce::TextEditor>();
    auto& ed = *labelEditor_;
    ed.setFont(juce::Font(panel::mono(11.0f)));
    ed.setInputRestrictions(kMaxLabelLength);
    ed.setColour(juce::TextEditor::backgroundColourId, panel::kLcd);
    ed.setColour(juce::TextEditor::textColourId, panel::kAmber);
    ed.setColour(juce::TextEditor::outlineColourId, panel::kAmber);
    ed.setColour(juce::TextEditor::focusedOutlineColourId, panel::kAmber);
    ed.setText(regionLabel(labels_, region), false);
    ed.onReturnKey = [this] { endLabelEdit(true); };
    ed.onEscapeKey = [this] { endLabelEdit(false); };
    ed.onFocusLost = [this] { endLabelEdit(true); };
    const auto [lo, hi] = regions_.bounds(region);
    juce::ignoreUnused(hi);
    const float x = xFor(lo) + 2.0f;
    const int w = std::min(120, std::max(40, getWidth() - static_cast<int>(x)));
    ed.setBounds(juce::roundToInt(x), getHeight() - 20, w, 18);
    addAndMakeVisible(ed);
    ed.grabKeyboardFocus();
    ed.selectAll();
    repaint();
    return true;
}

void LoopLane::endLabelEdit(bool commit)
{
    if (labelEditor_ == nullptr)
        return;
    const int region = editingRegion_;
    // Take it out of the member first: removing a focused child fires its onFocusLost, which must
    // find the lane no longer editing.
    auto ed = std::move(labelEditor_);
    editingRegion_ = -1;
    const auto typed = ed->getText();
    removeChildComponent(ed.get());
    retiredEditor_ = std::move(ed); // may be the caller: freed on the next edit or with the lane
    repaint();
    if (!commit || !onLabelEdited)
        return;
    auto text = sanitizeLabel(typed);
    if (text == regionLetters(region))
        text = {}; // typing the default letter means "no custom name"
    onLabelEdited(region, text);
}

void LoopLane::setActiveRegion(int region)
{
    const int r = region >= 0 && region < regions_.regionCount() ? region : -1;
    if (r == activeRegion_)
        return;
    activeRegion_ = r;
    repaint();
}

void LoopLane::setTeaser(int what, double progress)
{
    if (!std::isfinite(progress))
        progress = 0.0;
    progress = std::clamp(progress, 0.0, 1.0);
    const int w = what >= 0 && what < regions_.regionCount() ? what : (what == kSilence ? kSilence : kNone);
    if (w == teaser_ && std::abs(progress - teaserProgress_) < 0.01)
        return;
    teaser_ = w;
    teaserProgress_ = w == kNone ? 0.0 : progress;
    repaint();
}

void LoopLane::setPlayhead(double phase)
{
    const double p = std::isfinite(phase) && phase >= 0.0 && phase <= 1.0 ? phase : -1.0;
    if (p == playhead_)
        return;
    const auto strip = [this](double ph) {
        return ph < 0.0 ? juce::Rectangle<int>()
                        : juce::Rectangle<int>(juce::roundToInt(xFor(ph)) - 3, 0, 6, getHeight());
    };
    const auto before = strip(playhead_);
    playhead_ = p;
    repaint(before.getUnion(strip(p)));
}

void LoopLane::setGrid(int bars, int beatsPerBar)
{
    if (bars == gridBars_ && beatsPerBar == gridBeatsPerBar_)
        return;
    gridBars_ = bars;
    gridBeatsPerBar_ = beatsPerBar;
    repaint();
}

double LoopLane::beatPixels() const
{
    if (!hasData() || gridBars_ <= 0 || gridBeatsPerBar_ <= 0 || getWidth() <= 0)
        return 0.0;
    return static_cast<double>(getWidth()) / (static_cast<double>(gridBars_) * gridBeatsPerBar_);
}

bool LoopLane::beatLinesShown() const { return beatPixels() >= kMinGridPx; }

bool LoopLane::barLinesShown() const
{
    const double beat = beatPixels();
    return beat > 0.0 && beat * gridBeatsPerBar_ >= kMinGridPx;
}

double LoopLane::gridStep() const
{
    if (beatLinesShown())
        return 1.0 / (static_cast<double>(gridBars_) * gridBeatsPerBar_);
    if (barLinesShown()) // beats too dense to draw: snap to what is drawn
        return 1.0 / gridBars_;
    return 0.0;
}

double LoopLane::snapPhase(double phase) const
{
    const double step = gridStep();
    if (step <= 0.0 || !std::isfinite(phase))
        return phase;
    return std::clamp(std::round(phase / step) * step, 0.0, 1.0);
}

juce::String LoopLane::hintText() const
{
    if (hasData())
        return {};
    switch (status_)
    {
        case Status::Loading: return juce::String(juce::CharPointer_UTF8("Loading\xe2\x80\xa6"));
        case Status::Missing: return "File missing";
        case Status::Failed:  return "Unreadable";
        case Status::Empty:
        case Status::Loaded:  break;
    }
    return "Drop an audio file here";
}

int LoopLane::boundaryAt(float x) const
{
    int best = -1;
    float bestDist = kHit;
    for (int i = 0; i < static_cast<int>(regions_.boundaries.size()); ++i)
    {
        const float d = std::abs(xFor(regions_.boundaries[static_cast<std::size_t>(i)]) - x);
        if (d <= bestDist)
        {
            best = i;
            bestDist = d;
        }
    }
    return best;
}

void LoopLane::changed()
{
    selected_ = std::clamp(selected_, 0, regions_.regionCount() - 1);
    repaint();
    if (onRegionsChanged)
        onRegionsChanged(regions_);
}

void LoopLane::pressAt(float x)
{
    release();
    if (!interactive())
        return;
    dragging_ = boundaryAt(x);
    if (isDragging())
    {
        grabOffset_ = xFor(regions_.boundaries[static_cast<std::size_t>(dragging_)]) - x;
        dragStart_ = regions_;
        return;
    }
    selected_ = regions_.regionAt(std::clamp(phaseAt(x), 0.0, std::nextafter(1.0, 0.0)));
    repaint();
    if (onRegionSelected)
        onRegionSelected(selected_);
}

void LoopLane::auditionAt(float x)
{
    if (!interactive() || !onRegionPreview)
        return;
    onRegionPreview(regions_.regionAt(std::clamp(phaseAt(x), 0.0, std::nextafter(1.0, 0.0))));
}

void LoopLane::dragTo(float x, bool free)
{
    if (!interactive() || !isDragging())
        return;
    const double before = regions_.boundaries[static_cast<std::size_t>(dragging_)];
    const double target = phaseAt(x + grabOffset_);
    if (regions_.moveBoundary(dragging_, free ? target : snapPhase(target))
        && regions_.boundaries[static_cast<std::size_t>(dragging_)] != before)
        repaint();
}

void LoopLane::release()
{
    if (!isDragging())
        return;
    dragging_ = -1;
    if (regions_.boundaries != dragStart_.boundaries)
        changed();
}

void LoopLane::cancelDrag()
{
    if (!isDragging())
        return;
    dragging_ = -1;
    regions_ = dragStart_;
    repaint();
}

void LoopLane::doubleClickAt(float x, float y, bool free)
{
    if (const int r = labelAt(x, y); r >= 0)
    {
        release();
        beginLabelEdit(r);
        return;
    }
    doubleClickAt(x, free);
}

void LoopLane::doubleClickAt(float x, bool free)
{
    release();
    if (!interactive())
        return;
    const int b = boundaryAt(x);
    if (b >= 0 ? regions_.removeBoundary(b) : regions_.addBoundary(free ? phaseAt(x) : snapPhase(phaseAt(x))))
        changed();
}

void LoopLane::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(interactive() && boundaryAt(e.position.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                                                                  : juce::MouseCursor::NormalCursor);
}

bool LoopLane::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isAcceptedAudioFile(juce::File(f)))
            return true;
    return false;
}

void LoopLane::filesDropped(const juce::StringArray& files, int, int)
{
    setDropHighlight(false);
    for (const auto& f : files)
    {
        const juce::File file(f);
        if (isAcceptedAudioFile(file))
        {
            if (onFileDropped)
                onFileDropped(file);
            return;
        }
    }
}

void LoopLane::setDropHighlight(bool on)
{
    if (on == dropHighlight_)
        return;
    dropHighlight_ = on;
    repaint();
}

void LoopLane::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(kBackground);
    g.fillRoundedRectangle(bounds, kCorner);

    if (!hasData())
    {
        g.setColour(kHint);
        g.setFont(juce::FontOptions(15.0f));
        g.drawText(hintText(), getLocalBounds(), juce::Justification::centred);
    }
    else
    {
        const int w = getWidth();
        if (peaksData_ != data_.get() || peaksWidth_ != w)
        {
            peaks_ = computePeaks(data_->left, data_->right, w);
            peaksData_ = data_.get();
            peaksWidth_ = w;
        }

        // Beat grid behind everything else: faint beats, stronger bar starts (the ends are the loop edges).
        if (barLinesShown())
        {
            const int perBar = beatLinesShown() ? gridBeatsPerBar_ : 1;
            const int lines = gridBars_ * perBar;
            for (int i = 1; i < lines; ++i)
            {
                g.setColour(i % perBar == 0 ? kBarLine : kBeatLine);
                g.fillRect(xFor(static_cast<double>(i) / lines) - 0.5f, 0.0f, 1.0f, bounds.getHeight());
            }
        }

        const auto [lo, hi] = regions_.bounds(selected_);
        // Also for an unsplit loop: its single region spans the whole lane.
        g.setColour(kSelected);
        g.fillRect(juce::Rectangle<float>(xFor(lo), 0.0f, xFor(hi) - xFor(lo), bounds.getHeight()));

        // What plays now, and what is about to: the tint of the coming region builds up before it starts.
        const auto tint = [&](int region, float alpha) {
            const auto [from, to] = regions_.bounds(region);
            g.setColour(panel::kGreen.withAlpha(alpha));
            g.fillRect(juce::Rectangle<float>(xFor(from), 0.0f, xFor(to) - xFor(from), bounds.getHeight()));
        };
        if (activeRegion_ >= 0)
            tint(activeRegion_, 0.20f);
        const float teaserP = static_cast<float>(teaserProgress_);
        if (teaser_ >= 0 && teaser_ != activeRegion_)
        {
            tint(teaser_, 0.20f * teaserP);
            const auto [from, to] = regions_.bounds(teaser_);
            g.setColour(panel::kGreen.withAlpha(0.35f + 0.65f * teaserP));
            g.fillRect(xFor(from), bounds.getHeight() - 3.0f, (xFor(to) - xFor(from)) * teaserP, 3.0f);
        }
        else if (teaser_ == kSilence)
        {
            g.setColour(panel::kWindow.withAlpha(0.45f * teaserP));
            g.fillRect(bounds);
            g.setColour(kHint.withAlpha(teaserP));
            g.setFont(panel::mono(10.0f));
            g.drawText("SILENCE", bounds.withTrimmedBottom(bounds.getHeight() * 0.5f), juce::Justification::centred);
        }

        const float mid = bounds.getCentreY();
        const float half = bounds.getHeight() * 0.45f;
        g.setColour(kWave);
        for (int x = 0; x < static_cast<int>(peaks_.size()); ++x)
        {
            const auto& p = peaks_[static_cast<std::size_t>(x)];
            const float top = mid - p.max * half;
            const float bottom = mid - p.min * half;
            g.fillRect(static_cast<float>(x), top, 1.0f, std::max(1.0f, bottom - top));
        }

        g.setColour(kBoundary);
        for (double b : regions_.boundaries)
            g.fillRect(xFor(b) - 1.0f, 0.0f, 2.0f, bounds.getHeight());
        // Start and end of the loop, so an unsplit loop reads as a region too (not draggable).
        g.fillRect(0.0f, 0.0f, 3.0f, bounds.getHeight()); // 1 px is covered by the outline
        g.fillRect(bounds.getWidth() - 3.0f, 0.0f, 3.0f, bounds.getHeight());

        g.setFont(panel::mono(kLabelFont));
        for (int r = 0; r < regions_.regionCount(); ++r)
        {
            const auto rect = labelRect(r);
            if (rect.isEmpty() || r == editingRegion_)
                continue;
            g.setColour(r == selected_ ? kBoundary : kHint);
            g.drawText(regionLabel(labels_, r), rect, juce::Justification::centredLeft);
        }
    }

    if (hasData() && playhead_ >= 0.0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillRect(xFor(playhead_) - 0.5f, 0.0f, 1.0f, bounds.getHeight());
    }

    g.setColour(dropHighlight_ ? kBoundary : kOutline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), kCorner, dropHighlight_ ? 2.0f : 1.0f);
}

} // namespace dc::ui
