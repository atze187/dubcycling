# MIDI Trigger Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A per-instance Autoplay | MIDI switch; in MIDI mode a held note (any note, any channel) starts both loops from their beginning at the next host bar line and releasing the last note stops them at the next bar line.

**Architecture:** A pure, JUCE-free class `dc::MidiTrigger` (engine/) turns one host block plus the block's note events into consecutive *segments*, each with its own virtual `TransportInfo` (`playing=false` = silent, otherwise a virtual ppq that starts at 0 at the start bar and advances at the host tempo). `DubcyclingProcessor::processBlock` reads MIDI, asks the trigger for segments and calls `Engine::process` once per segment. `Engine` stays unchanged: Autoplay passes the host transport through untouched; MIDI mode feeds it a virtual transport. The mode lives in the processor (atomic), is saved as a separate state attribute (not in `PluginState`, so presets/banks never carry or change it) and is toggled from a top-bar button.

**Tech Stack:** C++20, JUCE 8.0.15, Catch2. Build/test: `powershell -NoProfile -ExecutionPolicy Bypass -File ./build.ps1 test|build|validate`.

**Spec:** `docs/superpowers/specs/2026-11-06-midi-trigger-design.md` (binding; its "Edge cases" section decides the cases below).

## Global Constraints

- English everywhere (code comments, UI strings, docs, commit messages). UI strings exactly: button text `Autoplay` / `MIDI`.
- Default mode is Autoplay. A state without the mode attribute, or with an unknown value, loads as Autoplay. `kStateVersion` stays 1; `bank.json` and the preset/bank format are not touched.
- Autoplay behaviour is byte-for-byte what it is today (the segment is the unmodified host transport, one segment per block).
- Audio thread: no allocation, no locks, no exceptions. `MidiTrigger` uses fixed-size storage only.
- Baseline: 472 tests pass before starting; all pass after every task. Commits: `git -c user.name=atze187 -c user.email=14954322+atze187@users.noreply.github.com commit ...`, message ends with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never push.
- Default tempo when none was ever known: 120 BPM.

## Review Focus

1. Stuck playback: a lost note-off, all-notes-off/all-sound-off (CC 123/120), `prepareToPlay`, `releaseResources` must clear held notes and any armed start/stop.
2. A block longer than a bar, a bar line inside the block, and a note exactly on/after the bar-line sample (segment boundaries must be contiguous, positive, sum to the block length, never exceed the fixed capacity even with hundreds of events).
3. Host oddities: `playing` with no/NaN `ppq`, `bpm` 0, time-signature change (3/4, 6/8), host jump or stop while a start/stop is armed or while playing.
4. Mode switching while playing (Midi→Autoplay and Autoplay→Midi with and without held notes) and the state: unknown/missing attribute, preset load must not change the mode.
5. VST3 validator and pluginval run in Autoplay by default and must stay green.

---

### Task 1: `MidiTrigger` core (pure logic)

**Files:**
- Create: `engine/MidiTrigger.h`, `engine/MidiTrigger.cpp`, `tests/test_MidiTrigger.cpp`
- Modify: `engine/CMakeLists.txt`, `tests/CMakeLists.txt` (register the new files like the existing ones)

**Interfaces:**
- Consumes: `dc::TransportInfo { bool playing; double ppq; double bpm; int tsNum; int tsDen; }` and `dc::beatsPerBar(int tsNum, int tsDen)` (existing, `engine/LoopTiming.h`).
- Produces (namespace `dc`):
  ```cpp
  enum class TriggerMode { Autoplay, Midi };
  struct GateEvent { int frame; enum class Kind { NoteOn, NoteOff, AllOff } kind; int channel; int note; };
  struct TriggerSegment { int frame; int length; TransportInfo t; }; // t.playing == false: silent
  class MidiTrigger {
  public:
      static constexpr int kMaxSegments = 32;
      static constexpr double kDefaultBpm = 120.0;
      void prepare(double sampleRate);   // stores the rate, then reset()
      void reset();                      // clears held notes, gate, armed start/stop, virtual clock, known tempo
      void setMode(TriggerMode m);       // audio thread, between blocks
      TriggerMode mode() const;
      // events sorted by frame ascending, frame in [0, n). Writes contiguous segments covering [0, n)
      // exactly into `out`, returns their count (1..kMaxSegments).
      int plan(const TransportInfo& host, const GateEvent* events, int numEvents, int n,
               std::array<TriggerSegment, kMaxSegments>& out);
  };
  ```
