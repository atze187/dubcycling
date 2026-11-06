#include "plugin/presets/BankStore.h"
#include <algorithm>

namespace dc {

namespace {

const char* const kBankFile = BankStore::kBankFileName;
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

// The file in samples/ that holds `source` (Spec 3): `source` itself when it already lies in
// samples/ under a portable name, otherwise a copy under portableSampleName, with " (n)" when the
// name is taken by other content; a file with the same content is reused. A name counts as taken
// when any entry of samples/ matches it case-insensitively, and a reused file is returned in its
// spelling on disk, so the stored path loads and exports on every file system. Empty File on a
// failed copy.
juce::File placeSample(const juce::File& source, const juce::String& sourceHash, const juce::File& samples)
{
    if (source.getParentDirectory() == samples && isPortableFileName(source.getFileName()))
        return source;
    const auto name = portableSampleName(source.getFileName());
    const auto dot = name.lastIndexOfChar('.');
    const auto stem = dot > 0 ? name.substring(0, dot) : name;
    const auto ext = dot > 0 ? name.substring(dot) : juce::String();
    const auto onDisk = samples.findChildFiles(juce::File::findFilesAndDirectories, false);
    for (int n = 1;; ++n)
    {
        const auto candidate = n == 1 ? name : stem + " (" + juce::String(n) + ")" + ext;
        bool taken = false;
        for (auto& existing : onDisk)
        {
            if (!existing.getFileName().equalsIgnoreCase(candidate))
                continue;
            if (existing.existsAsFile() && hashFile(existing) == sourceHash)
                return existing;
            taken = true;
        }
        const auto target = samples.getChildFile(candidate);
        if (!taken && !target.exists())
            return source.copyFileTo(target) ? target : juce::File();
    }
}

} // namespace

juce::File storedSampleFile(const juce::File& bankFolder, const juce::String& storedPath)
{
    const juce::String prefix = juce::String(kSamplesDir) + "/";
    if (!storedPath.startsWith(prefix))
        return {};
    const auto name = storedPath.substring(prefix.length());
    if (name.isEmpty() || name == "." || name == ".." || name.containsAnyOf("/\\:"))
        return {};
    const auto samples = bankFolder.getChildFile(kSamplesDir);
    const auto file = samples.getChildFile(name);
    return file.getParentDirectory() == samples ? file : juce::File();
}

juce::String portableSampleName(const juce::String& sourceName)
{
    const auto stripEnd = [](juce::String s) {
        while (s.endsWithChar('.') || s.endsWithChar(' '))
            s = s.dropLastCharacters(1);
        return s;
    };
    juce::String clean;
    for (auto p = sourceName.getCharPointer(); !p.isEmpty(); ++p)
    {
        const auto c = *p;
        const bool bad = c < 32 || c == 127 || juce::String("\\/:*?\"<>|").containsChar(c);
        clean += juce::String::charToString(bad ? static_cast<juce::juce_wchar>('_') : c);
    }
    clean = stripEnd(clean);

    // One character stays free for the '_' in front of a reserved name.
    const int limit = kMaxSampleNameLength - 1;
    const auto dot = clean.lastIndexOfChar('.');
    auto ext = dot > 0 ? clean.substring(dot) : juce::String();
    if (ext.length() >= limit)
        ext = {};
    const auto stem = clean.substring(0, (ext.isEmpty() ? clean.length() : dot)).substring(0, limit - ext.length());
    clean = stripEnd(stem + ext);

    if (clean.isEmpty())
        return "sample";
    return isPortableFileName(clean) ? clean : "_" + clean; // only a reserved Windows name is left
}

BankStore::Result BankStore::checkParsed(const juce::File& bankFile, const BankParseResult& parsed)
{
    if (!bankFile.existsAsFile())
        return fail(Error::BankMissing, text("The bank does not exist."));
    if (parsed.tooNew)
        return fail(Error::BankTooNew,
                    text("The bank comes from a newer version of Dubcycling and is not modified."));
    if (!parsed.bank)
        return fail(Error::BankUnreadable, text("The bank file is corrupt and cannot be read."));
    return {};
}

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
        return fail(Error::NameTaken, text("A bank with this name already exists."));

    const auto dir = bankDir(name);
    if (!dir.getChildFile(kSamplesDir).createDirectory().wasOk() || !writeAtomically(dir.getChildFile(kBankFile), bankToJson({})))
        return fail(Error::WriteFailed, text("The bank could not be created."));
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
    if (parsed.skipped > 0)
        return fail(Error::BankHasUnreadableEntries,
                    text("The bank contains entries this plugin cannot read. "
                         "Saving would delete them, so nothing is saved."));
    Bank& stored = *parsed.bank;

    if (const auto problem = presetNameProblem(presetName, Bank {}); problem.isNotEmpty())
        return fail(Error::InvalidName, problem);
    const auto name = sanitizeName(presetName, kMaxPresetNameLength);
    auto existing = std::find_if(stored.presets.begin(), stored.presets.end(),
                                 [&](const Preset& p) { return p.name.equalsIgnoreCase(name); });
    if (existing != stored.presets.end() && !overwrite)
        return fail(Error::NameTaken, presetNameProblem(name, stored));
    if (existing == stored.presets.end() && stored.presets.size() >= static_cast<std::size_t>(kMaxPresetsPerBank))
        return fail(Error::BankFull, text("The bank is full (at most ") + juce::String(kMaxPresetsPerBank)
                                         + " Presets).");

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
            return fail(Error::SampleMissing, text("The loop file is missing or unreadable: ") + path);
    }

    const auto samples = dir.getChildFile(kSamplesDir);
    if (!samples.createDirectory().wasOk())
        return fail(Error::CopyFailed, text("The samples folder could not be created."));

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
            return fail(Error::CopyFailed, text("The loop file could not be copied into the bank: ") + source.getFileName());
        targets[i]->path = juce::String(kSamplesDir) + "/" + placed.getFileName();
        targets[i]->fileName = placed.getFileName();
        targets[i]->hash = placed == source ? hashes[i] : hashFile(placed);
    }

    if (existing != stored.presets.end())
        *existing = std::move(preset);
    else
        stored.presets.push_back(std::move(preset));

    if (!writeAtomically(bankFile, bankToJson(stored)))
        return fail(Error::WriteFailed, text("The bank file could not be written."));
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
        loaded.result = fail(Error::PresetMissing, text("The preset does not exist in this bank."));
        return loaded;
    }

    loaded.state = found->state;
    for (LoopRef* l : { &loaded.state.loop1, &loaded.state.loop2 })
        l->path = storedSampleFile(dir, l->path).getFullPathName(); // empty when not "samples/<name>"
    loaded.state.activeBank = bank;
    loaded.state.activePreset = found->name;
    return loaded;
}

} // namespace dc
