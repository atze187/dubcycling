# Dubcycling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A VST3 plugin in which two stereo loops are split into regions and loop 1 regions trigger loop 2 regions by weighted random draw, clocked by the host.

**Architecture:** `engine/` is pure C++ without JUCE GUI and contains timing, regions, rules, sequencer and playback. `plugin/` contains processor, state, file loading and UI. Both loops read phase-locked to the host position (PPQ) with pitch-preserving grain stretch. This way they follow tempo changes and transport jumps without precomputation, and the LCM multiplication costs no memory.

**Tech Stack:** C++20, JUCE 8.0.15, CMake presets, Catch2 3.16.0, VST3. Structure and `build.ps1` as in `E:\Repos\dubgefahren`.

**Spec:** `docs/superpowers/specs/2026-11-02-dubcycling-design.md`

## Global Constraints

- C++20, JUCE 8.0.15, Catch2 v3.16.0, CMake ≥ 3.25, VST3 only.
- Platforms one after the other: Windows, macOS, Linux. No platform-specific code in `engine/` and `plugin/`.
- Two loops, both stereo, one stereo output, no audio input, no MIDI control.
- File formats: WAV, MP3, FLAC, AIFF.
- Loops are quantized to whole bars, stretched to the host BPM and follow BPM changes live.
- Region boundaries are free, not bound to the grid.
- Per loop 1 region at most one row per loop 2 region with weight (0–100 %) and mode Gate/One-Shot. The sum of the weights is at most 100 %, the remainder is silence.
- A draw is made on every entry into the region. The loop 2 region starts at its beginning. Loop 2 has at most one voice. A new draw aborts the running region.
- Without a rule, without transport or with an invalid BPM, loop 2 or the whole plugin respectively is silent.
- Session state contains per loop only the reference (path, file name, hash of the whole file), regions, bars and rules.
- Effects are not part of this plan, only a hook in the signal path.
- No performance mode in this plan.
- License: `LICENSE` (AGPL-3.0) is taken over from dubgefahren because code is copied from there. This is an assumption of the planner.

## Review Focus

The spec does not mention these cases, but they would occur in use:

1. A mono file or an extremely short or silent file is loaded: mono is duplicated to both channels, files under 1 beat in length are rejected (Task 2, Task 3).
2. The host delivers BPM 0, NaN or a negative PPQ position: output is silence, no crash, no NaNs (Task 3, Task 8).
3. Two region boundaries lie almost on top of each other or on 0/1: boundaries are clamped with a minimum distance (Task 5).
4. A weight set with a sum over 100 % comes from a loaded state: weights are scaled proportionally to 100 % (Task 6, Task 9).
5. The state is empty, damaged or comes from an unknown version: the plugin starts empty (Task 9).
6. Time signature is not 4/4: beats per bar come from the host time signature (Task 3, Task 8).

---

## File Structure

```
CMakeLists.txt  CMakePresets.json  build.ps1  LICENSE  README.md  .gitignore
engine/   StereoData.h  LoopTiming.{h,cpp}  SampleInterp.h  GrainStretch.{h,cpp}
          LoopReader.{h,cpp}  RegionMap.{h,cpp}  RuleSet.{h,cpp}  Sequencer.{h,cpp}
          Engine.{h,cpp}  TransientDetector.{h,cpp}  FxSlot.h
plugin/   DubcyclingProcessor.{h,cpp}  LoopLoader.{h,cpp}  PluginState.{h,cpp}
          FileRelink.{h,cpp}  ui/Editor.{h,cpp}  ui/LoopLane.{h,cpp}  ui/RulePanel.{h,cpp}
tests/    one test_*.cpp per engine file, tests/plugin/test_*.cpp per plugin file
```

---

### Task 1: Project scaffold and empty plugin

**Files:**
- Create: `CMakeLists.txt`, `CMakePresets.json`, `build.ps1`, `LICENSE`, `.gitignore`, `README.md`, `engine/CMakeLists.txt`, `plugin/CMakeLists.txt`, `tests/CMakeLists.txt`, `plugin/DubcyclingProcessor.{h,cpp}`, `tests/test_Smoke.cpp`
- Copy from `E:\Repos\dubgefahren` and adapt (names `dg`→`dc`, `Dubgefahren`→`Dubcycling`): `CMakeLists.txt`, `CMakePresets.json` (Windows presets only), `build.ps1` (commands configure/build/test/all), `LICENSE`.

