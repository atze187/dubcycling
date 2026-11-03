# Dubcycling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ein VST3-Plugin, in dem zwei Stereo-Loops in Regionen geteilt werden und Loop-1-Regionen per gewichteter Zufallsziehung Loop-2-Regionen auslösen, getaktet vom Host.

**Architecture:** `engine/` ist reines C++ ohne JUCE-GUI und enthält Timing, Regionen, Regeln, Sequencer und Abspielen. `plugin/` enthält Prozessor, Zustand, Dateiladen und UI. Beide Loops lesen phasengekoppelt an der Host-Position (PPQ) mit pitch-erhaltendem Grain-Stretch. Damit folgen sie Tempowechseln und Transportsprüngen ohne Vorberechnung, und das KGV-Vervielfachen kostet keinen Speicher.

**Tech Stack:** C++20, JUCE 8.0.15, CMake-Presets, Catch2 3.16.0, VST3. Aufbau und `build.ps1` wie in `E:\Repos\dubgefahren`.

**Spec:** `docs/superpowers/specs/2026-11-02-dubcycling-design.md`

## Global Constraints

- C++20, JUCE 8.0.15, Catch2 v3.16.0, CMake ≥ 3.25, nur VST3.
- Plattformen nacheinander: Windows, macOS, Linux. Kein plattformspezifischer Code in `engine/` und `plugin/`.
- Zwei Loops, beide Stereo, ein Stereo-Ausgang, kein Audio-Eingang, keine MIDI-Steuerung.
- Dateiformate: WAV, MP3, FLAC, AIFF.
- Loops werden auf ganze Takte quantisiert, auf Host-BPM gestreckt und folgen BPM-Änderungen live.
- Regionsgrenzen sind frei, nicht ans Raster gebunden.
- Pro Loop-1-Region höchstens eine Zeile je Loop-2-Region mit Gewicht (0–100 %) und Modus Gate/One-Shot. Die Summe der Gewichte ist höchstens 100 %, der Rest ist Stille.
- Gezogen wird bei jedem Eintritt in die Region. Die Loop-2-Region startet an ihrem Anfang. Loop 2 hat höchstens eine Stimme. Eine neue Ziehung bricht die laufende Region ab.
- Ohne Regel, ohne Transport oder bei ungültigem BPM ist Loop 2 bzw. das ganze Plugin stumm.
- Session-Zustand enthält pro Loop nur Referenz (Pfad, Dateiname, Hash der ganzen Datei), Regionen, Takte und Regeln.
- Effekte sind nicht Teil dieses Plans, nur ein Haken im Signalweg.
- Kein Performance-Modus in diesem Plan.
- Lizenz: `LICENSE` (AGPL-3.0) wird aus dubgefahren übernommen, weil Code von dort kopiert wird. Das ist eine Annahme des Planers.

## Review Focus

Diese Fälle nennt die Spec nicht, sie würden aber im Gebrauch auftreten:

1. Eine Mono-Datei oder eine extrem kurze oder stille Datei wird geladen: Mono wird auf beide Kanäle dupliziert, Dateien unter 1 Beat Länge werden abgelehnt (Task 2, Task 3).
2. Der Host liefert BPM 0, NaN oder eine negative PPQ-Position: Ausgabe ist Stille, kein Absturz, keine NaNs (Task 3, Task 8).
3. Zwei Regionsgrenzen liegen fast aufeinander oder auf 0/1: Grenzen werden mit Mindestabstand geklemmt (Task 5).
4. Ein Gewichtssatz mit Summe über 100 % kommt aus einem geladenen Zustand: Gewichte werden proportional auf 100 % skaliert (Task 6, Task 9).
5. Der Zustand ist leer, beschädigt oder stammt von einer unbekannten Version: Das Plugin startet leer (Task 9).
6. Taktart ist nicht 4/4: Beats pro Takt kommen aus der Host-Taktart (Task 3, Task 8).

---

## File Structure

