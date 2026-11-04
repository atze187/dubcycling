#include "plugin/presets/BankArchive.h"
#include <algorithm>
#include <vector>

namespace dc {

namespace {

const char* const kBankFile = BankStore::kBankFileName;
const char* const kSamplesDir = "samples";
const char* const kMacMetadataDir = "__MACOSX"; // resource forks the macOS archiver adds
constexpr int kCompressionLevel = 9;
constexpr int kCopyChunk = 65536;
constexpr int kMaxNameCounter = 9999;

juce::String text(const char* utf8) { return juce::String::fromUTF8(utf8); }

BankStore::Result fail(BankStore::Error error, const juce::String& message) { return { error, message }; }
BankStore::Result invalid(const juce::String& message) { return fail(BankStore::Error::ArchiveInvalid, message); }
BankStore::Result unpackFailed()
{
    return fail(BankStore::Error::WriteFailed, text("Die Bank konnte nicht in den Preset-Ordner entpackt werden."));
}

// One file to unpack: zip entry index, path below the bank folder ("bank.json" or
// "samples/<name>") and the declared uncompressed size.
struct ArchiveFile
{
    int index = 0;
    juce::String relative;
    juce::int64 size = 0;
};

struct ArchivePlan
{
    juce::String top;           // the one top folder
    std::vector<ArchiveFile> files;
    juce::StringArray samples;  // names of the files in samples/
};

// Checks every entry of the zip (Spec 10) without writing anything and fills plan.
BankStore::Result planImport(const juce::ZipFile& zip, const ArchiveLimits& limits, ArchivePlan& plan)
{
    const int count = zip.getNumEntries();
    if (count == 0)
        return invalid(text("Die Zip-Datei ist leer oder kann nicht gelesen werden."));
    if (count > limits.maxEntries)
        return fail(BankStore::Error::ArchiveTooLarge,
                    text("Die Zip-Datei hat zu viele Eintr\xc3\xa4ge (h\xc3\xb6" "chstens ") + juce::String(limits.maxEntries) + ").");

    const auto oneFolder = text("Die Zip-Datei muss genau einen Bank-Ordner enthalten.");
    juce::int64 total = 0; // at most 65535 entries of at most 4 GiB each: no overflow
    juce::StringArray seen;
    for (int i = 0; i < count; ++i)
    {
        const auto& entry = *zip.getEntry(i);
        const auto& name = entry.filename;
        total += entry.uncompressedSize;

        const bool isFolder = name.endsWithChar('/');
        const auto path = isFolder ? name.dropLastCharacters(1) : name;
        juce::StringArray parts;
        parts.addTokens(path, "/", "");
        const bool safe = !entry.isSymbolicLink && path.isNotEmpty() && !path.startsWithChar('/') && !path.contains("//")
                          && std::all_of(parts.begin(), parts.end(), [](const juce::String& p) { return isPortableFileName(p); });
        if (!safe)
            return invalid(text("Die Zip-Datei enth\xc3\xa4lt einen unzul\xc3\xa4ssigen Pfad: ") + name);

        // macOS metadata is skipped, never unpacked (checked for safe paths above all the same).
        if (parts[0] == kMacMetadataDir || (!isFolder && parts.size() > 1 && parts[parts.size() - 1].startsWith("._")))
            continue;
        if (parts.size() == 1 && !isFolder)
            return invalid(oneFolder);
        if (plan.top.isEmpty())
            plan.top = parts[0];
        else if (parts[0] != plan.top)
            return invalid(oneFolder);

        parts.remove(0);
        juce::String relative;
        if (isFolder && (parts.isEmpty() || parts == juce::StringArray { kSamplesDir }))
            continue;
        if (!isFolder && parts.size() == 1 && parts[0] != kBankFile)
            continue; // other files beside bank.json (.DS_Store, Thumbs.db, desktop.ini, ...) stay packed
        if (!isFolder && parts == juce::StringArray { kBankFile })
        {
            if (entry.uncompressedSize > limits.maxBankJsonBytes)
                return fail(BankStore::Error::ArchiveTooLarge,
                            text("Die bank.json in der Zip-Datei ist zu gro\xc3\x9f (h\xc3\xb6" "chstens ")
                                + juce::String(limits.maxBankJsonBytes / 1024) + " KB).");
            relative = kBankFile;
        }
        else if (!isFolder && parts.size() == 2 && parts[0] == kSamplesDir)
            relative = juce::String(kSamplesDir) + "/" + parts[1];
        else
            return invalid(text("Die Zip-Datei enth\xc3\xa4lt einen fremden Ordner: ") + name);

        if (seen.contains(relative, true))
            return invalid(text("Die Zip-Datei enth\xc3\xa4lt eine Datei doppelt: ") + name);
        seen.add(relative);
        if (parts.size() == 2)
            plan.samples.add(parts[1]);
        plan.files.push_back({ i, relative, entry.uncompressedSize });
    }

    if (total > limits.maxBytes)
        return fail(BankStore::Error::ArchiveTooLarge,
                    text("Die Zip-Datei ist entpackt zu gro\xc3\x9f (h\xc3\xb6" "chstens ")
                        + juce::String(limits.maxBytes / (1024 * 1024)) + " MB).");
    if (!seen.contains(kBankFile))
        return invalid(text("In der Zip-Datei fehlt bank.json."));
    if (const auto problem = bankNameProblem(plan.top, {}); problem.isNotEmpty())
        return invalid(text("Der Bank-Ordner in der Zip-Datei hat keinen g\xc3\xbcltigen Namen: ") + problem);
    return {};
}

// Unpacks one entry to target, reading at most one byte more than declared: an entry whose data
// does not match its declared size (a zip bomb lying about it, or a damaged zip) is rejected.
BankStore::Result unpack(juce::ZipFile& zip, const ArchiveFile& file, const juce::File& target)
{
    const auto damaged = invalid(text("Ein Eintrag der Zip-Datei ist besch\xc3\xa4" "digt: ") + zip.getEntry(file.index)->filename);
    std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(file.index));
    if (in == nullptr)
        return damaged;
    if (!target.getParentDirectory().createDirectory())
        return unpackFailed();
    juce::FileOutputStream out(target);
    if (out.failedToOpen())
        return unpackFailed();