**Interfaces:**
- Produces: CMake targets `dc_engine` (STATIC, include root = project root), `Dubcycling` (VST3), `dc_tests`. `dc::DubcyclingProcessor : juce::AudioProcessor` with stereo output, without input, `processBlock` clears the buffer.

- [ ] **Step 1:** Test `test_Smoke.cpp`: `TEST_CASE("engine library links")` calls a trivial function `dc::version()` from `engine/Version.h` and checks `std::string(dc::version()) == "0.1.0"`.
- [ ] **Step 2:** Run `powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 test`. Expected: build error because of `Version.h`.
- [ ] **Step 3:** Create the scaffold and `Version.h`; plugin with empty `processBlock`. The editor is an empty `juce::GenericAudioProcessorEditor`.
- [ ] **Step 4:** Run `build.ps1 all`. Expected: build green, `ctest` reports 1 test passed, `build\plugin\Dubcycling_artefacts\Release\VST3\Dubcycling.vst3` exists.
- [ ] **Step 5:** Commit `chore: project scaffold`.

### Task 2: Load stereo

**Files:**
- Create: `engine/StereoData.h`, `plugin/LoopLoader.{h,cpp}`, `tests/plugin/test_LoopLoader.cpp`
- Reference: `E:\Repos\dubgefahren\plugin\SampleLoader.{h,cpp}`

**Interfaces:**
- Produces: `struct dc::StereoData { std::vector<float> left, right; double sampleRate = 44100.0; size_t frames() const; }` and `std::shared_ptr<const StereoData> dc::LoopLoader::decode(const juce::File&, juce::String& error)` (static) as well as `request(int slot, uint64_t ticket, const juce::File&)` and `takeResults()` as in the original, result `LoopLoadResult { int slot; uint64_t ticket; std::shared_ptr<const StereoData> data; juce::String error; }`.

- [ ] **Step 1:** Tests: `decodes stereo wav keeps both channels` (left 0.5 / right −0.5 stay separate), `mono file is duplicated to both channels`, `flac and aiff decode`, `mp3 decode is registered` (format via `registerBasicFormats`; if MP3 is missing on the platform, skip the test with `SKIP`), `missing file reports error "file not found"`, `unsupported file reports error`, `file shorter than 1000 frames is rejected`.
- [ ] **Step 2:** Run `ctest --preset windows-release -R LoopLoader`. Expected: FAIL (class missing).
- [ ] **Step 3:** Derive from `SampleLoader`, but without the mono average and without the 60-second limit. Instead an upper limit `kMaxLoopFrames = 48000 * 600` (assumption: ten minutes at 48 kHz). More than two channels: only the first two.
- [ ] **Step 4:** Run the same tests. Expected: PASS.
- [ ] **Step 5:** Commit `feat(plugin): stereo loop loader`.

### Task 3: Loop timing

**Files:**
- Create: `engine/LoopTiming.{h,cpp}`, `tests/test_LoopTiming.cpp`

**Interfaces:**
- Produces:
  - `double dc::beatsPerBar(int tsNum, int tsDen)` = `tsNum * 4.0 / tsDen`; invalid values (≤ 0) give 4.0.
  - `int dc::suggestBars(double seconds, double bpm, double beatsPerBar)`: number of bars, rounded, at least 1; for invalid input 1.
  - `long long dc::lcmBars(int a, int b)`: common cycle length of both loops in bars. The engine reads phase-locked and therefore multiplies nothing in memory; `lcmBars` serves to display the total cycle length in the editor (Task 10).
  - `double dc::sourceStep(size_t sourceFrames, double loopBeats, double bpm, double outRate)`: source frames per output frame, limited to `[kMinStep, kMaxStep]` = `[0.25, 4.0]`; for `bpm <= 0`, NaN, `outRate <= 0` or `sourceFrames == 0` it gives 0.0.

