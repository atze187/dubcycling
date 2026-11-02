#include "engine/GrainStretch.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include "engine/SampleInterp.h"

namespace dc {

namespace {
float readGrain(const std::vector<float>& x, double pos, const GrainFrame& f)
{
    const double span = f.hi - f.lo;
    if (f.cycle && span > 0.0)
    {
        pos = f.lo + std::fmod(pos - f.lo, span);
        if (pos < f.lo)
            pos += span;
    }
    else
        pos = std::clamp(pos, f.lo, std::max(f.lo, f.hi - 1.0));
    return cubicAt(x, pos);
}
} // namespace

void GrainStretch::reset()
{
    for (auto& g : grains_)
        g.active = false;
    untilNext_ = 0;
}

int GrainStretch::activeGrains() const
{
    return static_cast<int>(std::count_if(grains_.begin(), grains_.end(), [](const Grain& g) { return g.active; }));
}

void GrainStretch::launch(double srcPos, double dir, int grainLen, bool fadeIn)
{
    const int fade = std::max(1, grainLen / 4);
    // Alle älteren Stücke laufen genau innerhalb der neuen Überblendzeit aus. Ihre Gewichte fallen
    // dann zusammen mit 1/fade pro Sample, das neue steigt mit 1/fade: Die Summe bleibt eins, auch
    // wenn ein schon ausblendendes Stück mit kürzerer Überblendzeit begonnen hat.
    for (auto& g : grains_)
    {
        if (!g.active)
            continue;
        g.delta = -g.gain / static_cast<float>(fade);
        g.fadingOut = true;
    }
    Grain* slot = nullptr;
    for (auto& g : grains_)
        if (!g.active)
        {
            slot = &g;
            break;
        }
    // Siehe Klassenkommentar: Es leben höchstens zwei Stücke, ein freier Platz ist also garantiert.
    assert(slot != nullptr && "GrainStretch: both slots busy on launch");
    if (slot == nullptr)
        slot = &grains_[0];
    slot->active = true;
    slot->fadingOut = false;
    slot->pos = srcPos;
    slot->dir = dir;
    slot->gain = fadeIn ? 0.0f : 1.0f;
    slot->delta = fadeIn ? 1.0f / static_cast<float>(fade) : 0.0f;
    slot->remaining = grainLen;
    slot->fade = fade;
    untilNext_ = grainLen;
}

void GrainStretch::start(double srcPos, double dir, int grainLen)
{
    reset();
    launch(srcPos, dir, grainLen, false);
}

void GrainStretch::retrigger()
{
    int wait = 0;
    for (const auto& g : grains_)
        if (g.active && g.fadingOut && g.delta < 0.0f)
            wait = std::max(wait, static_cast<int>(std::ceil(g.gain / -g.delta)));
    untilNext_ = std::min(untilNext_, wait);
}

float GrainStretch::process(const std::vector<float>& x, double srcPos, int grainLen, const GrainFrame& f)
{
    if (untilNext_ <= 0)
        launch(srcPos, f.dir, grainLen, true);
    --untilNext_;

    float y = 0.0f;
    for (auto& g : grains_)
    {
        if (!g.active)
            continue;
        y += g.gain * readGrain(x, g.pos, f);
        g.pos += g.dir * f.step;
        if (g.fadingOut)
        {
            g.gain += g.delta;
            if (g.gain <= 0.0f)
                g.active = false;
        }
        else
        {
            if (g.delta > 0.0f)
                g.gain = std::min(1.0f, g.gain + g.delta);
            if (--g.remaining <= 0)
            {
                // Ausblenden über ein Viertel der Stückdauer, ab dem aktuellen Gewicht.
                g.fadingOut = true;
                g.delta = -g.gain / static_cast<float>(g.fade);
            }
        }
    }
    return y;
}

} // namespace dc
