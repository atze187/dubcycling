# Dubcycling – Presets and Banks (Design)

As of: 2026-11-04. Came out of the conversation with the user (backlog item 5). What the user decided is stated
as a decision; what Claude added as a proposal is under "Decisions by Claude".

## What it is about and why

For each live session the user builds setups from two loops, regions, labels and rules. When a setup sounds good,
they want to capture it with one button, a name and no file dialogs, repeat it later and hand it on to others
without hassle. Presets are not meant for switching while playing ("probably not at all"), but for preparing,
finding again and passing on. Later, when non-drum loops and effects per region are added, presets become more
important.

## Terms

- **Preset:** the complete saved state of the plugin: both loops (file, hash, bars, region boundaries,
  labels) and the rules (weights, mode Gate/One-Shot, the silence share follows from the remainder).
  Effects per region will be added later; the format has to be able to take them.
- **Bank:** a folder in the Dubcycling preset folder. It belongs to a live session or a style (the user's
  examples: "atmospheric jungle", "jump up", "techstep") and contains several presets together with the loop files
  they need.
- **Preset folder:** the Dubcycling folder in which all banks live and into which imports are unpacked.

## Goals

1. Saving a preset is one button press plus entering a name.
2. A bank is self-contained: bank file and loop files lie together, the paths are relative. A
   bank can be copied, moved and passed on without anything breaking.
3. Export is a zip of a bank, import is unpacking into the preset folder. No paths have to be
   rewritten.
4. Several banks side by side.
5. The file format is extensible (effects per region, further state fields) without invalidating old banks.

## Non-goals

- Preset switching while playing: no MIDI program changes, no connection to the host's program list,
  no seamless switching during playback. Loading a preset may take a moment.
- Sharing via cloud, online library or standard host preset formats (.vstpreset, .fxp/.fxb in the
  VST2 format). The concept is modeled on fxb/fxp, the format is its own.
- Cleanup function for unused samples (folder grows; possible later).
- Changing the preset folder via a setting.

## Anti-goals (it would fail on these even if it works technically)

- An exported zip that does not work on another machine (missing samples, broken paths).
- A load that leaves the plugin in a half state, or a save that damages an existing bank.
- The plugin silently deleting something the user saved.
- File access in the audio thread.

## Decisions by the user

- Flow: press a button, the plugin asks for a name, the user enters it, the plugin saves.
- There are several banks, named by the user.
- A bank is a folder with a bank file and a subfolder `samples`. The paths in the bank file are relative.
- The samples are only copied into the bank folder when a preset is saved, not when the loop is loaded.
  Each bank has its own copies, even if the same file appears in several banks (double space is
  fine).
- The plugin never deletes a sample from `samples`. If the user replaces a loop in a preset, the old
  sample stays there locally.
- Export only packs samples that at least one preset of the bank uses. Unused ones are kept locally.
- The zip contains exactly one folder with the name of the bank as its top-level element.
- Import: the user unpacks the zip into the Dubcycling preset folder, the bank then appears in the plugin.
- Format of the bank file: JSON.
- In the selection lists the current entry is marked: in the bank list the selected bank, in the
  preset list the active preset.
- Prompt on loading: if the user picks a preset while the current state is unsaved, the
  plugin asks before loading. It replaces the state only after confirmation (protection against a misclick).
  Implementation following the pattern of the name field: in the same row, instead of the preset selection, a message
  appears ("Discard unsaved changes?") with LOAD and CANCEL, no separate dialog window (Claude's proposal
  for the presentation, the user only decided the prompt itself).
- Save overwrites the loaded preset. Without a loaded preset it asks for a name. "Save as"
  creates a variant under a new name.
- Interface (draft "A", approved by the user, in the look of the existing panel): in the top row next to the
  logo: field BANK (selection list), field PRESET (selection list), buttons SAVE (emphasized in blue), SAVE AS and "⋯", BPM on the right.
  The "⋯" menu contains: New bank…, Export bank…, Import bank…, Open presets folder. After SAVE AS (and
  for SAVE without a loaded preset) a name field with OK and CANCEL replaces the
  preset selection in the same row. No separate dialog window. The loop strips and the rule panel below stay unchanged.
  The row gets tight with a narrow window; the display "Cycle: n Bars" may have to give way or
  move for that (build question).

## Decisions by Claude (please check while reading)

1. **File format (format JSON specified by the user):** The bank file is called `bank.json`. It contains an object with
   `version` and a list `presets`; each preset has `name` and the same content as the session state
   (both loops with file, hash, bars, region boundaries and labels, plus the rules), only with a relative
   `path` (slashes `/`, relative to the bank folder). The session state in the host stays XML as before; the
   bank gets its own JSON reader and writer (e.g. `juce::JSON`), which applies the same sanitizing rules and
   upper limits as `deserialize` in `PluginState.cpp`. Any extension of the format that older
   versions could not preserve when saving (new fields, new entry kinds) raises `version`; an
   older version rejects such a bank with a message (too new) and never writes it. A bank with an unknown
   higher version is skipped with a message, never overwritten. If a bank contains entries that the plugin
   skips when reading (not an object, preset without a name, more than 1024 presets), saving is refused with a
   message instead of losing them on write-back. Numbers are written with full precision (up to
   17 significant digits) so that a saved preset exactly equals the state it was saved
   from.
2. **Preset folder:** default `Documents/Dubcycling/Presets` (`juce::File::userDocumentsDirectory`). Every
   subfolder with `bank.json` is a bank.
