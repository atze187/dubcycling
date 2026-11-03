# Dubcycling – Backlog (gesammelt beim Jammen, 2026-11-02)

Punkte stehen so, wie der Nutzer sie formuliert hat. Einschätzungen darunter sind von Claude.

## Abgenommen

- Kompaktes Industrie-Bedienpanel (Entwurf im Chat): Kanalstreifen mit LED/LCD, Segmentbalken für Gewichte, Zwei-Stellungs-Schalter GATE/1-SHOT, Regionsbuchstaben in den Lanes, Fenster ca. 760x440.

## Offen

0. ERLEDIGT (2026-11-03): Kompaktes Industrie-Panel umgesetzt (PanelLook, LCD-Anzeigen, Segmentbalken, GATE/1-SHOT-Schalter, Regionsbuchstaben in den Lanes, BPM-Anzeige). Noch nicht in der DAW angesehen.

1. ERLEDIGT (2026-11-03, Playhead pro Lane; nicht umgesetzt: aktive Loop-2-Region hervorheben). "die loops brauchen einen indikator an welcher position sie sich gerade befinden"
2. "können wir die anhand der wahrscheinlichkeit ermittelte entscheidung ob eine region aus loop 2 gespielt wird im hundertstel sekunden bereich schon vorher visuell anteasern? als sich ganz kurz aufbauenden balken oder die abgespielte regionen bekommen eine andere farbe und zu dieser wird dann schon ganz bevor die region abgespielt wird geblended"
   - Einschätzung: Machbar, aber die Ziehung muss dafür früher fallen. Heute würfelt der Sequencer erst beim Eintritt in die Loop-1-Region. Neu: kurz vor der Grenze (Vorlauf z. B. 100-250 ms) ziehen, Ergebnis (Region oder Stille) festhalten und am Eintritt genau dieses Ergebnis verwenden; die UI liest es lock-frei. Die Vorab-Ziehung muss verfallen bei Host-Sprung, Tempo-/Transportwechsel, Regel- oder Regionsänderung (Modelltausch) und bei Loop-Wrap, sonst teasert die UI etwas anderes als dann gespielt wird. Ein Vorlauf von 10-30 ms wäre unter einem UI-Frame (30-60 Hz) und kaum sichtbar; ein Aufbau-Effekt braucht eher 100+ ms. Vorlauf als einstellbarer Wert. Hängt mit dem offenen Double-Draw-Thema am Loop-Wrap zusammen, sollte gemeinsam angegangen werden.

3. "beim tweaken hat sich ergeben das ein klick auf eine region diese abspielen sollte"
   - Einschätzung: Audition per Klick. Heute selektiert der Klick die Region (Loop 1: für die Regeln). Offene Fragen an den Nutzer: (a) Läuft das auch bei gestopptem Host? Dann braucht die Vorschau eine eigene Uhr (Tempo = letztes Host-BPM). (b) Gate-artig (spielt solange die Maustaste gedrückt ist, als Loop der Region) oder One-Shot? (c) Verdrängt die Vorschau das laufende Playback der Lane oder wird dazugemischt? Technisch: eigener Preview-Voice im Audio-Thread, per Lock-free-Kommando von der UI gestartet; fertige Bausteine (LoopReader, 5-ms-Crossfade) sind vorhanden.

   - Entschieden (Nutzer): Vorschau ist immer möglich (auch bei gestopptem Host), immer One-Shot, und wird dazugemischt.

4. ERLEDIGT (2026-11-03; Umbenennen per Doppelklick auf den Namen in der Lane oder in der Regelzeile von Loop 2, max. 16 Zeichen; Loop-1-Name steht im Regel-Titel; AUTO (Vorschlag) lässt die Namen wie die Regeln auf ihren Indizes stehen). "die labels der regionen bearbeitbar machen"
   - Einschätzung: Pro Region ein frei benennbares Label, Standard bleibt A, B, C. Im State speichern (Versionsfeld erhöhen, Längenlimit z. B. 16 Zeichen, alte Sessions laden weiter). Labels müssen beim Hinzufügen/Entfernen von Grenzen mit ihren Regionen wandern, wie die Regeln (`remapRulesForBoundaryEdit`). Sie erscheinen in der Lane und in den Regelzeilen. Bearbeiten nicht per Doppelklick in die Lane (der fügt Grenzen hinzu/entfernt sie), sondern per Doppelklick auf das Label-Feld oder im Regelpanel.

5. Presets (später): "eventuell nehmen wir dann noch presets mit rein (das kann vor allem später interessant werden, wenn man auch nicht-drums-loops verwendet)"
   - Entschieden (Nutzer, 2026-11-03): Loops bleiben im Session-State als Pfad plus Hash, kein Einbetten der Audiodaten.
   - Einschätzung: Preset = Regionsgrenzen, Regeln, Labels und Takte ohne Audiodatei, vermutlich als Datei neben dem Loop oder in einem Preset-Ordner. Passt zu den Region-Labels (Punkt 4) und zum Vorschlag-Algorithmus für Nicht-Drum-Loops.

6. Effekte pro Region (ursprünglicher Plan, noch nicht gebaut)
   - Stand aus der Brainstorming-Runde: Effekte liegen direkt auf den Regionen. Details wurden bewusst vertagt ("details zu effekten besprechen wir später").
   - Einschätzung: Im Code gibt es nur den ungenutzten Haken `engine/FxSlot.h`. Zu klären: welche Effekte (dubgefahren als mögliche Quelle), wie viele Slots pro Region, ob Loop 1 und Loop 2 gleich behandelt werden, Parameter im State, Umschalten beim Regionswechsel ohne Klicks (Tails). Braucht eigenes Brainstorming vor einem Plan.

Hinweis des Nutzers: Der Rest ergibt sich aus dem UI-Umbau; es macht "einen mordsspaß".

1b. Playhead-Details zu Punkt 1
   - Einschätzung: Playhead pro Lane. Die Engine kennt die Phase beider Loops; die UI braucht eine lock-freie Anzeige-Position (Atomic) vom Audio-Thread, per Timer mit ~30 Hz gezeichnet. Loop 2 zeigt nichts, wenn gerade Stille/keine Region läuft. Zusätzlich sinnvoll: aktive Loop-2-Region hervorheben.
