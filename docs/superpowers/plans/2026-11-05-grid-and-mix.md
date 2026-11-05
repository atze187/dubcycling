# Raster und Loop-Mix pro Regel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** (A) Regionsgrenzen in beiden Spuren rasten auf Viertelnoten ein, das Raster ist sichtbar; (B) pro Regel (Loop-1-Region × Loop-2-Region) gibt es einen Mix-Regler, der Loop 1 gegen die Loop-2-Region überblendet.

**Architecture:** Mix: `RuleTarget::mix` (0..1, Standard 0,5) wandert durch Sequencer, Engine (Pegel der Loop-2-Voice und geglättete Loop-1-Dämpfung), Plugin-Zustand (XML-Attribut), Bank-Format (Version 3) und die Regelzeile des `RulePanel`. Raster: reine Rasterlogik und Zeichnen in `LoopLane`, Takteinteilung vom Editor (Takte-Feld und Host-Taktart).

**Tech Stack:** C++20, JUCE 8.0.15, Catch2 v3.16.0, CMake.

**Spec:** `docs/superpowers/specs/2026-11-05-grid-and-mix-design.md` (verbindlich; "Entscheidungen des Nutzers" und "Entscheidungen von Claude" gelten beide).

## Global Constraints

- Arbeitsbranch `feat/effects` (weiterarbeiten, nicht auf `main`; der Branch ist noch nicht gepusht).
- Commits: `git -c user.name=atze187 -c user.email=14954322+atze187@users.noreply.github.com commit ...`, Nachricht endet mit der Zeile `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Tests: `powershell -NoProfile -ExecutionPolicy Bypass -File ./build.ps1 test` baut und führt alle Tests aus; alle bisherigen 412 Tests bleiben grün. Engine-Tests in `tests/` (`dc_tests`), Plugin-Tests in `tests/plugin/` (`dc_plugin_tests`), Eintrag in `tests/CMakeLists.txt` bei neuen Dateien.
- Audio-Thread: keine Speicheranforderung, keine Sperren, keine Ausnahmen; Ausgabe immer endlich.
- Mix-Formel (verbindlich): `gain2 = min(1, 2 × mix)`, `gain1 = min(1, 2 × (1 − mix))`; Standard `mix = 0.5` (beide voll). `NaN`, Unendlich und Text im Zustand ergeben 0,5; Werte außerhalb 0..1 werden begrenzt.
- Vorhandene Projekte, Presets und Banks klingen unverändert. `kStateVersion` bleibt 1 (`mix` ist ein optionales Attribut, nur geschrieben wenn ungleich 0,5). `kBankVersion` wird 3; Lesen von 1, 2 und 3, Schreiben immer 3, 4 und höher ist "zu neu".
- Raster: Viertelnoten; Schritt in Phase `1 / (bars × beatsPerBar)`; Einrasten immer an, Umschalt-Taste = frei; Schläge unter etwa 4 px Abstand werden nicht gezeichnet (dann nur Takte), Takte unter etwa 4 px ebenfalls nicht, und dann rastet auch nichts ein; ohne Daten kein Raster.
- Texte der Oberfläche auf Deutsch. Steinberg-Validator und pluginval bleiben grün (Task 8).

## Review Focus

1. Mix 0 und 1 an den Rändern; NaN und Unendlich im Projekt oder in einer importierten Bank (Task 1, 3, 4).
2. Regionswechsel mitten im Block mit verschiedenen Mix-Werten: kein Klick, kein Pegelsprung; Voice endet: Loop 1 kehrt per Rampe auf voll zurück (Task 2).
3. Alte Projekte und Banks (Version 1, 2) laden und klingen wie vorher (Task 3, 4).
4. Taktart 3/4, 6/8 und 7/8, Spuren mit 4 und 8 Takten, sehr schmale Spuren: Raster ausblenden statt Pixelbrei (Task 6, 7).
5. Mix-Änderung im Editor während der Wiedergabe: sofort hörbar ohne Klick; Grenzbearbeitung vor der klingenden Region ändert den klingenden Mix nicht (Task 2, 5).

## File Structure

- Create `engine/MixGain.h`: `MixGains`, `mixGains`, `sanitizeMix`.
- Modify `engine/RuleSet.{h,cpp}`, `engine/Sequencer.{h,cpp}`, `engine/Engine.{h,cpp}`.
- Modify `plugin/PluginState.cpp`, `plugin/presets/BankFormat.{h,cpp}`.
- Modify `plugin/ui/RulePanel.{h,cpp}`, `plugin/ui/LoopLane.{h,cpp}`, `plugin/ui/Editor.{h,cpp}`, `plugin/DubcyclingProcessor.{h,cpp}`.
- Tests: `tests/test_RuleSet.cpp`, `test_Sequencer.cpp`, `test_Engine.cpp`, `tests/plugin/test_PluginState.cpp`, `test_BankFormat.cpp`, `test_RulePanel.cpp`, `test_LoopLane.cpp`, `test_Editor.cpp`, `test_Processor.cpp` (alle erweitern).
- Doku: `README.md`, `docs/superpowers/backlog.md`.

---

### Task 1: Mix im Regelwerk und im Sequencer

**Files:**
- Create: `engine/MixGain.h`
- Modify: `engine/RuleSet.h`, `engine/RuleSet.cpp`, `engine/Sequencer.h`, `engine/Sequencer.cpp`
- Test: `tests/test_RuleSet.cpp`, `tests/test_Sequencer.cpp`

**Interfaces:**
- Produces (Namespace `dc`):
  - `engine/MixGain.h`: `struct MixGains { float loop1 = 1.0f; float loop2 = 1.0f; };` `float sanitizeMix(float mix);` (NaN und Unendlich ergeben 0,5, sonst auf 0..1 begrenzt) `MixGains mixGains(float mix);` (Formel aus den Global Constraints, `mix` wird vorher bereinigt). Header-only (`inline`).
  - `RuleTarget::mix` (`float`, Standard `0.5f`, als letztes Feld, damit bestehende Initialisierungen `{region2, weight, mode}` weiter funktionieren). `RuleSet::setTargets` bereinigt `mix` über `sanitizeMix`.
  - `Sequencer::Draw::mix` und `Sequencer::Command::mix` (`float`, Standard 0,5), gefüllt aus dem gezogenen Ziel (`Command` bei `Start`). Zusätzlich `Sequencer::Command::region1` (`int`, Standard -1): die Loop-1-Region, die den Start ausgelöst hat (für die Neubewertung bei Modelltausch in Task 2).

- [ ] **Step 1: Tests schreiben.** `MixGain: the centre keeps both loops at full level` (`mixGains(0.5f)` = 1/1). `MixGain: the ends solo one loop` (0: loop1 1, loop2 0; 1: loop1 0, loop2 1). `MixGain: left of centre only loop 2 gets quieter, right of centre only loop 1` (0.25 → 1 und 0.5; 0.75 → 0.5 und 1). `MixGain: NaN, infinity and out-of-range values are sanitized` (NaN und ±inf → 0.5; -3 → 0; 7 → 1). `RuleSet: mix defaults to 0.5 and is sanitized in setTargets` (NaN → 0.5, 2 → 1). `Sequencer: the start command carries the mix of the drawn target and the triggering region` (Regel mit `mix = 0.8`, `onRegion1(3, rules)` liefert `mix == 0.8f`, `region1 == 3`). `Sequencer: a draw carries the mix` (`draw()` und `apply()`).
- [ ] **Step 2: Tests laufen lassen**, sie scheitern (Symbole fehlen).
- [ ] **Step 3: Implementieren** wie unter Interfaces; `engine/CMakeLists.txt` braucht nur den Header in der Liste.
- [ ] **Step 4: Alle Tests laufen lassen**, grün.
- [ ] **Step 5: Commit** `feat(engine): per-target loop mix in rules and sequencer`.

### Task 2: Engine: Mix wirkt auf Loop 2 und dämpft Loop 1

**Files:**
- Modify: `engine/Engine.h`, `engine/Engine.cpp`
- Test: `tests/test_Engine.cpp`

**Interfaces:**
- Consumes: `MixGains`, `mixGains`, `Sequencer::Command::mix/region1`.
- Produces: `Voice` bekommt Mix-Zustand (Wert, geglätteter Pegel, Ursprung: Loop-1-Region und deren Grenzen beim Start); sonst keine öffentliche Schnittstelle.
- Verhalten (Spec "Entscheidungen von Claude / Mix" und "Engine"):
  - Start einer Loop-2-Voice (`startVoice`): `mix` aus dem Command; Loop-2-Pegel der Voice = `mixGains(mix).loop2` (geglättet über etwa 5 bis 10 ms pro Sample; beim Start gleich auf Ziel, die Einblendung der Voice deckt den Anfang). Der auslaufende Voice `tail_` behält seinen Mix-Pegel.
  - Loop 1: eine geglättete Verstärkung `g1` multipliziert `l[i]`, `r[i]` (nur Loop 1, vor der Addition des Busses und vor dem Loop-1-Vorhören). Ziel von `g1`: `mixGains(voice_.mix).loop1`, solange `voice_` im Zustand `Playing` ist; sonst 1 (Stille, Voice endet oder fadet aus, kein Voice, ungültiger Transport). Rampe etwa 5 bis 10 ms (pro Sample).
  - Der Mix gilt für den Voice, der ihn gestartet hat; ein Gate-Voice, der in eine andere Loop-1-Region hineinläuft, behält den Wert.
  - Vorhören (Loop 2 und Loop 1) ignoriert den Mix: Pegel 1, keine Loop-1-Dämpfung durch das Vorhören.
  - Nahtloser Modelltausch (`adoptSwap`, nur wenn die Voice `Playing` ist): der Mix der laufenden Voice wird aus dem neuen Modell neu gelesen, wenn (a) die Grenzen ihrer Loop-2-Region und (b) die Grenzen der Loop-1-Region, die sie gestartet hat, im neuen Modell unverändert sind (Vergleich der 0..1-Grenzen mit sehr kleiner Toleranz) und `rules.targets(region1)` ein Ziel mit derselben `region2` enthält; dann gilt dessen `mix`. Sonst bleibt der Wert. (So hört man einen Mix-Wechsel im Editor sofort, und eine Grenzbearbeitung vor der klingenden Region ändert den Mix nicht.)
  - `resetPlayback` (Transportstopp, Sprung, harter Neustart) setzt den Mix-Zustand zurück (`g1` ruft die Rampe von dem aktuellen Wert auf 1, kein Sprung; ein Hart-Neustart darf `g1` auf 1 setzen).
  - Kein Mix-Pfad belegt Speicher oder sperrt.

- [ ] **Step 1: Tests schreiben** in `tests/test_Engine.cpp` (vorhandene Helfer für Models und Daten nutzen): `Engine mix: the default mix keeps both loops at full level` (Modell wie bisher, Summe bis 1e-5 gleich wie ein Lauf mit `mix = 0.5` und wie die Erwartung aus Loop 1 + Loop 2 einzeln gerendert). `Engine mix: mix 1 plays only loop 2 while the region sounds` (Loop 1 Sinus, Loop 2 anderer Sinus; während der Voice kein Loop-1-Anteil, nach dem Ende Loop 1 wieder voll). `Engine mix: mix 0 silences the loop 2 region and keeps loop 1`. `Engine mix: mix 0.25 halves loop 2 and keeps loop 1` und `0.75` halbiert Loop 1. `Engine mix: loop 1 returns to full level after silence is drawn` (Rampe, kein Sprung: größter Sprung zwischen Nachbarwerten bei Amplitude 0.5 unter 0.05). `Engine mix: switching between targets with different mix does not click` (zwei Regeln, Wechsel mitten im Block; größter Sprung unter 0.1). `Engine mix: the audition ignores the mix` (`requestPreview(1, k)` bei Ziel mit mix 0: Vorschau voll hörbar, Loop 1 nicht gedämpft). `Engine mix: an edit of the sounding target's mix is heard at once without a click` (seamless swap, gleiche Daten und Bars, neues Modell mit anderem Mix). `Engine mix: a boundary edit before the sounding region keeps its mix` (Loop-2-Region verschiebt sich im Index, Grenzen gleich: Mix unverändert; die Neulesung darf nicht den Mix einer anderen Region nehmen). `Engine mix: mix values at the ends and NaN stay finite` (NaN im Modell wird beim Bereinigen 0.5).
- [ ] **Step 2: Tests laufen lassen**, sie scheitern.
- [ ] **Step 3: Implementieren.** Die Glättung als kleine private Hilfsstruktur in `Engine.h` (kein neues Modul nötig). Bei Abweichungen von bestehenden Tests, die exakte Summen prüfen: die Mix-Mitte muss bit-gleich zum bisherigen Ergebnis bleiben (Pegel 1.0 exakt, kein Rundungsrauschen aus der Rampe); wenn das nicht ohne Sonderfall geht, Toleranz 1e-6 und im Bericht nennen.
- [ ] **Step 4: Alle Tests laufen lassen**, grün.
- [ ] **Step 5: Commit** `feat(engine): mix between loop 1 and the playing loop 2 region`.

