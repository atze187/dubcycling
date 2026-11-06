# Grid and Loop Mix per Rule Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** (A) Region boundaries in both tracks snap to quarter notes, the grid is visible; (B) per rule (loop 1 region × loop 2 region) there is a mix knob that crossfades loop 1 against the loop 2 region.

**Architecture:** Mix: `RuleTarget::mix` (0..1, default 0.5) travels through sequencer, engine (level of the loop 2 voice and smoothed loop 1 attenuation), plugin state (XML attribute), bank format (version 3) and the rule row of the `RulePanel`. Grid: pure grid logic and drawing in `LoopLane`, bar division from the editor (Bars field and host time signature).

**Tech Stack:** C++20, JUCE 8.0.15, Catch2 v3.16.0, CMake.

**Spec:** `docs/superpowers/specs/2026-11-05-grid-and-mix-design.md` (binding; "Decisions by the user" and "Decisions by Claude" both apply).

## Global Constraints

- Working branch `feat/effects` (keep working there, not on `main`; the branch has not been pushed yet).
- Commits: `git -c user.name=atze187 -c user.email=14954322+atze187@users.noreply.github.com commit ...`, message ends with the line `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Tests: `powershell -NoProfile -ExecutionPolicy Bypass -File ./build.ps1 test` builds and runs all tests; all previous 412 tests stay green. Engine tests in `tests/` (`dc_tests`), plugin tests in `tests/plugin/` (`dc_plugin_tests`), entry in `tests/CMakeLists.txt` for new files.
- Audio thread: no memory allocation, no locks, no exceptions; output always finite.
- Mix formula (binding): `gain2 = min(1, 2 × mix)`, `gain1 = min(1, 2 × (1 − mix))`; default `mix = 0.5` (both full). `NaN`, infinity and text in the state give 0.5; values outside 0..1 are clamped.
- Existing projects, presets and banks sound unchanged. `kStateVersion` stays 1 (`mix` is an optional attribute, only written if not equal to 0.5). `kBankVersion` becomes 3; reading 1, 2 and 3, writing always 3, 4 and higher is "too new".
- Grid: quarter notes; step in phase `1 / (bars × beatsPerBar)`; snapping always on, Shift key = free; beats under about 4 px spacing are not drawn (then only bars), bars under about 4 px likewise, and then nothing snaps either; without data no grid.
- UI texts in English. Steinberg validator and pluginval stay green (Task 8).

## Review Focus

1. Mix 0 and 1 at the edges; NaN and infinity in the project or in an imported bank (Task 1, 3, 4).
2. Region change mid-block with different mix values: no click, no level jump; voice ends: loop 1 returns to full via a ramp (Task 2).
3. Old projects and banks (version 1, 2) load and sound as before (Task 3, 4).
4. Time signature 3/4, 6/8 and 7/8, tracks with 4 and 8 bars, very narrow tracks: hide the grid instead of pixel mush (Task 6, 7).
5. Mix change in the editor during playback: immediately audible without a click; boundary editing before the sounding region does not change the sounding mix (Task 2, 5).

## File Structure

- Create `engine/MixGain.h`: `MixGains`, `mixGains`, `sanitizeMix`.
- Modify `engine/RuleSet.{h,cpp}`, `engine/Sequencer.{h,cpp}`, `engine/Engine.{h,cpp}`.
- Modify `plugin/PluginState.cpp`, `plugin/presets/BankFormat.{h,cpp}`.
- Modify `plugin/ui/RulePanel.{h,cpp}`, `plugin/ui/LoopLane.{h,cpp}`, `plugin/ui/Editor.{h,cpp}`, `plugin/DubcyclingProcessor.{h,cpp}`.
- Tests: `tests/test_RuleSet.cpp`, `test_Sequencer.cpp`, `test_Engine.cpp`, `tests/plugin/test_PluginState.cpp`, `test_BankFormat.cpp`, `test_RulePanel.cpp`, `test_LoopLane.cpp`, `test_Editor.cpp`, `test_Processor.cpp` (all extend).
- Docs: `README.md`, `docs/superpowers/backlog.md`.

---

### Task 1: Mix in the rule set and in the sequencer

**Files:**
- Create: `engine/MixGain.h`
- Modify: `engine/RuleSet.h`, `engine/RuleSet.cpp`, `engine/Sequencer.h`, `engine/Sequencer.cpp`
- Test: `tests/test_RuleSet.cpp`, `tests/test_Sequencer.cpp`

**Interfaces:**
- Produces (namespace `dc`):
  - `engine/MixGain.h`: `struct MixGains { float loop1 = 1.0f; float loop2 = 1.0f; };` `float sanitizeMix(float mix);` (NaN and infinity give 0.5, otherwise clamped to 0..1) `MixGains mixGains(float mix);` (formula from the Global Constraints, `mix` is sanitized beforehand). Header-only (`inline`).
  - `RuleTarget::mix` (`float`, default `0.5f`, as the last field so that existing initializations `{region2, weight, mode}` keep working). `RuleSet::setTargets` sanitizes `mix` via `sanitizeMix`.
  - `Sequencer::Draw::mix` and `Sequencer::Command::mix` (`float`, default 0.5), filled from the drawn target (`Command` on `Start`). Additionally `Sequencer::Command::region1` (`int`, default -1): the loop 1 region that triggered the start (for the re-evaluation on model swap in Task 2).

- [ ] **Step 1: Write tests.** `MixGain: the centre keeps both loops at full level` (`mixGains(0.5f)` = 1/1). `MixGain: the ends solo one loop` (0: loop1 1, loop2 0; 1: loop1 0, loop2 1). `MixGain: left of centre only loop 2 gets quieter, right of centre only loop 1` (0.25 → 1 and 0.5; 0.75 → 0.5 and 1). `MixGain: NaN, infinity and out-of-range values are sanitized` (NaN and ±inf → 0.5; -3 → 0; 7 → 1). `RuleSet: mix defaults to 0.5 and is sanitized in setTargets` (NaN → 0.5, 2 → 1). `Sequencer: the start command carries the mix of the drawn target and the triggering region` (rule with `mix = 0.8`, `onRegion1(3, rules)` delivers `mix == 0.8f`, `region1 == 3`). `Sequencer: a draw carries the mix` (`draw()` and `apply()`).
- [ ] **Step 2: Run the tests**, they fail (symbols missing).
- [ ] **Step 3: Implement** as under Interfaces; `engine/CMakeLists.txt` only needs the header in the list.
- [ ] **Step 4: Run all tests**, green.
- [ ] **Step 5: Commit** `feat(engine): per-target loop mix in rules and sequencer`.

### Task 2: Engine: mix acts on loop 2 and attenuates loop 1

**Files:**
- Modify: `engine/Engine.h`, `engine/Engine.cpp`
- Test: `tests/test_Engine.cpp`

**Interfaces:**
- Consumes: `MixGains`, `mixGains`, `Sequencer::Command::mix/region1`.
- Produces: `Voice` gets mix state (value, smoothed level, origin: loop 1 region and its boundaries at the start); otherwise no public interface.
- Behavior (spec "Decisions by Claude / Mix" and "Engine"):
  - Start of a loop 2 voice (`startVoice`): `mix` from the command; loop 2 level of the voice = `mixGains(mix).loop2` (smoothed over about 5 to 10 ms per sample; at the start immediately at target, the fade-in of the voice covers the beginning). The fading-out voice `tail_` keeps its mix level.
  - Loop 1: a smoothed gain `g1` multiplies `l[i]`, `r[i]` (loop 1 only, before the addition of the bus and before the loop 1 preview). Target of `g1`: `mixGains(voice_.mix).loop1` as long as `voice_` is in state `Playing`; otherwise 1 (silence, voice ends or fades out, no voice, invalid transport). Ramp about 5 to 10 ms (per sample).
  - The mix applies to the voice that started it; a gate voice that runs into another loop 1 region keeps the value.
  - Preview (loop 2 and loop 1) ignores the mix: level 1, no loop 1 attenuation by the preview.
  - Seamless model swap (`adoptSwap`, only if the voice is `Playing`): the mix of the running voice is re-read from the new model if (a) the boundaries of its loop 2 region and (b) the boundaries of the loop 1 region that started it are unchanged in the new model (comparison of the 0..1 boundaries with a very small tolerance) and `rules.targets(region1)` contains a target with the same `region2`; then its `mix` applies. Otherwise the value stays. (This way a mix change in the editor is heard immediately, and a boundary edit before the sounding region does not change the mix.)
  - `resetPlayback` (transport stop, jump, hard restart) resets the mix state (`g1` ramps from the current value to 1, no jump; a hard restart may set `g1` to 1).
  - No mix path allocates memory or locks.

- [ ] **Step 1: Write tests** in `tests/test_Engine.cpp` (use the existing helpers for models and data): `Engine mix: the default mix keeps both loops at full level` (model as before, sum equal to a run with `mix = 0.5` up to 1e-5 and to the expectation from loop 1 + loop 2 rendered individually). `Engine mix: mix 1 plays only loop 2 while the region sounds` (loop 1 sine, loop 2 another sine; during the voice no loop 1 share, after the end loop 1 full again). `Engine mix: mix 0 silences the loop 2 region and keeps loop 1`. `Engine mix: mix 0.25 halves loop 2 and keeps loop 1` and `0.75` halves loop 1. `Engine mix: loop 1 returns to full level after silence is drawn` (ramp, no jump: largest jump between neighboring values at amplitude 0.5 below 0.05). `Engine mix: switching between targets with different mix does not click` (two rules, switch mid-block; largest jump below 0.1). `Engine mix: the audition ignores the mix` (`requestPreview(1, k)` with a target with mix 0: preview fully audible, loop 1 not attenuated). `Engine mix: an edit of the sounding target's mix is heard at once without a click` (seamless swap, same data and bars, new model with a different mix). `Engine mix: a boundary edit before the sounding region keeps its mix` (loop 2 region shifts in index, boundaries the same: mix unchanged; the re-read must not take the mix of another region). `Engine mix: mix values at the ends and NaN stay finite` (NaN in the model becomes 0.5 on sanitizing).
- [ ] **Step 2: Run the tests**, they fail.
- [ ] **Step 3: Implement.** The smoothing as a small private helper struct in `Engine.h` (no new module needed). For deviations in existing tests that check exact sums: the mix center must stay bit-identical to the previous result (level exactly 1.0, no rounding noise from the ramp); if that is not possible without a special case, tolerance 1e-6 and mention it in the report.
- [ ] **Step 4: Run all tests**, green.
- [ ] **Step 5: Commit** `feat(engine): mix between loop 1 and the playing loop 2 region`.

