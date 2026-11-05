# Dubcycling – Presets und Banks (Design)

Stand: 2026-11-04. Entstanden im Gespräch mit dem Nutzer (Backlog-Punkt 5). Was der Nutzer entschieden hat, steht
als Entscheidung da; was Claude als Vorschlag ergänzt hat, steht unter "Entscheidungen von Claude".

## Worum es geht und warum

Der Nutzer baut pro Live-Session Aufbauten aus zwei Loops, Regionen, Beschriftungen und Regeln. Wenn ein Aufbau gut
klingt, will er ihn mit einem Knopf, einem Namen und ohne Dateidialoge festhalten, später wiederholen und
unkompliziert an andere weitergeben. Presets sind nicht für den Wechsel während des Spielens gedacht ("vermutlich
gar nicht"), sondern zum Vorbereiten, Wiederfinden und Weiterreichen. Später, wenn auch Nicht-Drum-Loops und Effekte
pro Region dazukommen, werden Presets wichtiger.

## Begriffe

- **Preset:** der komplette gespeicherte Zustand des Plugins: beide Loops (Datei, Hash, Takte, Regionsgrenzen,
  Beschriftungen) und die Regeln (Gewichte, Modus Gate/One-Shot, Stille-Anteil ergibt sich aus der Restmenge).
  Effekte pro Region kommen später dazu; das Format muss sie aufnehmen können.
- **Bank:** ein Ordner im Dubcycling-Preset-Ordner. Er gehört zu einer Live-Session oder einem Stil (Beispiele des
  Nutzers: "atmospheric jungle", "jump up", "techstep") und enthält mehrere Presets samt den Loop-Dateien, die sie
  brauchen.
- **Preset-Ordner:** der Dubcycling-Ordner, in dem alle Banks liegen und in den beim Import entpackt wird.

## Ziele

1. Ein Preset speichern ist ein Knopfdruck plus Namenseingabe.
2. Eine Bank ist in sich geschlossen: Bank-Datei und Loop-Dateien liegen zusammen, die Pfade sind relativ. Eine
   Bank lässt sich kopieren, verschieben und weitergeben, ohne dass etwas bricht.
3. Export ist ein Zip einer Bank, Import ist Entpacken in den Preset-Ordner. Es müssen keine Pfade umgeschrieben
   werden.
4. Mehrere Banks nebeneinander.
5. Das Dateiformat ist erweiterbar (Effekte pro Region, weitere Zustandsfelder), ohne alte Banks zu entwerten.

## Nicht-Ziele

- Preset-Wechsel während des Spielens: keine MIDI-Programmwechsel, keine Anbindung an die Programmliste des Hosts,
  kein nahtloses Umschalten bei laufender Wiedergabe. Ein Preset zu laden darf kurz dauern.
- Teilen über Cloud, Online-Bibliothek oder Standard-Preset-Formate von Hosts (.vstpreset, .fxp/.fxb im
  VST2-Format). Das Konzept ist an fxb/fxp angelehnt, das Format ist eigen.
- Aufräum-Funktion für unbenutzte Samples (Ordner wächst; später möglich).
- Ändern des Preset-Ordners per Einstellung.

## Anti-Ziele (daran würde es scheitern, selbst wenn es technisch läuft)

- Ein exportiertes Zip, das auf einem anderen Rechner nicht funktioniert (fehlende Samples, kaputte Pfade).
- Ein Laden, das das Plugin in einem halben Zustand lässt, oder ein Speichern, das eine bestehende Bank beschädigt.
- Dass das Plugin vom Nutzer Gespeichertes still löscht.
- Dateizugriffe im Audio-Thread.

## Entscheidungen des Nutzers

- Ablauf: Knopf drücken, das Plugin fragt nach einem Namen, der Nutzer gibt ihn ein, das Plugin speichert.
- Es gibt mehrere Banks, benannt vom Nutzer.
- Eine Bank ist ein Ordner mit Bank-Datei und Unterordner `samples`. Die Pfade in der Bank-Datei sind relativ.
- Die Samples werden erst beim Speichern eines Presets in den Bank-Ordner kopiert, nicht beim Laden des Loops.
  Jede Bank hat ihre eigenen Kopien, auch wenn dieselbe Datei in mehreren Banks vorkommt (doppelter Platz ist in
  Ordnung).
- Das Plugin löscht nie ein Sample aus `samples`. Ersetzt der Nutzer einen Loop in einem Preset, bleibt das alte
  Sample lokal liegen.
- Export packt nur Samples ein, die mindestens ein Preset der Bank nutzt. Unbenutzte bleiben lokal erhalten.
- Das Zip enthält als oberstes Element genau einen Ordner mit dem Namen der Bank.
- Import: Der Nutzer entpackt das Zip in den Dubcycling-Preset-Ordner, die Bank erscheint danach im Plugin.
- Format der Bank-Datei: JSON.
- In den Auswahllisten ist der aktuelle Eintrag gekennzeichnet: in der Bank-Liste die ausgewählte Bank, in der
  Preset-Liste das aktive Preset.
- Rückfrage beim Laden: Wählt der Nutzer ein Preset, während der aktuelle Zustand ungespeichert ist, fragt das
  Plugin vor dem Laden nach. Es ersetzt den Zustand nur nach Bestätigung (Schutz vor einem Fehlklick).
  Umsetzung nach dem Muster des Namensfeldes: In derselben Zeile erscheint statt der Preset-Auswahl eine Meldung
  ("Ungespeicherte Änderungen verwerfen?") mit LADEN und ABBRECHEN, kein eigenes Dialogfenster (Claudes Vorschlag
  zur Darstellung, der Nutzer hat nur die Rückfrage selbst entschieden).
- Speichern überschreibt das geladene Preset. Ohne geladenes Preset fragt es nach einem Namen. "Speichern unter"
  legt eine Variante unter neuem Namen an.
- Oberfläche (Entwurf "A", vom Nutzer abgenommen, im Look des bestehenden Panels): In der oberen Zeile neben dem
  Logo: Feld BANK (Auswahlliste), Feld PRESET (Auswahlliste), Knöpfe SAVE (blau betont), SAVE AS und "⋯", rechts BPM.
  Das Menü "⋯" enthält: Neue Bank…, Bank exportieren…, Bank importieren…, Preset-Ordner öffnen. Nach SAVE AS (und
  bei SAVE ohne geladenes Preset) ersetzt in derselben Zeile ein Namensfeld mit OK und ABBRECHEN die
  Preset-Auswahl. Kein eigenes Dialogfenster. Die Loop-Streifen und das Regelpanel darunter bleiben unverändert.
  Die Zeile wird bei schmalem Fenster eng; die Anzeige "Zyklus: n Takte" muss dafür eventuell weichen oder
  umziehen (Baufrage).

## Entscheidungen von Claude (bitte beim Lesen prüfen)

1. **Dateiformat (Format JSON vom Nutzer vorgegeben):** Die Bank-Datei heißt `bank.json`. Sie enthält ein Objekt mit
   `version` und einer Liste `presets`; jedes Preset hat `name` und denselben Inhalt wie der Session-Zustand
   (beide Loops mit Datei, Hash, Takten, Regionsgrenzen und Beschriftungen, dazu die Regeln), nur mit relativem
   `path` (Schrägstriche `/`, relativ zum Bank-Ordner). Der Session-Zustand im Host bleibt XML wie bisher; die
   Bank bekommt einen eigenen JSON-Leser und -Schreiber (z. B. `juce::JSON`), der dieselben Bereinigungsregeln und
   Obergrenzen wie `deserialize` in `PluginState.cpp` anwendet. Jede Erweiterung des Formats, die ältere
   Versionen beim Speichern nicht erhalten könnten (neue Felder, neue Eintragsarten), erhöht `version`; eine
   ältere Version lehnt eine solche Bank mit Meldung ab (zu neu) und schreibt sie nie. Eine Bank mit unbekannter
   höherer Version wird mit Meldung übersprungen, nie überschrieben. Enthält eine Bank Einträge, die das Plugin
   beim Lesen überspringt (kein Objekt, Preset ohne Namen, mehr als 1024 Presets), verweigert Speichern mit
   Meldung, statt sie beim Zurückschreiben zu verlieren. Zahlen werden mit voller Genauigkeit geschrieben (bis zu
   17 signifikante Stellen), damit ein gespeichertes Preset genau dem Zustand gleicht, aus dem es gespeichert
   wurde.
2. **Preset-Ordner:** Standard `Dokumente/Dubcycling/Presets` (`juce::File::userDocumentsDirectory`). Jeder
   Unterordner mit `bank.json` ist eine Bank.
3. **Sample-Ablage beim Speichern:** Liegt der Loop schon in `samples` dieser Bank (z. B. nach dem Laden eines
   Presets), wird nichts kopiert. Sonst wird er unter dem Originalnamen kopiert, wobei Zeichen, die nicht auf
   allen Systemen in Dateinamen erlaubt sind, durch `_` ersetzt werden (dazu Punkte und Leerzeichen am Ende
   entfernt, reservierte Windows-Namen mit `_` davor, Länge begrenzt); gibt es den Namen schon (in beliebiger
   Groß-/Kleinschreibung) mit anderem Hash, wird ein Zähler angehängt (`name (2).wav`), bei gleichem Hash wird
   die vorhandene Datei in ihrer Schreibweise auf der Platte weiterverwendet.
4. **Speichern bei fehlendem Loop:** Ist ein Slot Missing oder Failed (Datei nicht lesbar), wird nicht gespeichert
   und das Plugin meldet es. Ist ein Slot leer, wird er als leer gespeichert.
5. **Preset laden:** Ersetzt den kompletten Plugin-Zustand (beide Loops, Regeln) wie ein Wiederherstellen des
   Host-Zustands, mit absolut aufgelösten Pfaden in den Bank-Ordner. Laden und Dekodieren der Loops läuft wie
   bisher im Hintergrund. Ein fehlendes Sample ergibt den vorhandenen Zustand "Missing" für diesen Slot, nicht einen
   Absturz. Ist der aktuelle Zustand ungespeichert, fragt das Plugin vorher nach (siehe Entscheidung des Nutzers
   zur Rückfrage). Ungespeichert heißt: Der Plugin-Zustand (Loops, Takte, Grenzen, Beschriftungen, Regeln) weicht
   vom Stand ab, der zuletzt geladen oder gespeichert wurde; ohne aktives Preset gilt der leere Zustand als Stand,
   also ist ein Aufbau mit geladenem Loop ungespeichert. Verglichen wird der Zustand, nicht die Oberfläche. Eine
   dauerhafte "geändert"-Markierung ist nicht vorgesehen.
6. **Aktives Preset im Host-Zustand:** Bank-Name und Preset-Name des zuletzt geladenen oder gespeicherten Presets
   werden als optionale Felder im Session-Zustand gespeichert (Version bleibt 1), damit SAVE nach dem erneuten
   Öffnen des Projekts weiter das richtige Preset überschreibt. Existiert es nicht mehr, wirkt SAVE wie SAVE AS.
7. **Namen:** Preset-Namen: wie Beschriftungen bereinigt (Steuerzeichen raus, getrimmt), höchstens 32 Zeichen,
   nicht leer, innerhalb einer Bank eindeutig (existiert der Name, zeigt das Namensfeld eine Meldung statt zu
   überschreiben; zum Überschreiben dient SAVE). Bank-Namen sind Ordnernamen: ungültige Dateinamen-Zeichen
   (`\ / : * ? " < > |`), Punkte am Ende und reservierte Windows-Namen werden abgelehnt oder bereinigt, höchstens 64
   Zeichen, eindeutig im Preset-Ordner (Groß-/Kleinschreibung egal).
8. **Reihenfolge:** Presets stehen in der Reihenfolge, in der sie angelegt wurden. Banks alphabetisch.
9. **Export:** Zielort wählt der Nutzer per Dateidialog, Vorschlag `<Bank>.zip`. Fehlt ein Sample, das ein Preset
   braucht, bricht der Export mit einer Meldung ab und nennt die Datei, statt ein unvollständiges Zip zu
   schreiben. Zip erzeugt mit `juce::ZipFile::Builder`.
10. **Import:** Zip per Dateidialog wählen und in den Preset-Ordner entpacken (das ist die Bedienung für "Zip in
    den Preset-Ordner entpacken"; ein von Hand entpackter Ordner funktioniert genauso). Existiert eine Bank mit
    dem Namen schon, wird unter `Name (2)` importiert; nie überschreiben. Beim Entpacken: genau ein oberster
    Ordner (ein `__MACOSX`-Ordner zählt nicht mit), Einträge mit `..`, absoluten Pfaden oder Laufwerksbuchstaben
    werden abgelehnt (Zip-Slip), auch wenn sie sonst übersprungen würden. Nur `bank.json` und Dateien unter
    `samples/` werden entpackt; andere Dateien neben `bank.json` (z. B. `.DS_Store`, `Thumbs.db`,
    `desktop.ini`), der `__MACOSX`-Ordner und `._*`-Dateien werden übersprungen, andere Unterordner abgelehnt.
    Größenobergrenze gegen Zip-Bomben. Beim Export muss der Bank-Ordner einen gültigen Bank-Namen haben. Entpackt wird in
    einen temporären Ordner im Preset-Ordner und erst nach vollständiger Prüfung an den Zielnamen verschoben.
11. **Atomares Schreiben:** `bank.json` wird in eine Temp-Datei geschrieben und dann ersetzt, damit ein Abbruch die
    Bank nicht beschädigt.
12. **Kein Preset löschen oder umbenennen in dieser Version.** Die Bank-Datei ist lesbares JSON, das Menü kann den
    Preset-Ordner im Dateimanager öffnen. Das ist ein bewusster Verzicht, kein Versehen.
13. **Threading:** Alle Dateizugriffe laufen nie im Audio-Thread. Kopieren und Zippen dürfen die Oberfläche kurz
    blockieren (Loops sind klein) oder im Hintergrund laufen, das entscheidet der Bauende.
14. **Zuschnitt im Code (Vorschlag):** Die Logik für Banks (Lesen, Schreiben, Sample-Ablage, Zip, Namensregeln) liegt
    in einer eigenen Einheit ohne UI (z. B. `plugin/presets/`), damit sie ohne Editor testbar ist. Die
    Oberfläche (Leiste im Editor) bindet sie nur an.

15. **Bedeutung der Auswahllisten (Ergebnis der Gegenlese):** Die Bank-Auswahl wählt nur, welche Presets die
    Preset-Liste zeigt und in welche Bank SAVE und SAVE AS schreiben; sie lädt nichts. Ein Preset wird erst
    geladen, wenn der Nutzer es in der Preset-Liste wählt. Wechselt der Nutzer die Bank, ohne ein Preset zu wählen,
    gibt es kein aktives Preset, und SAVE fragt wie SAVE AS nach einem Namen. Beim ersten Start gibt es keine Bank:
    SAVE und SAVE AS fragen dann zusätzlich nach einem Bank-Namen und legen die Bank an ("Neue Bank…" im Menü
    bleibt der direkte Weg). Die Rückfrage vor dem Laden vergleicht immer mit dem aktiven Preset (zuletzt
    geladen oder gespeichert), auch wenn gerade eine andere Bank ausgewählt ist; die Bank-Auswahl allein ändert
    den Vergleichsstand nicht.
16. **Mehrere Plugin-Instanzen:** Die Bank-Logik hält keinen eigenen Zwischenspeicher. Jedes Speichern liest
    `bank.json` unmittelbar davor neu ein, ändert das eine Preset und schreibt atomar zurück. Die Preset-Liste wird
    beim Öffnen der Auswahl neu eingelesen. Gleichzeitiges Speichern in dieselbe Bank von zwei Instanzen im selben
    Moment ist nicht abgesichert (letzter gewinnt).
17. **Hash-Abweichung beim Laden:** Ein Sample, dessen Hash nicht zum gespeicherten passt, wird behandelt wie beim
    Wiederherstellen eines Host-Zustands; der Bauende folgt dem vorhandenen Verhalten und erfindet keinen zweiten
    Weg.

## Was der Bauende entscheidet

- Aufbau und Namen der Klassen, Dateiaufteilung, Test-Strategie (empfohlen: Bank-Logik mit temporären Ordnern
  testen, UI-Test für die Leiste im Stil der bestehenden Editor-Tests).
- Genaues Verhalten der Leiste bei schmalem Fenster und wie die Auswahllisten aussehen.
- Ob Kopieren und Zippen im Hintergrund laufen.
- Texte der Meldungen (Sprache wie im restlichen Plugin, Deutsch).

## Randbedingungen

- C++20, JUCE 8.0.15, bestehende Struktur (`engine/` reines C++, `plugin/` mit Prozessor, State, UI).
- Neue Tests laufen mit Catch2 in der bestehenden Suite; alle bisherigen Tests (217) bleiben grün.
- VST3-Validierung (Steinberg-Validator und pluginval) bleibt grün; die Programmliste des Hosts bleibt
  unverändert (ein Programm "Default").
- Der bestehende Session-Zustand bleibt rückwärtskompatibel.
