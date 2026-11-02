# Dubcycling – Design

Stand: 2026-11-02. Aussagen von atze187 sind unmarkiert. Alles mit **[Annahme]** ist eine Entscheidung des Entwurfs-Autors, die noch bestätigt werden muss.

## Worum es geht

Dubcycling ist ein VST3-Plugin, in dem zwei Audio-Loops geladen und in Regionen geteilt werden. Loop 1 ist der Master und läuft immer hörbar durch. Loop 2 ist der Slave: Er spielt nur, wenn eine Regel es auslöst. Erreicht Loop 1 eine bestimmte Region, wird per Zufall (nach Gewichten) entschieden, welche Region aus Loop 2 dazu erklingt. Die Regionen können eigene Effekte bekommen. Das Tempo richtet sich nach dem Host-BPM.

Der Reiz liegt darin, dass ein Loop-Paar bei jedem Durchlauf ein bisschen anders klingt, ohne dass man es von Hand neu arrangiert.

## Ziele

- Zwei Loops als Audiodateien von der Platte laden (WAV, MP3, FLAC, AIFF), in Stereo.
- Beide Loops auf ganze Takte quantisieren und auf das Host-BPM strecken.
- Beide Loops in Regionen teilen, die nicht ans Raster gebunden sind.
- Regeln pro Loop-1-Region: Welche Loop-2-Regionen mit welcher Wahrscheinlichkeit.
- Effekte direkt auf den Regionen (Details später).
- Ein Stereo-Ausgang.
- Start und Stopp folgen dem Host-Transport.
- Zustand wird in der DAW-Session gespeichert.

## Nicht-Ziele (vorerst)

- Kein Performance-Modus in der ersten Phase. Er entsteht erst, wenn alle Funktionen im Edit-Modus stehen; dann wird entschieden, was darin relevant ist.
- Kein MIDI-Instrument und keine Pads.
- Keine getrennten Ausgänge für Loop 1 und Loop 2.
- Keine Aufnahme aus dem Host, nur Dateien von der Platte.
- Keine Audio-Einbettung in die Session (kann später als Option pro Loop kommen).
- Keine externen VST-Plugins als Effekte, nur eingebaute.
- Regionsvorschläge für Nicht-Drum-Loops bleiben offen.
- Plattformen in dieser Reihenfolge: zuerst Windows, danach macOS, danach Linux, jeweils als VST3. Andere Plugin-Formate sind nicht geplant.

## Anti-Ziele

Die Frage, woran atze187 ein Scheitern erkennen würde, wurde so beantwortet: „durch Benutzen“. Es gibt also keine vorab festgelegten Ausschlusskriterien. Daraus folgt für die Umsetzung: früh etwas Benutzbares liefern und im Gebrauch nachschärfen, statt alles vorab zu perfektionieren.

## Loops und Tempo

- Jeder Loop wird beim Laden auf ganze Takte quantisiert und auf das Host-BPM gestreckt.
- Haben die Loops unterschiedliche Längen in Takten, werden beide auf das kleinste gemeinsame Vielfache (KGV) vervielfacht, damit sie synchron bleiben. Beispiel: 3 und 4 Takte ergeben 12 Takte.
- Beide Loops sind Stereo. Das ist eine Abweichung von dubgefahren, wo `SampleData` Mono ist und auf 60 Sekunden begrenzt.
- Start, Stopp und Position kommen vom Host-Transport.
- Ändert sich das BPM im Host (auch mitten im Song), spiegelt sich das in beiden Loops wider: Sie folgen dem neuen Tempo live. Das ist eine Entscheidung von atze187. Folge für die Umsetzung: Das Strecken muss auf BPM-Änderungen reagieren, sei es in Echtzeit oder durch Neuberechnung im Hintergrund. Ein Limit für das Streckverhältnis ist nötig und liegt beim Builder.
- **[Annahme]** Wie die Taktzahl eines Loops bestimmt wird (automatisch aus der Länge und dem Host-BPM geraten oder vom Nutzer eingegeben), ist nicht festgelegt. Siehe „Dem Builder überlassen“.

## Regionen

- Beide Loops werden in Regionen geteilt. Die Regionsgrenzen liegen frei und sind nicht ans Takt- oder Beat-Raster gebunden.
- Regionen werden per Maus gesetzt.
- Das Plugin macht außerdem Vorschläge für Grenzen, zunächst optimiert für Drum-Loops (zum Beispiel an Transienten). Wie das für Nicht-Drum-Loops aussehen soll, bleibt offen.

