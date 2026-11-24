#include "plugin/ui/CpuMeter.h"
#include <cmath>
#include "plugin/ui/PanelLook.h"

namespace dc::ui {

namespace {
constexpr double kWarnLoad = 0.5;
constexpr double kDangerLoad = 0.8;
constexpr int kRefreshHz = 4;
} // namespace

CpuMeter::CpuMeter()
{
    panel::styleLcd(*this, panel::kDim);
    setLoad(0.0);
}

CpuMeter::~CpuMeter() { stopTimer(); }

void CpuMeter::setSource(std::function<double()> source)
{
    source_ = std::move(source);
    if (source_)
        startTimerHz(kRefreshHz);
    else
        stopTimer();
}

void CpuMeter::setLoad(double proportion)
{
    if (!std::isfinite(proportion) || proportion < 0.0)
        proportion = 0.0;
    setText("CPU " + juce::String(juce::roundToInt(proportion * 100.0)) + " %", juce::dontSendNotification);
    const auto colour = proportion >= kDangerLoad ? panel::kWarn
                      : proportion >= kWarnLoad   ? panel::kAmber
                                                  : panel::kDim;
    setColour(juce::Label::textColourId, colour);
}

void CpuMeter::timerCallback()
{
    if (source_)
        setLoad(source_());
}

} // namespace dc::ui
