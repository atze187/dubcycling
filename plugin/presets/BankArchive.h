#pragma once
#include <juce_core/juce_core.h>
#include "plugin/presets/BankStore.h"

namespace dc {

constexpr int kMaxArchiveEntries = 2048;
constexpr juce::int64 kMaxArchiveBytes = juce::int64(1024) * 1024 * 1024; // sum of the uncompressed sizes
constexpr juce::int64 kMaxBankJsonBytes = juce::int64(4) * 1024 * 1024;   // bank.json alone (parsed in memory)

struct ArchiveLimits
{
    int maxEntries = kMaxArchiveEntries;
    juce::int64 maxBytes = kMaxArchiveBytes;
    juce::int64 maxBankJsonBytes = kMaxBankJsonBytes;
};

// Writes <bank>/bank.json and the samples the presets use as <bank>/samples/<name> into zipFile.
// A bank folder name that bankNameProblem rejects aborts first (InvalidName, with that message).
// Every used sample is checked first: a missing one (or a stored path that is not
// "samples/<name>") aborts with SampleMissing naming it; a name import would reject (not
// isPortableFileName, spelled differently from the file on disk, or two packed names differing
// only in case) aborts with SampleNameInvalid naming it. The zip is written to a temp file next
// to zipFile and moved over it only when complete, so a failure leaves zipFile as it was.
BankStore::Result exportBank(const BankStore& store, const juce::String& bank, const juce::File& zipFile);

// Imports a zip from someone else (hostile input). Before anything is created in store.root(),
// every entry is checked: exactly one top folder with a valid bank name (a "__MACOSX" tree does
// not count), only <top>/bank.json and files <top>/samples/<name> are unpacked (plus folder
// entries for both); other files directly in <top>, the "__MACOSX" tree and "._*" files are
// skipped, any other folder is refused. For every entry, skipped or not: no "..", ".", empty
// parts, absolute paths, drive letters, backslashes, symlinks or unsafe names; no case-insensitive
// duplicates among the unpacked files; at most
// limits.maxEntries entries, limits.maxBytes declared bytes in all and limits.maxBankJsonBytes
// for bank.json (ArchiveInvalid, ArchiveTooLarge).
// Then it unpacks into a temporary folder inside store.root() (an entry with more or less data
// than declared is rejected), requires bank.json to parse (BankTooNew, BankUnreadable) and every
// loop path in it to be "samples/<name>" of a file in the zip (ArchiveInvalid), and finally moves
// the folder to the top folder's name, or "Name (2)", "(3)" ... when that is taken; an existing
// bank is never touched. On any failure nothing remains in store.root(). importedName gets the
// final name on success and stays unchanged otherwise.
BankStore::Result importBank(BankStore& store, const juce::File& zipFile, juce::String& importedName,
                             const ArchiveLimits& limits = {});

} // namespace dc