## Regeln

- Eine Regel gehört zu einer Loop-1-Region. Wählt man im Editor eine Loop-1-Region aus, erscheint darunter die Regel dazu.
- Die Regel besteht aus einer Zeile pro Loop-2-Region, in dieser Form:

  `A  |-------------o-------|  70%  [GATE/ONESHOT]`

  Ein Regler für das Gewicht, der Prozentwert und ein Umschalter zwischen Gate und One-Shot. Der Modus gilt pro Zielregion, nicht pro Regel.
- Erreicht Loop 1 eine Region mit Regel, wird bei jedem Durchlauf neu gezogen: genau eine Loop-2-Region nach Gewicht. Die Gewichte ergeben zusammen höchstens 100 %. Der Rest bis 100 % ist die Wahrscheinlichkeit für Stille.
- Hat eine Loop-1-Region keine Regel, spielt Loop 2 dort nicht.
- **Gate:** Die gewählte Loop-2-Region spielt so lange, wie die auslösende Loop-1-Region läuft.
- **One-Shot:** Die gewählte Loop-2-Region spielt einmal ganz aus, auch über die Grenze der Loop-1-Region hinaus.
- Die Regionen aus Loop 2 haben unabhängig von Loop 1 eigene Längen, daher sind beide Modi nötig.

## Effekte

Entschieden ist bisher nur:
- Es gibt eingebaute Effekte.
- Sie liegen direkt auf den Regionen (nicht als gemeinsamer Send).
- Die Details werden später besprochen (welche Effekte, wie viele Slots, ob auch pro Loop).

Offener Punkt für diese Besprechung: Mit Effekten direkt auf der Region können Hall- und Echo-Schweife an der Regionsgrenze abgeschnitten werden. Eine Send-Lösung hätte das vermieden; sie wurde zurückgestellt.

Der ursprüngliche Wunsch nannte außerdem Effekt-Slots auf einzelnen Regionen oder auf ganzen Loops. Ob Loops als Ganzes Effekte bekommen, ist noch zu klären.

## Oberfläche

- Es gibt einen Edit-Modus und später einen Performance-Modus. Zuerst wird nur der Edit-Modus gebaut.
- Der Edit-Modus zeigt beide Loops als Wellenformen übereinander (Loop 1 oben, Loop 2 darunter, beide mit ihren Regionen). Darunter erscheint die Regel der gewählten Loop-1-Region.
- Beide Loops sind im Editor gleichzeitig sichtbar und bearbeitbar.
- Das Aussehen folgt der Skizze „Variante A“ aus dem Gespräch: Spuren übereinander, Regel-Zeilen darunter.

## Speichern und Laden

- Der Zustand liegt im Plugin-State der DAW-Session (bei Ableton in der Session-Datei).
- Die Loops werden dort nur als Referenz gespeichert: Dateipfad, Dateiname und Prüfsumme.
- Regionen, Regeln und Effekt-Einstellungen sind im Plugin-State und gehen nicht verloren, wenn eine Audiodatei fehlt.
- Ist ein Pfad ungültig, sucht das Plugin den Loop selbst (zum Beispiel im Ordner der Session oder per Dateiname und Prüfsumme). Findet es ihn nicht, meldet es „Datei fehlt“, und der Nutzer ordnet neu zu.

## Technik (Entscheidung des Autors)

Alles in diesem Abschnitt ist eine **[Annahme]**:
- C++20, JUCE 8, CMake, VST3, Catch2-Tests.
- Plattformen: Windows, dann macOS, dann Linux (Entscheidung von atze187). Der Code bleibt von Anfang an plattformneutral (kein Windows-spezifischer Code in `engine/` und `plugin/`), die Build-Presets für macOS und Linux kommen mit den jeweiligen Phasen.
- Aufbau wie dubgefahren: `engine/` (reines DSP, ohne JUCE-GUI) und `plugin/` (Prozessor, UI), mit `build.ps1` und CMake-Presets.

### Was aus dubgefahren übernommen werden kann

Dubgefahren liegt unter `E:\Repos\dubgefahren` (GitHub: atze187/dubgefahren). Geprüft wurde: Projektgerüst, `SampleLoader`, `GrainStretch`, die Effekt-Bausteine, `computePeaks` und `WaveformView`, `DgLookAndFeel`. Die Teile werden in das neue Repo kopiert, nicht verlinkt.