### Task 3: Zustand: Mix im Plugin-Zustand

**Files:**
- Modify: `plugin/PluginState.cpp` (und `.h`, falls ein Helfer exportiert wird)
- Test: `tests/plugin/test_PluginState.cpp`, `tests/plugin/test_Processor.cpp`

**Interfaces:**
- Consumes: `RuleTarget::mix`, `sanitizeMix`.
- Produces: `serialize` schreibt am `Target`-Element das Attribut `mix` (17 Stellen, über `num(...)`) nur wenn `mix != 0.5f`; `deserialize` liest es locale-unabhängig wie die Effekt-Zahlen (die vorhandene `parseFxNumber`-Logik darf wiederverwendet oder in einen gemeinsamen Helfer gezogen werden): Text, NaN, Unendlich, fehlendes Attribut → 0,5, sonst begrenzt auf 0..1. `addRuleTarget`/`sanitizeRuleEntry` lassen `mix` unverändert durch (die Bereinigung läuft in `RuleSet::setTargets`; zusätzlich wird in der Bereinigung des Zustands `mix` über `sanitizeMix` gesetzt, damit auch `PluginState` ohne Engine sauber ist).

- [ ] **Step 1: Tests schreiben:** `State: the mix of a target survives a round trip` (0.2, 0.8 und 0 / 1 an den Rändern). `State: a target without mix loads with 0.5` (altes XML). `State: the default mix writes no attribute` (serialisierte Zeichenkette enthält kein `mix=` bei Standard). `State: mix numbers are sanitized` (`"nan"`, `"abc"`, `"1e99"`, `"-5"`, `"inf"` → 0.5, 0.5, 1, 0, 0.5). `Processor: the mix is published in the model` (`applyEdit` mit Regel und `mix = 0.3`, `getModel()->rules.targets(r)[0].mix == 0.3f`).
- [ ] **Step 2: Tests laufen lassen**, sie scheitern.
- [ ] **Step 3: Implementieren.**
- [ ] **Step 4: Alle Tests laufen lassen**, grün.
- [ ] **Step 5: Commit** `feat(state): persist the per-target mix`.

