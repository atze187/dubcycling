#include "plugin/presets/BankFormat.h"
#include "engine/FxSettings.h"
#include "engine/MixGain.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace dc {

namespace {

juce::String num(double v) { return juce::String(v, 17); }

bool isNumber(const juce::var& v) { return v.isDouble() || v.isInt() || v.isInt64(); }

// v as a double clamped to [lo, hi] before any cast (a hostile 1e30 cast to int is undefined),
// fallback when v is no number.
double clampedNumber(const juce::var& v, double lo, double hi, double fallback)
{
    if (!isNumber(v))
        return fallback;
    const auto d = static_cast<double>(v);
    return std::isnan(d) ? fallback : std::clamp(d, lo, hi);
}

int intOf(const juce::var& v, int fallback)
{
    return static_cast<int>(clampedNumber(v, static_cast<double>(INT_MIN), static_cast<double>(INT_MAX), fallback));
}

bool isControl(juce::juce_wchar c) { return c < 32 || c == 127; }

bool isReservedWindowsName(const juce::String& name)
{
    auto stem = name.upToFirstOccurrenceOf(".", false, false).trim().toLowerCase();
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul")
        return true;
    if (stem.length() == 4 && (stem.startsWith("com") || stem.startsWith("lpt")))
        return stem[3] >= '1' && stem[3] <= '9';
    return false;
}

// True when [ and { nest deeper than kMaxJsonDepth outside string literals. juce::JSON::parse
// recurses once per level, so a hostile file of "[[[[..." would overflow the stack. Strings are
// tracked like the JUCE parser does it: '"' or '\'' quotes, a backslash escapes the next character.
bool nestsTooDeep(const juce::String& json)
{
    int depth = 0;
    juce::juce_wchar quote = 0;
    bool escaped = false;
    for (auto p = json.getCharPointer(); !p.isEmpty(); ++p)
    {
        const auto c = *p;
        if (quote != 0)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == quote)
                quote = 0;
        }
        else if (c == '"' || c == '\'')
            quote = c;
        else if ((c == '[' || c == '{') && ++depth > kMaxJsonDepth)
            return true;
        else if (c == ']' || c == '}')
            --depth; // may go below 0 on damaged input, which the parser rejects anyway
    }
    return false;
}

// The shortest text of at most 17 significant digits that juce::JSON reads back as exactly v
// (juce::JSON::toString keeps only 15 decimal places, so 137.0 / 700.0 came back different).
juce::String jsonDouble(double v)
{
    if (!std::isfinite(v))
        return "null"; // like juce::JSON; JSON has no inf or nan
    juce::String text;
    for (int digits = 15; digits <= 17; ++digits)
    {
        std::ostringstream s;
        s.imbue(std::locale::classic());
        s << std::setprecision(digits) << v;
        text = s.str();
        const auto back = juce::JSON::parse("[" + text + "]");
        if (back.isArray() && back.size() == 1 && static_cast<double>(back[0]) == v)
            break;
    }
    return text;
}

// juce::JSON::toString with jsonDouble for every double, laid out like juce's multi-line form.
void writeJson(juce::String& out, const juce::var& v, int indent)
{
    const auto newLine = [&](int level) { out << '\n' << juce::String::repeatedString(" ", level * 2); };
    if (v.isDouble())
        out << jsonDouble(static_cast<double>(v));
    else if (const auto* arr = v.getArray())
    {
        if (arr->isEmpty())
        {
            out << "[]";
            return;
        }
        out << '[';
        for (int i = 0; i < arr->size(); ++i)
        {
            newLine(indent + 1);
            writeJson(out, (*arr)[i], indent + 1);
            if (i + 1 < arr->size())
                out << ',';
        }
        newLine(indent);
        out << ']';
    }
    else if (auto* obj = v.getDynamicObject())
    {
        const auto& props = obj->getProperties();
        if (props.isEmpty())
        {
            out << "{}";
            return;
        }
        out << '{';
        for (int i = 0; i < props.size(); ++i)
        {
            newLine(indent + 1);
            out << juce::JSON::toString(juce::var(props.getName(i).toString())) << ": ";
            writeJson(out, props.getValueAt(i), indent + 1);
            if (i + 1 < props.size())
                out << ',';
        }
        newLine(indent);
        out << '}';
    }
    else
        out << juce::JSON::toString(v); // strings, ints, bools
}