### Task 3: State: mix in the plugin state

**Files:**
- Modify: `plugin/PluginState.cpp` (and `.h` if a helper is exported)
- Test: `tests/plugin/test_PluginState.cpp`, `tests/plugin/test_Processor.cpp`

**Interfaces:**
- Consumes: `RuleTarget::mix`, `sanitizeMix`.
- Produces: `serialize` writes on the `Target` element the attribute `mix` (17 digits, via `num(...)`) only if `mix != 0.5f`; `deserialize` reads it locale-independently like the effect numbers (the existing `parseFxNumber` logic may be reused or pulled into a shared helper): text, NaN, infinity, missing attribute → 0.5, otherwise clamped to 0..1. `addRuleTarget`/`sanitizeRuleEntry` let `mix` pass through unchanged (sanitizing runs in `RuleSet::setTargets`; additionally the state sanitizing sets `mix` via `sanitizeMix`, so that `PluginState` is also clean without the engine).

- [ ] **Step 1: Write tests:** `State: the mix of a target survives a round trip` (0.2, 0.8 and 0 / 1 at the edges). `State: a target without mix loads with 0.5` (old XML). `State: the default mix writes no attribute` (serialized string contains no `mix=` at default). `State: mix numbers are sanitized` (`"nan"`, `"abc"`, `"1e99"`, `"-5"`, `"inf"` → 0.5, 0.5, 1, 0, 0.5). `Processor: the mix is published in the model` (`applyEdit` with rule and `mix = 0.3`, `getModel()->rules.targets(r)[0].mix == 0.3f`).
- [ ] **Step 2: Run the tests**, they fail.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run all tests**, green.
- [ ] **Step 5: Commit** `feat(state): persist the per-target mix`.

