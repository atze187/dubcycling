# Effects section redesign — design

Date: 2026-11-23. Status: draft (awaiting approval).

## Intent

The effects panel is cramped: four modules with all their knobs side by side, next to the rule panel. The user
wants a roomier, two-part panel, a fifth effect (a tempo-synced delay with a reverse function) and an Flanger
that can stand still. The rule panel should get the space the effects panel gives up.

## Goals

- **Two-part FX panel.** Left: a vertical list of all effects, each row with a drag handle, the effect's colour,
  its name and a quick on/off switch. Right: the edit view (the knobs) of the effect selected in the list.
- **Every effect is always in the list.** Each effect type exists exactly once; no effect is hidden or removable.
- **Chain order is user-editable by drag and drop** in the list.
- **New effect: Delay** with a reverse function, time synced to the host tempo in note values up to 2 bars.
- **Flanger can stand still** (a fixed sweep position instead of a moving one).
- **More room for the rule panel** (the FX panel gets narrower).

## Non-goals / anti-goals

- No LFO matrix and no modulation routing (the user will discuss "assignable LFOs" later, separately).
- No per-region parameter sets: one parameter set per effect for all Loop-2 regions, as today. Only the on/off
  switch is per region.
- No per-region chain order; no second instance of an effect type; no removing or hiding effects.
- No host automation of effect parameters (unchanged).
- Existing effects keep their sound: with the default order, Delay off and Flanger rate/depth unchanged, output
  stays as it is today.

## Decisions made with the user

- Two-part panel: list on the left (vertical, quick on/off per effect), edit view on the right.
- All effects are visible in the list; the order is changed by drag and drop.
- Delay with reverse. **Reverse acts on the echoes**: the delay records a block as long as the delay time and
  plays it back reversed; the dry signal is not reversed. With feedback every repeat is reversed again as it
  recirculates. (Not a pre-delay reversal of the input.) Settled: **every echo plays reversed**; the feedback is
  taken from the recorded (forward) block, not from the reversed output, so the second repeat is not forward again.
  The first echo comes one delay time after the input (inherent to reversing a block).
- **Delay time is tempo-synced in note values**, up to **2 bars**.
- The Flanger gets a standing mode.

## Approach

Only one sensible way: the effect chain already lives in `engine/fx` with a fixed order, the per-region switches
are per effect id (bit mask), and the UI is a Component per panel. So:

- **Chain order** is a permutation of the five effects stored in `FxSettings`, **global per plugin instance**,
  saved with presets and projects. The engine processes the chain in that order. The per-region switch bits stay
  keyed by effect id (not by position), so reordering never moves a switch from one effect to another.
- The FX panel list and edit view replace the four side-by-side modules.

Rejected: a per-region order (the per-region switches already cover "which effects for which region"; a second
dimension is not worth the complexity); fixed order (the user asked for reordering).

## Behaviour

### Effect identity and order

- A fifth effect id `Delay` is added with the next free bit (bit 4) in the per-region mask (the mask stays one
  byte). The ids of the existing effects do not change.
- The **default order** is Bitcrush, Filter, Flanger, Delay, Reverb. Older projects and presets load with this order
  and with Delay off in every region.
- The order is always a valid permutation of all five effects; anything else in saved data is replaced by the
  default order.