### Task 4: Bank-Format Version 3

**Files:**
- Modify: `plugin/presets/BankFormat.h`, `plugin/presets/BankFormat.cpp`
- Test: `tests/plugin/test_BankFormat.cpp` (bestehende Tests mit fester Version 2 anpassen), `tests/plugin/test_BankStore.cpp` falls nötig

**Interfaces:**
- Consumes: `RuleTarget::mix`, `sanitizeMix`.
- Produces: `constexpr int kBankVersion = 3;`. `rulesToVar` schreibt je Ziel `"mix"` (Zahl) nur wenn `mix != 0.5f`; `rulesFromVar` liest es (`isNumber`, vor jeder Umwandlung bereinigt: Text, `null`, NaN, riesige Zahlen → 0,5 beziehungsweise begrenzt auf 0..1). `bankFromJson` liest Version 1, 2 und 3 (`version < 1` oder fehlende Presets: verworfen, `version > 3` → `tooNew`). `presetFingerprint`: der Mix eines Ziels geht in die Zeile des Ziels ein, aber nur wenn ungleich 0,5, sodass ein Standard-Mix denselben Fingerabdruck ergibt wie ein Zustand ohne Mix-Daten.

- [ ] **Step 1: Tests schreiben:** `Bank: version 3 round trips the mix of a target`. `Bank: version 1 and 2 files still load, with mix 0.5`. `Bank: a version 4 file is refused as too new`. `Bank: bankToJson writes version 3`. `Bank: invalid mix numbers are sanitized` (Text, `null`, `1e99`, `-1e99`). `Bank: a changed mix changes the fingerprint, a default mix does not`. `BankStore: saving a loaded version 2 bank writes version 3` (falls vorhandene Store-Tests das tragen; sonst über `bankToJson`).
- [ ] **Step 2: Tests laufen lassen**, sie scheitern.
- [ ] **Step 3: Implementieren.** Vorhandene Tests mit hart codierter Version 2 anpassen.
- [ ] **Step 4: Alle Tests laufen lassen**, grün.
- [ ] **Step 5: Commit** `feat(presets): bank format version 3 with the per-target mix`.