    juce::HeapBlock<char> buffer(kCopyChunk);
    juce::int64 written = 0;
    for (;;)
    {
        const auto wanted = static_cast<int>(std::min<juce::int64>(kCopyChunk, file.size - written + 1));
        const int got = in->read(buffer, wanted);
        if (got <= 0)
            break;
        written += got;
        if (written > file.size)
            return damaged;
        if (!out.write(buffer, static_cast<size_t>(got)))
            return unpackFailed();
    }
    if (written != file.size)
        return damaged;
    out.flush();
    return out.getStatus().wasOk() ? BankStore::Result {} : unpackFailed();
}

// The wanted name, or "wanted (n)" (shortened to kMaxBankNameLength) when a bank or any other
// file already uses it. Empty when no counter up to kMaxNameCounter is free.
juce::String freeBankName(const BankStore& store, const juce::String& wanted)
{
    const auto existing = store.listBanks();
    for (int n = 1; n <= kMaxNameCounter; ++n)
    {
        const juce::String suffix = n == 1 ? juce::String() : " (" + juce::String(n) + ")";
        const auto candidate = wanted.substring(0, kMaxBankNameLength - suffix.length()) + suffix;
        if (bankNameProblem(candidate, existing).isEmpty() && !store.bankDir(candidate).exists())
            return candidate;
    }
    return {};
}

} // namespace