- Semantics (from the spec): Autoplay → one segment `{0, n, host}`. Midi → gate = any held note (per-channel/note bit set, so duplicate note-ons and stray note-offs are harmless). "Running" = `host.playing` with finite `ppq >= 0` and a usable `bpm` (else the last known tempo; none ever → `kDefaultBpm`); anything else counts as stopped. Running: a gate edge arms a start/stop at the next host bar line (`ppq` of a bar = multiple of `beatsPerBar(tsNum, tsDen)`; frame = `ceil((barPpq - ppq) / ppqPerFrame - 1e-9)`; an edge at or before that frame's sample uses that bar, otherwise the next). Stopped: start/stop happen at the edge's own frame. Playing segments carry a virtual ppq that is 0 at the start frame and then advances by `bpm / (60 * sampleRate)` per frame, continuous across blocks, independent of host jumps; `bpm`, `tsNum`, `tsDen` are copied from the host (tempo falls back as above). A start always restarts from virtual ppq 0. Pending arms follow the spec's edge-case rules (host stops → becomes immediate; re-press before a pending stop cancels it; release before the start bar cancels the start; mode switch takes effect at the next bar line when running, at once when stopped; Autoplay→Midi with a held note = a gate-on start at the next bar; Midi→Autoplay = pass-through from that bar). When more segments than `kMaxSegments` would be needed, remaining state changes are deferred to the next block (never overflow).

- [ ] **Step 1: Write the failing tests** in `tests/test_MidiTrigger.cpp` (Catch2, test names as given; sampleRate 48000, bpm 120, 4/4 ⇒ 1 beat = 24000 frames, ppq per frame = 1/24000; `n = 512` unless stated; host block start ppq 3.99 puts the bar line (4.0) at frame 240):
  - `MidiTrigger: autoplay passes the host transport through` — one segment `{0,512,host}` equal field by field, also with note events present.
  - `MidiTrigger: midi mode without notes is silent` — one segment, `playing == false`.
  - `MidiTrigger: a running note-on at frame 100 starts at the next bar line (frame 240) from virtual ppq 0` — segments `{0,240,silent}`, `{240,272,playing, ppq==0.0, bpm==120}`; the next block (host ppq 4.00+) starts with `ppq == 272 / 24000.0` (continuity).
  - `MidiTrigger: a note-on after the bar-line frame waits for the next bar` — note-on at frame 241 → whole block silent; on exactly frame 240 → start at 240.
  - `MidiTrigger: a note released before the start bar never plays` — on at 10, off at 20 → silent block and silent next block.
  - `MidiTrigger: releasing the last note stops at the next bar line` — playing, off at frame 100, bar line at 240 → `{0,240,playing}`, `{240,272,silent}`.
  - `MidiTrigger: a re-press before the pending stop cancels it`.
  - `MidiTrigger: chords and duplicate note-ons` — two notes on, one off → keeps playing; duplicate note-on + one note-off → stops; note-off without note-on changes nothing.
  - `MidiTrigger: all-notes-off clears the gate` (Kind::AllOff).
  - `MidiTrigger: stopped host starts at the event frame and stops at once` — host `playing=false`: on at 50 → `{0,50,silent}`, `{50,462,playing, ppq 0, bpm = last known or 120}`; off at 50 in a playing block → immediate stop at 50.
  - `MidiTrigger: no valid position counts as stopped` — `playing=true` with `ppq = NaN` and with `bpm = 0` (falls back to last known tempo / 120).
  - `MidiTrigger: an armed start becomes immediate when the host stops` and `...an armed stop becomes immediate when the host stops`.
  - `MidiTrigger: playback is decoupled from host jumps` — host ppq jumps backwards between blocks while playing: virtual ppq stays continuous.
  - `MidiTrigger: bar lines follow the time signature` — 3/4: bar line at ppq 3.0; 6/8 (6 beats of ppq 0.5 each ⇒ `beatsPerBar == 3.0`).
  - `MidiTrigger: a block longer than a bar` — n = 200000 at 120 BPM from ppq 0.5 with a note-on at frame 0 → start at the frame of ppq 4.0, no second start.
  - `MidiTrigger: mode switching` — Midi playing → Autoplay: host passes through from the next bar (running) / at once (stopped); Autoplay → Midi with no note: silent from the next bar; with a held note: restarts from virtual ppq 0 at the next bar.
  - `MidiTrigger: segments are always valid` — 1000 alternating note on/off events in one block: `1 <= count <= kMaxSegments`, every `length > 0`, frames contiguous from 0, lengths sum to n; also `reset()` clears held notes and arms.
- [ ] **Step 2: Run** `build.ps1 test` — expect the new tests to fail to compile/link (`MidiTrigger` undefined).
- [ ] **Step 3: Implement** `MidiTrigger` as specified above in `engine/MidiTrigger.{h,cpp}` (no JUCE includes). Use a 16×128 bit set for held notes and a small fixed state machine (Silent, ArmedStart, Playing, ArmedStop) plus the pending mode change.
- [ ] **Step 4: Run** `build.ps1 test` — all tests pass (472 + new).
- [ ] **Step 5: Commit** `feat(engine): MidiTrigger, held-note gating with bar-quantised start and stop`.

---

### Task 2: Processor integration and persistence

**Files:**
- Modify: `plugin/DubcyclingProcessor.h`, `plugin/DubcyclingProcessor.cpp`, `plugin/PluginState.h`/`.cpp` (only if the extra attribute needs a helper there), the comment on `acceptsMidi()`
- Test: `tests/plugin/test_Processor.cpp` (and `tests/plugin/test_PluginState.cpp` if the helper lives there)

**Interfaces:**
- Consumes: Task 1's `MidiTrigger`, `TriggerMode`, `GateEvent`, `TriggerSegment`; existing `Engine::process(const TransportInfo&, float* l, float* r, int n)`.
- Produces on `DubcyclingProcessor`: `void setTriggerMode(TriggerMode)` and `TriggerMode triggerMode() const` (any thread; atomic, applied to the trigger at the start of the next `processBlock`).

- [ ] **Step 1: Write the failing tests** (names exact):
  - `Processor: autoplay output is unchanged by MIDI events` — same block with and without a note-on in the `MidiBuffer`, mode Autoplay: identical output.
  - `Processor: midi mode plays only while a note is held (stopped host)` — with a loaded model like the other processor tests: no note → silence; note-on in the MidiBuffer → non-silent output from that frame; note-off → silent afterwards (after the cancel fade); `isMidiEffect()` still false.
  - `Processor: midi mode starts at the next bar with a running host` — using the existing test play head helper, a note-on arms the start; output stays silent until the bar-line frame, non-silent after.
  - `Processor: all-notes-off and prepareToPlay clear held notes`.
  - `Processor: the mode is saved with the project, not with presets` — `getStateInformation`/`setStateInformation` round trip keeps Midi; a state without the attribute and one with `Trigger="bogus"` give Autoplay; `restoreState(presetState)` leaves the mode untouched; the preset/bank serialisation (`BankFormat`) contains no mode.
- [ ] **Step 2: Run** `build.ps1 test` — new tests fail.
- [ ] **Step 3: Implement.** In `processBlock`: apply the atomic mode to the trigger; iterate the `MidiBuffer` (note-on with velocity 0 counts as note-off; CC 123 and CC 120 count as `AllOff`; read at most 512 events per block into a fixed array, later events ignored); call `trigger_.plan(...)`; per segment call `engine_.process(seg.t, l + seg.frame, r + seg.frame, seg.length)`. `prepareToPlay` → `trigger_.prepare(sampleRate)`; `releaseResources` → `trigger_.reset()`. `getStateInformation` writes the mode as an optional root attribute `Trigger="midi"` (absent = Autoplay) next to what `serialize(state_)` produces — append it to that XML/memory block without changing `PluginState` or `kStateVersion`; `setStateInformation` reads it (sanitised) but `restoreState`/preset loads do not. Keep `hostBpm_`/`hostBeatsPerBar_` updates as they are (from the real host transport).
- [ ] **Step 4: Run** `build.ps1 test` — all pass.
- [ ] **Step 5: Commit** `feat(plugin): MIDI trigger mode in processBlock, saved per instance`.

---

### Task 3: Editor toggle, docs, validation

**Files:**
- Modify: `plugin/ui/Editor.h`, `plugin/ui/Editor.cpp` (top bar), `README.md`, `docs/superpowers/backlog.md`, `docs/superpowers/specs/2026-11-06-midi-trigger-design.md` (status line → implemented)
- Test: `tests/plugin/test_Editor.cpp`

**Interfaces:**
- Consumes: `DubcyclingProcessor::setTriggerMode/triggerMode` (Task 2).
- Produces: a two-state toggle (text `Autoplay` / `MIDI`, tooltip explaining the gate and bar sync) in the editor's top bar.

- [ ] **Step 1: Write the failing tests:** `Editor: the mode button reflects and sets the trigger mode` (click toggles `processor.triggerMode()`; changing the mode on the processor shows after `refresh()`); `Editor: top bar has no overlaps with the mode button at 800x540 and 900x600` (same style as the existing layout tests; if the cycle label must be hidden for space, the existing `cycleFits` logic decides).
- [ ] **Step 2: Run** `build.ps1 test` — new tests fail.
- [ ] **Step 3: Implement** the toggle button in the top bar (next to the cycle readout / preset bar; keep the 800 px minimum layout overlap-free), wire `onClick` and `refresh()`. Update the README (a "Playing with MIDI" paragraph: mode, any note gates, start/stop at the next host bar, stopped host starts at once at the last tempo, mode saved per instance, not in presets) and tick the item in the backlog.
- [ ] **Step 4: Run** `build.ps1 test` (all pass) and `build.ps1 validate` (Steinberg validator and pluginval level 5 both succeed; Autoplay is the default so MIDI never affects them).
- [ ] **Step 5: Commit** `feat(ui): Autoplay/MIDI mode toggle, docs`.
