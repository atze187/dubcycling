#include "engine/TransientDetector.h"
#include <algorithm>
#include <cmath>
#include "engine/RegionMap.h"

namespace dc {

namespace {
constexpr double kHopSeconds = 0.002;       // envelope step
constexpr int kWindowHops = 3;              // energy window = 3 hops (6 ms)
constexpr double kMinOnsetDistance = 0.040; // seconds between two suggestions
constexpr double kEdge = 0.01;              // no suggestions in the first/last 1 %
constexpr double kFloorPower = 1e-6;        // log floor relative to the loudest window (-60 dB)
constexpr double kAbsThreshold = 1.0;       // minimum energy rise in ln units (~4.3 dB)
constexpr double kMeanFactor = 2.0;         // adaptive part: factor * local mean of the onset strength
constexpr double kLocalSeconds = 0.25;      // half width of the local mean
constexpr double kAttackFraction = 0.1;     // attack starts where |x| first reaches 10 % of the local peak

struct Candidate
{
    double strength;
    long long pos; // sample index of the attack start
};
} // namespace

std::vector<double> suggestBoundaries(const StereoData& data, int maxBoundaries)
{
    std::vector<double> result;
    const size_t n = std::min(data.left.size(), data.right.size());
    const double sr = data.sampleRate;
    if (maxBoundaries <= 0 || !std::isfinite(sr) || sr <= 0.0)
        return result;
    const long long hop = std::max(1LL, std::llround(sr * kHopSeconds));
    const long long win = hop * kWindowHops;
    if (n < static_cast<size_t>(win * 4))
        return result;

    std::vector<float> mono(n);
    for (size_t i = 0; i < n; ++i)
    {
        const float l = std::isfinite(data.left[i]) ? data.left[i] : 0.0f;
        const float r = std::isfinite(data.right[i]) ? data.right[i] : 0.0f;
        mono[i] = 0.5f * (l + r);
    }

    std::vector<double> cum(n + 1, 0.0);
    for (size_t i = 0; i < n; ++i)
        cum[i + 1] = cum[i] + static_cast<double>(mono[i]) * mono[i];

    const long long frames = (static_cast<long long>(n) - win) / hop + 1;
    std::vector<double> energy(static_cast<size_t>(frames));
    double maxE = 0.0;
    for (long long k = 0; k < frames; ++k)
    {
        const double e = (cum[static_cast<size_t>(k * hop + win)] - cum[static_cast<size_t>(k * hop)])
                         / static_cast<double>(win);
        energy[static_cast<size_t>(k)] = std::max(0.0, e);
        maxE = std::max(maxE, energy[static_cast<size_t>(k)]);
    }
    if (!(maxE > 0.0) || !std::isfinite(maxE))
        return result;

    const double eps = maxE * kFloorPower;
    std::vector<double> logE(energy.size());
    for (size_t k = 0; k < energy.size(); ++k)
        logE[k] = std::log(energy[k] + eps);

    // Rise against the disjoint window just before (3 hops back). The first kWindowHops frames have
    // no such window; comparing them with the log floor would give a huge strength to whatever
    // sound starts the loop and inflate the local mean for the next kLocalSeconds, so they get 0
    // (they lie in the edge zone anyway).
    std::vector<double> onset(energy.size(), 0.0);
    for (long long k = kWindowHops; k < frames; ++k)
    {
        const double prev = logE[static_cast<size_t>(k - kWindowHops)];
        onset[static_cast<size_t>(k)] = std::max(0.0, logE[static_cast<size_t>(k)] - prev);
    }
    std::vector<double> onsetCum(onset.size() + 1, 0.0);
    for (size_t k = 0; k < onset.size(); ++k)
        onsetCum[k + 1] = onsetCum[k] + onset[k];

    const long long radius = std::max(1LL, std::llround(kLocalSeconds / kHopSeconds));
    const long long lo = std::max(1LL, std::llround(kEdge * static_cast<double>(n))); // never 0 (tiny n)
    const long long hi = static_cast<long long>(n) - lo;

    std::vector<Candidate> cands;
    for (long long k = 0; k < frames; ++k)
    {
        const double o = onset[static_cast<size_t>(k)];
        if (o <= 0.0)
            continue;
        const long long a = std::max(0LL, k - radius);
        const long long b = std::min(frames, k + radius + 1);
        const double mean = (onsetCum[static_cast<size_t>(b)] - onsetCum[static_cast<size_t>(a)])
                            / static_cast<double>(b - a);
        if (o <= kAbsThreshold + kMeanFactor * mean)
            continue;
        bool isPeak = true;
        for (long long j = std::max(0LL, k - kWindowHops); j <= std::min(frames - 1, k + kWindowHops) && isPeak; ++j)
        {
            const double oj = onset[static_cast<size_t>(j)];
            if (j < k ? oj >= o : (j > k && oj > o))
                isPeak = false;
        }
        if (!isPeak)
            continue;

        // Sample-accurate attack start inside the neighbourhood of the frame.
        const long long s0 = std::max(0LL, (k - kWindowHops) * hop);
        const long long s1 = std::min(static_cast<long long>(n), k * hop + 2 * win);
        float peak = 0.0f;
        for (long long i = s0; i < s1; ++i)
            peak = std::max(peak, std::fabs(mono[static_cast<size_t>(i)]));
        long long pos = k * hop;
        const float thr = static_cast<float>(kAttackFraction) * peak;
        for (long long i = s0; i < s1; ++i)
            if (std::fabs(mono[static_cast<size_t>(i)]) >= thr)
            {
                pos = i;
                break;
            }
        if (pos < lo || pos > hi)
            continue;
        cands.push_back({ o, pos });
    }

    // Strongest first (position breaks ties), keeping a minimum distance.
    std::sort(cands.begin(), cands.end(), [](const Candidate& x, const Candidate& y) {
        return x.strength != y.strength ? x.strength > y.strength : x.pos < y.pos;
    });
    const long long minDist = std::max(std::llround(kMinOnsetDistance * sr),
                                       static_cast<long long>(std::ceil(kMinRegion * static_cast<double>(n))) + 1);
    std::vector<long long> chosen;
    for (const auto& c : cands)
    {
        if (static_cast<int>(chosen.size()) >= maxBoundaries)
            break;
        bool ok = true;
        for (long long p : chosen)
            if (std::llabs(p - c.pos) < minDist)
            {
                ok = false;
                break;
            }
        if (ok)
            chosen.push_back(c.pos);
    }
    std::sort(chosen.begin(), chosen.end());
    for (long long p : chosen)
        result.push_back(static_cast<double>(p) / static_cast<double>(n));
    return result;
}

} // namespace dc