### Task 5: Mix-Regler in der Regelzeile

**Files:**
- Modify: `plugin/ui/RulePanel.h`, `plugin/ui/RulePanel.cpp`, `plugin/ui/Editor.cpp` (nur falls nötig), `plugin/ui/PanelLook.{h,cpp}` (nur falls ein Slider-Stil nötig ist)
- Test: `tests/plugin/test_RulePanel.cpp`, `tests/plugin/test_Editor.cpp`

**Interfaces:**
- Consumes: `RuleTarget::mix`, `mixGains` (für die Anzeige).
- Produces: `RulePanel` bekommt pro Zeile einen Mix-Regler (0..1, Standard 0,5, Doppelklick setzt die Mitte), sichtbar auch für Zeilen mit Gewicht 0. API analog zu `setWeight`/`setMode`: `void setMix(int row, float mix);` (veröffentlicht einmal über `onChanged`), `float mix(int row) const;`, `juce::String mixText(int row) const;`, Drag-Nähte `beginMixDrag(int row)`, `dragMix(int row, float mix)`, `endMixDrag()` (ein Drag veröffentlicht einmal am Ende wie beim Gewicht). `buildTargets()` und der Vergleich in `setContext` (Zeile ~76, vergleicht `region2`, `weight`, `mode`) berücksichtigen `mix`; `mix` bleibt beim Ändern von Gewicht und Modus erhalten; ein berührter Mix speichert das Ziel (auch mit Gewicht 0) wie ein berührtes Gewicht; unberührte Zeilen werden weiter nicht gespeichert. Anzeige `mixText`: Mitte "L1 | L2" (oder gleichwertig), links davon der Loop-2-Anteil in Prozent ("L2 40 %"), rechts davon der Loop-1-Anteil ("L1 40 %"), ganz links "nur L1", ganz rechts "nur L2" (Schreibweise darf der Bauende anpassen, solange die Mitte, die Ränder und der Anteil lesbar sind). Layout: der Regler passt in die Zeile neben Gewicht und GATE/1-SHOT; Mindestbreite des `RulePanel` im 900×600-Editor bleibt ohne Überlappung (Overlap-Test wie beim `FxPanel`); eine Zeile ist weiterhin etwa 20 px hoch.