```
CMakeLists.txt  CMakePresets.json  build.ps1  LICENSE  README.md  .gitignore
engine/   StereoData.h  LoopTiming.{h,cpp}  SampleInterp.h  GrainStretch.{h,cpp}
          LoopReader.{h,cpp}  RegionMap.{h,cpp}  RuleSet.{h,cpp}  Sequencer.{h,cpp}
          Engine.{h,cpp}  TransientDetector.{h,cpp}  FxSlot.h
plugin/   DubcyclingProcessor.{h,cpp}  LoopLoader.{h,cpp}  PluginState.{h,cpp}
          FileRelink.{h,cpp}  ui/Editor.{h,cpp}  ui/LoopLane.{h,cpp}  ui/RulePanel.{h,cpp}
tests/    ein test_*.cpp je Engine-Datei, tests/plugin/test_*.cpp je Plugin-Datei
```

---

### Task 1: Projektgerüst und leeres Plugin

**Files:**
- Create: `CMakeLists.txt`, `CMakePresets.json`, `build.ps1`, `LICENSE`, `.gitignore`, `README.md`, `engine/CMakeLists.txt`, `plugin/CMakeLists.txt`, `tests/CMakeLists.txt`, `plugin/DubcyclingProcessor.{h,cpp}`, `tests/test_Smoke.cpp`
- Copy from `E:\Repos\dubgefahren` und anpassen (Namen `dg`→`dc`, `Dubgefahren`→`Dubcycling`): `CMakeLists.txt`, `CMakePresets.json` (nur Windows-Presets), `build.ps1` (Befehle configure/build/test/all), `LICENSE`.

**Interfaces:**
- Produces: CMake-Targets `dc_engine` (STATIC, Include-Root = Projektwurzel), `Dubcycling` (VST3), `dc_tests`. `dc::DubcyclingProcessor : juce::AudioProcessor` mit Stereo-Ausgang, ohne Eingang, `processBlock` löscht den Puffer.

- [ ] **Step 1:** Test `test_Smoke.cpp`: `TEST_CASE("engine library links")` ruft eine triviale Funktion `dc::version()` aus `engine/Version.h` auf und prüft `std::string(dc::version()) == "0.1.0"`.
- [ ] **Step 2:** Run `powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 test`. Expected: Build-Fehler wegen `Version.h`.
- [ ] **Step 3:** Gerüst und `Version.h` anlegen; Plugin mit leerem `processBlock`. Editor ist ein leeres `juce::GenericAudioProcessorEditor`.
- [ ] **Step 4:** Run `build.ps1 all`. Expected: Build grün, `ctest` meldet 1 Test bestanden, `build\plugin\Dubcycling_artefacts\Release\VST3\Dubcycling.vst3` existiert.
- [ ] **Step 5:** Commit `chore: project scaffold`.

### Task 2: Stereo laden

**Files:**
- Create: `engine/StereoData.h`, `plugin/LoopLoader.{h,cpp}`, `tests/plugin/test_LoopLoader.cpp`
- Referenz: `E:\Repos\dubgefahren\plugin\SampleLoader.{h,cpp}`

**Interfaces:**
- Produces: `struct dc::StereoData { std::vector<float> left, right; double sampleRate = 44100.0; size_t frames() const; }` und `std::shared_ptr<const StereoData> dc::LoopLoader::decode(const juce::File&, juce::String& error)` (statisch) sowie `request(int slot, uint64_t ticket, const juce::File&)` und `takeResults()` wie im Vorbild, Ergebnis `LoopLoadResult { int slot; uint64_t ticket; std::shared_ptr<const StereoData> data; juce::String error; }`.

- [ ] **Step 1:** Tests: `decodes stereo wav keeps both channels` (links 0.5 / rechts −0.5 bleiben getrennt), `mono file is duplicated to both channels`, `flac and aiff decode`, `mp3 decode is registered` (Format über `registerBasicFormats`; falls MP3 auf der Plattform fehlt, Test überspringen mit `SKIP`), `missing file reports error "file not found"`, `unsupported file reports error`, `file shorter than 1000 frames is rejected`.
- [ ] **Step 2:** Run `ctest --preset windows-release -R LoopLoader`. Expected: FAIL (Klasse fehlt).
- [ ] **Step 3:** Aus `SampleLoader` ableiten, aber ohne Mono-Mittelwert und ohne 60-Sekunden-Grenze. Stattdessen Obergrenze `kMaxLoopFrames = 48000 * 600` (Annahme: zehn Minuten bei 48 kHz). Mehr als zwei Kanäle: nur die ersten zwei.
- [ ] **Step 4:** Run dieselben Tests. Expected: PASS.
- [ ] **Step 5:** Commit `feat(plugin): stereo loop loader`.

