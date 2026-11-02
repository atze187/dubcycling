#include "engine/Sequencer.h"
namespace dc {

Sequencer::Sequencer()
{
    std::random_device rd;
    rng.seed(rd());
}

void Sequencer::setRandom(std::function<float()> uniform01)
{
    if (uniform01)
        random = std::move(uniform01);
}

float Sequencer::nextUniform()
{
    const float u = random ? random() : dist(rng);
    // uniform_real_distribution<float> may return exactly 1.0f (rounding); draw() wants [0,1).
    constexpr float kBelowOne = 0x1.fffffep-1f; // std::nextafter(1.0f, 0.0f), as a constant (no static init)
    return u >= 1.0f ? kBelowOne : u; // NaN and negatives pass through (draw() treats them as silence)
}

Sequencer::Command Sequencer::onRegion1(int region1, const RuleSet& rules)
{
    Command c;
    if (rules.targets(region1).empty())
        return c; // no rule: not a draw (a running one-shot plays on; Gate ends via shouldGateStop)
    const auto target = rules.draw(region1, nextUniform());
    if (!target)
    {
        reset();
        c.type = Command::Type::Stop;
        return c;
    }
    isActive = true;
    activeRegion2 = target->region2;
    activeMode = target->mode;
    triggerRegion1 = region1;
    c.type = Command::Type::Start;
    c.region2 = activeRegion2;
    c.mode = activeMode;
    return c;
}

bool Sequencer::shouldGateStop(int region1Now) const
{
    return isActive && activeMode == PlayMode::Gate && region1Now != triggerRegion1;
}

void Sequencer::onVoiceEnded() { reset(); }

void Sequencer::reset()
{
    isActive = false;
    activeRegion2 = -1;
    triggerRegion1 = -1;
}

} // namespace dc
