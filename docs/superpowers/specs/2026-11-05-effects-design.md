# Dubcycling – Effekte pro Region (Design)

Stand: 2026-11-05. Entstanden im Gespräch mit dem Nutzer (Backlog-Punkt 6). Was der Nutzer entschieden hat, steht
als Entscheidung da; was Claude ergänzt hat, steht unter "Entscheidungen von Claude" und ist als Vorschlag zu lesen.

## Worum es geht und warum

Der Nutzer will auf einzelne Regionen von Loop 2 kreative Effekte legen: Reverb, Filter, Bitcrush, Flanger. Wird
eine Region gespielt, klingt sie mit den Effekten, die für sie eingeschaltet sind. So lassen sich Aufbauten
gestalten, in denen zum Beispiel nur die dritte Region durch einen Filter und Hall geht, während die anderen trocken
bleiben. Die Zufallsauswahl der Regionen (Regeln) bleibt unverändert; Effekte sind eine zusätzliche Färbung darüber.

## Begriffe

- **Insert:** einer der vier Effekte (Reverb, Filter, Bitcrush, Flanger). Es gibt jeden Insert genau einmal.
- **Regler-Satz:** die Parameter eines Inserts. Es gibt pro Insert genau einen Satz; er gilt in allen Regionen, in
  denen der Insert eingeschaltet ist.
- **Region:** gemeint sind ausschließlich die Regionen von Loop 2.
- **Insert-Schalter:** pro Region und Insert ein Ein/Aus. Eine Region hat also vier Schalter.

## Ziele

1. Pro Region von Loop 2 lässt sich für jeden der vier Inserts festlegen, ob er auf diese Region wirkt.
2. Ein Regler-Satz pro Insert; eine Änderung wirkt sofort in allen Regionen, die den Insert benutzen.
3. Beim Wechsel zwischen Regionen klickt und knackt nichts, und der Pegel springt nicht hörbar.
4. Ein Reverb-Nachhall klingt aus, wenn die nächste Region den Reverb nicht benutzt oder wenn Stille gezogen wird.
5. Effekt-Einstellungen sind Teil des Zustands: Projekt, Presets, Export und Import von Banks.
6. Bedienung bleibt auch bei vielen Regionen (bis in den zweistelligen Bereich) übersichtlich.

## Nicht-Ziele

- Effekte auf Loop 1 (Entscheidung des Nutzers: "nur für Loop 2"). Loop 1 bleibt trocken.
- Automation der Effekt-Regler durch den Host ("erstmal ohne Automatisierung"). Das Plugin bleibt ohne
  Host-Parameter. Das Format und die Engine dürfen es später nicht verbauen, es wird aber nichts dafür gebaut.
- Eigene Reglerwerte pro Region (ein Regler-Satz pro Insert, nicht pro Region).
- Mehrere Instanzen desselben Inserts, frei wählbare Reihenfolge, weitere Effekt-Arten.
- Sidechain, Tempo-Synchronisation der Modulation, Oversampling.

## Anti-Ziele

Es wäre ein Fehlschlag, wenn: ein Regionswechsel hörbar klickt, ein Reverb-Nachhall abgeschnitten wird, ein Effekt
im ausgeschalteten Zustand den Klang färbt oder CPU frisst, der Pegel bei Bitcrush, Resonanz oder Feedback explodiert
oder die Audio-Verarbeitung Speicher anfordert oder auf Sperren wartet.

## Entscheidungen des Nutzers

1. Effekte nur für Loop 2.
2. Alle vier Effekte als Inserts: ein Regler-Satz pro Insert, Änderungen wirken in allen Regionen, die ihn nutzen.
   Pro Region wird nur gewählt, welche Inserts aktiv sind.
3. Wenn die nächste Region einen Insert nicht benutzt, klingt der Hall aus (er wird nicht abgeschnitten).
4. Parameter (Vorschlag von Claude, vom Nutzer als "gute Basis" angenommen):
   - Reverb: Mix, Size, Damping.
   - Filter: Typ (Tiefpass, Hochpass, Bandpass), Cutoff, Resonanz.
   - Bitcrush: Bits, Rate (Downsampling), Mix.
   - Flanger: Rate, Depth, Feedback, Mix.
5. Reihenfolge in der Kette: Bitcrush, Filter, Flanger, Reverb (der Hall am Ende, damit er das Verzerrte und
   Gefilterte aufnimmt und ausklingen kann).
6. Oberfläche "Mischform": vier Insert-Module mit Reglern; in jedem Modul ein Schalter für die gewählte Region von
   Loop 2; zusätzlich ein schmaler Streifen unter der Loop-2-Spur, der pro Region mit farbigen Markierungen zeigt,
   welche Inserts laufen. Geschaltet wird im Streifen oder im Modul. Gewählt wurde das, weil es bei jeder
   Regionenzahl lesbar bleibt und trotzdem den Überblick gibt.
7. Keine Host-Automation (siehe Nicht-Ziele).

## Entscheidungen von Claude (zum Gegenprüfen)

Diese Punkte hat der Nutzer nicht entschieden; sie sind Vorschläge.

