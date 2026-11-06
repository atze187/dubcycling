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
    // All older grains run out exactly within the new crossfade time. Their weights fall
    // together by 1/fade per sample, the new one rises by 1/fade: the sum stays one, even
    // if a grain that is already fading out started with a shorter crossfade time.
    for (auto& g : grains_)
    {
        if (!g.active)
            continue;
        g.delta = -g.gain / static_cast<float>(fade);
        g.fadingOut = true;
        g.fadeLeft = fade;
    }
    Grain* slot = nullptr;
    for (auto& g : grains_)
        if (!g.active)
        {
            slot = &g;
            break;
        }
    // See the class comment: at most two grains live, so a free slot is guaranteed.
    assert(slot != nullptr && "GrainStretch: both slots busy on launch");
    if (slot == nullptr)
    {
        // Emergency (must not happen): replace the quieter grain.
        ++collisions_;
        slot = grains_[0].gain <= grains_[1].gain ? &grains_[0] : &grains_[1];
    }
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
        if (g.active && g.fadingOut)
            wait = std::max(wait, g.fadeLeft);
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
            // Integer end: a float remainder in the weight cannot keep the grain alive beyond fadeLeft.
            if (--g.fadeLeft <= 0 || g.gain <= 0.0f)
                g.active = false;
        }
        else
        {
            if (g.delta > 0.0f)
                g.gain = std::min(1.0f, g.gain + g.delta);
            if (--g.remaining <= 0)
            {
                // Fade out over a quarter of the grain duration, from the current weight.
                g.fadingOut = true;
                g.delta = -g.gain / static_cast<float>(g.fade);
                g.fadeLeft = g.fade;
            }
        }
    }
    return y;
}

} // namespace dc