### Task 4: Bank format version 3

**Files:**
- Modify: `plugin/presets/BankFormat.h`, `plugin/presets/BankFormat.cpp`
- Test: `tests/plugin/test_BankFormat.cpp` (adapt existing tests with fixed version 2), `tests/plugin/test_BankStore.cpp` if needed

**Interfaces:**
- Consumes: `RuleTarget::mix`, `sanitizeMix`.
- Produces: `constexpr int kBankVersion = 3;`. `rulesToVar` writes per target `"mix"` (number) only if `mix != 0.5f`; `rulesFromVar` reads it (`isNumber`, sanitized before any conversion: text, `null`, NaN, huge numbers → 0.5 or clamped to 0..1 respectively). `bankFromJson` reads version 1, 2 and 3 (`version < 1` or missing presets: discarded, `version > 3` → `tooNew`). `presetFingerprint`: the mix of a target goes into the line of the target, but only if not equal to 0.5, so that a default mix gives the same fingerprint as a state without mix data.

- [ ] **Step 1: Write tests:** `Bank: version 3 round trips the mix of a target`. `Bank: version 1 and 2 files still load, with mix 0.5`. `Bank: a version 4 file is refused as too new`. `Bank: bankToJson writes version 3`. `Bank: invalid mix numbers are sanitized` (text, `null`, `1e99`, `-1e99`). `Bank: a changed mix changes the fingerprint, a default mix does not`. `BankStore: saving a loaded version 2 bank writes version 3` (if existing store tests can carry it; otherwise via `bankToJson`).
- [ ] **Step 2: Run the tests**, they fail.
- [ ] **Step 3: Implement.** Adapt existing tests with hard-coded version 2.
- [ ] **Step 4: Run all tests**, green.
- [ ] **Step 5: Commit** `feat(presets): bank format version 3 with the per-target mix`.