- **Tail-Verhalten der Inserts.** Reverb und Flanger sind "Nachhall-Effekte": Ist ein Insert für die aktuelle
  Region aus, wird sein Eingang weich auf null gefahren (einige Millisekunden), der Insert selbst läuft aber weiter
  und sein Ausgang bleibt in der Summe. So klingt der Reverb aus, und auch die Rückkopplung des Flangers. Filter und
  Bitcrush haben keinen Nachhall; für sie wird zwischen unbehandeltem und behandeltem Signal weich übergeblendet.
- **Welche Region zählt.** Die Schalter folgen dem Loop-2-Voice, der gerade (neu) gestartet wurde. Läuft ein
  auslaufender Voice (kurzes Ausblenden beim Wechsel) mit, geht er durch dieselben Schalter wie der neue.
  Gezogene Stille (kein Voice) schaltet alle Insert-Eingänge weich auf aus; die Nachhall-Effekte klingen aus.
- **Vorhören (Rechtsklick auf eine Region)** läuft durch die Inserts, die für diese Region eingeschaltet sind. Beim
  Gestalten hört man so, wie die Region klingen wird.
- **Regionsgrenzen bearbeiten.** Wird eine Grenze eingefügt, übernimmt die neue Hälfte die Schalter der geteilten
  Region (die Region klingt nach dem Teilen weiter wie vorher). Beim Zusammenführen gelten die Schalter der linken
  Region. Das entspricht dem Verhalten bei Namen und Regeln (siehe Beschriftungen und Regeln im Basis-Design).
- **Vorschlagswerte und Bereiche** (der Bauende darf nachjustieren, solange hörbar sinnvoll und stabil):

  | Insert   | Regler    | Bereich                       | Standard      |
  | -------- | --------- | ----------------------------- | ------------- |
  | Reverb   | Mix       | 0 bis 100 %                   | 30 %          |
  | Reverb   | Size      | 0 bis 100 %                   | 50 %          |
  | Reverb   | Damping   | 0 bis 100 %                   | 50 %          |
  | Filter   | Typ       | Tiefpass, Hochpass, Bandpass  | Tiefpass      |
  | Filter   | Cutoff    | 20 Hz bis 20 kHz, logarithmisch | 2 kHz       |
  | Filter   | Resonanz  | 0 bis 100 %                   | 20 %          |
  | Bitcrush | Bits      | 1 bis 16                      | 8             |
  | Bitcrush | Rate      | Downsampling-Faktor 1 bis 64  | 4             |
  | Bitcrush | Mix       | 0 bis 100 %                   | 100 %         |
  | Flanger  | Rate      | 0,05 bis 5 Hz                 | 0,3 Hz        |
  | Flanger  | Depth     | 0 bis 100 %                   | 50 %          |
  | Flanger  | Feedback  | -90 bis 90 %                  | 40 %          |
  | Flanger  | Mix       | 0 bis 100 %                   | 50 %          |

- **Neu angelegtes Projekt oder Preset:** alle Schalter aus. Dann klingt das Plugin wie vorher, die Standardwerte
  sind erst hörbar, sobald ein Schalter an ist.
- **Format.** Die Effekt-Daten (Regler-Sätze und Schalter pro Loop-2-Region) kommen optional in den Plugin-Zustand
  (XML, fehlen sie, gilt "alles aus") und in die Presets der Bank. Weil sich das Bank-Format erweitert, steigt
  `version` in `bank.json` von 1 auf 2 (Regel der Preset-Entscheidung R6: ältere Plugin-Versionen lehnen neuere
  Banks ab und schreiben nie in sie). Banks mit Version 1 bleiben lesbar und werden beim nächsten Speichern auf 2
  gehoben. Beim Laden werden alle Zahlen geprüft und auf die Bereiche begrenzt (NaN und Unendlich fallen auf den
  Standard); der Nutzer kann nichts einspielen, was die Engine in einen unzulässigen Zustand bringt.
- **Ungespeichert-Abfrage.** Die Effekt-Einstellungen sind Teil des Preset-Fingerabdrucks: Ändert der Nutzer einen
  Regler oder Schalter, gilt das Preset als geändert.

- **Eine Kette, ein Stand der Schalter.** Es gibt genau eine Insert-Kette (kein zweiter Satz für das Vorhören). Die
  Schalter folgen dem, was zuletzt gestartet wurde: einem Loop-2-Voice oder einem Vorhören. Wird während eines
  laufenden Voice eine andere Region vorgehört, gelten für die Dauer des Vorhörens deren Schalter auch für den
  laufenden Voice. Das ist ein Nebeneffekt des Gestaltens und bewusst so gewählt (halbe CPU, ein Nachhall).
- **Zeitpunkt des Schaltens.** Die Schalter wechseln, wenn ein neuer Voice startet oder Stille gezogen wird, nicht
  am Ende einer Region. Ein Gate-Voice, der über sein Regionsende hinaus weiterspielt, behält die Schalter seiner
  Region, bis ein neuer Voice oder Stille kommt.
