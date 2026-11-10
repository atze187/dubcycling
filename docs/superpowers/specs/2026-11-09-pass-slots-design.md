# Pass slots — design

Date: 2026-11-09. Status: implemented.

## Intent

In Autoplay the Loop-2 playback contains random choices (which region plays when). The user sits in front
of the plugin, with two loops loaded and the regions configured, and listens. When a pass sounded good they
want to **keep that exact pass** and be able to bring it back later, by button or by MIDI note, without any
randomness. This is not a bounced audio file: it is a recorded sequence of events ("which region entered
when") that the plugin replays through its normal playback path.

## Goals

- The plugin keeps the **last finished pass** and lets the user **save** it to a slot.
- **8 slots**, stored **in the preset** (the saved-presets format and `bank.json` grow accordingly).
- **Replay** a slot (repeat until something else is triggered) or **chain** all non-empty slots.
- All controls are available as editor buttons and as learnable MIDI notes.
- Slots keep working when the model changes afterwards: they play what still fits.

## Non-goals / anti-goals

- No audio rendering or export. No per-pass copies of region settings, effects or mix.
- No editing of the content of a pass. No naming, no reordering slots, no undo of Clear.
- No MIDI output, no velocity or pitch dependence, no host automation of the slots.
- Autoplay without slots must stay identical to today. A preset with no saved passes behaves as before.
- Must not click, hang or play wrong data if a slot is empty, stale or damaged.

## Decisions made with the user

### What a pass is

One pass is **one pass of Loop 1**, from its start to its wrap. The plugin always holds the **last finished
pass**; it can be saved while the next pass is already running. A pass records the **Loop-2 region
entries** of that pass: for each entry, which region played and where in the pass it entered. Choices that were
random are recorded as made, so replay has no randomness. Nothing else is stored (no audio, no copies of
region parameters).

### Slots

- 8 slots, part of the **preset** (loading a preset replaces them; saving a preset stores them).
- **Save** stores the last finished pass in the **lowest free slot**. When all 8 slots are full, Save is
  **disabled**.
- Each slot has a **clear** button. A **Clear all** button clears every slot.

### Replay

- Clicking a slot **repeats that pass** until something else is triggered.
- A **Play** button (next to the group of eight slots) turns on the **chain**: all non-empty slots are
  played in order, slot 1 to 8, empty slots skipped, and the chain repeats.
- Turning Play off returns to the normal random playing. This is also how the user leaves a slot replay.
- If the model changed after saving, a slot **plays what still fits** and skips entries that can no
  longer be played (region deleted, outside the loop, ...). Region effects and mix come from the **current**
  region settings, since a pass stores only which region plays when.

### MIDI

- A single **Learn** button in the editor. In Learn mode, clicking a slot, Save or Play **arms** it; the
  next MIDI note binds to it. Outside Learn mode clicking a slot triggers it.
- Learnable targets: the 8 slots, Save, Play.
- Any note on any channel counts, as in MIDI trigger mode; velocity, length and channel are ignored.
- A second click on a waiting target only cancels the wait (a double-click must never delete a binding).
  Removing a binding is its own gesture: a right-click on a slot, Save or Play while Learn mode is on
  removes that target's note (and cancels its wait). (Set in the final review.)

## Designer's calls (to be checked by the user)

- Slot/Save/Play switches take effect at a **pass boundary** (the loop start), so replay always starts at
  the loop's beginning. An exception for a stopped host is the builder's call.
- Learned notes are stored **per plugin instance** (with the project state), **not** in presets or banks,
  because they describe the user's controller. Loading a preset never changes the bindings.
- A note bound to a slot/Save/Play **no longer counts as a gate note** in MIDI trigger mode; every other note
  still gates as today.
- Slots, Save, Play and replay work in **both** trigger modes. In MIDI mode the gate still decides whether
  anything sounds; replay only decides what plays while it is open.
- "Last finished pass" is the most recent pass that ran completely while playing; an incomplete pass (stop,
  jump, gate-off mid-pass) is not saved. Save is disabled while there is no such pass.
- Triggering an empty slot does nothing. The chain with no non-empty slot does nothing.

## Designer's calls (builder check)

- **Recorded entry:** one entry per Loop-2 draw of the pass: the **phase** of the pass (fraction 0..1 of one
  Loop-1 pass, so it survives tempo and length changes), the outcome (region 2 index, or **Silence**), and the
  mode (Gate or One-Shot) as drawn. The Loop-1 region index is not the key. Mix and effects are not
  stored; they come from the current region settings.
- **Silence is recorded** too, so a replay stops voices where the original did and does not sound
  different from the saved pass. Voices that span the pass wrap behave as in normal playback while
  recording and playing randomly. During **replay** a voice carried into a pass is stopped at every
  pass boundary: a replayed pass starts with Loop 2 silent unless it has an event at phase ~0 (amended
  in the final review; the recorded pass cannot tell a carried-over voice from silence).
- **"Plays what still fits":** an entry is replayed at its phase; it is skipped when its region 2 no
  longer exists (or the loop is gone). Boundary edits in Loop 1 do not invalidate slots, since entries are keyed
  by phase and not by Loop-1 region index.
- **Loop or tempo changes** keep the slots as they are (fractions of a pass). Loading a different preset
  replaces them with that preset's slots.
- **Replay gap:** a slot click, Play or MIDI trigger waits for the next pass boundary; until then the normal
  random playing continues. With the host stopped nothing sounds in Autoplay anyway, so triggers just arm.
- **Replayed passes are not recorded:** while a slot or the chain plays, the recorder is off, so "last
  finished pass" only comes from random passes and Save cannot store a copy of a replay. Save is disabled
  while replay is on.
- **Clearing the slot that is playing:** replay continues with the pass already loaded until the next
  pass boundary, then falls back to random playing (single slot) or goes on with the next non-empty
  slot (chain). Clear all while replaying ends replay at the next boundary.
- **Completeness** of a pass is decided by Loop-1 position only (one full wrap-to-wrap while playing);
  silence draws do not make a pass incomplete.
- **Bindings:** one note maps to at most one target; learning a note that is already bound moves it. Learn
  mode is not persisted (off after loading). Bindings are saved with the instance state, separate from the
  preset.
- **Threading and UI refresh** (playing slot, Save enabled): the editor polls atomics, as it does today;
  saved passes are handed to the message thread through a lock-free snapshot. Builder's call.

## UI: pad bar

Decided with the user after the first build (the single PASSES row was too inconspicuous):

- The pass controls are a **pad bar at the bottom edge** of the editor, over the full window width, in the
  panel look (flat dark surfaces, 1 px edges, monospace, squared corners; no gradients, glows or shadows).
- On top a slim **control strip**: PLAY, SAVE, LEARN on the left, CLEAR ALL on the right, the Learn hint
  LCD ("Waiting for note: slot 8") between the two groups when it fits. PLAY lights green while replay is on.
- Under it **eight equal pads** labelled 1..8 in large monospace, ~6 px apart; they grow with the window
  (72..110 px tall). Each pad has a fixed colour from one 8-colour palette (red, amber, green, aqua, blue,
  violet, magenta, orange; `plugin/ui/PadLook`), tested for distinct hues and a text-on-playing contrast of
  at least 4.5:1 (WCAG).
- **Pad states:** empty = LCD black, dim number, edge only. Filled = a dim version of the pad colour, number in
  text colour, a 6 px strip along the bottom with the pass's Loop-2 sequence (one segment per Start, from its
  phase to the next event; Stops and silence are gaps; `passStripSegments`) and a small x top-right
  (18x18 hit area) that clears the slot. Queued = 2 px amber frame. Playing = the full colour, dark text, a light
  inner edge.
