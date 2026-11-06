# Dubcycling – Grid and Loop Mix per Rule (Design)

As of: 2026-11-05. Came out of the conversation with the user after the first test of the effects in Ableton. What the user
decided is under "Decisions by the user"; what Claude added is under "Decisions by
Claude" and is to be read as a proposal. This document builds on the state of `feat/effects` (effects, bank format
version 2).

Two independent changes in one document: A) grid for the region boundaries, B) mix knob per rule.

## What it is about and why

**A) Grid.** With more complex loops (for example 8 bars) it is tedious to set region boundaries exactly on beats by
hand. The waveform tracks get a visible grid, and the boundaries snap to it.

**B) Mix per rule.** The user plays an 8-bar drum loop on top (loop 1) and an 8-bar loop with
variations (drums, basses, pads) in whole bars below (loop 2). The same loop 2 region has to be
louder or quieter depending on the running loop 1 region so that it neither gets lost nor drowns things out. Today there is only the weight (probability)
and GATE/1-SHOT per rule (loop 1 region to loop 2
region). New is a mix knob that crossfades loop 1 against loop 2. This also makes it possible to hear only loop 2.

## Terms

- **Rule / target:** an entry in the rule list: loop 1 region (`region1`) points to a loop 2 region (`region2`)
  with weight and mode (`RuleTarget`).
- **Mix:** one value per target that determines how loop 1 and the played loop 2 region relate to each other.
- **Beat:** a quarter note (one beat). **Bar:** `time signature numerator` beats (4/4: 4 beats).

## Goals

1. Region boundaries in both tracks snap to quarter notes when setting, dragging and adding; the grid is visible in
   both tracks (beats faint, bar starts stronger).
2. Per target (loop 1 region × loop 2 region) there is a mix knob that crossfades loop 1 against the loop 2 region.
   It only acts while this loop 2 region is being played.
3. Existing projects, presets and banks sound unchanged (default = both loops full).
4. Changes of the mix (region change, knob movement) do not click and do not make the level jump.
5. The mix value belongs to the target: it moves along when splitting and merging regions, is saved with project and
   preset and is taken along in the bank export.

## Non-goals

- Grid finer than quarter notes, adjustable grid width, triplets. (Extensible, but not now.)
- Mix per loop 2 region independent of the loop 1 region (the user wants it per rule).
- Host automation of the mix, mix for loop 1 regions alone, mix for the preview.
- A master knob for loop 1 or loop 2.
- Moving existing boundaries onto the grid on loading or opening (only new movements snap).

## Anti-goals

It would be a failure if: an existing project sounds different after the update, the mix audibly
clicks or pumps on a region change, the grid makes setting harder instead of easier (for example jumping boundaries that cannot be
set freely), or the audio processing allocates memory or waits on locks.

## Decisions by the user

1. Grid: quarter notes (1/4). Visually a faint line per beat, a stronger one per whole bar. Visible and
   snapping, in the waveforms (both tracks).
2. Mix knob per rule, i.e. per loop 2 region for each loop 1 region (next to weight and GATE/1-SHOT in the rule row).
3. The mix is a true crossfade between loop 1 and the loop 2 region (not just a loop 2 level). It must be
   possible to let only loop 2 run.
4. The user will listen to the behavior at the changes (see "Timing") and then judge it.

## Decisions by Claude (to be cross-checked)

### Grid

- **Snapping always on, Shift key for free placement.** When dragging a boundary, when newly setting it by double-click and when
  adding, the position snaps to the nearest beat; if Shift is held while dragging, the position is free.
  If the nearest beat is not allowed because of the minimum distance between regions (`kMinRegion`), the normal
  clamping of `RegionMap` applies.
- **Grabbing stays unchanged.** Grabbing a boundary (hit tolerance) and selecting regions do not
  change; an existing boundary not lying on the grid stays where it is until the user drags it.
- **Bar division.** Bars per loop come from the Bars field of the track (`bars`), beats per bar from the numerator of the
  host time signature; 4 without a host value. Grid step in phase: `1 / (bars × beats per bar)`. The host's time signature is
  passed on from the audio thread to the editor for this (like the host BPM).
- **Drawing.** Beats as a faint line, bar starts stronger (color subtle, behind the region boundaries so that the
  boundaries remain the most prominent). If beats are less than about 4 pixels apart, only bar lines are
  drawn; if those are also less than about 4 pixels apart, nothing is drawn and nothing snaps.
- **Suggest (AUTO).** The suggested boundaries are rounded to the nearest beat; duplicates and boundaries that
  violate the minimum distance are dropped (as otherwise in `RegionMap::addBoundary`).
- **No grid without data.** Without a loaded file (or without a Bars field value) there is no grid.

### Mix