### Task 5: Mix knob in the rule row

**Files:**
- Modify: `plugin/ui/RulePanel.h`, `plugin/ui/RulePanel.cpp`, `plugin/ui/Editor.cpp` (only if needed), `plugin/ui/PanelLook.{h,cpp}` (only if a slider style is needed)
- Test: `tests/plugin/test_RulePanel.cpp`, `tests/plugin/test_Editor.cpp`

**Interfaces:**
- Consumes: `RuleTarget::mix`, `mixGains` (for the display).
- Produces: `RulePanel` gets per row a mix knob (0..1, default 0.5, double-click sets the center), also visible for rows with weight 0. API analogous to `setWeight`/`setMode`: `void setMix(int row, float mix);` (publishes once via `onChanged`), `float mix(int row) const;`, `juce::String mixText(int row) const;`, drag seams `beginMixDrag(int row)`, `dragMix(int row, float mix)`, `endMixDrag()` (a drag publishes once at the end as with the weight). `buildTargets()` and the comparison in `setContext` (line ~76, compares `region2`, `weight`, `mode`) take `mix` into account; `mix` is kept when changing weight and mode; a touched mix stores the target (also with weight 0) like a touched weight; untouched rows are still not stored. Display `mixText`: center "L1 | L2" (or equivalent), left of it the loop 2 share in percent ("L2 40 %"), right of it the loop 1 share ("L1 40 %"), fully left "L1 only", fully right "L2 only" (the builder may adapt the notation as long as the center, the edges and the share are readable). Layout: the knob fits into the row next to weight and GATE/1-SHOT; minimum width of the `RulePanel` in the 900×600 editor stays without overlap (overlap test like for the `FxPanel`); a row is still about 20 px high.