### Task 3: Loop-Timing

**Files:**
- Create: `engine/LoopTiming.{h,cpp}`, `tests/test_LoopTiming.cpp`

**Interfaces:**
- Produces:
  - `double dc::beatsPerBar(int tsNum, int tsDen)` = `tsNum * 4.0 / tsDen`; ungültige Werte (≤ 0) ergeben 4.0.
  - `int dc::suggestBars(double seconds, double bpm, double beatsPerBar)`: Anzahl Takte, gerundet, mindestens 1; bei ungültigem Eingang 1.
  - `long long dc::lcmBars(int a, int b)`: gemeinsame Zykluslänge beider Loops in Takten. Die Engine liest phasengekoppelt und vervielfacht daher nichts im Speicher; `lcmBars` dient der Anzeige der Gesamtzyklus-Länge im Editor (Task 10).
  - `double dc::sourceStep(size_t sourceFrames, double loopBeats, double bpm, double outRate)`: Quell-Frames pro Ausgabe-Frame, begrenzt auf `[kMinStep, kMaxStep]` = `[0.25, 4.0]`; bei `bpm <= 0`, NaN, `outRate <= 0` oder `sourceFrames == 0` ergibt sie 0.0.

- [ ] **Step 1:** Tests: `suggestBars(8.0, 120, 4) == 4`, `suggestBars(0.01, 120, 4) == 1`, `suggestBars(8.0, 0, 4) == 1`, `lcmBars(3,4) == 12`, `lcmBars(4,4) == 4`, `beatsPerBar(6,8) == 3.0`, `beatsPerBar(0,4) == 4.0`, `sourceStep(192000, 8.0, 120, 48000) == Approx(1.0)` (8 Beats bei 120 BPM sind 4 s, also 192000 Ausgabe-Frames), `sourceStep(96000, 8.0, 120, 48000) == Approx(0.5)`, Klemmung bei 10-fachem Tempo auf 4.0, NaN ergibt 0.0.
- [ ] **Step 2:** Run `ctest ... -R LoopTiming`. Expected: FAIL.
- [ ] **Step 3:** Implementieren nach den Signaturen.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat(engine): loop timing`.

### Task 4: Phasengekoppeltes Abspielen mit Grain-Stretch

**Files:**
- Create: `engine/SampleInterp.h`, `engine/GrainStretch.{h,cpp}` (Kopie aus dubgefahren, Namespace `dg`→`dc`), `engine/LoopReader.{h,cpp}`, `tests/test_LoopReader.cpp`
- `GrainStretch::process` allokiert nicht (zwei Stücke in `std::array`); das ist damit geklärt.

**Interfaces:**
- Consumes: `StereoData`, `sourceStep` (Task 3).
- Produces: `class dc::LoopReader { void setData(std::shared_ptr<const StereoData>); void reset(); void render(float* l, float* r, int n, double phase01, double phaseStep01, double step); }`. `phase01` ist die Position im Loop (0..1) am ersten Ausgabe-Frame, `phaseStep01` der Zuwachs pro Ausgabe-Frame, `step` das Quell-Verhältnis aus `sourceStep`. Ohne Daten oder bei `step == 0` schreibt `render` Nullen. Die Methode ist echtzeitsicher (keine Allokation).

- [ ] **Step 1:** Tests: `silence without data`, `sine keeps pitch at double tempo` (Sinus 440 Hz, Phase läuft doppelt so schnell, Grundfrequenz der Ausgabe bleibt 440 Hz ±2 %, per Nulldurchgänge gezählt), `position follows phase after a jump` (Phase springt von 0.1 auf 0.6, Ausgabe entspricht danach dem Quellmaterial bei 0.6 innerhalb einer Grain-Länge), `no NaN with step 0 or phase outside 0..1`.
- [ ] **Step 2:** Run `ctest ... -R LoopReader`. Expected: FAIL.
- [ ] **Step 3:** Zwei `GrainStretch`-Instanzen (links/rechts) mit gleichem Schedule; `srcPos = wrap(phase01) * frames`, `GrainFrame.cycle = true`, `lo = 0`, `hi = frames`. Grain-Länge ca. 40 ms als Konstante.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat(engine): phase-locked loop reader`.