- [ ] **Step 1:** Tests: `suggestBars(8.0, 120, 4) == 4`, `suggestBars(0.01, 120, 4) == 1`, `suggestBars(8.0, 0, 4) == 1`, `lcmBars(3,4) == 12`, `lcmBars(4,4) == 4`, `beatsPerBar(6,8) == 3.0`, `beatsPerBar(0,4) == 4.0`, `sourceStep(192000, 8.0, 120, 48000) == Approx(1.0)` (8 beats at 120 BPM are 4 s, i.e. 192000 output frames), `sourceStep(96000, 8.0, 120, 48000) == Approx(0.5)`, clamping at 10x tempo to 4.0, NaN gives 0.0.
- [ ] **Step 2:** Run `ctest ... -R LoopTiming`. Expected: FAIL.
- [ ] **Step 3:** Implement per the signatures.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat(engine): loop timing`.

### Task 4: Phase-locked playback with grain stretch

**Files:**
- Create: `engine/SampleInterp.h`, `engine/GrainStretch.{h,cpp}` (copy from dubgefahren, namespace `dg`→`dc`), `engine/LoopReader.{h,cpp}`, `tests/test_LoopReader.cpp`
- `GrainStretch::process` does not allocate (two pieces in `std::array`); that settles it.

**Interfaces:**
- Consumes: `StereoData`, `sourceStep` (Task 3).
- Produces: `class dc::LoopReader { void setData(std::shared_ptr<const StereoData>); void reset(); void render(float* l, float* r, int n, double phase01, double phaseStep01, double step); }`. `phase01` is the position in the loop (0..1) at the first output frame, `phaseStep01` the increment per output frame, `step` the source ratio from `sourceStep`. Without data or with `step == 0`, `render` writes zeros. The method is real-time safe (no allocation).

- [ ] **Step 1:** Tests: `silence without data`, `sine keeps pitch at double tempo` (sine 440 Hz, phase runs twice as fast, fundamental frequency of the output stays 440 Hz ±2 %, counted via zero crossings), `position follows phase after a jump` (phase jumps from 0.1 to 0.6, output afterwards corresponds to the source material at 0.6 within one grain length), `no NaN with step 0 or phase outside 0..1`.
- [ ] **Step 2:** Run `ctest ... -R LoopReader`. Expected: FAIL.
- [ ] **Step 3:** Two `GrainStretch` instances (left/right) with the same schedule; `srcPos = wrap(phase01) * frames`, `GrainFrame.cycle = true`, `lo = 0`, `hi = frames`. Grain length about 40 ms as a constant.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat(engine): phase-locked loop reader`.

### Task 5: Regions

**Files:**
- Create: `engine/RegionMap.{h,cpp}`, `tests/test_RegionMap.cpp`

**Interfaces:**
- Produces: `class dc::RegionMap` with `std::vector<double> boundaries` (ascending, values in (0,1), fractions of the loop length). `int regionCount() const` (= boundaries + 1), `int regionAt(double phase01) const`, `std::pair<double,double> bounds(int region) const`, `bool addBoundary(double x)`, `bool moveBoundary(int index, double x)`, `bool removeBoundary(int index)`. `constexpr double kMinRegion = 0.005`. All changes keep the minimum distance, `add`/`move` clamp or reject respectively and return `false`.

- [ ] **Step 1:** Tests: `no boundaries is one region`, `add keeps sorted order`, `add at 0.0 or 1.0 is rejected`, `add closer than kMinRegion to a neighbour is rejected`, `move is clamped between neighbours`, `regionAt returns correct index at boundaries and for 1.0`, `remove merges regions`.
- [ ] **Step 2:** Run `ctest ... -R RegionMap`. Expected: FAIL.
- [ ] **Step 3:** Implement; `regionAt` via binary search, `phase01` is wrapped into [0,1).
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat(engine): region map`.

### Task 6: Rules and draw

**Files:**
- Create: `engine/RuleSet.{h,cpp}`, `tests/test_RuleSet.cpp`

**Interfaces:**
- Produces: `enum class dc::PlayMode { Gate, OneShot }`; `struct dc::RuleTarget { int region2; float weight; PlayMode mode; }` (weight 0..1); `class dc::RuleSet { void setTargets(int region1, std::vector<RuleTarget>); const std::vector<RuleTarget>& targets(int region1) const; std::optional<RuleTarget> draw(int region1, float u) const; }`. `setTargets` scales weights with a sum over 1 proportionally to a sum of 1 and clamps individual ones to [0,1]. `draw` goes cumulatively through the targets; if `u` (0 ≤ u < 1) lies beyond the sum, it gives `std::nullopt` (silence). Without a rule always `nullopt`.

- [ ] **Step 1:** Tests: `no rule draws nothing`, `weights 0.5/0.3 draw first for u=0.2, second for u=0.6, nothing for u=0.85`, `weights summing to 1.4 are scaled so the sum is 1`, `negative or NaN weight becomes 0`, `u = 0 picks the first non-zero target`, `statistical: 10000 draws with 0.7/0.2 are within 3 percent`.
- [ ] **Step 2:** Run `ctest ... -R RuleSet`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat(engine): rule set and weighted draw`.