| Teil | Übernahme |
|------|-----------|
| CMake, Presets, `build.ps1`, Tests-Aufbau | direkt |
| `SampleLoader` (WAV, AIFF, FLAC u. a., eigener Thread) | mit Änderung: Stereo statt Mono-Mittelwert, 60-Sekunden-Grenze prüfen |
| `GrainStretch` (Strecken im S950-Stil) | brauchbar, klingt nach Hardware-Sampler; ob es für Drum-Loops reicht, ist zu hören |
| `Saturator`, `SvFilter`, `TapeDelay`, `Phaser`, `SpringReverb`, `Limiter` | einzeln übernehmbar; die feste Verkabelung `FxChain` wird neu gebaut |
| `computePeaks`, `WaveformView` | Anzeige übertragbar, aber Marker-Logik muss von 3 festen Markern auf beliebig viele Regionsgrenzen erweitert werden |
| `DgLookAndFeel`, Controls, Fonts | übernehmbar |

Nicht übernommen: `PadRouter`, `NoteMap`, `Kit` (MIDI-Pads), `SampleRegion` (nur eine Region pro Sample).

Nicht geprüft: ob dubgefahren schon Transienten-Erkennung oder Tempo-Analyse enthält.

## Dem Builder überlassen

- Wie die Taktzahl eines Loops bestimmt wird (Auto-Erkennung, Eingabe oder beides).
- Welches Verfahren zur Transienten-Erkennung für die Drum-Vorschläge.
- Qualität des Strecken-Verfahrens (`GrainStretch` oder ein anderes), solange Drum-Loops brauchbar klingen.
- Wie der Zufall realisiert wird (Zufallsquelle, ob reproduzierbar).
- Wie Übergänge an Regionsgrenzen geglättet werden (Fades gegen Knackser).
- Wie viele Regionen pro Loop maximal.
- Aufbau des Plugin-States und der Versionierung.
- Genaue Gestaltung der Oberfläche innerhalb der Skizze.

## Abspiellogik (Präzisierungen des Autors)

Alle Punkte hier sind eine **[Annahme]** und ergänzen die Regeln oben:
- Gezogen wird bei jedem Eintritt von Loop 1 in eine Region mit Regel, auch nach dem Loop-Wrap und nach einem Sprung des Transports.
- Die gezogene Loop-2-Region startet an ihrem eigenen Anfang, nicht an einer vom Host-Positionsversatz abgeleiteten Stelle.
- Loop 2 hat höchstens eine Stimme. Eine neue Ziehung bricht die laufende Loop-2-Region ab, auch einen noch laufenden One-Shot, und spielt die neu gezogene Region. Das ist eine Entscheidung von atze187. Der Abbruch geschieht mit einem kurzen Fade (Annahme des Autors, gegen Knackser).
- Springt oder loopt der Host-Transport, wird die Position von Loop 1 neu berechnet und Loop 2 beendet.
- Ohne laufenden Transport ist das Plugin stumm.
- Primäres Ziel-Host ist Ableton Live; andere DAWs werden nicht ausgeschlossen, aber nicht gezielt getestet.

## Umfang der ersten Version (Annahme des Autors)

- Die erste Version enthält Laden, Quantisieren und Strecken, Regionen, Regeln, Abspiellogik, Zustand in der Session und den Edit-Modus.
- Effekte sind nicht Teil der ersten Version. Der Plan reserviert nur Haken im Signalweg. Die Effekte werden wie vereinbart nach einer eigenen Besprechung eingeplant.
- Als erster benutzbarer Stand zählt: zwei Loops laden, Regionen setzen, Regeln bearbeiten, mit dem Host-Transport abspielen.
- Transienten-Vorschläge und der Relink bei fehlender Datei kommen nach diesem Stand.

## Dateien und Speicher (Annahmen des Autors)

- Die 60-Sekunden-Grenze aus dubgefahren entfällt; es gilt stattdessen ein Speicherbudget für die gestreckten und vervielfachten Loops, das der Builder festlegt.
- Die Prüfsumme ist ein Hash der gesamten Datei.
- Gesucht wird im Ordner der Session und im zuletzt gespeicherten Ordner der Datei.
- Der Builder prüft vor der Übernahme, ob `GrainStretch` echtzeitfähig ist und ob die Lizenz die Kopie erlaubt (gleicher Autor, daher vermutlich unproblematisch).

## Reihenfolge der Arbeit

1. Dieses Dokument bestätigen.
2. Umsetzungsplan (für Software: `superpowers:writing-plans`).
3. Danach Effekte im Detail und der Performance-Modus als eigene Runden.