juce::var loopToVar(const LoopRef& l)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("path", l.path);
    o->setProperty("fileName", l.fileName);
    o->setProperty("hash", l.hash);
    o->setProperty("bars", l.bars);
    juce::Array<juce::var> bounds;
    for (double b : l.boundaries)
        bounds.add(b);
    o->setProperty("boundaries", bounds);
    auto* labels = new juce::DynamicObject();
    for (std::size_t i = 0; i < l.labels.size() && i < static_cast<std::size_t>(kMaxRegion1); ++i)
        if (const auto text = sanitizeLabel(l.labels[i]); text.isNotEmpty())
            labels->setProperty(juce::Identifier(juce::String(static_cast<int>(i))), text);
    o->setProperty("labels", juce::var(labels));
    return juce::var(o);
}

LoopRef loopFromVar(const juce::var& v)
{
    LoopRef l;
    if (!v.isObject())
    {
        l.bars = 1;
        return l;
    }
    l.path = v["path"].toString();
    l.fileName = v["fileName"].toString();
    l.hash = v["hash"].toString();
    l.bars = std::clamp(intOf(v["bars"], 1), 1, 1024);
    std::vector<double> raw;
    if (auto* arr = v["boundaries"].getArray())
        for (auto& b : *arr)
            if (isNumber(b))
                raw.push_back(static_cast<double>(b));
    l.boundaries = sanitizeBoundaries(raw);
    int kept = 0;
    const auto& labels = v["labels"];
    if (auto* arr = labels.getArray())
    {
        for (int i = 0; i < arr->size(); ++i)
            if (!addLabelEntry(l, kept, i, (*arr)[i].toString()))
                break;
    }
    else if (auto* obj = labels.getDynamicObject())
    {
        for (auto& p : obj->getProperties())
            if (!addLabelEntry(l, kept, p.name.toString().getIntValue(), p.value.toString()))
                break;
    }
    return l;
}

juce::var rulesToVar(const std::vector<RuleEntry>& rules)
{
    juce::Array<juce::var> out;
    for (auto& e : rules)
    {
        auto* r = new juce::DynamicObject();
        r->setProperty("region1", e.region1);
        juce::Array<juce::var> targets;
        for (auto& t : e.targets)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("region2", t.region2);
            o->setProperty("weight", static_cast<double>(t.weight));
            o->setProperty("mode", t.mode == PlayMode::OneShot ? 1 : 0);
            if (t.mix != 0.5f)
                o->setProperty("mix", static_cast<double>(t.mix));
            targets.add(juce::var(o));
        }
        r->setProperty("targets", targets);
        out.add(juce::var(r));
    }
    return out;
}

std::vector<RuleEntry> rulesFromVar(const juce::var& v)
{
    std::vector<RuleEntry> rules;
    auto* arr = v.getArray();
    if (arr == nullptr)
        return rules;
    for (auto& r : *arr)
    {
        if (rules.size() >= static_cast<std::size_t>(kMaxRuleEntries))
            break;
        if (!r.isObject())
            continue;
        std::vector<RuleTarget> targets;
        bool hadTargets = false;
        if (auto* ta = r["targets"].getArray())
        {
            hadTargets = !ta->isEmpty();
            for (auto& tv : *ta)
            {
                RuleTarget t;
                t.region2 = intOf(tv["region2"], 0);
                // RuleSet clamps a weight to [0, 1] anyway; clamping first keeps the float cast defined.
                t.weight = static_cast<float>(clampedNumber(tv["weight"], 0.0, 1.0, 0.0));
                t.mode = intOf(tv["mode"], 0) == 1 ? PlayMode::OneShot : PlayMode::Gate;
                // Absent in version 1 and 2 files; clamped as a double before the float cast.
                t.mix = sanitizeMix(static_cast<float>(clampedNumber(tv["mix"], 0.0, 1.0, 0.5)));
                if (!addRuleTarget(targets, t))
                    break;
            }
        }
        const int region1 = intOf(r["region1"], -1);
        if (auto e = sanitizeRuleEntry(region1, std::move(targets), hadTargets))
            rules.push_back(std::move(*e));
    }
    return rules;
}