BankStore::Result exportBank(const BankStore& store, const juce::String& bank, const juce::File& zipFile)
{
    // Import refuses a top folder with such a name; a folder made by hand may have one.
    if (const auto problem = bankNameProblem(bank, {}); problem.isNotEmpty())
        return fail(BankStore::Error::InvalidName, problem);
    const auto dir = store.bankDir(bank);
    const auto bankFile = dir.getChildFile(kBankFile);
    const auto parsed = store.readBank(bank);
    if (auto r = BankStore::checkParsed(bankFile, parsed); !r.ok())
        return r;

    // Check every used sample before writing anything, with the rules import applies: a portable
    // name, spelled exactly as on disk, and no two packed names that differ only in case.
    const auto samplesDir = dir.getChildFile(kSamplesDir);
    juce::StringArray onDisk;
    for (auto& file : samplesDir.findChildFiles(juce::File::findFiles, false))
        onDisk.add(file.getFileName());
    juce::StringArray packed;
    for (auto& preset : parsed.bank->presets)
        for (const LoopRef* loop : { &preset.state.loop1, &preset.state.loop2 })
        {
            if (loop->path.isEmpty())
                continue;
            const auto sample = storedSampleFile(dir, loop->path);
            const auto name = sample.getFileName();
            if (sample != juce::File() && !isPortableFileName(name))
                return fail(BankStore::Error::SampleNameInvalid,
                            text("Der Name der Loop-Datei ist nicht auf allen Systemen g\xc3\xbcltig: ") + loop->path);
            if (!sample.existsAsFile())
                return fail(BankStore::Error::SampleMissing, text("Die Loop-Datei fehlt in der Bank: ") + loop->path);
            if (!onDisk.contains(name) || (!packed.contains(name) && packed.contains(name, true)))
                return fail(BankStore::Error::SampleNameInvalid,
                            text("Die Loop-Datei ist in der Bank unterschiedlich geschrieben: ") + loop->path);
            packed.addIfNotAlreadyThere(name);
        }

    juce::ZipFile::Builder builder;
    builder.addFile(bankFile, kCompressionLevel, bank + "/" + kBankFile);
    for (auto& name : packed)
        builder.addFile(samplesDir.getChildFile(name), kCompressionLevel, bank + "/" + kSamplesDir + "/" + name);

    // Written next to zipFile and moved over it when complete; the destructor removes it otherwise.
    juce::TemporaryFile temp(zipFile);
    bool written = false;
    {
        juce::FileOutputStream out(temp.getFile());
        if (out.openedOk() && builder.writeToStream(out, nullptr))
        {
            out.flush();
            written = out.getStatus().wasOk();
        }
    }
    if (!written || !temp.overwriteTargetFileWithTemporary())
        return fail(BankStore::Error::WriteFailed, text("Die Zip-Datei konnte nicht geschrieben werden."));
    return {};
}

BankStore::Result importBank(BankStore& store, const juce::File& zipFile, juce::String& importedName,
                             const ArchiveLimits& limits)
{
    if (!zipFile.existsAsFile())
        return invalid(text("Die Zip-Datei ist leer oder kann nicht gelesen werden."));
    juce::ZipFile zip(zipFile);
    ArchivePlan plan;
    if (auto r = planImport(zip, limits, plan); !r.ok())
        return r;

    // Unpack into <root>/.import/bank: one level deeper than a bank, so listBanks() never sees it.
    const auto root = store.root();
    if (!root.createDirectory())
        return unpackFailed();
    const auto temp = root.getNonexistentChildFile(".import", "", false);
    const juce::ScopeGuard removeTemp { [&] { temp.deleteRecursively(); } };
    const auto staging = temp.getChildFile("bank");
    if (!staging.createDirectory())
        return unpackFailed();
    for (auto& file : plan.files)
        if (auto r = unpack(zip, file, staging.getChildFile(file.relative)); !r.ok())
            return r;

    const auto parsed = bankFromJson(staging.getChildFile(kBankFile).loadFileAsString());
    if (parsed.tooNew)
        return fail(BankStore::Error::BankTooNew,
                    text("Die Bank stammt aus einer neueren Version von Dubcycling und kann nicht importiert werden."));
    if (!parsed.bank)
        return fail(BankStore::Error::BankUnreadable, text("Die bank.json in der Zip-Datei ist besch\xc3\xa4" "digt."));
    const juce::String prefix = juce::String(kSamplesDir) + "/";
    for (auto& preset : parsed.bank->presets)
        for (const LoopRef* loop : { &preset.state.loop1, &preset.state.loop2 })
            if (loop->path.isNotEmpty()
                && !(loop->path.startsWith(prefix) && plan.samples.contains(loop->path.substring(prefix.length()))))
                return invalid(text("Ein Preset verweist auf eine Loop-Datei, die nicht in der Zip-Datei liegt: ") + loop->path);

    // freeBankName only returns a name nothing uses: moveFileTo would delete an existing target.
    const auto name = freeBankName(store, plan.top);
    if (name.isEmpty() || !staging.moveFileTo(store.bankDir(name)))
        return unpackFailed();
    importedName = name;
    return {};
}

} // namespace dc
