# Pass slots Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Record the last finished Loop-1 pass of random Autoplay, save it into one of 8 preset-stored slots, and replay a slot (repeating) or all slots in a chain, driven by editor buttons and learnable MIDI notes.

**Architecture:** A JUCE-free `PassRecorder` in the engine records the Loop-2 voice commands (start region/mode, stop) of each pass with their phase (0..1). `Model` carries the 8 saved passes; the `Engine` replays one at pass boundaries by issuing the recorded commands instead of drawing. The processor owns Save/Clear/Replay/bindings; saved passes live in `PluginState` (so presets and banks carry them), bindings live per instance next to the trigger mode.

**Tech Stack:** C++20, JUCE 8.0.15, Catch2. Build/test: `powershell -NoProfile -ExecutionPolicy Bypass -File ./build.ps1 test` (also `build`, `validate`).

**Spec:** `docs/superpowers/specs/2026-11-09-pass-slots-design.md`

## Global Constraints

- Audio thread: no allocation, no locks. Engine/recorder code is JUCE-free and tested with Catch2.
- Autoplay without slots and without replay is **bit-identical** to today (existing Engine tests stay green unchanged).
- UI strings, comments, docs in English. `kStateVersion` stays 1; bank version goes 3 -> 4 (v1-v3 files still load with empty slots).
- 8 slots (`kPassSlots`), at most `kMaxPassEvents = 256` events per pass; larger or damaged data is dropped, never a crash.
- Commit trailer: `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never merge locally, never push without being asked.

## Rulings (decided here, argue from the spec)

- A pass stores the **voice commands** of the pass, not only random draws: each `PassEvent` is a Start (region2, mode) or a Stop at a phase, recorded where the engine issues `requestStart`/`requestStop` for entries, Gate stops and jump stops. Silence draws and Gate-ends therefore replay exactly.
- **Mix on replay** comes from the current rules: for a Start at phase p, `region1 = loop1.regions.regionAt(p)`; if the current rule of `region1` has a target `region2`, use that target's mix, else 0.5.
- A pass starts with Loop 2 silent: at a pass boundary replay stops a voice carried over from before, unless the pass has a Start at phase ~0 (which cancels it as usual).
- Replay requests are applied at the next pass boundary (Loop-1 cycle change, or a fresh start/jump that lands at phase ~0). A host jump during replay stops the voice and waits for the next boundary; replay stays requested.
- Recorder: a pass is **open** from a boundary and **finished** at the next continuous boundary. Abandoned (not saveable) on jump, transport stop/invalid, release, model change, replay running, or overflow of `kMaxPassEvents`.
- Replay request encoding (atomic int): `0` off, `1` chain, `2 + i` slot i. Play note/button: off -> chain, any replay on -> off. Slot trigger: `2 + i`; ignored if slot i is empty.
- Chain order: after slot i comes the next non-empty slot, wrapping; with no non-empty slot, replay does nothing (random continues).

## Review Focus

- Replay of a slot whose region 2 no longer exists, an empty slot, a chain with no filled slot: nothing sounds wrong, no crash, random continues.
- Old state/bank without passes loads with 8 empty slots; damaged `passes` data is dropped, rest of the preset loads.
- Transport stop or host jump mid-pass: no partial pass is saveable; replay resumes at the next boundary.
- Learning a note that is currently held must not leave the MIDI gate stuck; one note maps to one target; bound notes never open the gate.
- Save publishes a new model: playback and replay continue without click or restart (seamless swap, same loop data).
- The same finished pass cannot be saved twice; Save is disabled when all 8 slots are full or no new finished pass exists.

---

### Task 1: Pass type and PassRecorder

**Files:**
- Create: `engine/Pass.h`, `engine/Pass.cpp`, `tests/test_Pass.cpp`
- Modify: `engine/CMakeLists.txt`, `tests/CMakeLists.txt` (register the new files)

**Interfaces:**
- Produces (all in `namespace dc`, `#include "engine/RuleSet.h"` for `PlayMode`):
  - `constexpr int kPassSlots = 8; constexpr int kMaxPassEvents = 256;`
  - `struct PassEvent { double phase = 0.0; bool stop = false; int region2 = -1; PlayMode mode = PlayMode::Gate; };` (`region2`/`mode` only meaningful when `!stop`)
  - `struct Pass { std::vector<PassEvent> events; bool empty() const; };` with `bool operator==`.
  - `Pass sanitizePass(Pass p);` : drops non-finite phases and Starts with `region2 < 0`, clamps phase to [0,1], stable-sorts by phase, cuts to `kMaxPassEvents`.
  - `class PassRecorder` (audio thread writes, any thread reads; fixed storage, no allocation, no locks): `void begin();` opens a pass (discarding an open one); `void add(double phase, bool stop, int region2, PlayMode mode);` no-op when not open, overflow marks the open pass invalid; `void finish();` publishes the open pass as the last finished one if valid and increments the id; `void abandon();`; `bool isOpen() const;`; `bool read(Pass& out, std::uint32_t& id) const;` false if none has finished yet, consistent snapshot via a seqlock (retry on concurrent write).