- [ ] **Step 1: Write tests:** `RulePanel: every row shows a mix control that defaults to the centre`. `RulePanel: setMix publishes the whole target list once and keeps weight and mode`. `RulePanel: changing weight or mode keeps the mix`. `RulePanel: a touched mix is stored for a row with weight 0`. `RulePanel: an untouched row stores no target`. `RulePanel: a mix drag publishes once at its end`. `RulePanel: reset restores the centre and publishes`. `RulePanel: mixText reads centre, ends and sides` (0.5, 0, 1, 0.25, 0.75). `RulePanel: out-of-range mix values are clamped by setMix`. `RulePanel: setContext does not overwrite a mix drag in progress`. `RulePanel: controls do not overlap at the editor's panel width`. Editor: `Editor: a mix change reaches the processor state and the published model` (via `rulePanel()`), `Editor: boundary edits of Loop 2 keep the mix of the targets` (rule with `mix`, insert a loop 2 boundary: target with the same mix on the shifted region).
- [ ] **Step 2: Run the tests**, they fail.
- [ ] **Step 3: Implement.** Snapshot of the editor as PNG for visual inspection (headless `createComponentSnapshot` as possible in earlier tasks; to `C:\Users\atzet\AppData\Local\Temp\claude\7762a26053d134b1\40b8fe3f-39b0-4bc2-9f46-f5b9485ab946\scratchpad`, do not commit); fix truncated texts.
- [ ] **Step 4: Run all tests**, green.
- [ ] **Step 5: Commit** `feat(ui): mix control per rule row`.

### Task 6: Grid in the track (logic, snapping, drawing)

**Files:**
- Modify: `plugin/ui/LoopLane.h`, `plugin/ui/LoopLane.cpp`
- Test: `tests/plugin/test_LoopLane.cpp`

**Interfaces:**
- Consumes: `RegionMap`.
- Produces (namespace `dc::ui`):
  - `void LoopLane::setGrid(int bars, int beatsPerBar);` (both `<= 0` or without data: no grid) `double LoopLane::gridStep() const;` (phase per beat = `1 / (bars × beatsPerBar)`, 0 if no grid or the grid is too fine) `double LoopLane::snapPhase(double phase) const;` (nearest beat, unchanged if `gridStep() == 0`; result stays in 0..1).
  - `std::vector<double> snapBoundariesToGrid(const std::vector<double>& boundaries, double step);` (free function, rounds each boundary to `round(b / step) × step`, discards values outside (0,1) and then runs through `sanitizeBoundaries` (`PluginState.h`) for minimum distance/sorting/duplicates; with `step <= 0` unchanged).
  - Snapping: `pressAt` stays unchanged (grabs boundaries, selects regions); `dragTo(float x)` snaps the target position (`x + grabOffset_`) to the grid; `void dragTo(float x, bool free);` with `free = true` no snapping (the one-argument variant corresponds to `free = false`); `doubleClickAt(float x)` inserts the boundary at `snapPhase(phaseAt(x))` (removing a hit boundary unchanged); `void doubleClickAt(float x, bool free)` for the case without snapping (the variant `doubleClickAt(float x, float y)` for the name editor stays). `mouseDrag` passes `e.mods.isShiftDown()`; `mouseDoubleClick` likewise.
  - Drawing: in `paint`, behind waveform and region boundaries, faint vertical lines per beat and stronger ones per bar start (colors from the panel look, subtle; the region boundaries stay the most prominent); omit beats under about 4 px spacing (only bars); omit bars under about 4 px likewise; without data no grid.

- [ ] **Step 1: Write tests:** `LoopLane grid: gridStep follows bars and beats per bar` (8 bars, 4 beats → 1/32; 4 bars, 3 beats → 1/12; 7/8 with numerator 7). `LoopLane grid: no grid without data or with invalid bars` (step 0). `LoopLane grid: snapPhase rounds to the nearest beat and stays within 0..1`. `LoopLane grid: a dragged boundary snaps to beats` (width 800 px, 8 bars of 4 beats: dragging to x = 103 lands at beat 4/32 = 100 px; result after `release()`). `LoopLane grid: a free drag does not snap` (`dragTo(x, true)`). `LoopLane grid: a double click adds a boundary on the beat, a free double click on the exact position`. `LoopLane grid: snapping respects the minimum region width` (nearest beat too close to a neighboring boundary: the usual clamping of `RegionMap`, no crash, no double removal). `LoopLane grid: an existing off-grid boundary is not moved by selecting a region or by pressing`. `LoopLane grid: beats closer than 4 px are not drawn, then neither bars` (via a small query `bool beatLinesShown() const; bool barLinesShown() const;`) and `a too fine grid does not snap` (`gridStep()` = 0). `snapBoundariesToGrid: rounds, drops duplicates and values at the ends, keeps order` (step 0.25: `{0.1, 0.12, 0.49, 0.5, 1.0}` gives `{0.5}`, since 0.1 and 0.12 fall to 0 and 1.0 to 1 and are therefore dropped, and 0.49 and 0.5 both to 0.5; second case step 0.125: `{0.3, 0.7, 0.2}` rounds to `{0.25, 0.75, 0.25}` and gives `{0.25, 0.75}` after sanitizing).
- [ ] **Step 2: Run the tests**, they fail.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run all tests**, green.
- [ ] **Step 5: Commit** `feat(ui): beat grid with snapping in the loop lanes`.

