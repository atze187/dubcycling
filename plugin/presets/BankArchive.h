#pragma once
#include <juce_core/juce_core.h>
#include "plugin/presets/BankStore.h"

namespace dc {

constexpr int kMaxArchiveEntries = 2048;
constexpr juce::int64 kMaxArchiveBytes = juce::int64(1024) * 1024 * 1024; // sum of the uncompressed sizes

struct ArchiveLimits
{
    int maxEntries = kMaxArchiveEntries;
    juce::int64 maxBytes = kMaxArchiveBytes;
};

// Writes <bank>/bank.json and the samples the presets use as <bank>/samples/<name> into zipFile.
// Every used sample is checked first: a missing one (or a stored path that is not
// "samples/<name>") aborts with SampleMissing naming it. The zip is written to a temp file next
// to zipFile and moved over it only when complete, so a failure leaves zipFile as it was.
BankStore::Result exportBank(const BankStore& store, const juce::String& bank, const juce::File& zipFile);

// Imports a zip from someone else (hostile input). Before anything is created in store.root(),
// every entry is checked: exactly one top folder with a valid bank name, only <top>/bank.json and
// files <top>/samples/<name> (plus folder entries for both), no "..", ".", empty parts, absolute
// paths, drive letters, backslashes, symlinks, unsafe or duplicate file names, at most
// limits.maxEntries entries and limits.maxBytes declared bytes (ArchiveInvalid, ArchiveTooLarge).
// Then it unpacks into a temporary folder inside store.root() (an entry with more or less data
// than declared is rejected), requires bank.json to parse (BankTooNew, BankUnreadable) and every
// loop path in it to be "samples/<name>" of a file in the zip (ArchiveInvalid), and finally moves
// the folder to the top folder's name, or "Name (2)", "(3)" ... when that is taken; an existing
// bank is never touched. On any failure nothing remains in store.root(). importedName gets the
// final name on success and stays unchanged otherwise.
BankStore::Result importBank(BankStore& store, const juce::File& zipFile, juce::String& importedName,
                             const ArchiveLimits& limits = {});

} // namespace dc
