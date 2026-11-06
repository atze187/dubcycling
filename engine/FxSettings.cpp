#include "FxSettings.h"
#include <algorithm>
#include <cmath>

namespace dc {

namespace {
// The only place with ranges and defaults. Order matches enum FxParam.
const FxParamInfo kInfo[kFxParamCount] = {
    { "reverb.mix",         FxInsert::Reverb,   0.0,   1.0,     0.3,    false, 0 },
    { "reverb.size",        FxInsert::Reverb,   0.0,   1.0,     0.5,    false, 0 },
    { "reverb.damping",     FxInsert::Reverb,   0.0,   1.0,     0.5,    false, 0 },
    { "filter.type",        FxInsert::Filter,   0.0,   2.0,     0.0,    false, 3 },
    { "filter.cutoff",      FxInsert::Filter,   20.0,  20000.0, 2000.0, true,  0 },
    { "filter.resonance",   FxInsert::Filter,   0.0,   1.0,     0.2,    false, 0 },
    { "bitcrush.bits",      FxInsert::Bitcrush, 1.0,   16.0,    8.0,    false, 0 },
    { "bitcrush.rate",      FxInsert::Bitcrush, 1.0,   64.0,    4.0,    false, 0 },
    { "bitcrush.mix",       FxInsert::Bitcrush, 0.0,   1.0,     1.0,    false, 0 },
    { "flanger.rate",       FxInsert::Flanger,  0.05,  5.0,     0.3,    false, 0 },
    { "flanger.depth",      FxInsert::Flanger,  0.0,   1.0,     0.5,    false, 0 },
    { "flanger.feedback",   FxInsert::Flanger,  -0.9,  0.9,     0.4,    false, 0 },
    { "flanger.mix",        FxInsert::Flanger,  0.0,   1.0,     0.5,    false, 0 },
};
}

const FxParamInfo& fxParamInfo(FxParam p) { return kInfo[static_cast<size_t>(p)]; }

float sanitizeFxParam(FxParam p, double value)
{
    const auto& info = fxParamInfo(p);
    if (!std::isfinite(value))
        return static_cast<float>(info.def);
    value = std::clamp(value, info.min, info.max);
    if (info.steps > 0)
        value = std::clamp(std::round(value), info.min, info.max);
    return static_cast<float>(value);
}

FxParams::FxParams()
{
    for (int i = 0; i < kFxParamCount; ++i)
        v[static_cast<size_t>(i)] = static_cast<float>(kInfo[i].def);
}

void FxParams::set(FxParam p, double value)
{
    v[static_cast<size_t>(p)] = sanitizeFxParam(p, value);
}

std::uint8_t FxSettings::maskFor(int region) const
{
    if (region < 0 || region >= static_cast<int>(switches.size()))
        return 0;
    return switches[static_cast<size_t>(region)];
}

void FxSettings::setSwitch(int region, FxInsert i, bool on)
{
    if (region < 0 || region >= kMaxFxRegions)
        return;
    if (region >= static_cast<int>(switches.size()))
        switches.resize(static_cast<size_t>(region) + 1, 0);
    const auto bit = static_cast<std::uint8_t>(1u << static_cast<int>(i));
    auto& m = switches[static_cast<size_t>(region)];
    m = on ? static_cast<std::uint8_t>(m | bit) : static_cast<std::uint8_t>(m & ~bit);
}

bool FxSettings::switchOn(int region, FxInsert i) const
{
    return (maskFor(region) >> static_cast<int>(i)) & 1;
}

void FxSettings::trimTo(int regionCount)
{
    const auto n = static_cast<size_t>(std::max(0, regionCount));
    if (switches.size() > n)
        switches.resize(n);
    while (!switches.empty() && switches.back() == 0)
        switches.pop_back();
}

} // namespace dc
