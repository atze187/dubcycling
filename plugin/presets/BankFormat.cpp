#include "plugin/presets/BankFormat.h"
#include <algorithm>
#include <climits>

namespace dc {

namespace {

juce::String num(double v) { return juce::String(v, 17); }

bool isNumber(const juce::var& v) { return v.isDouble() || v.isInt() || v.isInt64(); }

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
    l.bars = std::clamp(isNumber(v["bars"]) ? static_cast<int>(v["bars"]) : 1, 1, 1024);
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
                t.region2 = isNumber(tv["region2"]) ? static_cast<int>(tv["region2"]) : 0;
                t.weight = isNumber(tv["weight"]) ? static_cast<float>(static_cast<double>(tv["weight"])) : 0.0f;
                t.mode = isNumber(tv["mode"]) && static_cast<int>(tv["mode"]) == 1 ? PlayMode::OneShot : PlayMode::Gate;
                if (!addRuleTarget(targets, t))
                    break;
            }
        }
        const int region1 = isNumber(r["region1"]) ? static_cast<int>(r["region1"]) : -1;
        if (auto e = sanitizeRuleEntry(region1, std::move(targets), hadTargets))
            rules.push_back(std::move(*e));
    }
    return rules;
}

} // namespace

juce::String presetNameProblem(const juce::String& name, const Bank& bank)
{
    const auto clean = sanitizeName(name, INT_MAX);
    if (clean.isEmpty())
        return juce::String::fromUTF8("Der Name darf nicht leer sein.");
    if (clean.length() > kMaxPresetNameLength)
        return juce::String::fromUTF8("Der Name ist zu lang (h\xc3\xb6" "chstens ") + juce::String(kMaxPresetNameLength)
               + " Zeichen).";
    for (auto& p : bank.presets)
        if (p.name.equalsIgnoreCase(clean))
            return juce::String::fromUTF8("Ein Preset mit diesem Namen gibt es schon.");
    return {};
}

juce::String bankNameProblem(const juce::String& name, const juce::StringArray& existingBanks)
{
    if (name.trim().isEmpty())
        return juce::String::fromUTF8("Der Name darf nicht leer sein.");
    if (name.length() > kMaxBankNameLength)
        return juce::String::fromUTF8("Der Name ist zu lang (h\xc3\xb6" "chstens ") + juce::String(kMaxBankNameLength)
               + " Zeichen).";
    for (auto p = name.getCharPointer(); !p.isEmpty(); ++p)
        if (isControl(*p) || juce::String("\\/:*?\"<>|").containsChar(*p))
            return juce::String::fromUTF8("Der Name darf keines der Zeichen \\ / : * ? \" < > | enthalten.");
    if (name.endsWithChar('.') || name.endsWithChar(' '))
        return juce::String::fromUTF8("Der Name darf nicht mit einem Punkt oder Leerzeichen enden.");
    if (isReservedWindowsName(name))
        return juce::String::fromUTF8("Dieser Name ist unter Windows reserviert.");
    for (auto& b : existingBanks)
        if (b.equalsIgnoreCase(name))
            return juce::String::fromUTF8("Eine Bank mit diesem Namen gibt es schon.");
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
        presets.add(juce::var(o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty("version", bank.version);
    root->setProperty("presets", presets);
    return juce::JSON::toString(juce::var(root), false);
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
        const int version = root["version"];
        if (version > kBankVersion)
        {
            result.tooNew = true;
            return result;
        }
        auto* arr = root["presets"].getArray();
        if (version < kBankVersion || arr == nullptr)
            return result;

        Bank bank;
        for (auto& pv : *arr)
        {
            if (static_cast<int>(bank.presets.size()) >= kMaxPresetsPerBank)
                break;
            if (!pv.isObject())
                continue;
            Preset p;
            p.name = sanitizeName(pv["name"].toString(), kMaxPresetNameLength);
            if (p.name.isEmpty())
                continue;
            p.state.loop1 = loopFromVar(pv["loop1"]);
            p.state.loop2 = loopFromVar(pv["loop2"]);
            p.state.rules = rulesFromVar(pv["rules"]);
            bank.presets.push_back(std::move(p));
        }
        result.bank = std::move(bank);
    }
    catch (...)
    {
        result.bank.reset();
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
            out << '|' << t.region2 << ',' << num(static_cast<double>(t.weight)) << ',' << (t.mode == PlayMode::OneShot ? 1 : 0);
        out << '\n';
    }
    return out;
}

} // namespace dc
