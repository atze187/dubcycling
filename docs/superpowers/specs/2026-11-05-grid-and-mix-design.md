# Dubcycling – Raster und Loop-Mix pro Regel (Design)

Stand: 2026-11-05. Entstanden im Gespräch mit dem Nutzer nach dem ersten Test der Effekte in Ableton. Was der Nutzer
entschieden hat, steht unter "Entscheidungen des Nutzers"; was Claude ergänzt hat, steht unter "Entscheidungen von
Claude" und ist als Vorschlag zu lesen. Das Dokument baut auf dem Stand von `feat/effects` auf (Effekte, Bank-Format
Version 2).

Zwei unabhängige Änderungen in einem Dokument: A) Raster für die Regionsgrenzen, B) Mix-Regler pro Regel.

## Worum es geht und warum

**A) Raster.** Bei komplexeren Loops (zum Beispiel 8 Takte) ist es mühsam, Regionsgrenzen von Hand genau auf Schläge zu
setzen. Die Waveform-Spuren bekommen ein sichtbares Raster, und die Grenzen rasten darauf ein.

**B) Mix pro Regel.** Der Nutzer spielt oben einen 8-Takte-Drumloop (Loop 1) und unten einen 8-Takte-Loop mit
Variationen (Drums, Bässe, Pads) in ganzen Takten (Loop 2). Dieselbe Loop-2-Region muss je nach laufender Loop-1-Region
lauter oder leiser sein, damit sie nicht untergeht oder übertönt. Heute gibt es pro Regel (Loop-1-Region auf Loop-2-
Region) nur das Gewicht (Wahrscheinlichkeit) und GATE/1-SHOT. Neu kommt ein Mix-Regler dazu, der Loop 1 gegen Loop 2
überblendet. Damit lässt sich auch erreichen, dass nur Loop 2 zu hören ist.

## Begriffe

- **Regel / Ziel:** ein Eintrag in der Regelliste: Loop-1-Region (`region1`) zeigt auf eine Loop-2-Region (`region2`)
  mit Gewicht und Modus (`RuleTarget`).
- **Mix:** ein Wert pro Ziel, der bestimmt, wie Loop 1 und die gespielte Loop-2-Region zueinander stehen.
- **Beat:** eine Viertelnote (ein Schlag). **Takt:** `Taktart-Zähler` Schläge (4/4: 4 Schläge).

## Ziele

1. Regionsgrenzen in beiden Spuren rasten beim Setzen, Ziehen und Hinzufügen auf Viertelnoten ein; das Raster ist in
   beiden Spuren sichtbar (Schläge leicht, Taktanfänge kräftiger).
2. Pro Ziel (Loop-1-Region × Loop-2-Region) gibt es einen Mix-Regler, der Loop 1 gegen die Loop-2-Region überblendet.
   Er wirkt nur, solange diese Loop-2-Region gespielt wird.
3. Vorhandene Projekte, Presets und Banks klingen unverändert (Standard = beide Loops voll).
4. Wechsel des Mix (Regionswechsel, Reglerbewegung) klicken nicht und lassen den Pegel nicht springen.
5. Der Mix-Wert gehört zum Ziel: er wandert beim Teilen und Zusammenführen von Regionen mit, wird mit Projekt und
   Preset gespeichert und im Bank-Export mitgenommen.

## Nicht-Ziele

- Raster feiner als Viertelnoten, einstellbare Rasterweite, Triolen. (Erweiterbar, aber nicht jetzt.)
- Mix pro Loop-2-Region unabhängig von der Loop-1-Region (der Nutzer will pro Regel).
- Host-Automation des Mix, Mix für Loop-1-Regionen allein, Mix für das Vorhören.
- Ein Master-Regler für Loop 1 oder Loop 2.
- Verschieben bestehender Grenzen auf das Raster beim Laden oder Öffnen (nur neue Bewegungen rasten).

## Anti-Ziele

Es wäre ein Fehlschlag, wenn: ein vorhandenes Projekt nach dem Update anders klingt, der Mix beim Regionswechsel hörbar
klickt oder pumpt, das Raster das Setzen schwerer statt leichter macht (zum Beispiel springende Grenzen, die sich nicht
frei setzen lassen), oder die Audio-Verarbeitung Speicher anfordert oder auf Sperren wartet.

## Entscheidungen des Nutzers

1. Raster: Viertelnoten (1/4). Optisch pro Schlag eine leichte Linie, pro ganzem Takt eine kräftigere. Sichtbar und
   einrastend, in den Waveforms (beide Spuren).
2. Mix-Regler pro Regel, also pro Loop-2-Region je Loop-1-Region (neben Gewicht und GATE/1-SHOT in der Regelzeile).
3. Der Mix ist eine echte Überblendung zwischen Loop 1 und der Loop-2-Region (nicht nur ein Loop-2-Pegel). Es muss
   möglich sein, nur Loop 2 laufen zu lassen.