3. **Sample storage on save:** If the loop already lies in `samples` of this bank (e.g. after loading a
   preset), nothing is copied. Otherwise it is copied under the original name, where characters that are not allowed in
   file names on all systems are replaced by `_` (also trailing dots and spaces
   removed, reserved Windows names prefixed with `_`, length limited); if the name already exists (in any
   upper/lower case) with a different hash, a counter is appended (`name (2).wav`), with the same hash the
   existing file is reused in its on-disk spelling.
4. **Saving with a missing loop:** If a slot is Missing or Failed (file not readable), nothing is saved
   and the plugin reports it. If a slot is empty, it is saved as empty.
5. **Loading a preset:** Replaces the complete plugin state (both loops, rules) like restoring the
   host state, with paths resolved absolutely into the bank folder. Loading and decoding the loops runs as
   before in the background. A missing sample results in the existing state "Missing" for that slot, not a
   crash. If the current state is unsaved, the plugin asks beforehand (see decision by the user
   on the prompt). Unsaved means: the plugin state (loops, bars, boundaries, labels, rules) deviates
   from the state that was last loaded or saved; without an active preset the empty state counts as the baseline,
   so a setup with a loaded loop is unsaved. The state is compared, not the interface. A
   permanent "modified" marker is not planned.
6. **Active preset in the host state:** Bank name and preset name of the last loaded or saved preset
   are stored as optional fields in the session state (version stays 1), so that SAVE keeps overwriting the right preset after
   reopening the project. If it no longer exists, SAVE acts like SAVE AS.
7. **Names:** Preset names: sanitized like labels (control characters removed, trimmed), at most 32 characters,
   not empty, unique within a bank (if the name exists, the name field shows a message instead of
   overwriting; SAVE is for overwriting). Bank names are folder names: invalid file name characters
   (`\ / : * ? " < > |`), trailing dots and reserved Windows names are rejected or sanitized, at most 64
   characters, unique in the preset folder (case-insensitive).
8. **Order:** Presets are in the order in which they were created. Banks alphabetical.
9. **Export:** The user picks the destination via a file dialog, suggestion `<Bank>.zip`. If a sample that a preset
   needs is missing, the export aborts with a message and names the file, instead of writing an incomplete zip. Zip created with
   `juce::ZipFile::Builder`.
10. **Import:** Pick a zip via a file dialog and unpack it into the preset folder (this is the operation for "unpack the zip into
    the preset folder"; a folder unpacked by hand works just the same). If a bank with
    that name already exists, it is imported as `Name (2)`; never overwrite. When unpacking: exactly one top-level
    folder (a `__MACOSX` folder does not count), entries with `..`, absolute paths or drive letters
    are rejected (zip slip), even if they would otherwise be skipped. Only `bank.json` and files under
    `samples/` are unpacked; other files next to `bank.json` (e.g. `.DS_Store`, `Thumbs.db`,
    `desktop.ini`), the `__MACOSX` folder and `._*` files are skipped, other subfolders rejected.
    Size upper limit against zip bombs. On export the bank folder must have a valid bank name. Unpacking goes into
    a temporary folder in the preset folder and is only moved to the target name after complete verification.
11. **Atomic writing:** `bank.json` is written to a temp file and then replaced, so that an abort does not
    damage the bank.
12. **No deleting or renaming of presets in this version.** The bank file is readable JSON, the menu can open the
    preset folder in the file manager. This is a deliberate omission, not an oversight.
13. **Threading:** File accesses never run in the audio thread. Copying and zipping may block the UI briefly
    (loops are small) or run in the background, the builder decides.
14. **Structure in the code (proposal):** The logic for banks (reading, writing, sample storage, zip, naming rules) lives
    in a unit of its own without UI (e.g. `plugin/presets/`), so that it is testable without the editor. The
    interface (bar in the editor) only hooks into it.

15. **Meaning of the selection lists (result of the review):** The bank selection only chooses which presets the
    preset list shows and which bank SAVE and SAVE AS write to; it loads nothing. A preset is only
    loaded when the user picks it in the preset list. If the user switches the bank without picking a preset,
    there is no active preset, and SAVE asks for a name like SAVE AS. On first start there is no bank:
    SAVE and SAVE AS then additionally ask for a bank name and create the bank ("New bank…" in the menu
    remains the direct way). The prompt before loading always compares with the active preset (last
    loaded or saved), even if another bank is currently selected; the bank selection alone does not change
    the comparison baseline.
16. **Several plugin instances:** The bank logic keeps no cache of its own. Each save reads
    `bank.json` anew immediately beforehand, changes the one preset and writes back atomically. The preset list is
    re-read when the selection is opened. Saving into the same bank from two instances at the same
    moment is not safeguarded (last one wins).
17. **Hash mismatch on loading:** A sample whose hash does not match the stored one is treated like when
    restoring a host state; the builder follows the existing behavior and does not invent a second
    way.

## What the builder decides

- Structure and names of the classes, file split, test strategy (recommended: test the bank logic with temporary folders,
  UI test for the bar in the style of the existing editor tests).
- Exact behavior of the bar with a narrow window and what the selection lists look like.
- Whether copying and zipping run in the background.
- Texts of the messages (language like the rest of the plugin, English).

## Constraints

- C++20, JUCE 8.0.15, existing structure (`engine/` pure C++, `plugin/` with processor, state, UI).
- New tests run with Catch2 in the existing suite; all previous tests (217) stay green.
- VST3 validation (Steinberg validator and pluginval) stays green; the host's program list stays
  unchanged (one program "Default").
- The existing session state stays backward compatible.