- [ ] **Step 1: Write failing tests in `tests/test_Pass.cpp`:** `sanitizePass` drops NaN phase and `region2 = -1` starts, clamps and sorts, caps at 256; recorder: add before `begin` is ignored; `begin/add/add/finish` then `read` returns both events and id 1, a second finished pass gives id 2; `abandon` publishes nothing; 257 adds make `finish` publish nothing; `begin` twice discards the first; `read` before any finish returns false.
- [ ] **Step 2: Run the tests and confirm they fail to build/pass.**
- [ ] **Step 3: Implement** `Pass.h/.cpp` with the signatures above (seqlock: writer bumps an atomic counter to odd, copies into a `std::array<PassEvent, kMaxPassEvents>` plus count, bumps to even; `read` retries until the counter is even and unchanged).
- [ ] **Step 4: Run the tests, all green. Commit** `feat(engine): Pass type and PassRecorder`.

---

### Task 2: Engine records passes

**Files:**
- Modify: `engine/Engine.h`, `engine/Engine.cpp`
- Test: `tests/test_Engine.cpp` (new `TEST_CASE`s, existing ones unchanged)

**Interfaces:**
- Consumes: Task 1 `PassRecorder`, `PassEvent`.
- Produces on `Engine`: `bool lastPass(Pass& out, std::uint32_t& id) const;` (any thread, delegates to the recorder). `Model` gets `std::array<Pass, kPassSlots> passes;` (empty by default; used in Task 3).

Recording rules (see Rulings): in `processSlice`'s per-sample loop, where an entry results in `requestStart`/`requestStop`, a Gate stop, or the `jumpNow` stop, call `recorder_.add(ph, ...)` with the Loop-1 phase `ph` of that sample (Start carries `c.region2` and `c.mode`). `begin()` at a continuous cycle change (finishing the previous pass first via `finish()`), and at a fresh start or jump that lands within `kJumpToleranceBeats` of phase 0. `abandon()` on: transport invalid/stopped (`resetPlayback`), jump, `releasing_`, any model swap (seamless or hard), and while replay is running (Task 3).

- [ ] **Step 1: Write failing tests** (deterministic `setRandom`, a model with Loop 1 of 2 regions and rules so that region 0 draws region2 = 0 Gate and region 1 draws Silence): after two full passes `lastPass` returns the first pass with events `{phase 0, Start region2 0 Gate}`, `{phase of the region-1 boundary, Stop}`, id 1, and id 2 after the second; playback starting mid-loop records nothing until the first wrap; a stop mid-pass (invalid transport block) leaves no new id; a model swap mid-pass leaves no new id; a Gate ending by Loop 1 leaving its region records a Stop at that phase.
- [ ] **Step 2: Run, confirm failure.** Step 3: implement. Step 4: run all tests, existing Engine tests untouched and green.
- [ ] **Step 5: Commit** `feat(engine): record finished passes`.

---

### Task 3: Engine replays passes

**Files:**
- Modify: `engine/Engine.h`, `engine/Engine.cpp`
- Test: `tests/test_Engine.cpp`