- [ ] **Step 1: Tests schreiben:** `RulePanel: every row shows a mix control that defaults to the centre`. `RulePanel: setMix publishes the whole target list once and keeps weight and mode`. `RulePanel: changing weight or mode keeps the mix`. `RulePanel: a touched mix is stored for a row with weight 0`. `RulePanel: an untouched row stores no target`. `RulePanel: a mix drag publishes once at its end`. `RulePanel: reset restores the centre and publishes`. `RulePanel: mixText reads centre, ends and sides` (0.5, 0, 1, 0.25, 0.75). `RulePanel: out-of-range mix values are clamped by setMix`. `RulePanel: setContext does not overwrite a mix drag in progress`. `RulePanel: controls do not overlap at the editor's panel width`. Editor: `Editor: a mix change reaches the processor state and the published model` (über `rulePanel()`), `Editor: boundary edits of Loop 2 keep the mix of the targets` (Regel mit `mix`, Loop-2-Grenze einfügen: Ziel mit demselben Mix auf der verschobenen Region).
- [ ] **Step 2: Tests laufen lassen**, sie scheitern.
- [ ] **Step 3: Implementieren.** Schnappschuss des Editors als PNG zur Sichtkontrolle (headless `createComponentSnapshot` wie in früheren Tasks möglich; nach `C:\Users\atzet\AppData\Local\Temp\claude\7762a26053d134b1\40b8fe3f-39b0-4bc2-9f46-f5b9485ab946\scratchpad`, nicht committen); abgeschnittene Texte beheben.
- [ ] **Step 4: Alle Tests laufen lassen**, grün.
- [ ] **Step 5: Commit** `feat(ui): mix control per rule row`.

### Task 6: Raster in der Spur (Logik, Einrasten, Zeichnen)