4. Das Verhalten bei den Wechseln (siehe "Zeitpunkt") wird der Nutzer hören und dann beurteilen.

## Entscheidungen von Claude (zum Gegenprüfen)

### Raster

- **Einrasten immer an, Umschalt-Taste frei.** Beim Ziehen einer Grenze, beim Neu-Setzen per Doppelklick und beim
  Hinzufügen rastet die Position auf den nächsten Schlag ein; wird beim Ziehen Umschalt gehalten, ist die Position frei.
  Ist der nächste Schlag wegen des Mindestabstands zwischen Regionen (`kMinRegion`) nicht erlaubt, gilt das normale
  Klemmen von `RegionMap`.
- **Greifen bleibt unverändert.** Das Greifen einer Grenze (Treffer-Toleranz) und das Auswählen von Regionen ändern sich
  nicht; eine bestehende, nicht auf dem Raster liegende Grenze bleibt, wo sie ist, bis der Nutzer sie zieht.
- **Takteinteilung.** Takte pro Loop kommen aus dem Takte-Feld der Spur (`bars`), Schläge pro Takt aus dem Zähler der
  Host-Taktart; ohne Host-Angabe 4. Raster-Schritt in Phase: `1 / (bars × Schläge pro Takt)`. Die Taktart-Angabe des
  Hosts wird dafür vom Audio-Thread zum Editor weitergereicht (wie das Host-BPM).
- **Zeichnen.** Schläge als leichte Linie, Taktanfänge kräftiger (Farbe dezent, hinter den Regionsgrenzen, damit die
  Grenzen weiterhin am deutlichsten sind). Liegen Schläge weniger als etwa 4 Pixel auseinander, werden nur Taktlinien
  gezeichnet; liegen auch die weniger als etwa 4 Pixel auseinander, wird nichts gezeichnet und nichts rastet.
- **Vorschlag (AUTO).** Die vorgeschlagenen Grenzen werden auf den nächsten Schlag gerundet; Duplikate und Grenzen, die
  den Mindestabstand verletzen, fallen weg (wie sonst in `RegionMap::addBoundary`).
- **Kein Raster ohne Daten.** Ohne geladene Datei (oder ohne Takte-Feld-Wert) gibt es kein Raster.

### Mix

- **Wertebereich und Standard.** `mix` 0 bis 1, Standard 0,5 (Mitte). Mitte heißt: Loop 1 und die Loop-2-Region
  laufen beide mit voller Lautstärke, genau wie bisher. Nach links (Richtung 0) wird die Loop-2-Region leiser bis
  stumm, Loop 1 bleibt voll. Nach rechts (Richtung 1) wird Loop 1 leiser bis stumm, die Loop-2-Region bleibt voll.
  Formel: `gain2 = min(1, 2 × mix)`, `gain1 = min(1, 2 × (1 − mix))`.
- **Wann der Mix wirkt.** Solange eine Loop-2-Voice gespielt wird, gelten ihr Mix-Wert für den Loop-2-Pegel und
  derselbe Wert für die Loop-1-Dämpfung. Endet die Voice (Stille gezogen, Gate endet, One-Shot fertig), kehrt Loop 1 auf
  vollen Pegel zurück. Ohne laufende Voice ist Loop 1 voll.
- **Welcher Wert gilt.** Der Mix des Ziels, das den Voice gestartet hat (Loop-1-Region beim Start, Loop-2-Region). Ein
  Gate-Voice, der in eine andere Loop-1-Region hineinläuft, behält den Wert seines Ziels, bis er endet.
- **Wechsel.** Alle Pegel (Loop 1 und Loop 2) ändern sich über eine kurze Rampe (etwa 5 bis 10 ms), nie als Sprung. Ein
  auslaufender Voice (`tail_`) fadet mit seinem Mix-Wert aus; beginnt ein neuer Voice, geht Loop 1 von dem aktuellen
  Pegel auf den neuen Zielpegel.
- **Änderung im Editor während des Spielens.** Ändert der Nutzer den Mix des Ziels, das gerade klingt, hört er es sofort
  (wie bei den Effekt-Schaltern); die laufende Voice liest den Wert aus dem neuen Modell, wenn ihr Ziel noch existiert
  (gleiche Loop-1-Region beim Start und gleiche Loop-2-Region mit gleichen Grenzen).
- **Vorhören.** Das Vorhören einer Region ignoriert den Mix (Loop-2-Vorhören mit vollem Pegel, Loop 1 bleibt voll), damit
  das Gestalten nicht davon abhängt, welche Regel gerade gilt.
- **Effekte.** Der Mix wirkt auf das Loop-2-Signal vor den Effekten (Eingang der Kette) und auf Loop 1 unabhängig von
  den Effekten. Die Effektkette bleibt unverändert.
