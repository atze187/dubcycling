#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace dc {

// Effect ids; bit (1 << int(insert)) in the per-region switch mask. The chain order is FxSettings::order.
enum class FxInsert { Bitcrush = 0, Filter = 1, Flanger = 2, Reverb = 3, Delay = 4 };
constexpr int kFxInserts = 5;

enum class FxParam
{
    ReverbMix, ReverbSize, ReverbDamping,
    FilterType, FilterCutoff, FilterResonance,
    BitcrushBits, BitcrushRate, BitcrushMix,
    FlangerRate, FlangerDepth, FlangerFeedback, FlangerMix,
    FlangerManual, DelayTime, DelayFeedback, DelayMix, DelayReverse,
    FilterLfoRate, FilterLfoDepth, FilterLfoShape
};
constexpr int kFxParamCount = 21;

// steps: 0 = continuous, otherwise the number of discrete steps (FilterType: 0 LP, 1 HP, 2 BP, 3 Notch, 4 Peak, 5 LP24).
struct FxParamInfo
{
    const char* id;
    FxInsert insert;
    double min, max, def;
    bool logarithmic;
    int steps;
};

const FxParamInfo& fxParamInfo(FxParam p);

// Delay time note values, from 1/32 up to 2 bars, ascending for 4/4 and longer bars (for 2/4 and 3/4 the bar values are
// shorter than some note values). Bar values scale with beatsPerBar.
constexpr int kDelayTimeSteps = 15;
double delayTimeBeats(int step, double beatsPerBar); // out-of-range steps are clamped
const char* delayTimeName(int step);

// Chain order: position -> insert. Always a permutation of all inserts.
using FxOrder = std::array<FxInsert, kFxInserts>;
FxOrder defaultFxOrder();
FxOrder sanitizeFxOrder(const FxOrder& o); // a valid permutation passes, anything else -> default

// NaN/infinity -> default, otherwise clamped to the range; FilterType rounded to 0..5, stepped params to their steps.
float sanitizeFxParam(FxParam p, double value);

struct FxParams
{
    std::array<float, kFxParamCount> v;

    FxParams();
    float get(FxParam p) const { return v[static_cast<size_t>(p)]; }
    void set(FxParam p, double value); // sanitized
    bool operator==(const FxParams& o) const { return v == o.v; }
};

constexpr int kMaxFxRegions = 4096;

struct FxSettings
{
    FxParams params;
    std::vector<std::uint8_t> switches; // mask per Loop-2 region index
    FxOrder order = defaultFxOrder();

    std::uint8_t maskFor(int region) const; // 0 for missing/invalid indices
    void setSwitch(int region, FxInsert i, bool on); // grows; index < 0 or >= kMaxFxRegions is ignored
    bool switchOn(int region, FxInsert i) const;
    void trimTo(int regionCount); // drops surplus entries and trailing zeros
    bool operator==(const FxSettings& o) const { return params == o.params && switches == o.switches && order == o.order; }
};

} // namespace dc