### Task 7: Sequencer (loop 2 voice)

**Files:**
- Create: `engine/Sequencer.{h,cpp}`, `tests/test_Sequencer.cpp`

**Interfaces:**
- Consumes: `RegionMap`, `RuleSet`, `RuleTarget`, `PlayMode`.
- Produces: `class dc::Sequencer`.
  - `struct Command { enum class Type { None, Stop, Start } type; int region2; PlayMode mode; }`
  - `void setRandom(std::function<float()> uniform01)` (default: `std::mt19937`).
  - `Command onRegion1(int region1, const RuleSet&)` is called on every entry into a loop 1 region and draws anew. Result `Start` with target or `Stop` (silence or no rule).
  - `bool shouldGateStop(int region1Now) const`: for gate mode, when the triggering loop 1 region has been left.
  - The sequencer only holds state (active target, triggering region); `Engine` renders the audio (Task 8).
  - `constexpr int kCancelFadeMs = 5`.

- [ ] **Step 1:** Tests with a fixed random sequence: `entry with rule starts target`, `entry without rule stops voice`, `silence draw stops voice`, `new entry cancels a running one-shot and starts the new target`, `re-entering the same region draws again (wrap)`, `gate stops when region 1 changes`, `one-shot does not stop when region 1 changes`.
- [ ] **Step 2:** Run `ctest ... -R Sequencer`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat(engine): sequencer`.

### Task 8: Engine and processor

**Files:**
- Create: `engine/Engine.{h,cpp}`, `engine/FxSlot.h`, `tests/test_Engine.cpp`
- Modify: `plugin/DubcyclingProcessor.{h,cpp}`, `engine/CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Tasks 3–7.
- Produces:
  - `struct dc::TransportInfo { bool playing; double ppq; double bpm; int tsNum = 4; int tsDen = 4; }`
  - `struct dc::LoopSlot { std::shared_ptr<const StereoData> data; int bars = 1; RegionMap regions; }`
  - `struct dc::Model { LoopSlot loop1, loop2; RuleSet rules; }`
  - `class dc::Engine { void prepare(double sampleRate, int maxBlock); void setModel(std::shared_ptr<const Model>); void process(const TransportInfo&, float* l, float* r, int n); }`. `setModel` is thread-safe (atomic exchange of the `shared_ptr`).
  - `struct dc::FxSlot { virtual void process(float* l, float* r, int n) {} virtual ~FxSlot() = default; }` as a hook per region, unused in this plan.
- Behavior: loop 1 phase = `fmod(ppq, loop1Beats) / loop1Beats`. Entry into a new region is detected when `regionAt` changes between two frames or the phase jumps (transport jump, wrap). Loop 2 plays the target region from its beginning with `sourceStep` from loop 2, in gate mode the region is repeated until the loop 1 region ends, in one-shot mode once. Abort and start crossfade `kCancelFadeMs`. Not playing, invalid `bpm` or negative PPQ gives silence and resets the voice.

- [ ] **Step 1:** Tests: `silent when not playing`, `silent for bpm 0 and NaN and negative ppq`, `loop 1 output matches source at ppq 0 with matching tempo`, `output follows a tempo change without discontinuity above a threshold`, `loop 2 silent without rule`, `loop 2 plays target region when rule weight is 1`, `gate region repeats until region 1 ends`, `one-shot plays once and then is silent`, `transport jump restarts the draw`, `6/8 time signature uses 3 beats per bar`, `process does not allocate` (allocation counter via an operator-new hook in the test, if available; otherwise a review point).
- [ ] **Step 2:** Run `ctest ... -R Engine`. Expected: FAIL.
- [ ] **Step 3:** Implement `Engine`. In the processor read `AudioPlayHead::getPosition()` (PPQ, BPM, time signature, `isPlaying`) and map it into `TransportInfo`. Missing fields count as invalid (silence).
- [ ] **Step 4:** Run `build.ps1 all`. Expected: all tests green, plugin builds.
- [ ] **Step 5:** Commit `feat: engine and processor integration`.

