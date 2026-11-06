# Dubcycling – Effects per Region (Design)

As of: 2026-11-05. Came out of the conversation with the user (backlog item 6). What the user decided is stated
as a decision; what Claude added is under "Decisions by Claude" and is to be read as a proposal.

## What it is about and why

The user wants to put creative effects on individual regions of loop 2: reverb, filter, bitcrush, flanger. When
a region is played, it sounds with the effects that are switched on for it. This makes it possible to shape setups
in which, for example, only the third region goes through a filter and reverb while the others stay dry. The random selection of regions (rules) stays unchanged; effects are an additional coloring on top.

## Terms

- **Insert:** one of the four effects (reverb, filter, bitcrush, flanger). Each insert exists exactly once.
- **Knob set:** the parameters of an insert. There is exactly one set per insert; it applies in all regions in
  which the insert is switched on.
- **Region:** exclusively the regions of loop 2 are meant.
- **Insert switch:** per region and insert an on/off. A region therefore has four switches.

## Goals

1. For each region of loop 2 it can be set, for each of the four inserts, whether it acts on that region.
2. One knob set per insert; a change takes effect immediately in all regions that use the insert.
3. When switching between regions nothing clicks or pops, and the level does not jump audibly.
4. A reverb tail rings out when the next region does not use the reverb or when silence is drawn.
5. Effect settings are part of the state: project, presets, export and import of banks.
6. Operation stays clear even with many regions (up to double digits).

## Non-goals

- Effects on loop 1 (user decision: "only for loop 2"). Loop 1 stays dry.
- Automation of the effect knobs by the host ("without automation for now"). The plugin stays without
  host parameters. The format and the engine must not block it later, but nothing is built for it.
- Separate knob values per region (one knob set per insert, not per region).
- Several instances of the same insert, freely selectable order, further effect types.
- Sidechain, tempo synchronization of the modulation, oversampling.

## Anti-goals

It would be a failure if: a region change clicks audibly, a reverb tail is cut off, an effect
colors the sound or eats CPU in the switched-off state, the level explodes with bitcrush, resonance or feedback,
or the audio processing allocates memory or waits on locks.

## Decisions by the user

1. Effects only for loop 2.
2. All four effects as inserts: one knob set per insert, changes take effect in all regions that use it.
   Per region only the choice of which inserts are active is made.
3. If the next region does not use an insert, the reverb rings out (it is not cut off).
4. Parameters (proposal by Claude, accepted by the user as a "good basis"):
   - Reverb: Mix, Size, Damping.
   - Filter: Type (Lowpass, Highpass, Bandpass), Cutoff, Resonance.
   - Bitcrush: Bits, Rate (downsampling), Mix.
   - Flanger: Rate, Depth, Feedback, Mix.
5. Order in the chain: Bitcrush, Filter, Flanger, Reverb (the reverb at the end so that it picks up the distorted and
   filtered signal and can ring out).
6. Interface "hybrid": four insert modules with knobs; in each module a switch for the selected region of
   loop 2; additionally a narrow strip below the loop 2 track that shows per region with colored markers
   which inserts are running. Switching is done in the strip or in the module. This was chosen because it stays
   readable with any number of regions and still gives the overview.
7. No host automation (see non-goals).

## Decisions by Claude (to be cross-checked)

The user did not decide these points; they are proposals.

- **Tail behavior of the inserts.** Reverb and flanger are "tail effects": if an insert is off for the current
  region, its input is smoothly faded to zero (a few milliseconds), but the insert itself keeps running
  and its output stays in the sum. This is how the reverb rings out, and the flanger's feedback too. Filter and
  bitcrush have no tail; for them there is a smooth crossfade between the unprocessed and the processed signal.
- **Which region counts.** The switches follow the loop 2 voice that was just (re)started. If a
  fading-out voice (short fade-out on switching) is still running, it goes through the same switches as the new one.
  Drawn silence (no voice) smoothly switches all insert inputs to off; the tail effects ring out.
- **Preview (right-click on a region)** runs through the inserts that are switched on for that region. When
  shaping, you hear how the region will sound.
- **Editing region boundaries.** When a boundary is inserted, the new half takes over the switches of the split
  region (the region keeps sounding the same after the split). On merging, the switches of the left
  region apply. This corresponds to the behavior for names and rules (see labels and rules in the base design).
- **Suggested values and ranges** (the builder may readjust, as long as audibly sensible and stable):

  | Insert   | Knob      | Range                         | Default       |
  | -------- | --------- | ----------------------------- | ------------- |
  | Reverb   | Mix       | 0 to 100 %                    | 30 %          |
  | Reverb   | Size      | 0 to 100 %                    | 50 %          |
  | Reverb   | Damping   | 0 to 100 %                    | 50 %          |
  | Filter   | Type      | Lowpass, Highpass, Bandpass   | Lowpass       |
  | Filter   | Cutoff    | 20 Hz to 20 kHz, logarithmic  | 2 kHz         |
  | Filter   | Resonance | 0 to 100 %                    | 20 %          |
  | Bitcrush | Bits      | 1 to 16                       | 8             |
  | Bitcrush | Rate      | downsampling factor 1 to 64   | 4             |
  | Bitcrush | Mix       | 0 to 100 %                    | 100 %         |
  | Flanger  | Rate      | 0.05 to 5 Hz                  | 0.3 Hz        |
  | Flanger  | Depth     | 0 to 100 %                    | 50 %          |
  | Flanger  | Feedback  | -90 to 90 %                   | 40 %          |
  | Flanger  | Mix       | 0 to 100 %                    | 50 %          |