juce::var fxToVar(const FxSettings& fx)
{
    auto* params = new juce::DynamicObject();
    for (int p = 0; p < kFxParamCount; ++p)
        params->setProperty(juce::Identifier(fxParamInfo(static_cast<FxParam>(p)).id),
                            static_cast<double>(fx.params.get(static_cast<FxParam>(p))));
    auto* switches = new juce::DynamicObject();
    for (std::size_t i = 0; i < fx.switches.size() && i < static_cast<std::size_t>(kMaxFxRegions); ++i)
        if (fx.switches[i] != 0)
            switches->setProperty(juce::Identifier(juce::String(static_cast<int>(i))), static_cast<int>(fx.switches[i]));
    auto* o = new juce::DynamicObject();
    o->setProperty("params", juce::var(params));
    o->setProperty("switches", juce::var(switches));
    return juce::var(o);
}

FxSettings fxFromVar(const juce::var& v, int loop2RegionCount)
{
    FxSettings fx;
    if (!v.isObject())
        return fx;
    if (auto* params = v["params"].getDynamicObject())
        for (int p = 0; p < kFxParamCount; ++p)
        {
            const auto param = static_cast<FxParam>(p);
            const auto& value = params->getProperty(juce::Identifier(fxParamInfo(param).id));
            if (isNumber(value))
                fx.params.set(param, static_cast<double>(value)); // sanitized (NaN, infinity, range) by set
        }
    if (auto* switches = v["switches"].getDynamicObject())
    {
        int kept = 0;
        for (auto& e : switches->getProperties())
        {
            if (kept++ >= kMaxFxRegions)
                break;
            const auto key = e.name.toString();
            if (key.isEmpty() || !key.containsOnly("0123456789") || key.length() > 6)
                continue; // not a plain non-negative number
            const int region = key.getIntValue();
            if (region >= kMaxFxRegions || !isNumber(e.value))
                continue;
            const auto mask = static_cast<std::uint8_t>(clampedNumber(e.value, 0.0, 15.0, 0.0));
            if (mask == 0)
                continue;
            if (fx.switches.size() <= static_cast<std::size_t>(region))
                fx.switches.resize(static_cast<std::size_t>(region) + 1, 0);
            fx.switches[static_cast<std::size_t>(region)] = mask;
        }
    }
    return sanitizeFx(std::move(fx), loop2RegionCount);
}

} // namespace

juce::String presetNameProblem(const juce::String& name, const Bank& bank)
{
    const auto clean = sanitizeName(name, INT_MAX);
    if (clean.isEmpty())
        return juce::String::fromUTF8("The name must not be empty.");
    if (clean.length() > kMaxPresetNameLength)
        return juce::String::fromUTF8("The name is too long (at most ") + juce::String(kMaxPresetNameLength)
               + " characters).";
    for (auto& p : bank.presets)
        if (p.name.equalsIgnoreCase(clean))
            return juce::String::fromUTF8("A preset with this name already exists.");
    return {};
}

juce::String bankNameProblem(const juce::String& name, const juce::StringArray& existingBanks)
{
    if (name.trim().isEmpty())
        return juce::String::fromUTF8("The name must not be empty.");
    if (name.length() > kMaxBankNameLength)
        return juce::String::fromUTF8("The name is too long (at most ") + juce::String(kMaxBankNameLength)
               + " characters).";
    for (auto p = name.getCharPointer(); !p.isEmpty(); ++p)
        if (isControl(*p) || juce::String("\\/:*?\"<>|").containsChar(*p))
            return juce::String::fromUTF8("The name must not contain any of the characters \\ / : * ? \" < > |");
    if (name.endsWithChar('.') || name.endsWithChar(' '))
        return juce::String::fromUTF8("The name must not end with a dot or a space.");
    if (isReservedWindowsName(name))
        return juce::String::fromUTF8("This name is reserved on Windows.");
    for (auto& b : existingBanks)
        if (b.equalsIgnoreCase(name))
            return juce::String::fromUTF8("A bank with this name already exists.");
    return {};
}

bool isPortableFileName(const juce::String& name)
{
    if (name.isEmpty() || name.endsWithChar('.') || name.endsWithChar(' ')) // also "." and ".."
        return false;
    for (auto p = name.getCharPointer(); !p.isEmpty(); ++p)
        if (isControl(*p) || juce::String("\\/:*?\"<>|").containsChar(*p))
            return false;
    return !isReservedWindowsName(name);
}