### Task 7: Grid in the editor (bars, time signature, suggestion)

**Files:**
- Modify: `plugin/DubcyclingProcessor.h`, `plugin/DubcyclingProcessor.cpp`, `plugin/ui/Editor.h`, `plugin/ui/Editor.cpp`
- Test: `tests/plugin/test_Processor.cpp`, `tests/plugin/test_Editor.cpp`

**Interfaces:**
- Consumes: `LoopLane::setGrid`, `snapBoundariesToGrid`, `TransportInfo::tsNum`.
- Produces: `int DubcyclingProcessor::hostBeatsPerBar() const;` (numerator of the time signature last reported by the host, 4 before the first; `std::atomic<int>`, set in `processBlock` like `hostBpm_`, only for numerators `1..64`). The editor sets in `refresh()` and on a change of the bars for each track `setGrid(bars of the track, hostBeatsPerBar())`. `Editor::suggestRegions` rounds the suggested boundaries before writing with `snapBoundariesToGrid(found, lane.gridStep())` (unchanged without a grid).

- [ ] **Step 1: Write tests:** `Processor: hostBeatsPerBar defaults to 4 and follows the host time signature` (via the existing helpers for `processBlock` with host position; otherwise make it testable via the `toTransportInfo` path). `Editor: both lanes get the grid of their own bars and the host beats per bar` (loop 1 with 4 bars, loop 2 with 8: `lane(0).gridStep()` = 1/16, `lane(1).gridStep()` = 1/32). `Editor: changing the bars field updates the grid`. `Editor: the suggestion snaps its boundaries to the grid` (data with transients between beats: all boundaries in the state lie on multiples of `gridStep()`; count not larger than before). `Editor: without a loaded file the lane has no grid`.
- [ ] **Step 2: Run the tests**, they fail.
- [ ] **Step 3: Implement.** Snapshot of the editor with grid for visual inspection (as in Task 5); grid and boundaries must be readable in both tracks, bars stronger than beats.
- [ ] **Step 4: Run all tests**, green.
- [ ] **Step 5: Commit** `feat(ui): wire the grid to bars, host time signature and the suggestion`.

### Task 8: Documentation and overall check

**Files:**
- Modify: `README.md`, `docs/superpowers/backlog.md`

**Interfaces:** none.

- [ ] **Step 1: README** extend with the two functions (grid: quarter notes, snapping always on, Shift key free, lines for beats and bars, AUTO snaps; mix: knob per rule row, center = both full, left loop 2 quieter, right loop 1 quieter, only effective while the loop 2 region plays, preview ignores the mix; bank format version 3: older plugin versions reject new banks; projects with mix that an older version saves lose the mix values). Do not change the plugin version number. Backlog: new items 7 (grid) and 8 (mix per rule) as DONE (2026-11-05) in the style of the existing ones, name open ideas (adjustable grid width, triplets, host automation of the mix, mix for the preview).
- [ ] **Step 2: Overall test** `powershell -NoProfile -ExecutionPolicy Bypass -File ./build.ps1 all`, green (state the count).
- [ ] **Step 3: Validation** `powershell -NoProfile -ExecutionPolicy Bypass -File ./build.ps1 validate`: Steinberg validator and pluginval (level 5) without errors; report the result literally, do not gloss over a failure.
- [ ] **Step 4: Commit** `docs: beat grid and loop mix in the README and backlog`.