### Task 5: Regionen

**Files:**
- Create: `engine/RegionMap.{h,cpp}`, `tests/test_RegionMap.cpp`

**Interfaces:**
- Produces: `class dc::RegionMap` mit `std::vector<double> boundaries` (aufsteigend, Werte in (0,1), Anteile der Loop-Länge). `int regionCount() const` (= Grenzen + 1), `int regionAt(double phase01) const`, `std::pair<double,double> bounds(int region) const`, `bool addBoundary(double x)`, `bool moveBoundary(int index, double x)`, `bool removeBoundary(int index)`. `constexpr double kMinRegion = 0.005`. Alle Änderungen halten den Mindestabstand ein, `add`/`move` klemmen bzw. lehnen ab und geben `false` zurück.

- [ ] **Step 1:** Tests: `no boundaries is one region`, `add keeps sorted order`, `add at 0.0 or 1.0 is rejected`, `add closer than kMinRegion to a neighbour is rejected`, `move is clamped between neighbours`, `regionAt returns correct index at boundaries and for 1.0`, `remove merges regions`.
- [ ] **Step 2:** Run `ctest ... -R RegionMap`. Expected: FAIL.
- [ ] **Step 3:** Implementieren; `regionAt` per binärer Suche, `phase01` wird in [0,1) gewrappt.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat(engine): region map`.

### Task 6: Regeln und Ziehung

**Files:**
- Create: `engine/RuleSet.{h,cpp}`, `tests/test_RuleSet.cpp`

**Interfaces:**
- Produces: `enum class dc::PlayMode { Gate, OneShot }`; `struct dc::RuleTarget { int region2; float weight; PlayMode mode; }` (weight 0..1); `class dc::RuleSet { void setTargets(int region1, std::vector<RuleTarget>); const std::vector<RuleTarget>& targets(int region1) const; std::optional<RuleTarget> draw(int region1, float u) const; }`. `setTargets` skaliert Gewichte mit Summe über 1 proportional auf Summe 1 und klemmt einzelne auf [0,1]. `draw` geht kumulativ durch die Ziele; liegt `u` (0 ≤ u < 1) hinter der Summe, ergibt es `std::nullopt` (Stille). Ohne Regel immer `nullopt`.

- [ ] **Step 1:** Tests: `no rule draws nothing`, `weights 0.5/0.3 draw first for u=0.2, second for u=0.6, nothing for u=0.85`, `weights summing to 1.4 are scaled so the sum is 1`, `negative or NaN weight becomes 0`, `u = 0 picks the first non-zero target`, `statistical: 10000 draws with 0.7/0.2 are within 3 percent`.
- [ ] **Step 2:** Run `ctest ... -R RuleSet`. Expected: FAIL.
- [ ] **Step 3:** Implementieren.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat(engine): rule set and weighted draw`.

### Task 7: Sequencer (Loop-2-Stimme)

**Files:**
- Create: `engine/Sequencer.{h,cpp}`, `tests/test_Sequencer.cpp`

**Interfaces:**
- Consumes: `RegionMap`, `RuleSet`, `RuleTarget`, `PlayMode`.
- Produces: `class dc::Sequencer`.
  - `struct Command { enum class Type { None, Stop, Start } type; int region2; PlayMode mode; }`
  - `void setRandom(std::function<float()> uniform01)` (Standard: `std::mt19937`).
  - `Command onRegion1(int region1, const RuleSet&)` wird bei jedem Eintritt in eine Loop-1-Region aufgerufen und zieht neu. Ergebnis `Start` mit Ziel oder `Stop` (Stille oder keine Regel).
  - `bool shouldGateStop(int region1Now) const`: für Gate-Modus, wenn die auslösende Loop-1-Region verlassen wurde.
  - Der Sequencer hält nur Zustand (aktives Ziel, auslösende Region); Audio rendert `Engine` (Task 8).
  - `constexpr int kCancelFadeMs = 5`.