juce::String bankToJson(const Bank& bank)
{
    juce::Array<juce::var> presets;
    for (auto& p : bank.presets)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("name", p.name);
        o->setProperty("loop1", loopToVar(p.state.loop1));
        o->setProperty("loop2", loopToVar(p.state.loop2));
        o->setProperty("rules", rulesToVar(p.state.rules));
        o->setProperty("fx", fxToVar(p.state.fx));
        presets.add(juce::var(o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty("version", kBankVersion);
    root->setProperty("presets", presets);
    juce::String out;
    writeJson(out, juce::var(root), 0);
    return out;
}

BankParseResult bankFromJson(const juce::String& json)
{
    BankParseResult result;
    if (nestsTooDeep(json))
        return result;
    try
    {
        const auto root = juce::JSON::parse(json);
        if (!root.isObject() || !isNumber(root["version"]))
            return result;
        const int version = intOf(root["version"], 0);
        if (version > kBankVersion)
        {
            result.tooNew = true;
            return result;
        }
        auto* arr = root["presets"].getArray();
        if (version < 1 || arr == nullptr)
            return result;

        Bank bank;
        int skipped = 0;
        for (int i = 0; i < arr->size(); ++i)
        {
            const auto& pv = (*arr)[i];
            if (static_cast<int>(bank.presets.size()) >= kMaxPresetsPerBank)
            {
                skipped += arr->size() - i; // this entry and all after it
                break;
            }
            if (!pv.isObject())
            {
                ++skipped;
                continue;
            }
            Preset p;
            p.name = sanitizeName(pv["name"].toString(), kMaxPresetNameLength);
            if (p.name.isEmpty())
            {
                ++skipped;
                continue;
            }
            p.state.loop1 = loopFromVar(pv["loop1"]);
            p.state.loop2 = loopFromVar(pv["loop2"]);
            p.state.rules = rulesFromVar(pv["rules"]);
            p.state.fx = fxFromVar(pv["fx"], static_cast<int>(p.state.loop2.boundaries.size()) + 1);
            bank.presets.push_back(std::move(p));
        }
        result.bank = std::move(bank);
        result.skipped = skipped;
    }
    catch (...)
    {
        result.bank.reset();
        result.skipped = 0;
    }
    return result;
}

juce::String presetFingerprint(const PluginState& state)
{
    juce::String out;
    for (const LoopRef* l : { &state.loop1, &state.loop2 })
    {
        out << "L|" << l->hash << '|' << l->bars << "|B";
        for (double b : l->boundaries)
            out << ',' << num(b);
        out << "|T";
        for (std::size_t i = 0; i < l->labels.size(); ++i)
            if (const auto text = sanitizeLabel(l->labels[i]); text.isNotEmpty())
                out << ',' << static_cast<int>(i) << '=' << text.replace("\\", "\\\\").replace(",", "\\,");
        out << '\n';
    }
    for (auto& e : state.rules)
    {
        out << "R|" << e.region1;
        for (auto& t : e.targets)
            out << '|' << t.region2 << ',' << num(static_cast<double>(t.weight)) << ',' << (t.mode == PlayMode::OneShot ? 1 : 0)
                << (t.mix != 0.5f ? ",m" + num(static_cast<double>(t.mix)) : juce::String());
        out << '\n';
    }
    // Effect settings: nothing at all for the default state, so presets without effect data keep their fingerprint.
    // Compared by value, not with operator==: trailing zero masks (a switch turned on and off again) are no change.
    const auto& fx = state.fx;
    const bool anySwitch = std::any_of(fx.switches.begin(), fx.switches.end(), [](std::uint8_t m) { return m != 0; });
    if (fx.params != FxSettings {}.params || anySwitch)
    {
        out << "F";
        for (int p = 0; p < kFxParamCount; ++p)
            out << '|' << fxParamInfo(static_cast<FxParam>(p)).id << '=' << num(static_cast<double>(state.fx.params.get(static_cast<FxParam>(p))));
        out << "|S";
        for (std::size_t i = 0; i < state.fx.switches.size(); ++i)
            if (state.fx.switches[i] != 0)
                out << ',' << static_cast<int>(i) << '=' << static_cast<int>(state.fx.switches[i]);
        out << '\n';
    }
    return out;
}

} // namespace dc