- **Anzeige.** In der Regelzeile ein Regler mit Doppelklick = Mitte, Beschriftung "MIX". Der Wert wird lesbar angezeigt
  (zum Beispiel "L1 | L2" in der Mitte, "nur L2" ganz rechts, "nur L1" ganz links; der Bauende wählt die Schreibweise).
  Der Regler steht auch für Zeilen mit Gewicht 0.
- **Format.** `RuleTarget` bekommt ein Feld `mix` (Float 0..1, Standard 0,5). Im Plugin-Zustand (XML) kommt `mix` als
  optionales Attribut an `Target` (nur geschrieben, wenn ungleich 0,5; `kStateVersion` bleibt 1; fehlt es, gilt 0,5).
  In den Presets (`bank.json`) kommt `mix` als optionales Feld je Ziel; weil sich das Bank-Format erweitert, steigt
  `version` von 2 auf 3 (Regel R6 der Preset-Entscheidungen: ältere Plugin-Versionen lehnen neuere Banks ab). Version 1
  und 2 bleiben lesbar. Beim Laden: NaN, Unendlich und Text fallen auf 0,5, Zahlen werden auf 0..1 begrenzt. Der Mix
  zählt zum Preset-Fingerabdruck (Ungespeichert-Abfrage), ein Standardwert 0,5 ergibt denselben Fingerabdruck wie
  fehlende Daten.
- **Zusammenführen und Teilen.** Der Mix hängt am Ziel; die bestehenden Regeln für Index-Verschiebung (Regeln wandern
  mit ihren Regionen) übernehmen ihn mit, ohne neue Sonderregel.

## Wie es sich anfühlt (Bedienung)

- Beim Ziehen einer Grenze zieht sie spürbar auf die Schläge; mit gehaltener Umschalt-Taste ist sie frei. Die leichten
  und kräftigen Linien in beiden Spuren zeigen Schläge und Takte; sie stehen übereinander in beiden Spuren, weil beide
  Loops gleich viele Takte haben können (bei verschiedenen Taktzahlen hat jede Spur ihr eigenes Raster).
- In der Regelzeile einer Loop-1-Region steht neben jeder Loop-2-Region (Zeile) Gewicht, GATE/1-SHOT und der Mix-Regler.
  Bei Mix links vom Mittelpunkt hört man Loop 2 weniger, rechts davon Loop 1 weniger.

## Engine (Rahmen für den Bauenden)

- Heute wird Loop 1 direkt in `l`/`r` gerendert, Loop 2 (Voice, auslaufender Voice, Vorhören von Loop 2) in einen Bus
  durch die Effektkette und danach auf `l`/`r` addiert. Der Mix greift an zwei Stellen: die Loop-2-Voices erhalten einen
  Pegel (vor dem Bus, also vor den Effekten), und Loop 1 wird in `l`/`r` vor der Addition des Busses mit einer
  geglätteten Verstärkung multipliziert.
- `Voice` bekommt den Mix-Wert, mit dem sie gestartet wurde (und für die Neubewertung bei einem nahtlosen Modelltausch
  die Angabe, aus welchem Ziel er stammt: Loop-1-Region beim Start und `region2`).
- Das Ziel, das den Voice gestartet hat, wird im `Sequencer::Command` bzw. beim Start in der Engine bekannt gemacht
  (der Sequencer kennt `region1` und das Ziel bereits).
- `RuleSet`/`RuleTarget`: das Feld `mix` wird beim Setzen bereinigt (wie das Gewicht: NaN/außerhalb begrenzen).
- Keine Allokation, keine Sperren in der Audio-Verarbeitung; alle Glättungen laufen pro Sample.

## Was dem Bauenden überlassen bleibt

- Die konkrete Glättungsform und -dauer (innerhalb 5 bis 10 ms), die Schreibweise der Mix-Anzeige, die Farben und
  Strichstärken des Rasters, die Aufteilung auf Dateien und Funktionen.
- Wie die Taktart des Hosts vom Audio-Thread zum Editor kommt (ein Atomic neben dem Host-BPM genügt).

## Prüfaspekte (nicht ausgespielte Eingaben)

- Loop 1 und Loop 2 mit verschiedener Taktzahl (zum Beispiel 4 und 8): jede Spur hat ihr eigenes Raster.
- Taktart 3/4, 6/8 und 7/8 (Zähler 3, 6, 7): Schläge pro Takt aus dem Zähler.
- Sehr kurze Spuren, sehr schmal gezeichnete Spuren (Raster ausblenden statt Pixelbrei).
- Mix 0 und 1 an den Rändern: nur Loop 1 beziehungsweise nur Loop 2; Mix NaN im Projekt.
- Regionswechsel mitten im Block mit unterschiedlichen Mix-Werten: kein Klick, kein Pegelsprung.
- Alte Projekte und Banks (Version 1 und 2): laden und klingen wie vorher.
- Mix-Änderung im Editor während der Wiedergabe: sofort hörbar, ohne Klick.
- Teilen und Zusammenführen von Loop-2-Regionen: der Mix wandert mit den Zielen.
