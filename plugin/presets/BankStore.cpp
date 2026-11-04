#include "plugin/presets/BankStore.h"
#include <algorithm>

namespace dc {

namespace {

const char* const kBankFile = "bank.json";
const char* const kSamplesDir = "samples";

juce::String text(const char* utf8) { return juce::String::fromUTF8(utf8); }

BankStore::Result fail(BankStore::Error error, const juce::String& message) { return { error, message }; }

// Writes bank.json.tmp next to bank.json and replaces bank.json with it.
bool writeAtomically(const juce::File& target, const juce::String& content)
{
    const auto tmp = target.getSiblingFile(target.getFileName() + ".tmp");
    if (!tmp.replaceWithText(content, false, false, "\n"))
    {
        tmp.deleteFile();
        return false;
    }
    if (!tmp.replaceFileIn(target))
    {
        tmp.deleteFile();
        return false;
    }
    return true;
}

// Error result for a bank that could not be read; None when parsed has a bank.
BankStore::Result checkParsed(const juce::File& bankFile, const BankParseResult& parsed)
{
    if (!bankFile.existsAsFile())
        return fail(BankStore::Error::BankMissing, text("Die Bank gibt es nicht."));
    if (parsed.tooNew)
        return fail(BankStore::Error::BankTooNew,
                    text("Die Bank stammt aus einer neueren Version von Dubcycling und wird nicht ver\xc3\xa4ndert."));
    if (!parsed.bank)
        return fail(BankStore::Error::BankUnreadable,
                    text("Die Bank-Datei ist besch\xc3\xa4" "digt und kann nicht gelesen werden."));
    return {};
}

// The file in samples/ that holds `source` (Spec 3): `source` itself when it already lies in
// samples/, otherwise a copy under its name, with " (n)" when the name is taken by other content;
// a file with the same content is reused. Empty File on a failed copy.
juce::File placeSample(const juce::File& source, const juce::String& sourceHash, const juce::File& samples)
{
    if (source.getParentDirectory() == samples)
        return source;
    const auto stem = source.getFileNameWithoutExtension();
    const auto ext = source.getFileExtension();
    for (int n = 1;; ++n)
    {
        const auto candidate = samples.getChildFile(n == 1 ? stem + ext : stem + " (" + juce::String(n) + ")" + ext);
        if (!candidate.exists())
            return source.copyFileTo(candidate) ? candidate : juce::File();
        if (candidate.existsAsFile() && hashFile(candidate) == sourceHash)
            return candidate;
    }
}

} // namespace

BankStore::BankStore(juce::File root) : rootDir(std::move(root)) {}

juce::File BankStore::defaultRoot()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Dubcycling").getChildFile("Presets");
}

juce::File BankStore::root() const { return rootDir; }

juce::File BankStore::bankDir(const juce::String& bank) const { return rootDir.getChildFile(bank); }

juce::StringArray BankStore::listBanks() const
{
    juce::StringArray banks;
    for (auto& dir : rootDir.findChildFiles(juce::File::findDirectories, false))
        if (dir.getChildFile(kBankFile).existsAsFile())
            banks.add(dir.getFileName());
    banks.sort(true);
    return banks;
}

BankStore::Result BankStore::createBank(const juce::String& name)
{
    if (const auto problem = bankNameProblem(name, {}); problem.isNotEmpty())
        return fail(Error::InvalidName, problem);
    if (listBanks().contains(name, true) || bankDir(name).exists())
        return fail(Error::NameTaken, text("Eine Bank mit diesem Namen gibt es schon."));

    const auto dir = bankDir(name);
    if (!dir.getChildFile(kSamplesDir).createDirectory().wasOk() || !writeAtomically(dir.getChildFile(kBankFile), bankToJson({})))
        return fail(Error::WriteFailed, text("Die Bank konnte nicht angelegt werden."));
    return {};
}

BankParseResult BankStore::readBank(const juce::String& bank) const
{
    const auto file = bankDir(bank).getChildFile(kBankFile);
    if (!file.existsAsFile())
        return {};
    return bankFromJson(file.loadFileAsString());
}