- [ ] **Step 1:** Tests mit fester Zufallsfolge: `entry with rule starts target`, `entry without rule stops voice`, `silence draw stops voice`, `new entry cancels a running one-shot and starts the new target`, `re-entering the same region draws again (wrap)`, `gate stops when region 1 changes`, `one-shot does not stop when region 1 changes`.
- [ ] **Step 2:** Run `ctest ... -R Sequencer`. Expected: FAIL.
- [ ] **Step 3:** Implementieren.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat(engine): sequencer`.

### Task 8: Engine und Prozessor

**Files:**
- Create: `engine/Engine.{h,cpp}`, `engine/FxSlot.h`, `tests/test_Engine.cpp`
- Modify: `plugin/DubcyclingProcessor.{h,cpp}`, `engine/CMakeLists.txt`

**Interfaces:**
- Consumes: alles aus Tasks 3–7.
- Produces:
  - `struct dc::TransportInfo { bool playing; double ppq; double bpm; int tsNum = 4; int tsDen = 4; }`
  - `struct dc::LoopSlot { std::shared_ptr<const StereoData> data; int bars = 1; RegionMap regions; }`
  - `struct dc::Model { LoopSlot loop1, loop2; RuleSet rules; }`
  - `class dc::Engine { void prepare(double sampleRate, int maxBlock); void setModel(std::shared_ptr<const Model>); void process(const TransportInfo&, float* l, float* r, int n); }`. `setModel` ist thread-sicher (atomarer Austausch von `shared_ptr`).
  - `struct dc::FxSlot { virtual void process(float* l, float* r, int n) {} virtual ~FxSlot() = default; }` als Haken pro Region, ungenutzt in diesem Plan.
- Verhalten: Loop-1-Phase = `fmod(ppq, loop1Beats) / loop1Beats`. Eintritt in eine neue Region wird erkannt, wenn `regionAt` sich zwischen zwei Frames ändert oder die Phase springt (Transportsprung, Wrap). Loop 2 spielt die Zielregion ab deren Anfang mit `sourceStep` aus Loop 2, im Gate-Modus wird die Region wiederholt, bis die Loop-1-Region endet, im One-Shot-Modus einmal. Abbruch und Start überblenden `kCancelFadeMs`. Nicht spielend, `bpm` ungültig oder PPQ negativ ergibt Stille und setzt die Stimme zurück.

- [ ] **Step 1:** Tests: `silent when not playing`, `silent for bpm 0 and NaN and negative ppq`, `loop 1 output matches source at ppq 0 with matching tempo`, `output follows a tempo change without discontinuity above a threshold`, `loop 2 silent without rule`, `loop 2 plays target region when rule weight is 1`, `gate region repeats until region 1 ends`, `one-shot plays once and then is silent`, `transport jump restarts the draw`, `6/8 time signature uses 3 beats per bar`, `process does not allocate` (Allokationszähler über Operator-new-Hook im Test, falls verfügbar; sonst Reviewpunkt).
- [ ] **Step 2:** Run `ctest ... -R Engine`. Expected: FAIL.
- [ ] **Step 3:** `Engine` implementieren. Im Prozessor `AudioPlayHead::getPosition()` auslesen (PPQ, BPM, Taktart, `isPlaying`) und in `TransportInfo` abbilden. Fehlende Felder gelten als ungültig (Stille).
- [ ] **Step 4:** Run `build.ps1 all`. Expected: alle Tests grün, Plugin baut.
- [ ] **Step 5:** Commit `feat: engine and processor integration`.

### Task 9: Zustand und Relink

**Files:**
- Create: `plugin/PluginState.{h,cpp}`, `plugin/FileRelink.{h,cpp}`, `tests/plugin/test_PluginState.cpp`, `tests/plugin/test_FileRelink.cpp`
- Modify: `plugin/DubcyclingProcessor.{h,cpp}` (`getStateInformation`/`setStateInformation`)

**Interfaces:**
- Produces:
  - `struct dc::LoopRef { juce::String path, fileName, hash; int bars; std::vector<double> boundaries; }`
  - `struct dc::PluginState { int version = 1; LoopRef loop1, loop2; std::vector<RuleEntry> rules; }`, `RuleEntry { int region1; std::vector<RuleTarget> targets; }`
  - `juce::MemoryBlock dc::serialize(const PluginState&)` und `std::optional<PluginState> dc::deserialize(const void*, size_t)`; unbekannte Version, leere oder beschädigte Daten ergeben `nullopt`.
  - `juce::String dc::hashFile(const juce::File&)` (SHA-256 der ganzen Datei).
  - `std::optional<juce::File> dc::relink(const LoopRef&, const juce::File& sessionDir)`: sucht erst `path`, dann `sessionDir` nach Dateiname plus Hash, dann den Ordner des gespeicherten Pfads. Findet nichts, ergibt `nullopt`; der Zustand (Regionen, Regeln) bleibt erhalten.

- [ ] **Step 1:** Tests: `roundtrip keeps boundaries rules and bars`, `deserialize of empty block returns nullopt`, `deserialize of garbage returns nullopt`, `deserialize of version 99 returns nullopt`, `rules over 100 percent are scaled after load`, `relink finds moved file by name and hash in session dir`, `relink does not accept a file with the same name but different hash`, `relink returns nullopt when nothing matches`.
- [ ] **Step 2:** Run `ctest ... -R "PluginState|FileRelink"`. Expected: FAIL.
- [ ] **Step 3:** Serialisieren als `juce::ValueTree`→XML-Binärblock. Beim Laden Loops asynchron über `LoopLoader` holen. Fehlt eine Datei, bleibt der Slot leer, aber Regionen und Regeln bleiben im Zustand, und der Editor zeigt „Datei fehlt“.
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat(plugin): session state and relink`.