### Task 9: State and relink

**Files:**
- Create: `plugin/PluginState.{h,cpp}`, `plugin/FileRelink.{h,cpp}`, `tests/plugin/test_PluginState.cpp`, `tests/plugin/test_FileRelink.cpp`
- Modify: `plugin/DubcyclingProcessor.{h,cpp}` (`getStateInformation`/`setStateInformation`)

**Interfaces:**
- Produces:
  - `struct dc::LoopRef { juce::String path, fileName, hash; int bars; std::vector<double> boundaries; }`
  - `struct dc::PluginState { int version = 1; LoopRef loop1, loop2; std::vector<RuleEntry> rules; }`, `RuleEntry { int region1; std::vector<RuleTarget> targets; }`
  - `juce::MemoryBlock dc::serialize(const PluginState&)` and `std::optional<PluginState> dc::deserialize(const void*, size_t)`; unknown version, empty or damaged data give `nullopt`.
  - `juce::String dc::hashFile(const juce::File&)` (SHA-256 of the whole file).
  - `std::optional<juce::File> dc::relink(const LoopRef&, const juce::File& sessionDir)`: searches first `path`, then `sessionDir` by file name plus hash, then the folder of the stored path. If it finds nothing, it gives `nullopt`; the state (regions, rules) is kept.