**Files:**
- Modify: `plugin/ui/LoopLane.h`, `plugin/ui/LoopLane.cpp`
- Test: `tests/plugin/test_LoopLane.cpp`

**Interfaces:**
- Consumes: `RegionMap`.
- Produces (Namespace `dc::ui`):
  - `void LoopLane::setGrid(int bars, int beatsPerBar);` (beide `<= 0` oder ohne Daten: kein Raster) `double LoopLane::gridStep() const;` (Phase pro Schlag = `1 / (bars × beatsPerBar)`, 0 wenn kein Raster oder das Raster zu fein ist) `double LoopLane::snapPhase(double phase) const;` (nächster Schlag, unverändert wenn `gridStep() == 0`; Ergebnis bleibt in 0..1).
  - `std::vector<double> snapBoundariesToGrid(const std::vector<double>& boundaries, double step);` (freie Funktion, rundet jede Grenze auf `round(b / step) × step`, verwirft Werte außerhalb (0,1) und läuft anschließend durch `sanitizeBoundaries` (`PluginState.h`) für Mindestabstand/Sortierung/Duplikate; bei `step <= 0` unverändert).
  - Einrasten: `pressAt` bleibt unverändert (greift Grenzen, wählt Regionen); `dragTo(float x)` rastet die Zielposition (`x + grabOffset_`) auf den Raster ein; `void dragTo(float x, bool free);` mit `free = true` kein Einrasten (die Ein-Argument-Variante entspricht `free = false`); `doubleClickAt(float x)` fügt die Grenze bei `snapPhase(phaseAt(x))` ein (Entfernen einer getroffenen Grenze unverändert); `void doubleClickAt(float x, bool free)` für den Fall ohne Einrasten (die Variante `doubleClickAt(float x, float y)` für den Namens-Editor bleibt). `mouseDrag` übergibt `e.mods.isShiftDown()`; `mouseDoubleClick` ebenso.
  - Zeichnen: im `paint` hinter Wellenform und Regionsgrenzen leichte senkrechte Linien pro Schlag und kräftigere pro Taktanfang (Farben aus dem Panel-Look, dezent; die Regionsgrenzen bleiben am deutlichsten); Schläge unter etwa 4 px Abstand weglassen (nur Takte); Takte unter etwa 4 px ebenfalls weglassen; ohne Daten kein Raster.

- [ ] **Step 1: Tests schreiben:** `LoopLane grid: gridStep follows bars and beats per bar` (8 Takte, 4 Schläge → 1/32; 4 Takte, 3 Schläge → 1/12; 7/8 mit Zähler 7). `LoopLane grid: no grid without data or with invalid bars` (Schritt 0). `LoopLane grid: snapPhase rounds to the nearest beat and stays within 0..1`. `LoopLane grid: a dragged boundary snaps to beats` (Breite 800 px, 8 Takte à 4 Schläge: Ziehen auf x = 103 landet bei Schlag 4/32 = 100 px; Ergebnis nach `release()`). `LoopLane grid: a free drag does not snap` (`dragTo(x, true)`). `LoopLane grid: a double click adds a boundary on the beat, a free double click on the exact position`. `LoopLane grid: snapping respects the minimum region width` (nächster Schlag zu nahe an einer Nachbargrenze: das übliche Klemmen von `RegionMap`, kein Absturz, kein doppeltes Entfernen). `LoopLane grid: an existing off-grid boundary is not moved by selecting a region or by pressing`. `LoopLane grid: beats closer than 4 px are not drawn, then neither bars` (über eine kleine Abfrage `bool beatLinesShown() const; bool barLinesShown() const;`) und `a too fine grid does not snap` (`gridStep()` = 0). `snapBoundariesToGrid: rounds, drops duplicates and values at the ends, keeps order` (Schritt 0.25: `{0.1, 0.12, 0.49, 0.5, 1.0}` ergibt `{0.5}`, da 0.1 und 0.12 auf 0 und 1.0 auf 1 fallen und damit entfallen, und 0.49 und 0.5 beide auf 0.5; zweiter Fall Schritt 0.125: `{0.3, 0.7, 0.2}` rundet auf `{0.25, 0.75, 0.25}` und ergibt nach dem Bereinigen `{0.25, 0.75}`).
- [ ] **Step 2: Tests laufen lassen**, sie scheitern.
- [ ] **Step 3: Implementieren.**
- [ ] **Step 4: Alle Tests laufen lassen**, grün.
- [ ] **Step 5: Commit** `feat(ui): beat grid with snapping in the loop lanes`.