### Task 10: Editor mit Wellenform-Lanes

**Files:**
- Create: `plugin/ui/Editor.{h,cpp}`, `plugin/ui/LoopLane.{h,cpp}`, `tests/plugin/test_LoopLane.cpp`
- Referenz: `E:\Repos\dubgefahren\plugin\ui\WaveformView.{h,cpp}` (`computePeaks`, Maus-Logik über `pressAt/dragTo/release`)

**Interfaces:**
- Produces: `class dc::ui::LoopLane : juce::Component` mit `setData(std::shared_ptr<const StereoData>)`, `setRegions(const RegionMap&)`, `setSelectedRegion(int)`, Callback `std::function<void(const RegionMap&)> onRegionsChanged`, `std::function<void(int)> onRegionSelected`, `std::function<void(const juce::File&)> onFileDropped`. Bedienung testbar über `pressAt(float x)`, `dragTo(float x)`, `release()`, `doubleClickAt(float x)`: Klick in eine Region wählt sie, Doppelklick setzt eine Grenze, Ziehen verschiebt eine Grenze, Doppelklick auf eine Grenze entfernt sie. `Editor` zeigt Loop 1 oben, Loop 2 darunter, ein Eingabefeld für die Taktzahl je Loop (`suggestBars` als Vorbelegung) und „Datei fehlt“-Anzeige mit Neu-Zuordnen.

- [ ] **Step 1:** Tests: `double click adds a boundary at the clicked x`, `drag moves the boundary and is clamped by kMinRegion`, `double click on a boundary removes it`, `click selects the region under x and fires onRegionSelected`, `empty data shows hint text and ignores clicks`.
- [ ] **Step 2:** Run `ctest ... -R LoopLane`. Expected: FAIL.
- [ ] **Step 3:** Implementieren; Peaks aus Mittelwert beider Kanäle. Änderungen laufen über Callback in ein neues `Model`, das per `Engine::setModel` übergeben wird.
- [ ] **Step 4:** Run `build.ps1 all` und das Plugin in einem Host laden. Expected: Tests grün, beide Lanes zeigen Wellenform, Regionen lassen sich setzen.
- [ ] **Step 5:** Commit `feat(ui): editor with loop lanes`.

### Task 11: Regel-Panel

**Files:**
- Create: `plugin/ui/RulePanel.{h,cpp}`, `tests/plugin/test_RulePanel.cpp`
- Modify: `plugin/ui/Editor.{h,cpp}`