- Changing the order while audio plays must not click or hang. Effect tails (Flanger, Delay, Reverb) behave as
  today when an effect is switched off or the host stops. A brief crossfade or a short mute around a reorder is
  acceptable (builder's call, as long as there is no click); the bit-identical guarantee applies while the order
  is not being changed.

### Delay

- Parameters: **Time** (note value), **Feedback**, **Mix**, **Reverse** (on/off).
- **Time** is a stepped choice of note values synced to the host tempo (and to the time signature for "bar"
  values), from a short value (suggested 1/32) up to **2 bars**, including dotted and triplet values. The exact
  list is the builder's call but must contain at least 1/16, 1/8, 1/4, 1/2, 1 bar and 2 bars, each straight, and
  triplets and dotted values for the divisions up to 1/2.
- A "bar" is the host's beats per bar (the same numerator the beat grid uses; 4 without host info). Tempo changes
  retime the delay without clicks (builder's call how).
- **Feedback** 0..max below runaway (builder picks the maximum, suggested ≤ 0.9, never self-oscillating).
- **Mix** is wet/dry blend like the other inserts: the dry signal is never reversed.
- **Reverse on:** the echo plays the recorded block backwards (see Decisions). **Reverse off:** a normal delay.
  Toggling Reverse while sounding must not click.
- Behaviour with the gate (per-region switch) is like Flanger and Reverb: the delay input is gated, the wet tail
  rings out after the gate closes or the host stops, then the delay sleeps.
- The buffer holds up to **30 s** per channel at the current sample rate. If the host tempo is so slow that a
  chosen note value would exceed that (2 bars at 4/4 below 16 BPM), the delay time is **clamped** to the buffer
  length; the UI keeps showing the chosen note value. Without host tempo (or with the host stopped) the delay uses
  the last known tempo (the engine's existing fallback, 120 BPM before any), so the tail rings out with a stable
  time.
- Accepted deviation (implementation): with Reverse on the recorder needs twice the block length of history, so the
  reverse block is capped at about 15 s (2 bars of 4/4 below 32 BPM); forward mode still clamps at 30 s.

- Presets saved at one tempo play at the current host tempo (the time is a note value, not seconds).

### Flanger standing

- **Rate** now goes down to **0 Hz**. At rate 0 the sweep stops at a fixed position set by a new parameter
  **Manual** (0..1, the position within the sweep range; default 0.5). At rate > 0 Manual has no effect.
- The existing parameters, ranges (other than the lower bound of Rate) and defaults are unchanged, so existing
  presets sound as before. Changing Rate from 0 upwards and back must not click or jump (builder's call, e.g.
  the sweep keeps its phase).

### UI

- **List (left):** five rows, one per effect, in chain order. Each row: drag handle, a coloured marker in the
  effect's colour (as today), name, and an on/off switch that toggles that effect **for the selected Loop-2
  region** (the same switch as today's, the same one the strip markers under the Loop-2 lane show). Without a
  valid selected region the switches are disabled (greyed), as today. Clicking a row selects it. Drag and drop of a row
  changes the chain order live and works regardless of the region selection; the dragged row is shown lifted with
  an insertion line at the drop position.
- **Edit view (right):** the knobs (name, value, slider; Filter Type as buttons) of the selected effect, using the
  existing parameter controls, with double-click resetting to the default. The selected effect is a UI state, not
  persisted; initial selection: first effect in the list.
- **Strip under the Loop-2 lane** (marker rows per effect and region): follows the chain order too, so the rows
  line up with the list. (Row order only; the strip's behaviour stays the same.)
- **Layout:** the FX panel is narrower than today's (it needs the list ~120–150 px plus one effect's knobs); the
  rule panel gets the freed width. Minimum window size stays 800x600, and the rule panel's rows must still fit
  there. UI strings are English; the industrial panel look of the rest of the plugin applies (flat, monospace).
- Delay's Time control shows the note value as text ("1/8", "1/4 dotted", "2 bars", ...).

### Persistence and compatibility

- The order and the new parameters (Delay time/feedback/mix/reverse, Flanger Manual) are saved in the project
  state and **in each preset of a bank** (the order is part of the preset, like the effect settings). `bank.json` goes to the next version (6); the state XML gets optional
  attributes only (`kStateVersion` stays 1). Absent data means defaults (default order, Delay params at default,
  Manual at default), so older presets and projects load unchanged.
- Banks of a newer version than 6 are still refused as too new (existing behaviour). Older plugin versions drop the
  new data when they re-save, as with earlier additions.
- The preset fingerprint (unsaved-changes detection) includes the new data only when it differs from the default.

## Constraints

- Audio thread: no allocation, no locks. The engine part (order, Delay) is JUCE-free in `engine/fx`, tested with
  Catch2; with the default order and Delay off, output is bit-identical to today (existing tests unchanged).
- The switches stay one byte per Loop-2 region.
- VST3 validation (Steinberg validator + pluginval, strictness 5) stays green.
- UI strings English. Minimum window 800x600 stays usable.

## Designer's calls (to be checked by the user)

- The list's quick switch is the per-region switch for the selected region (not a global bypass).
- Order is global per instance and saved in each preset (not per region, not per pass slot).
- Delay parameters: Time, Feedback, Mix, Reverse; no filter or tone in the feedback path, no ping-pong, no free
  millisecond time.
- Flanger standing = Rate down to 0 plus a Manual position knob. (Rate is linear today, so a lower bound of 0
  needs no new scale.)
- Delay: all echoes reversed, buffer 30 s with clamping at very slow tempos, last known tempo as fallback.
- Default order places Delay between Flanger and Reverb.
- The effect selected in the list is not saved.

## Left to the builder

- How the engine applies a changed order without clicks; the data layout of the permutation.
- The delay's reverse algorithm (block recorder, windowing/crossfade of reversed blocks), tempo-change handling,
  buffer sizing and the exact note-value list.
- The widget implementation of the list and drag and drop, the panel's pixel layout, and the exact split between
  the FX panel and the rule panel.
- The serialisation names and the bank-version handling.