### Task 7: Raster im Editor (Takte, Taktart, Vorschlag)

**Files:**
- Modify: `plugin/DubcyclingProcessor.h`, `plugin/DubcyclingProcessor.cpp`, `plugin/ui/Editor.h`, `plugin/ui/Editor.cpp`
- Test: `tests/plugin/test_Processor.cpp`, `tests/plugin/test_Editor.cpp`

**Interfaces:**
- Consumes: `LoopLane::setGrid`, `snapBoundariesToGrid`, `TransportInfo::tsNum`.
- Produces: `int DubcyclingProcessor::hostBeatsPerBar() const;` (Zähler der zuletzt vom Host gemeldeten Taktart, 4 vor der ersten; `std::atomic<int>`, in `processBlock` gesetzt wie `hostBpm_`, nur für Zähler `1..64`). Der Editor setzt in `refresh()` und bei Änderung der Takte für jede Spur `setGrid(bars der Spur, hostBeatsPerBar())`. `Editor::suggestRegions` rundet die vorgeschlagenen Grenzen vor dem Schreiben mit `snapBoundariesToGrid(found, lane.gridStep())` (ohne Raster unverändert).

- [ ] **Step 1: Tests schreiben:** `Processor: hostBeatsPerBar defaults to 4 and follows the host time signature` (über die vorhandenen Helfer für `processBlock` mit Host-Position; sonst über `toTransportInfo`-Pfad testbar machen). `Editor: both lanes get the grid of their own bars and the host beats per bar` (Loop 1 mit 4 Takten, Loop 2 mit 8: `lane(0).gridStep()` = 1/16, `lane(1).gridStep()` = 1/32). `Editor: changing the bars field updates the grid`. `Editor: the suggestion snaps its boundaries to the grid` (Daten mit Transienten zwischen Schlägen: alle Grenzen im Zustand liegen auf Vielfachen von `gridStep()`; Zahl nicht größer als vorher). `Editor: without a loaded file the lane has no grid`.
- [ ] **Step 2: Tests laufen lassen**, sie scheitern.
- [ ] **Step 3: Implementieren.** Schnappschuss des Editors mit Raster zur Sichtkontrolle (wie in Task 5); Raster und Grenzen müssen in beiden Spuren lesbar sein, Takte kräftiger als Schläge.
- [ ] **Step 4: Alle Tests laufen lassen**, grün.
- [ ] **Step 5: Commit** `feat(ui): wire the grid to bars, host time signature and the suggestion`.

### Task 8: Dokumentation und Gesamtprüfung

**Files:**
- Modify: `README.md`, `docs/superpowers/backlog.md`

**Interfaces:** keine.

- [ ] **Step 1: README** um die zwei Funktionen ergänzen (Raster: Viertelnoten, Einrasten immer, Umschalt-Taste frei, Linien für Schläge und Takte, AUTO rastet; Mix: Regler pro Regelzeile, Mitte = beide voll, links Loop 2 leiser, rechts Loop 1 leiser, nur wirksam solange die Loop-2-Region spielt, Vorhören ignoriert den Mix; Bank-Format Version 3: ältere Plugin-Versionen lehnen neue Banks ab; Projekte mit Mix, die eine ältere Version speichert, verlieren die Mix-Werte). Plugin-Versionsnummer nicht ändern. Backlog: neue Einträge 7 (Raster) und 8 (Mix pro Regel) als ERLEDIGT (2026-11-05) im Stil der vorhandenen, offene Ideen nennen (Rasterweite einstellbar, Triolen, Host-Automation des Mix, Mix für das Vorhören).
- [ ] **Step 2: Gesamttest** `powershell -NoProfile -ExecutionPolicy Bypass -File ./build.ps1 all`, grün (Anzahl nennen).
- [ ] **Step 3: Validierung** `powershell -NoProfile -ExecutionPolicy Bypass -File ./build.ps1 validate`: Steinberg-Validator und pluginval (Stufe 5) ohne Fehler; Ergebnis wörtlich berichten, bei Fehlschlag nicht schönreden.
- [ ] **Step 4: Commit** `docs: beat grid and loop mix in the README and backlog`.