BankStore::Result BankStore::savePreset(const juce::String& bank, const juce::String& presetName,
                                        const PluginState& current, bool overwrite)
{
    // Re-read right before changing it (Spec 16).
    const auto dir = bankDir(bank);
    const auto bankFile = dir.getChildFile(kBankFile);
    auto parsed = readBank(bank);
    if (auto r = checkParsed(bankFile, parsed); !r.ok())
        return r;
    Bank& stored = *parsed.bank;

    if (const auto problem = presetNameProblem(presetName, Bank {}); problem.isNotEmpty())
        return fail(Error::InvalidName, problem);
    const auto name = sanitizeName(presetName, kMaxPresetNameLength);
    auto existing = std::find_if(stored.presets.begin(), stored.presets.end(),
                                 [&](const Preset& p) { return p.name.equalsIgnoreCase(name); });
    if (existing != stored.presets.end() && !overwrite)
        return fail(Error::NameTaken, presetNameProblem(name, stored));

    // Check every used slot before copying anything.
    const LoopRef* loops[] = { &current.loop1, &current.loop2 };
    juce::String hashes[2];
    for (int i = 0; i < 2; ++i)
    {
        if (loops[i]->path.isEmpty())
            continue;
        const auto& path = loops[i]->path;
        if (juce::File::isAbsolutePath(path))
            hashes[i] = hashFile(juce::File(path)); // empty when missing or unreadable
        if (hashes[i].isEmpty())
            return fail(Error::SampleMissing, text("Die Loop-Datei fehlt oder ist nicht lesbar: ") + path);
    }

    const auto samples = dir.getChildFile(kSamplesDir);
    if (!samples.createDirectory().wasOk())
        return fail(Error::CopyFailed, text("Der Ordner samples konnte nicht angelegt werden."));

    Preset preset;
    preset.name = name;
    preset.state.rules = current.rules;
    LoopRef* targets[] = { &preset.state.loop1, &preset.state.loop2 };
    for (int i = 0; i < 2; ++i)
    {
        *targets[i] = *loops[i];
        if (loops[i]->path.isEmpty())
            continue;
        const juce::File source(loops[i]->path);
        const auto placed = placeSample(source, hashes[i], samples);
        if (placed == juce::File())
            return fail(Error::CopyFailed, text("Die Loop-Datei konnte nicht in die Bank kopiert werden: ") + source.getFileName());
        targets[i]->path = juce::String(kSamplesDir) + "/" + placed.getFileName();
        targets[i]->fileName = placed.getFileName();
        targets[i]->hash = placed == source ? hashes[i] : hashFile(placed);
    }

    if (existing != stored.presets.end())
        *existing = std::move(preset);
    else
        stored.presets.push_back(std::move(preset));

    if (!writeAtomically(bankFile, bankToJson(stored)))
        return fail(Error::WriteFailed, text("Die Bank-Datei konnte nicht geschrieben werden."));
    return {};
}

BankStore::LoadedPreset BankStore::loadPreset(const juce::String& bank, const juce::String& presetName) const
{
    LoadedPreset loaded;
    const auto dir = bankDir(bank);
    const auto parsed = readBank(bank);
    if (loaded.result = checkParsed(dir.getChildFile(kBankFile), parsed); !loaded.result.ok())
        return loaded;

    const auto name = sanitizeName(presetName, kMaxPresetNameLength);
    const auto& presets = parsed.bank->presets;
    const auto found = std::find_if(presets.begin(), presets.end(),
                                    [&](const Preset& p) { return p.name.equalsIgnoreCase(name); });
    if (found == presets.end())
    {
        loaded.result = fail(Error::PresetMissing, text("Das Preset gibt es in dieser Bank nicht."));
        return loaded;
    }

    loaded.state = found->state;
    for (LoopRef* l : { &loaded.state.loop1, &loaded.state.loop2 })
        if (l->path.isNotEmpty())
            l->path = dir.getChildFile(l->path).getFullPathName();
    loaded.state.activeBank = bank;
    loaded.state.activePreset = found->name;
    return loaded;
}

} // namespace dc
