# MIDI trigger mode — design

Date: 2026-11-06. Status: draft for approval.

## Intent

Today Dubcycling plays whenever the host transport runs ("autoplay") and ignores MIDI. The user wants to
play it: a held MIDI note starts both loops, releasing it stops them, both aligned to host bar lines.
This turns the plugin into something that is performed rather than only arranged.

## Goals

- A per-instance mode switch **Autoplay | MIDI** in the editor's top bar.
- **Autoplay** behaves exactly as today (loop positions follow the host transport).
- **MIDI mode:** playback is gated by held notes (decided by the user):
  - Any note-on on any channel counts. Pitch, velocity, length and channel are ignored.
  - Playback is on while at least one note is held; a chord is one gate.
  - Host transport **running**: the first note-on arms a start at the **next host bar line**; both loops
    start **from their beginning** there. Releasing the last note arms a stop at the **next host bar
    line**.
  - Host transport **stopped**: playback starts immediately from the loop beginning and runs at the host's
    last known tempo (`hostBpm_`; if none yet, 120 BPM — builder's call, see below). Releasing stops it at once.
- The mode is saved with the plugin state (per instance, restored by the host). It is **not** part of
  presets or banks, and loading a preset never changes it.

## Non-goals / anti-goals

- No note-to-region mapping, no velocity or pitch effects, no MIDI output, no MIDI learn.
- No change to Autoplay behaviour, to the preset/bank format, or to `bank.json`.
- Must not click, pump or get stuck: a lost note-off must never leave the plugin playing forever.

## Behaviour details (decided with the user, or marked as the designer's call)

- A note released before its start bar means nothing plays at all (designer's call).
- A note pressed again while a stop is pending cancels that stop (designer's call).
- A stop uses the existing short voice/cancel fade (~5 ms, `kCancelFadeMs`); effect tails ring out as in
  Autoplay (designer's call).
- Switching the mode while playing takes effect at the next bar line when the transport runs (immediately
  when stopped). Switching to MIDI with no note held silences the plugin; switching back to Autoplay
  resumes following the transport (designer's call).
- Right-click audition keeps working in both modes, independent of the gate.
- While the gate is off in MIDI mode the plugin is silent but keeps its model/UI state; the editor's
  playheads sit at the loop start.
- Transport stop/start in the host while a note is held (MIDI mode, running): playback continues
  uninterrupted from its own start origin, since MIDI mode does not depend on the host position
  after the start bar (designer's call; see "Position model").
- All-notes-off / reset events (MIDI CC 123, `juce::MidiMessage::isAllNotesOff`, `isAllSoundOff`) and
  `releaseResources`/`prepareToPlay` clear the held-note count (prevents stuck notes).

## Edge cases (designer's calls, from the builder check)

- **No valid host position** (`playing` but no/invalid `ppq`, or no `bpm`) counts as **stopped**: internal
  clock, immediate start/stop. A host can never leave the plugin waiting for a bar line that never comes.
- **Pending arms and transport changes:** a pending start whose host stops before the bar line becomes an
  immediate start (stopped rule). A pending stop whose host stops becomes an immediate stop. A note pressed
  while stopped starts at once; when the host starts running afterwards, playback continues on its own
  clock and is **not** re-aligned to the host grid (it is decoupled from host position after the start).
  A later release then stops at the next host bar line, which is only a stop time, not a loop phase.
- **Note on a bar line:** a note-on at or before the bar line's sample within a block starts at that bar;
  after it, at the next bar. Same rule for note-off and stop.
- **Fresh start every gate-on:** Loop 1 restarts from its beginning and the Loop-2 `Sequencer` is reset, so
  the first Loop-1 region entry draws anew. Nothing is carried over from the previous gate.
- **Gate-off** is modelled like a transport stop: voices end with the existing cancel fade, effect tails
  ring out as they do on a transport stop. Model edits while the gate is on follow the existing swap rules.
- **Held notes** are tracked as a per-channel/per-note bit set (robust against duplicate note-ons and
  stray note-offs) and cleared on all-notes-off, all-sound-off, `prepareToPlay` and `releaseResources`.

## Position model (the main design point)

Engine positions are derived from the host `ppq`. In MIDI mode playback needs its own origin:

- Keep an `originPpq` (the ppq of the bar line at which playback started). Effective ppq passed to the
  existing engine logic is `t.ppq - originPpq` as long as the host runs; Loop-1/Loop-2 phase and region
  entries then work unchanged from loop beginning.
- While the host is stopped (or reports no position) the engine runs an internal clock: it advances the
  effective ppq by `n * bpm / (60 * sampleRate)` per block using `hostBpm_`. If the host starts/stops or
  jumps while playing, the internal clock simply continues from its own origin (see above).
- Bar lines for arming start/stop come from the host `ppq` and time signature (`tsNum`, `tsDen`) as in the
  existing `beatsPerBar` logic; the start/stop is applied at the exact sample of the bar line inside the
  block (sample-accurate, split like the effects mask change points).
- Where this lives is the builder's call; the natural seam is a `TransportInfo` produced by the processor
  (virtual transport) so `Engine::process` stays unchanged for Autoplay.

## Constraints

- Audio thread: no allocation, no locks. MIDI is read from the `juce::MidiBuffer` in `processBlock` (the
  plugin already reports `acceptsMidi() == true`; it is registered as an instrument).
- Mode is read/written from the UI thread: use an `std::atomic`. Persist in the PluginState XML as an
  optional attribute (absent = Autoplay), keeping `kStateVersion` at 1 and older states loadable; the
  value is sanitised on load (unknown value = Autoplay).
- Tests: Catch2, deterministic. UI strings in English (`"Autoplay"`, `"MIDI"`).
- VST3 validation (Steinberg validator + pluginval) stays green; both tools send no/odd MIDI, so the
  default must be Autoplay.

## Left to the builder

- Exact UI widget (two-state toggle button vs. segmented control) in the top bar, tooltip texts.
- Default tempo when none known yet (suggest 120 BPM) and where the internal clock lives.
- Naming of new types/fields; test file layout.
- How the start/stop sample offset is split inside `processBlock`.

## Out of scope / ideas for later

- Quantisation choices other than "next bar" (beat, 2 bars, immediate).
- Latch mode (note toggles playback), choosing a trigger note.