- **Aus heißt: trockenes Signal bleibt.** Bei Reverb und Flanger wird nur der Eingang des Effekts weich auf null
  gefahren; das trockene Signal läuft immer durch, der Mix-Regler bestimmt nur den Effektanteil. Ein ausgeschalteter
  Reverb mit Mix 30 % ist also zu 100 % trocken, während sein Nachhall ausklingt. Bei Filter und Bitcrush wird
  zwischen unbehandelt und behandelt übergeblendet.
- **Grenzen verschieben.** Schalter hängen am Regionsindex. Wird eine Grenze nur gezogen, behält jede Region ihren
  Index und ihre Schalter. Wird die Loop-2-Datei ersetzt, fallen die Regionen und alle Regeln weg (bestehendes
  Verhalten); die Schalter fallen mit weg, die Regler-Sätze bleiben. Ein Modelltausch während des Spielens läuft
  nahtlos weiter, wie bei Regeln.
- **Nachklingen bei Transport und Laden.** Transport-Stopp, Sprung und Loop des Hosts lassen die Nachhall-Effekte
  ausklingen (nichts wird abgeschnitten). Beim Laden eines Presets oder Ersetzen der Loop-2-Datei wird die Kette mit
  einem kurzen Ausblenden (etwa 50 ms) geleert, damit kein Nachhall aus dem alten Aufbau in den neuen hineinragt.
- **Wo die Daten liegen.** Regler-Sätze und Schalter gehören zum Preset und zum Projektzustand, nicht zur Bank
  insgesamt. Schalter sind pro Loop-2-Regionsindex gespeichert. Hat ein geladener Zustand mehr Schalter-Einträge als
  Regionen, werden die überzähligen verworfen; fehlende Einträge gelten als aus.

## Wie es sich anfühlt (Bedienung)

- Der Nutzer wählt eine Region in der Loop-2-Spur. Im Modul jedes Inserts zeigt ein Schalter, ob der Insert für diese
  Region an ist, und er schaltet ihn um. Alternativ klickt er im Streifen unter der Spur direkt auf die Markierung
  eines Inserts in einer Region.
- Der Streifen unter der Spur folgt der Breite der Regionen; jede Region hat vier kleine Markierungen in den Farben
  der Inserts. An ist gefüllt, aus ist leer.
- Regler werden mit der Maus gezogen; ein Doppelklick setzt den Standardwert. Die Werte werden angezeigt.
- Alles bleibt im Panel-Look des Plugins, ohne zusätzliche Fenster.

## Engine (Rahmen für den Bauenden)

- Heute wird Loop 2 direkt in den Ausgang gemischt, zusammen mit Loop 1. Für Effekte muss Loop 2 (Voice,
  auslaufender Voice, Vorhören) zuerst in einen eigenen Puffer, durch die Insert-Kette und dann in den Ausgang
  addiert werden. Loop 1 bleibt unberührt und trocken.
- Die Kette läuft Block für Block in der Audio-Verarbeitung: keine Speicheranforderung, keine Sperren, keine
  Ausnahmen. Alle Puffer werden in `prepare` angelegt; bei Änderung der Samplerate oder Blockgröße neu.
- Schalter und Regler kommen über dasselbe Modell-Handoff wie Regionen und Regeln zur Engine (der letzte Stand zählt,
  die Audio-Verarbeitung liest nur). Reglerwechsel werden geglättet (kein Zippern).
- Ein Insert, der für keine Region an ist und kein Nachklingen mehr hat, soll keine nennenswerte CPU kosten.
- `engine/FxSlot.h` (heute ungenutzter Haken) wird durch das ersetzt, was dieses Design braucht, oder entfernt.

## Was dem Bauenden überlassen bleibt

- Die konkrete DSP-Umsetzung (Reverb-Algorithmus, Filter-Struktur, Flanger-Interpolation, Glättungszeiten), solange
  die Anti-Ziele eingehalten werden und es hörbar sauber klingt.
- Die genaue Form der Regler und der Markierungen im Streifen, solange das Layout der "Mischform" erhalten bleibt.
- Interne Datenstrukturen für Schalter und Regler und ihre Aufteilung auf Dateien.

## Prüfaspekte (nicht ausgespielte Eingaben)

- Eine Region ohne Schalter, alle vier an, und der Wechsel zwischen ihnen mitten im Block.
- Gezogene Stille nach einer Region mit Reverb: Nachhall klingt aus, Pegel wird nie größer als der Eingang plus
  vernünftige Reserve.
- Extreme Reglerwerte (Resonanz 100 %, Feedback 90 %, Bits 1, Rate 64, Size 100 %): endlich, kein NaN, kein Dauerton
  nach Stille.
- Samplerate 44,1 bis 192 kHz und Blockgrößen von 1 bis 4096.
- Grenzen einfügen und entfernen: Schalter wandern mit den Regionen, wie oben beschrieben.
- Altes Projekt und alte Bank (Version 1, ohne Effekt-Daten): lädt und klingt wie vorher.
- Kaputte oder ungültige Effekt-Zahlen im Projekt oder in einer importierten Bank.
- Vorhören mit Effekten, während die Wiedergabe steht.