- **Newly created project or preset:** all switches off. Then the plugin sounds as before, the default values
  only become audible once a switch is on.
- **Format.** The effect data (knob sets and switches per loop 2 region) go optionally into the plugin state
  (XML, if they are missing "all off" applies) and into the presets of the bank. Because the bank format is extended,
  `version` in `bank.json` rises from 1 to 2 (rule of preset decision R6: older plugin versions reject newer
  banks and never write into them). Banks with version 1 stay readable and are raised to 2 on the next save.
  On loading, all numbers are checked and clamped to the ranges (NaN and infinity fall back to the
  default); the user cannot load anything that brings the engine into an invalid state.
- **Unsaved check.** The effect settings are part of the preset fingerprint: if the user changes a
  knob or switch, the preset counts as modified.

- **One chain, one state of the switches.** There is exactly one insert chain (no second set for the preview). The
  switches follow whatever was started last: a loop 2 voice or a preview. If another region is previewed during a
  running voice, its switches also apply to the running voice for the duration of the preview. This is a side effect of
  shaping and deliberately chosen (half the CPU, one reverb).
- **Time of switching.** The switches change when a new voice starts or silence is drawn, not
  at the end of a region. A gate voice that keeps playing beyond its region end keeps the switches of its
  region until a new voice or silence comes.
- **Off means: dry signal stays.** For reverb and flanger only the input of the effect is smoothly faded to zero;
  the dry signal always passes through, the mix knob only determines the effect share. A switched-off
  reverb with mix 30 % is therefore 100 % dry while its tail rings out. For filter and bitcrush
  there is a crossfade between unprocessed and processed.
- **Moving boundaries.** Switches are attached to the region index. If a boundary is merely dragged, each region keeps its
  index and its switches. If the loop 2 file is replaced, the regions and all rules are dropped (existing
  behavior); the switches are dropped with them, the knob sets stay. A model swap during playback continues
  seamlessly, like with rules.
- **Ringing out on transport and loading.** Transport stop, jump and loop of the host let the tail effects
  ring out (nothing is cut off). When loading a preset or replacing the loop 2 file the chain is cleared with
  a short fade-out (about 50 ms) so that no reverb from the old setup reaches into the new one.
- **Where the data live.** Knob sets and switches belong to the preset and the project state, not to the bank
  as a whole. Switches are stored per loop 2 region index. If a loaded state has more switch entries than
  regions, the surplus ones are discarded; missing entries count as off.

## How it feels (operation)

- The user selects a region in the loop 2 track. In the module of each insert a switch shows whether the insert is on for that
  region, and toggles it. Alternatively they click in the strip below the track directly on the marker
  of an insert in a region.
- The strip below the track follows the width of the regions; each region has four small markers in the colors
  of the inserts. On is filled, off is empty.
- Knobs are dragged with the mouse; a double-click sets the default value. The values are displayed.
- Everything stays in the panel look of the plugin, without additional windows.

## Engine (framework for the builder)

- Today loop 2 is mixed directly into the output, together with loop 1. For effects, loop 2 (voice,
  fading-out voice, preview) has to go first into a buffer of its own, through the insert chain and then be
  added to the output. Loop 1 stays untouched and dry.
- The chain runs block by block in the audio processing: no memory allocation, no locks, no
  exceptions. All buffers are created in `prepare`; anew on a change of sample rate or block size.
- Switches and knobs reach the engine through the same model handoff as regions and rules (the latest state counts,
  the audio processing only reads). Knob changes are smoothed (no zipper noise).
- An insert that is on for no region and has no tail left should cost no appreciable CPU.
- `engine/FxSlot.h` (today an unused hook) is replaced by what this design needs, or removed.

## What is left to the builder

- The concrete DSP implementation (reverb algorithm, filter structure, flanger interpolation, smoothing times), as long as
  the anti-goals are met and it sounds audibly clean.
- The exact form of the knobs and of the markers in the strip, as long as the layout of the "hybrid" is kept.
- Internal data structures for switches and knobs and their split into files.

## Test aspects (unplayed inputs)

- A region without switches, all four on, and switching between them mid-block.
- Drawn silence after a region with reverb: the tail rings out, the level never gets bigger than the input plus a
  reasonable margin.
- Extreme knob values (resonance 100 %, feedback 90 %, bits 1, rate 64, size 100 %): finite, no NaN, no sustained tone
  after silence.
- Sample rate 44.1 to 192 kHz and block sizes from 1 to 4096.
- Inserting and removing boundaries: switches move with the regions, as described above.
- Old project and old bank (version 1, without effect data): loads and sounds as before.
- Broken or invalid effect numbers in the project or in an imported bank.
- Preview with effects while playback is stopped.
