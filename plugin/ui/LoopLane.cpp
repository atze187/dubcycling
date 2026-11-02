#include "plugin/ui/LoopLane.h"
#include <algorithm>
#include <cmath>

namespace dc::ui {

namespace {
constexpr float kHit = 5.0f; // hit tolerance of a boundary, in pixels left and right
constexpr float kCorner = 4.0f;

const juce::Colour kBackground { 0xff202328 };
const juce::Colour kOutline { 0xff3a3f47 };
const juce::Colour kWave { 0xff8ab4d8 };
const juce::Colour kSelected { 0x268ab4d8 };
const juce::Colour kBoundary { 0xffe8c26a };
const juce::Colour kHint { 0xff9aa1ab };
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

juce::String LoopLane::hintText() const
{
    if (hasData())
        return {};
    switch (status_)
    {
        case Status::Loading: return juce::String(juce::CharPointer_UTF8("L\xc3\xa4" "dt\xe2\x80\xa6"));
        case Status::Missing: return "Datei fehlt";
        case Status::Failed:  return "Nicht lesbar";
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

void LoopLane::dragTo(float x)
{
    if (!interactive() || !isDragging())
        return;
    const double before = regions_.boundaries[static_cast<std::size_t>(dragging_)];
    if (regions_.moveBoundary(dragging_, phaseAt(x + grabOffset_))
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

void LoopLane::doubleClickAt(float x)
{
    release();
    if (!interactive())
        return;
    const int b = boundaryAt(x);
    if (b >= 0 ? regions_.removeBoundary(b) : regions_.addBoundary(phaseAt(x)))
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

        const auto [lo, hi] = regions_.bounds(selected_);
        if (regions_.regionCount() > 1)
        {
            g.setColour(kSelected);
            g.fillRect(juce::Rectangle<float>(xFor(lo), 0.0f, xFor(hi) - xFor(lo), bounds.getHeight()));
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
    }

    g.setColour(dropHighlight_ ? kBoundary : kOutline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), kCorner, dropHighlight_ ? 2.0f : 1.0f);
}

} // namespace dc::ui