- **Learn mode:** a waiting pad shows "..." (and an accent frame), a bound pad its note name (small label
  bottom-right); first click arms, a second click only cancels, right-click removes the note; the x is inactive.
- **Window:** default 900x660 (was 900x600), minimum 800x600 (was 800x540). The bar takes ~120 px at the
  default size; loop lanes and the rule/effects row share what is left above it.

## Constraints

- Audio thread: no allocation, no locks. Recording and replay live in the JUCE-free `engine/` where
  possible, tested with Catch2.
- UI strings in English. The editor must show slot state (empty / filled / playing) and the Play,
  Save, Learn state.
- Preset and bank formats stay loadable: files without slots load with 8 empty slots; unknown or
  malformed slot data is dropped, never a crash. Bump the bank version only if needed.
- VST3 validation (Steinberg validator + pluginval) stays green; the default state is no slots, replay off.

## Left to the builder

- Data layout of a recorded pass, and where the recorder sits relative to `Sequencer`/`Engine`.
- How replay is injected into the Sequencer so that "what fits" is decided at playback.
- Widget layout of slots, Play, Save, Learn, Clear in the editor (since settled: see "UI: pad bar").
- Serialisation names and version handling.
- How MIDI notes are routed between gate logic, Learn and bindings in `MidiTrigger`/processor.
