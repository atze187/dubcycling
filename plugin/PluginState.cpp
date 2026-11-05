#include "plugin/PluginState.h"
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>
#include <juce_cryptography/juce_cryptography.h>
#include "engine/RegionMap.h"

namespace dc {

namespace {

juce::String num(double v) { return juce::String(v, 17); }

juce::String joinDoubles(const std::vector<double>& v)
{
    juce::StringArray parts;
    for (double d : v)
        parts.add(num(d));
    return parts.joinIntoString(",");
}

std::vector<double> splitDoubles(const juce::String& s)
{
    std::vector<double> out;
    for (auto& p : juce::StringArray::fromTokens(s, ",", ""))
        if (p.trim().isNotEmpty())
            out.push_back(p.getDoubleValue()); // garbage -> 0.0, dropped by the sanitizer
    return out;
}

juce::ValueTree loopToTree(const juce::String& type, const LoopRef& l)
{
    juce::ValueTree t(type);
    t.setProperty("path", l.path, nullptr);
    t.setProperty("fileName", l.fileName, nullptr);
    t.setProperty("hash", l.hash, nullptr);
    t.setProperty("bars", l.bars, nullptr);
    t.setProperty("boundaries", joinDoubles(l.boundaries), nullptr);
    juce::ValueTree labels("Labels");
    for (std::size_t i = 0; i < l.labels.size() && i < static_cast<std::size_t>(kMaxRegion1); ++i)
        if (const auto text = sanitizeLabel(l.labels[i]); text.isNotEmpty())
        {
            juce::ValueTree n("Label");
            n.setProperty("region", static_cast<int>(i), nullptr);
            n.setProperty("text", text, nullptr);
            labels.appendChild(n, nullptr);
        }
    if (labels.getNumChildren() > 0)
        t.appendChild(labels, nullptr);
    return t;
}

LoopRef loopFromTree(const juce::ValueTree& t)
{
    LoopRef l;
    l.path = t.getProperty("path").toString();
    l.fileName = t.getProperty("fileName").toString();
    l.hash = t.getProperty("hash").toString();
    l.bars = std::clamp(static_cast<int>(t.getProperty("bars", 1)), 1, 1024);
    l.boundaries = sanitizeBoundaries(splitDoubles(t.getProperty("boundaries").toString()));
    int kept = 0;
    for (auto n : t.getChildWithName("Labels"))
        if (!addLabelEntry(l, kept, n.getProperty("region", -1), n.getProperty("text").toString()))
            break;
    return l;
}


// Garbage text -> NaN (sanitizeFxParam turns that into the default, as for NaN/inf). Locale
// independent: num() always writes '.', whatever decimal point a host set with setlocale.
double parseFxNumber(const juce::String& text)
{
    // A classic-locale stream (std::from_chars for double is missing in Apple libc++ before macOS 13.3).
    const auto t = text.trim().toStdString();
    if (t.empty())
        return std::nan("");
    std::istringstream in(t);
    in.imbue(std::locale::classic());
    in >> std::noskipws;
    double v = 0.0;
    in >> v; // accepts an optional sign; out of range, "inf" or "nan" text set failbit
    if (in.fail() || in.peek() != std::char_traits<char>::eof())
        return std::nan(""); // garbage, out of range or trailing characters
    return v;
}

juce::ValueTree fxToTree(const FxSettings& fx)
{
    juce::ValueTree t("Fx");
    for (int p = 0; p < kFxParamCount; ++p)
    {
        const auto param = static_cast<FxParam>(p);
        if (fx.params.get(param) != sanitizeFxParam(param, fxParamInfo(param).def))
            t.setProperty(fxParamInfo(param).id, num(static_cast<double>(fx.params.get(param))), nullptr);
    }
    juce::ValueTree switches("Switches");
    for (std::size_t i = 0; i < fx.switches.size() && i < static_cast<std::size_t>(kMaxFxRegions); ++i)
        if (fx.switches[i] != 0)
        {
            juce::ValueTree n("Switch");
            n.setProperty("region", static_cast<int>(i), nullptr);
            n.setProperty("mask", static_cast<int>(fx.switches[i]), nullptr);
            switches.appendChild(n, nullptr);
        }
    if (switches.getNumChildren() > 0)
        t.appendChild(switches, nullptr);
    return t;
}

FxSettings fxFromTree(const juce::ValueTree& t)
{
    FxSettings fx;
    for (int p = 0; p < kFxParamCount; ++p)
    {
        const auto param = static_cast<FxParam>(p);
        if (t.hasProperty(fxParamInfo(param).id))
            fx.params.set(param, parseFxNumber(t.getProperty(fxParamInfo(param).id).toString()));
    }
    int kept = 0;
    for (auto n : t.getChildWithName("Switches"))
    {
        if (kept++ >= kMaxFxRegions)
            break;
        const int region = n.getProperty("region", -1);
        if (region < 0 || region >= kMaxFxRegions)
            continue;
        if (fx.switches.size() <= static_cast<std::size_t>(region))
            fx.switches.resize(static_cast<std::size_t>(region) + 1, 0);
        fx.switches[static_cast<std::size_t>(region)] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(n.getProperty("mask", 0)), 0, 15));
    }
    return fx;
}

} // namespace

