#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace dc {

// Chain order; bit (1 << int(insert)) in the per-region switch mask.
enum class FxInsert { Bitcrush = 0, Filter = 1, Flanger = 2, Reverb = 3 };
constexpr int kFxInserts = 4;

enum class FxParam
{
    ReverbMix, ReverbSize, ReverbDamping,
    FilterType, FilterCutoff, FilterResonance,
    BitcrushBits, BitcrushRate, BitcrushMix,
    FlangerRate, FlangerDepth, FlangerFeedback, FlangerMix
};
constexpr int kFxParamCount = 13;

// steps: 0 = continuous, otherwise the number of discrete steps (FilterType: 0 LP, 1 HP, 2 BP).
struct FxParamInfo
{
    const char* id;
    FxInsert insert;
    double min, max, def;
    bool logarithmic;
    int steps;
};

const FxParamInfo& fxParamInfo(FxParam p);

// NaN/infinity -> default, otherwise clamped to the range; FilterType rounded to 0, 1 or 2.
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

    std::uint8_t maskFor(int region) const; // 0 for missing/invalid indices
    void setSwitch(int region, FxInsert i, bool on); // grows; index < 0 or >= kMaxFxRegions is ignored
    bool switchOn(int region, FxInsert i) const;
    void trimTo(int regionCount); // drops surplus entries and trailing zeros
    bool operator==(const FxSettings& o) const { return params == o.params && switches == o.switches; }
};

} // namespace dc
