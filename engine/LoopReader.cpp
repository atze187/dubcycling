#include "engine/LoopReader.h"
#include <algorithm>
#include <cmath>

namespace dc {

namespace {
// Wrappt nach [0,1), auch für negative Werte; Rundung auf 1.0 ergibt 0.0.
double wrap01(double p)
{
    p -= std::floor(p);
    return p >= 1.0 ? 0.0 : p;
}

// Kürzester Abstand zweier Phasen auf dem Kreis, in [0, 0.5].
double circularDistance(double a, double b)
{
    const double d = std::abs(a - b);
    return std::min(d, 1.0 - d);
}
} // namespace

void LoopReader::prepare(double outSampleRate)
{
    if (std::isfinite(outSampleRate) && outSampleRate > 0.0)
        outRate_ = outSampleRate;
    grainLen_ = std::max(16, static_cast<int>(std::lround(kGrainMs * 0.001 * outRate_)));
    reset();
}

void LoopReader::setData(std::shared_ptr<const StereoData> data)
{
    data_ = std::move(data);
    reset();
}

void LoopReader::reset()
{
    left_.reset();
    right_.reset();
    started_ = false;
}

void LoopReader::render(float* l, float* r, int n, double phase01, double phaseStep01, double step)
{
    if (n <= 0)
        return;
    const StereoData* d = data_.get();
    const size_t frames = d != nullptr ? std::min(d->left.size(), d->right.size()) : 0;
    if (frames == 0 || !std::isfinite(phase01) || !std::isfinite(phaseStep01) || !std::isfinite(step) || !(step > 0.0))
    {
        std::fill(l, l + n, 0.0f);
        std::fill(r, r + n, 0.0f);
        started_ = false; // nach Stille sauber neu einsetzen
        return;
    }

    const double len = static_cast<double>(frames);
    const double phase = wrap01(phase01);

    // Tonhöhe: Die Stücke lesen mit dem Verhältnis der Sampleraten, unabhängig vom Tempo. Das Tempo
    // (step aus sourceStep) steckt allein im Phasenzuwachs, den der Aufrufer vorgibt.
    const double srcRate = std::isfinite(d->sampleRate) && d->sampleRate > 0.0 ? d->sampleRate : outRate_;
    GrainFrame f;
    f.step = srcRate / outRate_;
    f.dir = 1.0;
    f.cycle = true;
    f.lo = 0.0;
    f.hi = len;

    if (!started_)
    {
        left_.start(phase * len, 1.0, grainLen_);
        right_.start(phase * len, 1.0, grainLen_);
        started_ = true;
    }
    else
    {
        // Sprungerkennung: Weicht die gegebene Phase von der Fortschreibung des letzten Blocks um mehr
        // als eine halbe Grain-Länge Spielzeit ab, setzen beide Kanäle sofort neu an (mit Überblendung).
        const double tolerance = std::abs(lastPhaseStep_) * 0.5 * grainLen_ + 1e-9;
        if (circularDistance(phase, expectedPhase_) > tolerance)
        {
            left_.retrigger();
            right_.retrigger();
        }
    }

    for (int i = 0; i < n; ++i)
    {
        const double srcPos = wrap01(phase + i * phaseStep01) * len;
        l[i] = left_.process(d->left, srcPos, grainLen_, f);
        r[i] = right_.process(d->right, srcPos, grainLen_, f);
    }

    expectedPhase_ = wrap01(phase + n * phaseStep01);
    lastPhaseStep_ = phaseStep01;
}

} // namespace dc