std::vector<double> sanitizeBoundaries(const std::vector<double>& raw)
{
    RegionMap map;
    for (double b : raw)
        map.addBoundary(b);
    return map.boundaries;
}

bool addLabelEntry(LoopRef& loop, int& kept, int region, const juce::String& text)
{
    if (kept >= kMaxLabelEntries)
        return false;
    const auto clean = sanitizeLabel(text);
    if (region < 0 || region >= kMaxRegion1 || clean.isEmpty())
        return true;
    if (loop.labels.size() <= static_cast<std::size_t>(region))
        loop.labels.resize(static_cast<std::size_t>(region) + 1);
    loop.labels[static_cast<std::size_t>(region)] = clean;
    ++kept;
    return true;
}

bool addRuleTarget(std::vector<RuleTarget>& targets, const RuleTarget& t)
{
    if (targets.size() >= static_cast<std::size_t>(kMaxRuleTargets))
        return false;
    if (t.region2 >= 0)
        targets.push_back(t);
    return true;
}

std::optional<RuleEntry> sanitizeRuleEntry(int region1, std::vector<RuleTarget> targets, bool hadTargets)
{
    if (region1 < 0 || region1 >= kMaxRegion1)
        return std::nullopt; // RuleSet ignores such an entry anyway
    RuleSet tmp; // sanitizes weights
    tmp.setTargets(region1, std::move(targets));
    auto sanitized = tmp.targets(region1);
    if (sanitized.empty() && !hadTargets)
        return std::nullopt;
    return RuleEntry { region1, std::move(sanitized) };
}

juce::String sanitizeLabel(const juce::String& text)
{
    return sanitizeName(text, kMaxLabelLength);
}

juce::String sanitizeName(const juce::String& text, int maxLength)
{
    juce::String out;
    for (auto p = text.getCharPointer(); !p.isEmpty(); ++p)
        if (const auto c = *p; c >= 32 && c != 127)
            out += juce::String::charToString(c);
    return out.trim().substring(0, maxLength).trim();
}

FxSettings sanitizeFx(FxSettings fx, int loop2RegionCount)
{
    for (int p = 0; p < kFxParamCount; ++p)
    {
        const auto param = static_cast<FxParam>(p);
        fx.params.set(param, fx.params.get(param)); // sanitized by set
    }
    if (fx.switches.size() > static_cast<std::size_t>(kMaxFxRegions))
        fx.switches.resize(static_cast<std::size_t>(kMaxFxRegions));
    for (auto& m : fx.switches)
        m &= 15;
    fx.trimTo(loop2RegionCount);
    return fx;
}