- **Value range and default.** `mix` 0 to 1, default 0.5 (center). Center means: loop 1 and the loop 2 region
  both run at full volume, exactly as before. To the left (towards 0) the loop 2 region gets quieter down to
  silent, loop 1 stays full. To the right (towards 1) loop 1 gets quieter down to silent, the loop 2 region stays full.
  Formula: `gain2 = min(1, 2 × mix)`, `gain1 = min(1, 2 × (1 − mix))`.
- **When the mix acts.** As long as a loop 2 voice is being played, its mix value applies to the loop 2 level and
  the same value to the loop 1 attenuation. When the voice ends (silence drawn, gate ends, one-shot finished), loop 1 returns to
  full level. Without a running voice loop 1 is full.
- **Which value applies.** The mix of the target that started the voice (loop 1 region at the start, loop 2 region). A
  gate voice that runs into another loop 1 region keeps the value of its target until it ends.
- **Changes.** All levels (loop 1 and loop 2) change via a short ramp (about 5 to 10 ms), never as a jump. A
  fading-out voice (`tail_`) fades out with its mix value; when a new voice begins, loop 1 goes from the current
  level to the new target level.
- **Change in the editor during playback.** If the user changes the mix of the target that is currently sounding, they hear it immediately
  (as with the effect switches); the running voice reads the value from the new model if its target still exists
  (same loop 1 region at the start and same loop 2 region with the same boundaries).
- **Preview.** Previewing a region ignores the mix (loop 2 preview at full level, loop 1 stays full) so that
  shaping does not depend on which rule currently applies.
- **Effects.** The mix acts on the loop 2 signal before the effects (input of the chain) and on loop 1 independently of
  the effects. The effect chain stays unchanged.
- **Display.** In the rule row a knob with double-click = center, labeled "MIX". The value is displayed readably
  (for example "L1 | L2" in the center, "L2 only" fully right, "L1 only" fully left; the builder chooses the notation).
  The knob is also there for rows with weight 0.
- **Format.** `RuleTarget` gets a field `mix` (float 0..1, default 0.5). In the plugin state (XML) `mix` comes as an
  optional attribute on `Target` (only written if not equal to 0.5; `kStateVersion` stays 1; if it is missing, 0.5 applies).
  In the presets (`bank.json`) `mix` comes as an optional field per target; because the bank format is extended,
  `version` rises from 2 to 3 (rule R6 of the preset decisions: older plugin versions reject newer banks). Version 1
  and 2 stay readable. On loading: NaN, infinity and text fall back to 0.5, numbers are clamped to 0..1. The mix
  counts toward the preset fingerprint (unsaved check), a default value of 0.5 gives the same fingerprint as
  missing data.
- **Merging and splitting.** The mix is attached to the target; the existing rules for index shifting (rules move
  with their regions) take it along, without a new special rule.

## How it feels (operation)

- When dragging a boundary it noticeably pulls onto the beats; with the Shift key held it is free. The faint
  and strong lines in both tracks show beats and bars; they line up in both tracks because both
  loops can have the same number of bars (with different bar counts each track has its own grid).
- In the rule row of a loop 1 region, next to each loop 2 region (row), there are weight, GATE/1-SHOT and the mix knob.
  With the mix left of the center point you hear loop 2 less, right of it loop 1 less.

## Engine (framework for the builder)

- Today loop 1 is rendered directly into `l`/`r`, loop 2 (voice, fading-out voice, loop 2 preview) into a bus
  through the effect chain and then added onto `l`/`r`. The mix acts in two places: the loop 2 voices receive a
  level (before the bus, i.e. before the effects), and loop 1 in `l`/`r` is multiplied with a
  smoothed gain before the bus is added.
- `Voice` gets the mix value with which it was started (and, for the re-evaluation on a seamless model swap,
  the information from which target it originates: loop 1 region at the start and `region2`).
- The target that started the voice is made known in the `Sequencer::Command` or at the start in the engine
  (the sequencer already knows `region1` and the target).
- `RuleSet`/`RuleTarget`: the field `mix` is sanitized on setting (like the weight: NaN/out of range gets clamped).
- No allocation, no locks in the audio processing; all smoothing runs per sample.

## What is left to the builder

- The concrete smoothing shape and duration (within 5 to 10 ms), the notation of the mix display, the colors and
  line widths of the grid, the split into files and functions.
- How the host's time signature gets from the audio thread to the editor (an atomic next to the host BPM is enough).

## Test aspects (unplayed inputs)

- Loop 1 and loop 2 with different bar counts (for example 4 and 8): each track has its own grid.
- Time signatures 3/4, 6/8 and 7/8 (numerator 3, 6, 7): beats per bar from the numerator.
- Very short tracks, very narrowly drawn tracks (hide the grid instead of pixel mush).
- Mix 0 and 1 at the edges: only loop 1 or only loop 2 respectively; mix NaN in the project.
- Region change mid-block with different mix values: no click, no level jump.
- Old projects and banks (version 1 and 2): load and sound as before.
- Mix change in the editor during playback: immediately audible, without a click.
- Splitting and merging loop 2 regions: the mix moves with the targets.
