# Filter LFO and New Filter Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a tempo-synced LFO on the Filter's cutoff and three new filter modes (Notch, Peak, LP 24), with a button-group mode choice in the FX panel.

**Architecture:** Three new `FxParam`s (LFO rate/depth/shape) and a six-step `FilterType` live in `engine/FxSettings`. The Filter (state-variable, TPT) gets the new modes and an LFO whose phase derives from the host's musical position; the position reaches it through `FxChain` from `Engine`, next to the tempo that already goes there. Persistence is generic over `kFxParamCount`; the bank format goes to 7. The FX panel's Filter edit view becomes two columns.

**Tech Stack:** C++20, JUCE 8.0.15, Catch2. Engine code is JUCE-free (`engine/`).

**Spec:** `docs/superpowers/specs/2026-11-24-filter-lfo-design.md`

## Global Constraints

- Audio thread: no allocation, no locks. Engine part JUCE-free in `engine/fx`, tested with Catch2.
- With LFO Depth 0 and FilterType 0..2 the Filter output is bit-identical to today; existing Filter tests stay unchanged and green.
- No LFO matrix, no routing, no LFO elsewhere; no free Hz rate, no phase/retrigger control, no Allpass, no host automation.
- `kStateVersion` stays 1 (optional attributes only); `kBankVersion` becomes 7; banks 1..6 still load, 8+ are refused as too new.
- UI strings English; the mode choice is a button group, never a continuous slider; the FX panel must fit 800x600 without clipping or overlap.
- VST3 validation (Steinberg validator + pluginval, strictness 5) stays green.
- Commit trailer: `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Review Focus

1. Host stopped, no position, or bpm 0/NaN: the LFO keeps running at the last known tempo, never produces NaN/inf (Task 3, Task 4).
2. Cutoff extremes: depth 100 % around Cutoff 20 Hz and around 0.45 x sample rate stays inside the filter's range and finite at 44.1/96/192 kHz (Task 3).
3. Old data: a preset/bank/project without LFO keys loads with defaults and sounds as before; a saved FilterType outside 0..5 is clamped (Task 1).
4. Switching mode, shape or rate while sounding: output stays finite and bounded, no large level jump (Task 2, Task 3).
5. Host jump or loop (position moves backwards or far ahead): the phase follows the new position at once, no blow-up, square LFO included (Task 4).

---

## File Structure

- Modify `engine/FxSettings.h/.cpp`: new params, six filter types, sanitizing.
- Create `engine/fx/Lfo.h/.cpp`: `LfoShape`, `lfoValue`, `lfoPhase`.
- Modify `engine/fx/Filter.h/.cpp`: new modes, LFO, `setTempo`, `setPosition`.
- Modify `engine/fx/FxChain.h/.cpp`, `engine/Engine.cpp`: position wiring.
- Modify `plugin/presets/BankFormat.h/.cpp` (version 7), `plugin/ui/FxPanel.h/.cpp`.
- Tests: `tests/test_FxSettings.cpp`, `tests/test_FxUnits.cpp`, new `tests/test_Lfo.cpp` (add to `tests/CMakeLists.txt` `dc_tests`), `tests/test_FxChain.cpp`, `tests/test_Engine.cpp`, `tests/plugin/test_PluginState.cpp`, `tests/plugin/test_BankFormat.cpp`, `tests/plugin/test_FxPanel.cpp`.

Run a focused test: `powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 build` then `ctest --test-dir build -C Release -R "<regex>" --output-on-failure`. Full run: `build.ps1 all`; validators: `build.ps1 validate`.

---

### Task 1: Parameters and persistence

**Files:**
- Modify: `engine/FxSettings.h`, `engine/FxSettings.cpp`, `plugin/presets/BankFormat.h`, `plugin/presets/BankFormat.cpp` (only if version text or clamps need it)
- Test: `tests/test_FxSettings.cpp`, `tests/plugin/test_PluginState.cpp`, `tests/plugin/test_BankFormat.cpp`

**Interfaces:**
- Produces: `FxParam::FilterLfoRate`, `FilterLfoDepth`, `FilterLfoShape` appended after `DelayReverse`; `kFxParamCount = 21`. Info rows: `"filter.lfoRate"` (Filter, 0..kDelayTimeSteps-1, default 8 = 1/4, steps kDelayTimeSteps), `"filter.lfoDepth"` (0..1, default 0, continuous), `"filter.lfoShape"` (0..3, default 0, steps 4: Sine, Triangle, Saw down, Square). `"filter.type"` becomes 0..5 with steps 6 (0 LP, 1 HP, 2 BP, 3 Notch, 4 Peak, 5 LP24). `sanitizeFxParam` rounds FilterType to 0..5 and the two stepped LFO params to their steps. `kBankVersion = 7`.

- [ ] **Step 1: Write failing tests.**
  - `test_FxSettings.cpp`: `"FxSettings: filter LFO defaults and ranges"` (defaults rate 8, depth 0, shape 0; `fxParamInfo(FilterType).max == 5`, steps 6); `"sanitizeFxParam: FilterType rounds into 0..5, LFO steps round, depth clamps"` (FilterType 9 -> 5, 4.4 -> 4, -1 -> 0; lfoRate 99 -> 14; lfoShape 3.6 -> 3; depth 2 -> 1; NaN -> default).
  - `test_PluginState.cpp`: `"State: filter LFO params and a new filter type round trip"` (set type 5, lfoRate 11, depth 0.6, shape 3; save/load XML; equal) and `"State: a project without LFO attributes loads the LFO defaults"` (use the existing `fxDoc` helper style from the Delay tests).
  - `test_BankFormat.cpp`: change `"Bank: bankToJson writes version 6"` to 7; add `"Bank: version 6 files load with LFO defaults, version 8 is tooNew"`; `"Bank: filter LFO params round trip and change the fingerprint only when non-default"`.
- [ ] **Step 2: Run them; expect failures** (unknown enum values, version 6 != 7).
- [ ] **Step 3: Implement** the enum/table/sanitizing changes in `engine/FxSettings.*` (`kFxParamCount` 18 -> 21, `kInfo` rows) and `kBankVersion = 7` in `BankFormat.h`; the loops over `kFxParamCount` in `PluginState.cpp` and `BankFormat.cpp` need no change (verify the fingerprint line at `BankFormat.cpp:562` only adds params that differ from the default, and fix if it does not).
- [ ] **Step 4: Run the Task-1 tests plus the whole `FxSettings|State|Bank` set; expect green.**
- [ ] **Step 5: Commit** `feat(fx): filter LFO parameters, six filter types, bank format 7`.

---

### Task 2: Filter modes Notch, Peak, LP 24

**Files:**
- Modify: `engine/fx/Filter.h`, `engine/fx/Filter.cpp`
- Test: `tests/test_FxUnits.cpp`

**Interfaces:**
- Consumes: `FxParam::FilterType` 0..5 (Task 1).
- Produces: `Filter` handles types 3 (Notch = low + high), 4 (Peak = low - high), 5 (LP24, two cascaded stages). Types 0..2 unchanged bit for bit.

- [ ] **Step 1: Write failing tests** (style of the existing Filter tests, helpers in `fxtest`):
  - `"Filter: notch removes a sine at the cutoff by at least 20 dB and passes 100 Hz and 10 kHz within 1.5 dB"` (cutoff 2 kHz, resonance 0.2).
  - `"Filter: peak lifts a sine at the cutoff above the same sine far away"` (at least +3 dB relative at resonance 1).
  - `"Filter: LP24 attenuates 8 kHz at least 30 dB more than LP at the same cutoff, passes 100 Hz within 1 dB"`.
  - `"Filter: notch, peak and LP24 stay finite and below 4x the input peak at resonance 1 and 44.1, 96, 192 kHz"`.
  - `"Filter: switching the type while sounding does not jump by more than 0.5 between samples"` (100 Hz sine at 0.5 amplitude, cycle through all six types every 1000 samples).
  - `"Filter: types 0..2 are bit-identical to a stored reference render"` (render noise through LP/HP/BP, compare to values captured before the change via a one-off helper that stays in the test as constants for the first 16 samples of each).
- [ ] **Step 2: Run; expect failures** for the new modes.
- [ ] **Step 3: Implement** in `Filter.cpp`: extend the `switch (type_)`, clamp `type_` to 0..5 in `setParams`; the second LP24 stage needs its own `ic1_/ic2_` state pairs and shares `g_`/`k_` (split Q so the cascade has no self-oscillation, builder's call); change mode with a short crossfade over the old and new output (about 5 ms) when `type_` changes while not `first_`.
- [ ] **Step 4: Run `Filter` tests; expect green (including all old ones).**
- [ ] **Step 5: Commit** `feat(fx): Notch, Peak and LP 24 filter modes`.

---

### Task 3: LFO and its use in the Filter

**Files:**
- Create: `engine/fx/Lfo.h`, `engine/fx/Lfo.cpp`, `tests/test_Lfo.cpp`
- Modify: `engine/CMakeLists.txt` (add `Lfo.cpp`), `tests/CMakeLists.txt` (add `test_Lfo.cpp` to `dc_tests`), `engine/fx/Filter.h`, `engine/fx/Filter.cpp`, `tests/test_FxUnits.cpp`

**Interfaces:**
- Produces in `Lfo.h` (`namespace dc::fx`):
  - `enum class LfoShape { Sine = 0, Triangle = 1, SawDown = 2, Square = 3 };`
  - `float lfoValue(LfoShape shape, double phase);` phase in [0,1), result in [-1,1], maximum +1 at phase 0: Sine `cos(2*pi*p)`, Triangle `1 - 4*min(p, 1-p)`, SawDown `1 - 2*p`, Square `p < 0.5 ? 1 : -1`.
  - `double lfoPhase(double beats, double cycleBeats);` the fractional part of `beats / cycleBeats`, in [0,1), also for negative `beats`; non-finite input or `cycleBeats <= 0` gives 0.
- Produces in `Filter`: `void setTempo(double bpm, double beatsPerBar);` and `void setPosition(double ppq);` (non-finite ignored). `setParams` reads `FilterLfoRate/Depth/Shape`.

- [ ] **Step 1: Write failing tests.**
  - `test_Lfo.cpp`: `"Lfo: every shape peaks at phase 0 and has the documented values at 0, 0.25, 0.5, 0.75"` (Sine 1,0,-1,0; Triangle 1,0,-1,0; SawDown 1,0.5,0,-0.5; Square 1,1,-1,-1); `"lfoPhase: wraps, handles negative beats and invalid input"` (e.g. `lfoPhase(-0.25, 1.0) == 0.75`, `lfoPhase(NaN, 1) == 0`, `lfoPhase(3, 0) == 0`).
  - `test_FxUnits.cpp`: `"Filter: depth 0 leaves the output identical for any rate and shape"`; `"Filter: LFO square at depth 1 moves the cutoff by plus/minus two octaves on the beat"` (noise input, cutoff 2 kHz, 120 BPM, rate 1/4: the high half of the cycle passes a 6 kHz sine more than the low half; bands measured by RMS over each half cycle); `"Filter: the LFO phase follows setPosition"` (two renders, position 0 and position 4 beats with a 1/4 cycle of 1 beat, identical; position 0.5 beats differs); `"Filter: the LFO keeps running at the last tempo without setPosition"`; `"Filter: extreme depth and cutoff stay finite at 44.1, 96 and 192 kHz"` (cutoff 20 Hz and 0.45*sr, depth 1, all shapes); `"Filter: invalid tempo or position keeps the output finite"` (bpm 0, NaN, inf; position NaN).
- [ ] **Step 2: Run; expect compile/link failures, then red tests.**
- [ ] **Step 3: Implement** `lfoValue`, `lfoPhase`; in `Filter`: hold `beatPos_` (double), advance it per sample by `bpm / (60 * sampleRate)` (bpm and beatsPerBar from `setTempo`; invalid bpm keeps the last valid one, 120 initially), set it from `setPosition`; cycle length in beats `delayTimeBeats(step, beatsPerBar)`; modulation in the log domain `logCutoff_ + depthSmoothed * 2 * ln(2) * lfo` (depth 1 = 2 octaves), clamped so `exp` stays in `[20, 0.45*sr]`; depth smoothed with the existing `smoothCoeff_`; the LFO value passes a one-pole slew of at most 2 ms; skip the whole LFO path when the smoothed depth is exactly 0.
- [ ] **Step 4: Run `Lfo|Filter` tests and the Task 2 tests; expect green.**
- [ ] **Step 5: Commit** `feat(fx): tempo-synced Filter LFO`.

---

### Task 4: Position wiring through chain and engine

**Files:**
- Modify: `engine/fx/FxChain.h`, `engine/fx/FxChain.cpp`, `engine/Engine.cpp`
- Test: `tests/test_FxChain.cpp`, `tests/test_Engine.cpp`

**Interfaces:**
- Consumes: `Filter::setTempo/setPosition` (Task 3).
- Produces: `void FxChain::setPosition(double ppq);` forwards to the Filter; `FxChain::setTempo(bpm, beatsPerBar)` also forwards to the Filter. The Engine calls `chain_.setPosition(<ppq of the first frame of the slice being processed>)` in the main path next to `chain_.setTempo` (`Engine.cpp` around line 995) and does not call it in the stopped/invalid-transport path (around line 766), so the LFO free-runs there at the last known tempo.

- [ ] **Step 1: Write failing tests.**
  - `test_FxChain.cpp`: `"FxChain: the Filter LFO follows setPosition and setTempo"` (chain with Filter on, depth 1 square, rate 1 beat; two renders positions 0 and 4 identical, position 0.5 differs).
  - `test_Engine.cpp`: `"Engine: the Filter LFO stays on the beat across a host jump"` (play, jump the host position back by a multiple of the LFO cycle plus an offset, assert finite output and that rendering again from the same position gives the same samples); `"Engine: the Filter LFO keeps going and stays finite while the host is stopped"`.
- [ ] **Step 2: Run; expect failure.**
- [ ] **Step 3: Implement** the forwarding and the two engine call sites.
- [ ] **Step 4: Run `FxChain|Engine` tests; expect green, then the full `dc_tests` set.**
- [ ] **Step 5: Commit** `feat(engine): host position reaches the Filter LFO`.

---

### Task 5: Filter panel with mode buttons and LFO controls

**Files:**
- Modify: `plugin/ui/FxPanel.h`, `plugin/ui/FxPanel.cpp`
- Test: `tests/plugin/test_FxPanel.cpp`

**Interfaces:**
- Consumes: the three LFO params and six types (Task 1).
- Produces: the Filter edit view: left column mode buttons in two rows (`LP HP BP` / `NOTCH PEAK LP24`), then Cutoff and Resonance; right column LFO Rate (stepped slider, value text from `delayTimeName`), LFO Depth (percent), LFO Shape as four buttons (`SINE TRI SAW SQR`). `paramName` gives "Rate", "Depth", "Shape" in the LFO column (no clash: one effect is shown at a time); `valueText(FilterType)` covers all six names. Button groups publish the whole parameter set once per click (like the existing type buttons).

- [ ] **Step 1: Write failing tests** (style of the existing `FxPanel` tests): `"FxPanel: Filter shows six mode buttons and no mode slider"`; `"FxPanel: clicking Notch, Peak and LP24 publishes FilterType 3, 4, 5 once each"`; `"FxPanel: LFO Rate shows note names and spans 15 steps"`; `"FxPanel: LFO Shape buttons publish 0..3 once and reflect setContext"`; `"FxPanel: LFO Depth shows percent and resets to 0 on double-click"`; extend `"FxPanel: controls do not overlap at the minimum size"` so it also selects Filter and checks no overlap and nothing clipped.
- [ ] **Step 2: Run; expect failures.**
- [ ] **Step 3: Implement** in `FxPanel.cpp`: `Knob::typeButtons` grows to six (two rows), a shape button array of four, `editParams(Filter)` lists the LFO params, `isButtons` includes `FilterLfoShape`, layout switches the Filter to two columns like the Flanger (`FxPanel.cpp` around line 463).
- [ ] **Step 4: Run all `FxPanel` and `Editor` tests; expect green.**
- [ ] **Step 5: Commit** `feat(ui): Filter edit view with six modes and LFO controls`.

---

### Task 6: Docs and release checks

**Files:**
- Modify: `README.md`, `docs/superpowers/specs/2026-11-24-filter-lfo-design.md`

- [ ] **Step 1:** README: `bank.json` version 7 in the known limitations; mention the Filter LFO and modes only if the README lists effect features. Spec: `Status: implemented`.
- [ ] **Step 2:** Run the full suite (`build.ps1 all`) and `build.ps1 validate`; expect all tests green, validator and pluginval SUCCESS. Record both results in the commit message body.
- [ ] **Step 3: Commit** `docs: Filter LFO status, README bank version`.
- [ ] **Step 4:** Hand back to the controller: the wiki Effects page (Filter section, new screenshot via the screenshot helper `.superpowers/shots/test_WikiShots.cpp`, selecting the Filter) is updated after the merge, in the wiki repo.