juce::MemoryBlock serialize(const PluginState& s)
{
    juce::ValueTree root("DubcyclingState");
    root.setProperty("version", s.version, nullptr);
    if (s.activeBank.isNotEmpty())
        root.setProperty("activeBank", s.activeBank, nullptr);
    if (s.activePreset.isNotEmpty())
        root.setProperty("activePreset", s.activePreset, nullptr);
    root.appendChild(loopToTree("Loop1", s.loop1), nullptr);
    root.appendChild(loopToTree("Loop2", s.loop2), nullptr);
    juce::ValueTree rules("Rules");
    for (auto& e : s.rules)
    {
        juce::ValueTree r("Rule");
        r.setProperty("region1", e.region1, nullptr);
        for (auto& t : e.targets)
        {
            juce::ValueTree tt("Target");
            tt.setProperty("region2", t.region2, nullptr);
            tt.setProperty("weight", num(static_cast<double>(t.weight)), nullptr);
            tt.setProperty("mode", t.mode == PlayMode::OneShot ? 1 : 0, nullptr);
            r.appendChild(tt, nullptr);
        }
        rules.appendChild(r, nullptr);
    }
    root.appendChild(rules, nullptr);
    if (s.fx != FxSettings {})
        root.appendChild(fxToTree(s.fx), nullptr);

    juce::MemoryBlock block;
    if (auto xml = root.createXml())
    {
        const auto text = xml->toString(juce::XmlElement::TextFormat().singleLine());
        block.append(text.toRawUTF8(), text.getNumBytesAsUTF8());
    }
    return block;
}

std::optional<PluginState> deserialize(const void* data, std::size_t size)
{
    if (data == nullptr || size == 0)
        return std::nullopt;
    try
    {
        const auto text = juce::String::fromUTF8(static_cast<const char*>(data), static_cast<int>(size));
        auto xml = juce::XmlDocument::parse(text);
        if (xml == nullptr)
            return std::nullopt;
        const auto root = juce::ValueTree::fromXml(*xml);
        if (!root.isValid() || root.getType().toString() != "DubcyclingState"
            || !root.hasProperty("version") || static_cast<int>(root.getProperty("version")) != kStateVersion)
            return std::nullopt;

        PluginState s;
        s.activeBank = sanitizeName(root.getProperty("activeBank").toString(), kMaxNameStoredLength);
        s.activePreset = sanitizeName(root.getProperty("activePreset").toString(), kMaxNameStoredLength);
        s.loop1 = loopFromTree(root.getChildWithName("Loop1"));
        s.loop2 = loopFromTree(root.getChildWithName("Loop2"));
        for (auto r : root.getChildWithName("Rules"))
        {
            if (s.rules.size() >= static_cast<std::size_t>(kMaxRuleEntries))
                break; // the rest is dropped
            std::vector<RuleTarget> targets;
            for (auto tt : r)
            {
                RuleTarget t;
                t.region2 = tt.getProperty("region2", 0);
                t.weight = static_cast<float>(tt.getProperty("weight").toString().getDoubleValue());
                t.mode = static_cast<int>(tt.getProperty("mode", 0)) == 1 ? PlayMode::OneShot : PlayMode::Gate;
                if (!addRuleTarget(targets, t))
                    break;
            }
            if (auto e = sanitizeRuleEntry(r.getProperty("region1", -1), std::move(targets), r.getNumChildren() > 0))
                s.rules.push_back(std::move(*e));
        }
        s.fx = sanitizeFx(fxFromTree(root.getChildWithName("Fx")), static_cast<int>(s.loop2.boundaries.size()) + 1);
        return s;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

juce::String hashFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return {};
    juce::FileInputStream in(file);
    if (!in.openedOk())
        return {};
    return juce::SHA256(in).toHexString();
}

} // namespace dc