**Interfaces:**
- Consumes: `Model::passes`, replay request encoding (Rulings).
- Produces on `Engine`: `void setReplay(int request);` (any thread, atomic, relaxed; out-of-range values mean off), `int replayRequest() const;`, and `int replaySlot() const;` (slot being played at the end of the last block, `-1` when random plays; any thread). `Playhead` stays unchanged.

Behavior: the request is read at each pass boundary (and at fresh start / jump landing at phase ~0). Replay on: pick the slot (single: that slot if non-empty, else fall back to random; chain: next non-empty after the current, wrapping), stop a carried-over voice, then per sample fire the slot's events whose phase has been reached, in order, via the same `requestStart`/`requestStop` path with `Command{Start, region2, mode, mix, region1}` (mix per Rulings; skip a Start whose `region2` does not exist in `m->loop2.regions`). No draws, no `pending_`/teaser while replaying; `sequencer_` is reset on entering replay and on leaving it (leaving also stops the replay voice). Request off: at the next boundary the voice stops and random resumes. A jump stops the voice and waits for the next boundary; `releasing_` suppresses events as it does entries. Recording is abandoned while replaying.

- [ ] **Step 1: Write failing tests:** with `passes[0]` set in the Model: request `2` at mid-pass starts the replay only at the next wrap and then the same Loop-2 output repeats each pass (compare two consecutive replayed passes sample for sample, with a random source that would otherwise differ); chain `1` over slots 0 and 2 plays 0,2,0,...; empty slot request leaves random playing (`replaySlot() == -1`); a Start with a missing region2 is skipped; request `0` returns to random at the next boundary with the voice stopped; host jump mid-replay stops the voice and replay resumes at the next boundary; during replay `lastPass` id does not change.
- [ ] **Step 2: Run, confirm failure. Step 3: implement. Step 4: all tests green.**
- [ ] **Step 5: Commit** `feat(engine): replay saved passes, single slot and chain`.

---

### Task 4: PluginState, bank format and fingerprint carry the passes

**Files:**
- Modify: `plugin/PluginState.h/.cpp`, `plugin/presets/BankFormat.h/.cpp`
- Test: `tests/plugin/test_PluginState.cpp`, `tests/plugin/test_BankFormat.cpp`

**Interfaces:**
- Consumes: `dc::Pass`, `sanitizePass`.
- Produces: `PluginState::passes` as `std::array<Pass, kPassSlots>` (optional in the stored state: only written when any slot is non-empty, `kStateVersion` stays 1); `kBankVersion = 4` with a per-preset `"passes"` array of 8 entries, each a list of `{ "phase", "stop" | "region2" + "mode" }` objects; `presetFingerprint` includes non-empty slots (so editing slots marks a preset as changed).

- [ ] **Step 1: Write failing tests:** state round trip with two filled slots and a stop event; state without `passes` loads 8 empty slots; damaged `passes` (wrong types, more than 256 events, NaN) is sanitized and the rest of the state loads; bank v3 JSON loads with empty slots; v4 round trip equals the input; fingerprint differs when a slot changes; a preset bank too new is still rejected as before.
- [ ] **Step 2-4: run to fail, implement, run to pass. Step 5: Commit** `feat(presets): store passes in state and bank (bank v4)`.

---

### Task 5: Processor — model, Save/Clear, replay control, MIDI bindings and learn

