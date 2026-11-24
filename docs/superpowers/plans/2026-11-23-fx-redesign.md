# FX Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the four side-by-side effect modules with a list + edit view panel, add user-reorderable chain order, a tempo-synced reverse Delay (up to 2 bars) and a Flanger that can stand still.

**Architecture:** The chain order is a permutation of five effects in `FxSettings` (global per instance, saved in state and in every preset). `FxChain` (JUCE-free, `engine/fx`) processes the inserts in that order; the per-region switch mask stays keyed by effect id (Delay = bit 4). Delay is a new JUCE-free unit fed with host tempo/bar length. The UI gets a list (drag to reorder, per-region quick switch) and an edit view of the selected effect.

**Tech Stack:** C++20, JUCE 8.0.15 (UI/state only), Catch2. Build/test: `powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 all` (tests via `ctest`; single tests: `build\tests\Release\dubcycling_tests.exe "<test name>"` — check `tests/CMakeLists.txt` for the exact target name).

**Spec:** `docs/superpowers/specs/2026-11-23-fx-redesign-design.md`

## Global Constraints

- Audio thread: no allocation, no locks. Engine part JUCE-free in `engine/`, tested with Catch2.
- With the default order and Delay off (all mask bit 4 clear), output is bit-identical to today: existing `test_FxChain.cpp`, `test_FxUnits.cpp`, `test_Engine.cpp` fx tests stay unchanged and green. (Exception: tests that assert `kFxInserts == 4` or mask range 0..15 are updated in Task 1/5.)
- Default order: Bitcrush, Filter, Flanger, Delay, Reverb. Mask stays one byte; Delay is bit 4; ids of existing effects do not change.
- Delay params: Time (stepped note value, 1/32 up to 2 bars, straight, dotted and triplet; at least 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars straight, triplet and dotted for divisions up to 1/2), Feedback (max 0.9), Mix, Reverse. All echoes reversed when Reverse is on; feedback is taken from the recorded forward block; dry never reversed; first echo one delay time after input.
- Delay buffer 30 s per channel; time clamped to it at very slow tempo; last known tempo (engine fallback 120 BPM) when the host gives none or is stopped; a "bar" = host beats per bar (4 without host info).
- Flanger Rate lower bound 0 Hz; at 0 the sweep stands at new param Manual (0..1, default 0.5); other ranges/defaults unchanged.
- Persistence: state XML optional attributes only (`kStateVersion` stays 1); `bank.json` version 6; absent data = defaults (default order, Delay at defaults, Manual 0.5); order saved in each preset; a bank newer than 6 is refused as before; fingerprint includes new data only when non-default.
- A reorder while sounding must not click (a brief fade/mute is acceptable). Bit-identical guarantee applies while the order is unchanged.
- UI strings English; industrial panel look; minimum window 800x600 stays usable (rule rows must fit); VST3 validation (Steinberg + pluginval strictness 5) stays green.
- Commit trailer: `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never merge locally; no push without the user.

## Review Focus

1. Saved data with an invalid order (duplicate/missing/garbage ids, wrong count) must load as the default order, never crash or lose an effect — pinned in Task 1 (`sanitizeFxOrder`) and Task 5 (state + bank).
2. Old presets/projects (no order, no delay params, mask < 16, Flanger rate in old range) load and sound as before — Task 5.
3. Delay at extreme tempi (20 BPM + 2 bars, 300 BPM + 1/32, tempo 0/NaN, sample rate 192 kHz): no overflow, no NaN, no allocation — Task 3.
4. Switching an effect on/off and changing Reverse / Time / order during playback: no clicks, no stuck tails; tails ring out and the unit sleeps afterwards — Tasks 3 and 4.
5. Switch bits stay with their effect after a reorder (strip, list and engine agree) — Tasks 4 and 6.

---

## File Structure

| File | Responsibility |
|---|---|
| `engine/FxSettings.h/.cpp` | `FxInsert::Delay`, new params, note-value table, order + sanitizer |
| `engine/fx/Flanger.cpp/.h` | standing mode (rate 0, Manual) |
| `engine/fx/Delay.h/.cpp` (new) | tempo-synced delay with reverse, tail, sleep |
| `engine/fx/FxChain.h/.cpp` | five inserts in user order, tempo input, reorder via flush |
| `engine/Engine.cpp/.h`, `engine/CMakeLists.txt` | pass tempo + order to the chain |
| `plugin/PluginState.cpp/.h` | order + mask range in the state XML |
| `plugin/presets/BankFormat.cpp/.h` | bank v6: order, mask range, fingerprint |
| `plugin/ui/FxStrip.*` | rows follow order; five colours |
| `plugin/ui/FxList.h/.cpp` (new) | the list: drag handle, colour, name, quick switch, selection, drag reorder |
| `plugin/ui/FxPanel.*` | list + edit view of the selected effect |
| `plugin/ui/Editor.*` | layout (narrower FX panel), order edit wiring |
| `tests/*`, `tests/plugin/*`, `tests/CMakeLists.txt` | tests per task |
| `README.md`, wiki `Effects.md`, spec status | docs |

---

### Task 1: FxSettings — Delay id, new params, note values, order

**Files:**
- Modify: `engine/FxSettings.h`, `engine/FxSettings.cpp`
- Test: `tests/test_FxSettings.cpp`

**Interfaces:**
- Produces:
  - `enum class FxInsert { Bitcrush = 0, Filter = 1, Flanger = 2, Reverb = 3, Delay = 4 }`; `constexpr int kFxInserts = 5;` (ids of existing effects unchanged = their bit).
  - New `FxParam` entries appended after `FlangerMix` (existing values unchanged): `FlangerManual, DelayTime, DelayFeedback, DelayMix, DelayReverse`; `kFxParamCount = 18`. Ids (strings): `"flanger.manual"` (0..1, def 0.5), `"delay.time"` (stepped index, def = index of 1/8 straight), `"delay.feedback"` (0..0.9, def 0.4), `"delay.mix"` (0..1, def 0.3), `"delay.reverse"` (0/1 stepped, def 0). `flanger.rate` min becomes 0.0 (max 5.0, def 0.3 unchanged).
  - `struct DelayTimeInfo { const char* name; double beats; bool bars; }`? — instead use: `constexpr int kDelayTimeSteps = <N>; double delayTimeBeats(int step, double beatsPerBar); const char* delayTimeName(int step);` Steps ascending in length from 1/32 to 2 bars; "bar" values scale with `beatsPerBar` (1 bar = beatsPerBar beats, 2 bars = 2×). Names like `"1/8"`, `"1/8 dot"`, `"1/8 trip"`, `"1 bar"`, `"2 bars"`.
  - `using FxOrder = std::array<FxInsert, kFxInserts>; FxOrder defaultFxOrder(); FxOrder sanitizeFxOrder(const FxOrder&);` (a valid permutation passes unchanged; anything else, e.g. duplicates or out-of-range ids, becomes `defaultFxOrder()`); `FxSettings::order` (default `defaultFxOrder()`), included in `operator==`.
  - `FxSettings::setSwitch/switchOn/maskFor` unchanged API; mask still `std::uint8_t`.

- [ ] **Step 1: Write failing tests** in `tests/test_FxSettings.cpp`: `default order is Bitcrush, Filter, Flanger, Delay, Reverb`; `sanitizeFxOrder keeps a valid permutation and replaces duplicates with the default` (e.g. {Reverb, Delay, Flanger, Filter, Bitcrush} unchanged; {Filter, Filter, Flanger, Delay, Reverb} → default; an out-of-range cast id → default); `Delay switch uses bit 4` (`setSwitch(0, FxInsert::Delay, true)` → `maskFor(0) == 16`); `flanger.rate range is 0..5 and default 0.3`; `delay time steps are ascending in length and contain the required values` (for beatsPerBar 4: lengths strictly increasing; names contain "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars"; 2 bars == 8 beats at 4/4 and 12 beats at 6/4... i.e. scales with beatsPerBar; "1/8 trip" == 1/3 beat, "1/8 dot" == 0.75 beat); `delay.feedback is clamped to 0.9`; defaults of all five new params; `FxSettings == ` compares order.
- [ ] **Step 2: Run to verify they fail** (compile errors / missing symbols): build + `ctest -R FxSettings`.
- [ ] **Step 3: Implement** the enum, params table rows (the `kInfo` table order must match the enum), note-value table (the exact list is the builder's call within the constraints above, 1/32 up to 2 bars), `defaultFxOrder`, `sanitizeFxOrder`, `FxSettings::order`.
- [ ] **Step 4: Run** `ctest -R "FxSettings"` — PASS. Other existing tests that assume four inserts or mask ≤ 15 may now fail to compile or assert: fix only those assertions (`kFxInserts`-sized loops in `test_Editor.cpp:938`, `test_FxPanel.cpp`, `test_FxStrip.cpp`, `test_Engine.cpp:1264` are handled in Tasks 4 and 6 — leave the build green by not breaking compilation; if a loop over `kFxInserts` now includes Delay and fails, note it for the owning task).
- [ ] **Step 5: Commit** `feat(fx): Delay id, new params, note values and chain order in FxSettings`.

---

### Task 2: Flanger standing mode

**Files:**
- Modify: `engine/fx/Flanger.h`, `engine/fx/Flanger.cpp`
- Test: `tests/test_FxUnits.cpp`

**Interfaces:**
- Consumes: `FxParam::FlangerRate` (min 0), `FxParam::FlangerManual` (Task 1).
- Produces: `Flanger::setParams` honours Rate 0 → the sweep position is fixed at `Manual` (0 = shortest delay `kMinDelayMs`, 1 = longest `kMinDelayMs + kDepthMs`, scaled by depth like the LFO value `lfo`, i.e. `lfo = Manual`); Rate > 0 ignores Manual. The lower clamp in `setParams` changes from 0.05 to 0.

- [ ] **Step 1: Write failing tests** in `tests/test_FxUnits.cpp`: `flanger at rate 0 is stationary` (feed the same noise burst twice at rate 0 with Manual 0.5; the wet output of identical input at different times is identical, i.e. the LFO phase does not matter; compare against a render started at a different internal phase); `flanger Manual 0 and 1 give different wet output at rate 0`; `flanger with rate > 0 ignores Manual` (two instances differing only in Manual produce identical output); `flanger rate 0 to 0.3 and back does not jump` (max sample difference of consecutive wet samples when switching the rate param stays below a bound equal to that of the existing smoothing test, or ≤ 0.5 for unit-level input); the existing flanger tests stay unchanged and pass.
- [ ] **Step 2: Run** `ctest -R FxUnits` — new tests fail.
- [ ] **Step 3: Implement** in `Flanger.cpp`: when the smoothed rate is 0 use `lfo = manual_` (smoothed like the other params, new member `manual_`/`manualTarget_`), otherwise the sine LFO as before; keep `phase_` untouched while rate is 0 so that going 0 → rate resumes without a jump (the position may change to the phase's LFO value over the 10 ms smoothing of `manual_`→ the LFO value: acceptable if no click; if a jump remains, ramp `lfoBlend` from manual to LFO over 10 ms).
- [ ] **Step 4: Run** `ctest -R FxUnits` — PASS (also all old tests).
- [ ] **Step 5: Commit** `feat(fx): Flanger can stand still (rate 0 with manual position)`.

---

### Task 3: Delay unit with reverse

**Files:**
- Create: `engine/fx/Delay.h`, `engine/fx/Delay.cpp`
- Modify: `engine/CMakeLists.txt` (add the new sources/header as the other fx units are listed)
- Test: `tests/test_FxUnits.cpp` (or a new `tests/test_Delay.cpp` registered in `tests/CMakeLists.txt` — builder's choice; keep tests next to the other unit tests if the file does not grow past ~900 lines)

**Interfaces:**
- Consumes: `FxParam::DelayTime/DelayFeedback/DelayMix/DelayReverse`, `delayTimeBeats` (Task 1).
- Produces (mirrors `Flanger`/`Reverb`):
```cpp
namespace dc::fx {
class Delay {
public:
    void prepare(double sampleRate);            // sizes the 30 s buffers
    void reset();
    void setParams(const FxParams& p);
    void setTempo(double bpm, double beatsPerBar); // non-finite or <= 0 bpm keeps the last value (initial 120, 4)
    void process(float* l, float* r, int n);     // replaces input by the pure wet signal (scaled by Mix, no dry part)
    float tailLevel() const;                     // like Flanger/Reverb, for the sleep decision
};
}
```
  Delay time in samples = `delayTimeBeats(step, beatsPerBar) * 60 / bpm * sampleRate`, clamped to the buffer length; time/tempo changes are smoothed (no click). Reverse off: normal feedback delay. Reverse on: record blocks of one delay time, play each recorded block backwards (short crossfade between consecutive reversed blocks to avoid clicks), feedback re-feeds the recorded forward block.

- [ ] **Step 1: Write failing tests**: `delay echoes an impulse one beat later at 120 BPM with time 1/4` (impulse at sample 0; the wet peak appears at `0.5 s * sr` ± 1; before it silence); `delay feedback repeats decay by the feedback factor`; `delay mix 0 is silent`; `delay with reverse plays the echo block backwards` (input: ramp of known length = delay time, one echo; the wet output over the following delay-time block equals the reversed input within the crossfade length, up to the Mix scale; the dry is not in the output); `delay reverse echo arrives one delay time after the input block` ; `delay tempo change does not click` (change bpm mid-stream; max sample-to-sample jump of a sine input bounded); `delay at 20 BPM with 2 bars clamps to the buffer and stays finite` ; `delay with NaN/0/negative bpm keeps the last tempo`; `delay at 192 kHz allocates only in prepare` (prepare at 192000, then process 10 s without the buffers' data pointers changing); `delay tail dies out and tailLevel falls below the sleep threshold after the input stops` (feedback 0.4); `delay reset clears the buffers` ; `delay toggling Reverse while sounding stays finite and bounded` (|out| ≤ input peak × 2).
- [ ] **Step 2: Run** — fail (missing header).
- [ ] **Step 3: Implement** `Delay` as above; one fixed-size buffer per channel (30 s × sampleRate, allocated in `prepare`); denormal-safe; non-finite input treated as 0 as in `Flanger`.
- [ ] **Step 4: Run** new tests — PASS.
- [ ] **Step 5: Commit** `feat(fx): tempo-synced Delay with reverse`.

---

### Task 4: FxChain — five inserts in user order, tempo, reorder

**Files:**
- Modify: `engine/fx/FxChain.h`, `engine/fx/FxChain.cpp`, `engine/Engine.h`, `engine/Engine.cpp`
- Test: `tests/test_FxChain.cpp`, `tests/test_Engine.cpp`

**Interfaces:**
- Consumes: `FxOrder`, `FxSettings::order` (Task 1), `Delay` (Task 3), `Flanger` standing (Task 2).
- Produces:
  - `void FxChain::setOrder(const FxOrder& order)` — sanitizes; no-op when equal; when the chain is idle (`!anyActive()`) applies at once; when active, starts a flush (reuse the existing 50 ms flush fade) and applies the new order when the fade has completed (state is cleared by the flush). `FxChain::order() const` returns the order in effect.
  - `void FxChain::setTempo(double bpm, double beatsPerBar)` — forwards to the Delay.
  - The chain loop iterates `order_`; per insert the existing blend (Bitcrush, Filter) or additive-gated (Flanger, Reverb, Delay) treatment applies; Delay uses the additive kind with `tailLevel()` and sleeps like Flanger/Reverb. `setMask` handles 5 bits. `sleep()` and `prepare()`/`setParams()` include Delay.
  - `Engine`: where `chain_.setParams(m->fx.params)` is called (lines ~762, ~989) also call `chain_.setOrder(m->fx.order)` and `chain_.setTempo(lastBpm_, lastBeatsPerBar_)` (names as in `Engine.h`; the second only changes values, so it is called every slice).

- [ ] **Step 1: Write failing tests** in `tests/test_FxChain.cpp`: `default order with delay off is bit-identical to the pre-change chain` (render a fixed noise signal through a chain with the old four effects on via mask 0b1111 with default params; compare to stored reference values — if no reference exists, assert equality against a second chain built from the same units in the old fixed order by hand-coding the old loop in the test); `chain applies effects in the chosen order` (Filter-then-Bitcrush differs from Bitcrush-then-Filter on the same input; with order {Reverb, Delay, Flanger, Filter, Bitcrush} output equals the manual composition); `reorder while idle applies immediately`; `reorder while an effect is active fades and then applies` (output stays finite, no sample jump above the flush-fade bound, and after the fade the new order is in effect; `order()` returns the new order only after the fade); `delay mask bit 4 gates the delay and its tail rings out` ; `chain sleeps all five inserts when masks are 0 and tails are gone` (`anyActive()` false); `tempo change reaches the delay`. In `tests/test_Engine.cpp`: `engine passes order and tempo to the chain` (model with `fx.order` reversed and mask bit 4; block output differs from the default-order render, finite) and fix the loop at line ~1264 for five inserts.
- [ ] **Step 2: Run** — fail.
- [ ] **Step 3: Implement** the chain (replace the two fixed loops by one loop over `order_` with the existing per-kind bodies; keep the flush logic; add `pendingOrder_` + `hasPendingOrder_`, applied where the flush completes and also in `reset()`), and the Engine calls.
- [ ] **Step 4: Run** full `ctest` — PASS; existing fx and engine tests unchanged and green.
- [ ] **Step 5: Commit** `feat(fx): five-insert chain in user order, tempo input, click-free reorder`.

---

### Task 5: Persistence — state XML and bank v6

**Files:**
- Modify: `plugin/PluginState.cpp`, `plugin/PluginState.h`, `plugin/presets/BankFormat.cpp`, `plugin/presets/BankFormat.h`
- Test: `tests/plugin/test_PluginState.cpp`, `tests/plugin/test_BankFormat.cpp`, `tests/plugin/test_BankStore.cpp`

**Interfaces:**
- Consumes: `FxSettings::order`, new params (Task 1).
- Produces:
  - State XML: the `Fx` node gets an optional attribute `order` = comma-separated ids (e.g. `"0,1,2,4,3"`), only written when not the default order; read through `sanitizeFxOrder` (absent/garbage → default). The mask clamp in `fxFromTree` becomes `0..31`; `sanitizeFx` also clamps masks to `0..31` and sanitizes the order. New params serialize through the existing id loop automatically (non-default only).
  - Bank: `kBankVersion` 6; per-preset `"fx"` object gets optional `"order"` (array of ints; only when non-default); mask clamp `0..31` (`BankFormat.cpp:307`); version 1–5 files still read (default order); a bank newer than 6 stays "too new". The preset fingerprint (`BankFormat.cpp` ~533) includes the order only when it is not the default, and new params only when they differ (already handled by the existing param loop: check that default-equality uses the new defaults).
  - Update the version comment block in `BankFormat.h` (6 adds the per-preset fx order and the Delay/Flanger params).

- [ ] **Step 1: Write failing tests**: state round-trip with a non-default order, Delay params and mask bit 4 (`maskFor` 16 survives; the old clamp to 15 would fail); `state with garbage order loads the default order` (duplicates, missing ids, non-numbers, wrong count); `state without order/delay params loads defaults and old masks` (Review Focus 2); `default order is not written` (XML has no `order` attribute); bank v6 round-trip of order + new params + mask; `bank v5 file loads with default order and Delay defaults`; `bank v7 file is refused as too new`; `fingerprint changes when only the order changes and is unchanged for default order`; `BankStore::savePreset keeps order` (via `BankStore` test pattern already used for fx/passes); a preset loaded with `order` applies it to `PluginState::fx`.
- [ ] **Step 2: Run** — fail.
- [ ] **Step 3: Implement** the serialisation as described.
- [ ] **Step 4: Run** `ctest -R "PluginState|BankFormat|BankStore"` — PASS, then full `ctest`.
- [ ] **Step 5: Commit** `feat(state): save fx order and delay settings; bank format v6`.

---

### Task 6: FxStrip follows the order and shows five effects

**Files:**
- Modify: `plugin/ui/FxStrip.h`, `plugin/ui/FxStrip.cpp`
- Test: `tests/plugin/test_FxStrip.cpp`

**Interfaces:**
- Consumes: `FxOrder` (Task 1).
- Produces: `void FxStrip::setOrder(const FxOrder& order)` (default order initially); marker columns are laid out in `order` (column position `c` shows insert `order[c]`); `fxInsertColour` gains `FxInsert::Delay` (a fifth colour clearly distinct from the other four on the dark panel; the existing distinctness test extends to it); `markerBounds/columnRange/markerAt` keep their signatures and now map by effect id (not by position); marker width uses `kFxInserts` = 5. The "collective marker" threshold logic stays, recomputed for five.

- [ ] **Step 1: Write failing tests**: `markers follow the chain order` (with a reversed order, `markerBounds(r, Reverb).getX() < markerBounds(r, Bitcrush).getX()`); `a click toggles the effect under the marker, not the column index` (`toggleAt` over the Reverb marker reports `FxInsert::Reverb` with a reversed order); `five markers are distinct and non-overlapping`; `colours of all five inserts are pairwise distinct`; `switch bits stay with their effect after setOrder` (Review Focus 5); existing tests with four inserts updated to five.
- [ ] **Step 2: Run** — fail.
- [ ] **Step 3: Implement** `setOrder` and the id↔column mapping.
- [ ] **Step 4: Run** `ctest -R FxStrip` — PASS.
- [ ] **Step 5: Commit** `feat(ui): effect strip follows chain order and shows Delay`.

---

### Task 7: FxList and the two-part FxPanel

**Files:**
- Create: `plugin/ui/FxList.h`, `plugin/ui/FxList.cpp` (add to `plugin/CMakeLists.txt` and the plugin test target in `tests/CMakeLists.txt` as the other ui files)
- Modify: `plugin/ui/FxPanel.h`, `plugin/ui/FxPanel.cpp`
- Test: new `tests/plugin/test_FxList.cpp`, `tests/plugin/test_FxPanel.cpp`

**Interfaces:**
- Consumes: `FxOrder`, `FxInsert`, `fxInsertColour` (Task 6), `FxParam` incl. Delay and `FlangerManual` (Task 1), `delayTimeName`.
- Produces:
  - `class FxList final : juce::Component`: five rows in `order`; each row = drag handle (≡), colour marker, name (BITCRUSH, FILTER, FLANGER, DELAY, REVERB), on/off switch (for the selected region; disabled/greyed without a valid region). API: `void setContext(const FxOrder&, int selectedInsert, std::uint8_t maskOfSelectedRegion, bool switchesEnabled)`; callbacks `std::function<void(FxInsert)> onSelected`, `std::function<void(FxInsert, bool on)> onSwitched`, `std::function<void(const FxOrder&)> onOrderChanged`; test seams `rowBounds(int position)`, `switchBounds(FxInsert)`, `handleBounds(int position)`, `dragRow(int fromPosition, int toPosition)` (what a completed drag calls; also drives the visual: lifted row + insertion line while dragging). A drag works regardless of region selection; click on a row (outside the switch/handle) selects it.
  - `FxPanel` keeps its existing public API where still meaningful (`setContext(region2, regionCount, labels, fx)`, `onParamsChanged`, `onSwitchChanged`, `setParam/resetParam/beginDrag/dragParam/endDrag/setSwitch`, `param`, `paramText`, `switchOn`, `switchEnabled`, `switchText`, `switchBounds`, `paramBounds`) and adds `std::function<void(const FxOrder&)> onOrderChanged`, `void selectInsert(FxInsert)`, `FxInsert selectedInsert() const` (initially the first effect in the order; not persisted), `FxList& list()`. The edit view shows only the knobs of the selected effect (existing knob widgets, double-click resets; Filter Type as buttons). Delay knobs: Time (stepped slider showing `delayTimeName`), Feedback, Mix, and a Reverse toggle button; Flanger gets Manual (enabled-looking only when Rate is 0: dim it otherwise but still editable). `paramBounds` is empty for knobs of non-selected effects. A quick switch in the list calls `onSwitchChanged(region2, insert, on)` exactly like today's per-module switch.
  - Layout: list ≈ 130 px wide on the left, edit view fills the rest; the panel needs ≈ 300–360 px total.

- [ ] **Step 1: Write failing tests**: FxList — `rows follow the order`, `dragRow moves an effect and publishes the new full permutation once`, `a drag works without a selected region`, `switch click reports the effect id (not the position)` and `switches are disabled without a region` (`switchesEnabled == false` → click reports nothing), `click on a row selects`, `handle drag across all positions yields valid permutations`. FxPanel — `edit view shows only the selected effect's params` (`paramBounds(FxParam::DelayTime)` non-empty only after `selectInsert(FxInsert::Delay)`), `Delay Time shows note names ("1/8") and spans all steps`, `Reverse toggle publishes params once`, `Flanger Manual is editable and publishes`, `order change from the list publishes onOrderChanged and the strip order input stays consistent`, `setContext shows the switch state of the selected region for every effect in the list`, `a slider drag still publishes once at its end` (existing behaviour kept), existing FxPanel tests adapted from "four modules side by side" to list + edit view.
- [ ] **Step 2: Run** — fail.
- [ ] **Step 3: Implement** `FxList` (custom paint in the panel look; drag via `mouseDown/mouseDrag/mouseUp` on the handle, row follows the cursor, insertion line; commit on mouse up through `dragRow`) and rework `FxPanel` accordingly (keep the `Knob` helpers; replace the four `Module`s).
- [ ] **Step 4: Run** `ctest -R "FxList|FxPanel"` — PASS.
- [ ] **Step 5: Commit** `feat(ui): FX panel as effect list and edit view with drag-to-reorder`.

---

### Task 8: Editor layout and wiring

**Files:**
- Modify: `plugin/ui/Editor.h`, `plugin/ui/Editor.cpp`
- Test: `tests/plugin/test_Editor.cpp`

**Interfaces:**
- Consumes: `FxPanel::onOrderChanged`, `FxStrip::setOrder` (Tasks 6, 7), `FxSettings::order`.
- Produces: `void Editor::orderEdited(const FxOrder&)` — copies `proc_.getPluginState()`, sets `st.fx.order`, `proc_.applyEdit(st)` (like `paramsEdited`); `refresh()` feeds `fxStrip_.setOrder(st.fx.order)` and the panel context. `kFxPanelMinW`/`kFxPanelMaxW` become the new, smaller panel widths (about 360 / 460; the old 440/600 comment "four modules" is replaced) and the FX panel width uses them so the rule panel gets the freed width; `kBottomMinH` is re-checked for the tallest edit view (Flanger: Rate, Depth, Feedback, Mix, Manual — five rows may need ≥ the current 190 px or a two-column knob layout; builder's call, the minimum window 800x600 must still show all of the tallest edit view and the rule rows).

- [ ] **Step 1: Write failing tests**: `dragging in the list changes the order in the plugin state` (Review Focus 5 end to end: switch bits unchanged, `fx.order` changed, strip marker order changed); `loading a preset with a non-default order shows it in list and strip`; `at the minimum window size (800x600) the FX panel's tallest edit view and the rule rows are fully inside their bounds` (extends the existing layout test); `the rule panel is wider than before at the default size`; existing editor tests adapted (fx loops over `kFxInserts` at line ~938 now include Delay).
- [ ] **Step 2: Run** — fail.
- [ ] **Step 3: Implement** the wiring and layout.
- [ ] **Step 4: Run** full build + `ctest` — all PASS.
- [ ] **Step 5: Commit** `feat(ui): editor uses the list/edit FX panel; rule panel gets more room`.

---

### Task 9: Validation, docs, status

**Files:**
- Modify: `README.md` (bank format note: version 6), `docs/superpowers/specs/2026-11-23-fx-redesign-design.md` (Status: implemented), `tests/test_Smoke.cpp` only if the version changes (it does not here)
- Wiki: `E:\Repos\dubcycling.wiki\Effects.md` (+ screenshot via the existing `tests/plugin/test_WikiShots.cpp` helper on branch `docs/wiki`, if the user wants new screenshots) — separate repo, push only on the user's go.

- [ ] **Step 1:** Run `powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 validate` (Steinberg validator and pluginval strictness 5); Expected: both pass. If `vsthost.exe` locks `build\`, validate in a temp build dir as before.
- [ ] **Step 2:** Update the README's known-limitations bank-version sentence ("currently version 6") and the spec status; draft the new `Effects.md` text (list + edit view, reorder, Delay incl. reverse and note values, Flanger standing/Manual, order saved in presets, older plugin versions drop the new data) in `.superpowers/wiki/Effects.md` for the user; do not push the wiki.
- [ ] **Step 3:** Run the full suite once more (`build.ps1 all`) — all PASS.
- [ ] **Step 4: Commit** `docs: FX redesign status, README bank version`.

---

## Self-Review

- **Spec coverage:** two-part panel + quick switches + drag reorder (7, 8); all effects always listed (7: fixed five rows); order global, in preset and state (1, 5); Delay incl. reverse, note values to 2 bars, buffer/clamp/tempo fallback (1, 3, 4); Flanger standing (1, 2); rule panel gets room (8); strip follows order (6); compatibility/bank 6/fingerprint (5); bit-identical + validation (4, 9); mid-play reorder (4). No gaps.
- **Types:** `FxOrder`, `defaultFxOrder`, `sanitizeFxOrder`, `FxSettings::order`, `delayTimeBeats(step, beatsPerBar)`, `delayTimeName(step)`, `FxChain::setOrder/setTempo/order`, `Delay::setTempo/process/tailLevel`, `FxStrip::setOrder`, `FxList::{setContext,onSelected,onSwitched,onOrderChanged,dragRow}`, `FxPanel::{onOrderChanged,selectInsert,selectedInsert}`, `Editor::orderEdited` are consistent across tasks.
- **Proportion:** nine tasks; tests carry the values; bodies left to the builder except where the spec fixes behaviour.