**Interfaces:**
- Produces: `class dc::ui::RulePanel : juce::Component` mit `setContext(int region1, int region2Count, const std::vector<RuleTarget>&)` und Callback `std::function<void(int region1, std::vector<RuleTarget>)> onChanged`. Pro Loop-2-Region eine Zeile: Regler (0–100 %), Prozentwert und Gate/One-Shot-Schalter. Testbar über `setWeight(int row, float percent)` und `setMode(int row, PlayMode)`. Eine Änderung, die die Summe über 100 % hebt, wird auf den Rest begrenzt, die anderen Zeilen bleiben unverändert. Anzeige „Stille: x %“ für den Rest.

- [ ] **Step 1:** Tests: `rows match region 2 count`, `setWeight caps at the remaining percent`, `silence label shows the remainder`, `setMode only changes that row`, `onChanged fires with updated targets`.
- [ ] **Step 2:** Run `ctest ... -R RulePanel`. Expected: FAIL.
- [ ] **Step 3:** Implementieren, an `onRegionSelected` der oberen Lane anbinden.
- [ ] **Step 4:** Run `build.ps1 all`. Expected: grün; im Host löst eine Regel hörbar Loop 2 aus.
- [ ] **Step 5:** Commit `feat(ui): rule panel`.

### Task 12: Regionsvorschläge für Drum-Loops

**Files:**
- Create: `engine/TransientDetector.{h,cpp}`, `tests/test_TransientDetector.cpp`
- Modify: `plugin/ui/Editor.{h,cpp}` (Schaltfläche „Vorschlag“ je Lane)

**Interfaces:**
- Produces: `std::vector<double> dc::suggestBoundaries(const StereoData&, int maxBoundaries)`: aufsteigende Anteile in (0,1), je Region mindestens `kMinRegion`, höchstens `maxBoundaries`. Verfahren (Annahme): Energie-Hüllkurve, Onset an positiven Sprüngen, die stärksten `maxBoundaries` Onsets behalten.

- [ ] **Step 1:** Tests: `synthetic click track with 4 clicks yields 3 boundaries near the clicks`, `silence yields no boundaries`, `result respects maxBoundaries and kMinRegion`, `stereo with right channel only is detected`.
- [ ] **Step 2:** Run `ctest ... -R TransientDetector`. Expected: FAIL.
- [ ] **Step 3:** Implementieren; Schaltfläche übernimmt das Ergebnis als `RegionMap` (ersetzt vorhandene Grenzen nach Rückfrage).
- [ ] **Step 4:** Run. Expected: PASS.
- [ ] **Step 5:** Commit `feat: transient based region suggestions`.

### Task 13: macOS-Build

**Files:**
- Modify: `CMakePresets.json`, `CMakeLists.txt`; Create: `.github/workflows/build.yml`
- Referenz: `E:\Repos\dubgefahren\CMakePresets.json` (Preset `macos-universal`) und dessen Workflow.

- [ ] **Step 1:** Presets `macos-universal` und `macos-release` ergänzen (arm64 + x86_64, Deployment-Target 11.0).
- [ ] **Step 2:** Workflow baut und testet auf `macos-latest` und lädt das VST3 als Artefakt hoch.
- [ ] **Step 3:** Plattformfremden Code in `engine/` und `plugin/` suchen und entfernen (`grep` auf `windows.h`, `\\`-Pfade, `_WIN32`). Expected: keine Treffer.
- [ ] **Step 4:** Push auf Branch, Workflow abwarten. Expected: Build und Tests grün (auf echter Hardware ungetestet, wie in dubgefahren).
- [ ] **Step 5:** Commit `build: macOS preset and CI`.

### Task 14: Linux-Build

**Files:**
- Modify: `CMakePresets.json`, `.github/workflows/build.yml`

- [ ] **Step 1:** Preset `linux-x64` und `linux-release` ergänzen; JUCE-Linux-Abhängigkeiten (ALSA, X11, FreeType u. a.) im Workflow per `apt` installieren.
- [ ] **Step 2:** Workflow baut und testet auf `ubuntu-latest`, Tests laufen headless.
- [ ] **Step 3:** Push, Workflow abwarten. Expected: Build und Tests grün, VST3-Artefakt vorhanden.
- [ ] **Step 4:** README um Build-Anleitung für alle drei Plattformen ergänzen.
- [ ] **Step 5:** Commit `build: Linux preset and CI`.