- [ ] **Step 1:** Tests: `roundtrip keeps boundaries rules and bars`, `deserialize of empty block returns nullopt`, `deserialize of garbage returns nullopt`, `deserialize of version 99 returns nullopt`, `rules over 100 percent are scaled after load`, `relink finds moved file by name and hash in session dir`, `relink does not accept a file with the same name but different hash`, `relink returns nullopt when nothing matches`.
- [ ] **Step 2:** Run `ctest ... -R "PluginState|FileRelink"`. Expected: FAIL.
- [ ] **Step 3:** Serialize as `juce::ValueTree`→XML binary block. On loading, fetch loops asynchronously via `LoopLoader`. If a file is missing, the slot stays empty, but regions and rules stay in the state, and the editor shows "File missing".
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat(plugin): session state and relink`.

### Task 10: Editor with waveform lanes

**Files:**
- Create: `plugin/ui/Editor.{h,cpp}`, `plugin/ui/LoopLane.{h,cpp}`, `tests/plugin/test_LoopLane.cpp`
- Reference: `E:\Repos\dubgefahren\plugin\ui\WaveformView.{h,cpp}` (`computePeaks`, mouse logic via `pressAt/dragTo/release`)

**Interfaces:**
- Produces: `class dc::ui::LoopLane : juce::Component` with `setData(std::shared_ptr<const StereoData>)`, `setRegions(const RegionMap&)`, `setSelectedRegion(int)`, callbacks `std::function<void(const RegionMap&)> onRegionsChanged`, `std::function<void(int)> onRegionSelected`, `std::function<void(const juce::File&)> onFileDropped`. Operation testable via `pressAt(float x)`, `dragTo(float x)`, `release()`, `doubleClickAt(float x)`: a click into a region selects it, a double-click sets a boundary, dragging moves a boundary, a double-click on a boundary removes it. `Editor` shows loop 1 on top, loop 2 below, an input field for the bar count per loop (`suggestBars` as the preset) and a "File missing" display with reassignment.

- [ ] **Step 1:** Tests: `double click adds a boundary at the clicked x`, `drag moves the boundary and is clamped by kMinRegion`, `double click on a boundary removes it`, `click selects the region under x and fires onRegionSelected`, `empty data shows hint text and ignores clicks`.
- [ ] **Step 2:** Run `ctest ... -R LoopLane`. Expected: FAIL.
- [ ] **Step 3:** Implement; peaks from the average of both channels. Changes go via callback into a new `Model`, which is handed over via `Engine::setModel`.
- [ ] **Step 4:** Run `build.ps1 all` and load the plugin in a host. Expected: tests green, both lanes show a waveform, regions can be set.
- [ ] **Step 5:** Commit `feat(ui): editor with loop lanes`.

### Task 11: Rule panel

**Files:**
- Create: `plugin/ui/RulePanel.{h,cpp}`, `tests/plugin/test_RulePanel.cpp`
- Modify: `plugin/ui/Editor.{h,cpp}`

**Interfaces:**
- Produces: `class dc::ui::RulePanel : juce::Component` with `setContext(int region1, int region2Count, const std::vector<RuleTarget>&)` and callback `std::function<void(int region1, std::vector<RuleTarget>)> onChanged`. One row per loop 2 region: knob (0–100 %), percentage value and gate/one-shot switch. Testable via `setWeight(int row, float percent)` and `setMode(int row, PlayMode)`. A change that raises the sum above 100 % is limited to the remainder, the other rows stay unchanged. Display "Silence: x %" for the remainder.

- [ ] **Step 1:** Tests: `rows match region 2 count`, `setWeight caps at the remaining percent`, `silence label shows the remainder`, `setMode only changes that row`, `onChanged fires with updated targets`.
- [ ] **Step 2:** Run `ctest ... -R RulePanel`. Expected: FAIL.
- [ ] **Step 3:** Implement, hook up to `onRegionSelected` of the top lane.
- [ ] **Step 4:** Run `build.ps1 all`. Expected: green; in the host a rule audibly triggers loop 2.
- [ ] **Step 5:** Commit `feat(ui): rule panel`.

### Task 12: Region suggestions for drum loops

**Files:**
- Create: `engine/TransientDetector.{h,cpp}`, `tests/test_TransientDetector.cpp`
- Modify: `plugin/ui/Editor.{h,cpp}` ("Suggest" button per lane)

**Interfaces:**
- Produces: `std::vector<double> dc::suggestBoundaries(const StereoData&, int maxBoundaries)`: ascending fractions in (0,1), at least `kMinRegion` per region, at most `maxBoundaries`. Method (assumption): energy envelope, onset at positive jumps, keep the strongest `maxBoundaries` onsets.

- [ ] **Step 1:** Tests: `synthetic click track with 4 clicks yields 3 boundaries near the clicks`, `silence yields no boundaries`, `result respects maxBoundaries and kMinRegion`, `stereo with right channel only is detected`.
- [ ] **Step 2:** Run `ctest ... -R TransientDetector`. Expected: FAIL.
- [ ] **Step 3:** Implement; the button takes over the result as a `RegionMap` (replaces existing boundaries after a prompt).
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat: transient based region suggestions`.

### Task 13: macOS build

**Files:**
- Modify: `CMakePresets.json`, `CMakeLists.txt`; Create: `.github/workflows/build.yml`
- Reference: `E:\Repos\dubgefahren\CMakePresets.json` (preset `macos-universal`) and its workflow.

- [ ] **Step 1:** Add presets `macos-universal` and `macos-release` (arm64 + x86_64, deployment target 11.0).
- [ ] **Step 2:** The workflow builds and tests on `macos-latest` and uploads the VST3 as an artifact.
- [ ] **Step 3:** Search `engine/` and `plugin/` for platform-foreign code and remove it (`grep` for `windows.h`, `\\` paths, `_WIN32`). Expected: no hits.
- [ ] **Step 4:** Push to a branch, wait for the workflow. Expected: build and tests green (untested on real hardware, as in dubgefahren).
- [ ] **Step 5:** Commit `build: macOS preset and CI`.

### Task 14: Linux build

**Files:**
- Modify: `CMakePresets.json`, `.github/workflows/build.yml`

- [ ] **Step 1:** Add presets `linux-x64` and `linux-release`; install JUCE Linux dependencies (ALSA, X11, FreeType etc.) in the workflow via `apt`.
- [ ] **Step 2:** The workflow builds and tests on `ubuntu-latest`, tests run headless.
- [ ] **Step 3:** Push, wait for the workflow. Expected: build and tests green, VST3 artifact present.
- [ ] **Step 4:** Extend the README with build instructions for all three platforms.
- [ ] **Step 5:** Commit `build: Linux preset and CI`.
