# Dubcycling – Backlog (collected while jamming, 2026-11-02)

Items are worded as the user phrased them. Assessments below them are Claude's.

## Approved

- Compact industrial control panel (draft in chat): channel strips with LED/LCD, segment bars for weights, two-position GATE/1-SHOT switch, region letters in the lanes, window approx. 760x440.

## Open

0. DONE (2026-11-03): Compact industrial panel implemented (PanelLook, LCD displays, segment bars, GATE/1-SHOT switch, region letters in the lanes, BPM display). Not yet viewed in a DAW.

1. DONE (2026-11-03, playhead per lane; the active loop 2 region has been tinted green since the teaser). "the loops need an indicator showing where they currently are"
2. DONE (2026-11-03; lead time fixed at 150 ms, the engine draws ahead, lane 2 fades in the upcoming region, Silence dims the lane; the running region is tinted green; double draw at the loop wrap caught; lead time not yet adjustable). "can we visually tease the decision, made from the probability, of whether a region from loop 2 gets played, in the hundredths-of-a-second range beforehand? as a briefly building bar, or the regions being played get a different color and the display blends to it well before the region is played"
   - Assessment: Feasible, but the draw has to happen earlier for that. Today the sequencer rolls the dice only when entering the loop 1 region. New: draw shortly before the boundary (lead time e.g. 100-250 ms), hold the result (region or silence) and use exactly that result at entry; the UI reads it lock-free. The look-ahead draw must expire on host jump, tempo/transport change, rule or region change (model swap) and on loop wrap, otherwise the UI teases something different from what is then played. A lead time of 10-30 ms would be below one UI frame (30-60 Hz) and barely visible; a build-up effect needs 100+ ms. Lead time as an adjustable value. Related to the open double-draw issue at the loop wrap, so they should be tackled together.

3. DONE (2026-11-03; a click on a region in loop 1 or 2 plays it once, even with the host stopped, tempo = last host BPM, mixed in; a new click fades out the running preview). "while tweaking it turned out that a click on a region should play it"
   - Assessment: Audition by click. Today the click selects the region (loop 1: for the rules). Open questions for the user: (a) Does this also work with the host stopped? Then the preview needs its own clock (tempo = last host BPM). (b) Gate-like (plays as long as the mouse button is held, as a loop of the region) or one-shot? (c) Does the preview displace the lane's running playback or is it mixed in? Technically: a dedicated preview voice in the audio thread, started from the UI by a lock-free command; ready-made building blocks (LoopReader, 5 ms crossfade) exist.

   - Decided (user): Preview is always possible (even with the host stopped), always one-shot, and is mixed in.

4. DONE (2026-11-03; rename by double-clicking the name in the lane or in the rule row of loop 2, max. 16 characters; the loop 1 name is in the rule title; AUTO (Suggest) leaves the names where the rules are, on their indices). "make the region labels editable"
   - Assessment: One freely nameable label per region, default stays A, B, C. Store in the state (bump the version field, length limit e.g. 16 characters, old sessions keep loading). Labels have to move with their regions when boundaries are added/removed, like the rules (`remapRulesForBoundaryEdit`). They appear in the lane and in the rule rows. Editing not by double-click in the lane (that adds/removes boundaries) but by double-click on the label field or in the rule panel.

5. DONE (2026-11-04; banks as folders with `bank.json` and `samples/`, preset bar with BANK/PRESET/SAVE/SAVE AS/⋯, prompt on unsaved changes, export and import of a bank as zip; draft: `docs/superpowers/specs/2026-11-04-presets-design.md`). "maybe we'll include presets then (that can become especially interesting later, when using non-drum loops too)"
   - Known limits: Presets cannot be deleted or renamed. Samples are never removed from a bank. On import, system files next to `bank.json` (`.DS_Store`, `Thumbs.db`, …), `__MACOSX` folders and `._*` files are skipped; other subfolders are rejected by the import.
   - Decided (user, 2026-11-03): Loops stay in the session state as path plus hash, no embedding of the audio data. A preset's samples live in the bank under `samples/`.
   - Assessment: Preset = region boundaries, rules, labels and bars without the audio file, probably as a file next to the loop or in a preset folder. Fits the region labels (item 4) and the Suggest algorithm for non-drum loops.

6. DONE (2026-11-05; four inserts for loop 2 in a fixed order Bitcrush, Filter, Flanger, Reverb, one set of knobs per insert for all regions, switch per region in the strip below loop 2 and in the panel, reverb and flanger ring out, effects in presets and projects, bank format version 2; draft: `docs/superpowers/specs/2026-11-05-effects-design.md`). Originally: "Effects per region"
   - Known limits: Effects only on loop 2. 1-bit Bitcrush has three levels (-1/0/+1).
   - Open ideas: host automation of the insert parameters, tempo sync of the flanger rate, parameter sets per region, effects on loop 1 too, FX indicator in the UI while only the reverb tail is still sounding.

7. DONE (2026-11-05; grid in quarter notes in the loop lanes, beats per bar from the numerator of the host time signature (4 without a host), faint lines per beat, strong ones per bar, snapping always on when dragging and adding, Shift key for free placement, AUTO snaps to the grid, lines hidden when too dense; draft: `docs/superpowers/specs/2026-11-05-grid-and-mix-design.md`).
   - Open ideas: adjustable grid width, triplets, a threshold when dragging before snapping kicks in.

8. DONE (2026-11-05; mix knob per rule row: center = both loops full, left loop 2 region quieter, right loop 1 quieter, fully right only loop 2; applies while the loop 2 region plays, afterwards loop 1 is full again; preview ignores the mix; stored per rule in project and preset, bank format version 3; draft: `docs/superpowers/specs/2026-11-05-grid-and-mix-design.md`).
   - Known limits: Older plugin versions reject version 3 banks; a project with mix that an older version saves loses the mix values.
   - Open ideas: host automation of the mix, mix for the preview, wider mix knob at the smallest window size.

9. DONE (2026-11-06; Autoplay/MIDI toggle in the top bar, in MIDI mode any held note plays both loops, start and stop at the next host bar, a stopped host starts at once at the last tempo, mode saved per instance and not in presets; draft: `docs/superpowers/specs/2026-11-06-midi-trigger-design.md`). "play the loops from MIDI notes"
   - Open ideas: per-note region triggering, note-to-region mapping, MIDI learn.

User note: The rest follows from the UI rework; it is "a hell of a lot of fun".

1b. Playhead details for item 1
   - Assessment: Playhead per lane. The engine knows the phase of both loops; the UI needs a lock-free display position (atomic) from the audio thread, drawn by a timer at ~30 Hz. Loop 2 shows nothing when silence/no region is currently running. Also useful: highlight the active loop 2 region.
