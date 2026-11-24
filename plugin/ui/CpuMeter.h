#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace dc::ui {

// LCD readout of the share of the real-time budget of one audio block that processBlock uses.
// Polls its source a few times per second so the number does not flicker.
class CpuMeter final : public juce::Label, private juce::Timer
{
public:
    CpuMeter();
    ~CpuMeter() override;

    // Source returns the smoothed load as a proportion (1 = 100 %). Empty stops polling.
    void setSource(std::function<double()> source);
    // Shows a load directly (negative or non-finite counts as 0).
    void setLoad(double proportion);

private:
    void timerCallback() override;

    std::function<double()> source_;
};

} // namespace dc::ui