**Files:**
- Modify: `plugin/DubcyclingProcessor.h/.cpp` (and `engine/MidiTrigger` only if the gate filtering is best placed there; builder's call)
- Test: `tests/plugin/test_Processor.cpp`

**Interfaces:**
- Consumes: Tasks 2-4.
- Produces on `DubcyclingProcessor` (message thread unless noted):
  - `bool canSave() const;` true when a finished pass exists whose id was not saved yet and a slot is free.
  - `bool savePass();` stores the last finished pass in the lowest free slot through `applyEdit` (publishes a model with `passes`), returns false when `!canSave()`.
  - `void clearPass(int slot);` `void clearAllPasses();` `bool passFilled(int slot) const;`
  - `void setReplayRequest(int request);` `int replayRequest() const;` `int replaySlot() const;` (any thread; delegates to the engine).
  - Bindings (per instance, **not** in presets/banks): targets are `0..7` slots, `8` Save, `9` Play (`constexpr int kTargetSave = 8, kTargetPlay = 9, kBindTargets = 10`). `void armLearn(int target);` (`-1` cancels; Learn mode itself is UI-only), `int learnArmed() const;`, `int boundNote(int target) const;` (`-1` unbound), `void clearBinding(int target);`, `int bindingsVersion() const;` (increments when bindings change, for UI refresh). Atomics only; any thread.
  - Audio thread: every note-on event of a block is checked in order: with a learn armed, the note binds to that target (moving it if bound elsewhere), is consumed (not a gate event) and a matching `NoteOff` gate event is fed to the trigger so a held gate cannot stick; a bound note triggers its target (slot -> replay request `2+i` if that slot is filled; Play -> toggle per Rulings; Save -> `saveRequested_` flag handled by the existing timer on the message thread via `savePass()`); bound notes (on and off) never reach the gate; everything else gates as today.
  - Persistence: bindings are written as optional root attributes next to `Trigger` (outside `PluginState`, missing or invalid values = unbound, one note per target, duplicates keep the first); `restoreState`/presets never touch them; Learn mode is never persisted.
  - Model: `publish()` fills `Model::passes` from `state_.passes`; `applyEdit`/`restoreState` keep it in sync. A preset load replaces the passes and resets the replay request to 0.

- [ ] **Step 1: Write failing tests:** Save with no finished pass fails; after two full passes in the test harness `savePass` fills slot 0, a second `savePass` of the same pass fails, a new pass fills slot 1; with 8 filled slots `canSave` is false; `clearPass`/`clearAllPasses`; learn: armed target 3, a note-on binds note 60, `boundNote(3) == 60`, the note never gates in MIDI mode, learning 60 for target 5 moves it; learning while that note is held does not leave the gate stuck; bound slot note sets `replayRequest() == 2 + i` (ignored for an empty slot); Play note toggles 0 -> 1 -> 0; bindings survive `getStateInformation`/`setStateInformation` and are untouched by `restoreState`; loading a preset with passes fills the model and resets replay.
- [ ] **Step 2-4: run to fail, implement, run all tests green. Step 5: Commit** `feat(plugin): save, clear, replay control and MIDI bindings for pass slots`.

---

### Task 6: Editor UI, README, validation

**Files:**
- Modify: `plugin/ui/Editor.h/.cpp` (new component file `plugin/ui/PassPanel.h/.cpp` is fine, builder's call), `README.md`, `docs/superpowers/backlog.md` if it lists this idea
- Test: `tests/plugin/test_Editor.cpp` (or a new `test_PassPanel.cpp`)

**Interfaces:**
- Consumes: Task 5 processor API.
- Produces: a panel with eight slot buttons (states: empty / filled / playing, labelled `1`..`8`), a **Play** button (lit while `replayRequest() != 0`), **Save** (disabled when `!canSave()`), **Learn** toggle, a per-slot clear button and **Clear all**. Outside Learn a slot click calls `setReplayRequest(2 + i)` (filled slots only), Play toggles per Rulings; in Learn mode a click on a slot, Save or Play calls `armLearn(target)` and the button shows "waiting for note" until the processor binds; a bound button shows its note name as a tooltip or small label. The panel refreshes from the existing editor timer by polling the processor (no locks). Strings are English.

- [ ] **Step 1: Write failing tests:** buttons exist and reflect `passFilled`/`replaySlot`; slot click requests replay only for a filled slot; Save click saves and then reads disabled; Learn mode click arms and does not trigger; Clear and Clear all empty the slots.
- [ ] **Step 2-4: run to fail, implement, run all tests. Step 5:** README section "Saving passes" (capture, 8 slots in the preset, Play/chain, Learn and Save/Clear, what a pass stores, notes bound per instance) and backlog update.
- [ ] **Step 6: `build.ps1 validate`** (Steinberg validator and pluginval stay green), then commit `feat(ui): pass slots panel, docs`.
