# Dubcycling – Design

As of: 2026-11-02. Statements by atze187 are unmarked. Everything marked **[Assumption]** is a decision by the design author that still needs to be confirmed.

## What it is about

Dubcycling is a VST3 plugin in which two audio loops are loaded and split into regions. Loop 1 is the master and always keeps playing audibly. Loop 2 is the slave: it only plays when a rule triggers it. When loop 1 reaches a certain region, chance (by weights) decides which region from loop 2 sounds along with it. The regions can get their own effects. The tempo follows the host BPM.

The appeal is that a loop pair sounds a little different on every pass, without having to rearrange it by hand.

## Goals

- Load two audio loops from disk (WAV, MP3, FLAC, AIFF), in stereo.
- Quantize both loops to whole bars and stretch them to the host BPM.
- Split both loops into regions that are not bound to the grid.
- Rules per loop 1 region: which loop 2 regions with which probability.
- Effects directly on the regions (details later).
- One stereo output.
- Start and stop follow the host transport.
- State is saved in the DAW session. Presets and banks (as of 2026-11-04) add to this, see `2026-11-04-presets-design.md`.

## Non-goals (for now)

- No performance mode in the first phase. It only comes once all functions are in place in edit mode; then it is decided what is relevant in it.
- No MIDI instrument and no pads.
- No separate outputs for loop 1 and loop 2.
- No recording from the host, only files from disk.
- No audio embedding in the session (can come later as an option per loop).
- No external VST plugins as effects, only built-in ones.
- Region suggestions for non-drum loops stay open.
- Platforms in this order: Windows first, then macOS, then Linux, each as VST3. Other plugin formats are not planned.

## Anti-goals

The question of how atze187 would recognize a failure was answered like this: "by using it". So there are no exclusion criteria defined up front. For the implementation this means: deliver something usable early and sharpen it in use, instead of perfecting everything beforehand.

## Loops and tempo

- Each loop is quantized to whole bars on loading and stretched to the host BPM.
- If the loops have different lengths in bars, both are multiplied up to the least common multiple (LCM) so they stay in sync. Example: 3 and 4 bars give 12 bars.
- Both loops are stereo. This deviates from dubgefahren, where `SampleData` is mono and limited to 60 seconds.
- Start, stop and position come from the host transport.
- If the BPM changes in the host (even mid-song), this is reflected in both loops: they follow the new tempo live. This is a decision by atze187. Consequence for the implementation: the stretching has to react to BPM changes, whether in real time or by recomputing in the background. A limit for the stretch ratio is needed and is up to the builder.
- **[Assumption]** How the bar count of a loop is determined (guessed automatically from the length and the host BPM, or entered by the user) is not fixed. See "Left to the builder".

## Regions

- Both loops are split into regions. The region boundaries are free and not bound to the bar or beat grid.
- Regions are set with the mouse.
- The plugin also makes suggestions for boundaries, initially optimized for drum loops (for example at transients). What this should look like for non-drum loops stays open.

## Rules

- A rule belongs to a loop 1 region. If you select a loop 1 region in the editor, its rule appears below.
- The rule consists of one row per loop 2 region, in this form:

  `A  |-------------o-------|  70%  [GATE/ONESHOT]`

  A knob for the weight, the percentage and a switch between gate and one-shot. The mode applies per target region, not per rule.
- When loop 1 reaches a region with a rule, a new draw is made on every pass: exactly one loop 2 region by weight. The weights add up to at most 100 %. The remainder up to 100 % is the probability of silence.
- If a loop 1 region has no rule, loop 2 does not play there.
- **Gate:** The chosen loop 2 region plays for as long as the triggering loop 1 region runs.
- **One-shot:** The chosen loop 2 region plays through once completely, even beyond the boundary of the loop 1 region.
- The regions from loop 2 have their own lengths independent of loop 1, so both modes are needed.

## Effects

So far only this is decided:
- There are built-in effects.
- They sit directly on the regions (not as a shared send).
- The details will be discussed later (which effects, how many slots, whether also per loop).

Open point for that discussion: with effects directly on the region, reverb and echo tails can be cut off at the region boundary. A send solution would have avoided that; it was put on hold.

The original wish also mentioned effect slots on individual regions or on whole loops. Whether whole loops get effects still has to be clarified.

## User interface

- There is an edit mode and, later, a performance mode. Only the edit mode is built first.
- The edit mode shows both loops as waveforms one above the other (loop 1 on top, loop 2 below, both with their regions). Below that appears the rule of the selected loop 1 region.
- Both loops are visible and editable in the editor at the same time.
- The look follows the "Variant A" sketch from the conversation: tracks one above the other, rule rows below.

## Saving and loading

- The state lives in the plugin state of the DAW session (the DAW stores it in the project file).
- The loops are stored there only as a reference: file path, file name and checksum.
- Regions, rules and effect settings are in the plugin state and are not lost when an audio file is missing.
- Presets: A bank is a folder with `bank.json` and `samples/`. A preset holds regions, rules, labels, bars and the bank's sample references. The preset bar (BANK, PRESET, SAVE, SAVE AS, ⋯ with export and import as zip) loads and saves them, and prompts on unsaved changes. The session keeps storing path and checksum of the loops. Details: `2026-11-04-presets-design.md`.
- If a path is invalid, the plugin looks for the loop itself (for example in the session folder or by file name and checksum). If it does not find it, it reports "File missing", and the user reassigns it.

## Technology (author's decision)

Everything in this section is an **[Assumption]**:
- C++20, JUCE 8, CMake, VST3, Catch2 tests.
- Platforms: Windows, then macOS, then Linux (decision by atze187). The code stays platform-neutral from the start (no Windows-specific code in `engine/` and `plugin/`), the build presets for macOS and Linux come with the respective phases.
- Structure like dubgefahren: `engine/` (pure DSP, without JUCE GUI) and `plugin/` (processor, UI), with `build.ps1` and CMake presets.

### What can be taken over from dubgefahren

Dubgefahren lives at `E:\Repos\dubgefahren` (GitHub: atze187/dubgefahren). Checked: project skeleton, `SampleLoader`, `GrainStretch`, the effect building blocks, `computePeaks` and `WaveformView`, `DgLookAndFeel`. The parts are copied into the new repo, not linked.

| Part | Takeover |
|------|-----------|
| CMake, presets, `build.ps1`, test setup | directly |
| `SampleLoader` (WAV, AIFF, FLAC etc., own thread) | with a change: stereo instead of the mono average, check the 60-second limit |
| `GrainStretch` (stretching in S950 style) | usable, sounds like a hardware sampler; whether it is enough for drum loops has to be heard |
| `Saturator`, `SvFilter`, `TapeDelay`, `Phaser`, `SpringReverb`, `Limiter` | individually portable; the fixed wiring `FxChain` is rebuilt |
| `computePeaks`, `WaveformView` | display transferable, but the marker logic has to be extended from 3 fixed markers to any number of region boundaries |
| `DgLookAndFeel`, controls, fonts | portable |

Not taken over: `PadRouter`, `NoteMap`, `Kit` (MIDI pads), `SampleRegion` (only one region per sample).

Not checked: whether dubgefahren already contains transient detection or tempo analysis.

## Left to the builder

- How the bar count of a loop is determined (auto-detection, input or both).
- Which method for transient detection for the drum suggestions.
- Quality of the stretching method (`GrainStretch` or another), as long as drum loops sound usable.
- How the randomness is realized (random source, whether reproducible).
- How transitions at region boundaries are smoothed (fades against clicks).
- How many regions per loop at most.
- Structure of the plugin state and versioning (since the presets, the state additionally carries the active bank and the active preset).
- Exact design of the interface within the sketch.

## Playback logic (author's clarifications)

All points here are an **[Assumption]** and supplement the rules above:
- A draw is made each time loop 1 enters a region with a rule, also after the loop wrap and after a jump of the transport.
- The drawn loop 2 region starts at its own beginning, not at a point derived from the host position offset.
- Loop 2 has at most one voice. A new draw aborts the running loop 2 region, including a still-running one-shot, and plays the newly drawn region. This is a decision by atze187. The abort happens with a short fade (author's assumption, against clicks).
- If the host transport jumps or loops, the position of loop 1 is recomputed and loop 2 is ended.
- Without a running transport the plugin is silent.
- No particular host is set as a target. Which DAWs are used for testing is up to the builder.

## Scope of the first version (author's assumption)

- The first version contains loading, quantizing and stretching, regions, rules, playback logic, state in the session and the edit mode.
- Effects are not part of the first version. The plan only reserves hooks in the signal path. The effects will be scheduled after a separate discussion, as agreed.
- The first usable state is: load two loops, set regions, edit rules, play along with the host transport.
- Transient suggestions and the relink for a missing file come after this state.

## Files and memory (author's assumptions)

- The 60-second limit from dubgefahren is dropped; instead a memory budget applies to the stretched and multiplied loops, which the builder sets.
- The checksum is a hash of the entire file.
- The search covers the session folder and the file's last saved folder.
- Before taking it over, the builder checks whether `GrainStretch` is real-time capable and whether the license allows the copy (same author, so probably unproblematic).

## Order of work

1. Confirm this document.
2. Implementation plan (for software: `superpowers:writing-plans`).
3. Afterwards effects in detail and the performance mode as separate rounds.
