# Filter LFO and new filter modes — design

Date: 2026-11-24. Status: draft, awaiting approval.

## Intent

The Filter insert gets a dedicated, tempo-synced LFO for rhythmic "pumping" cutoff movement (filter chops locked to
the host beat), and three more filter modes: Notch, Peak and LP 24. The LFO belongs to the Filter only.

## Goals

- **LFO on the cutoff**, synced to the host tempo in note values from the same list as the Delay time (1/32 to 2 bars,
  straight, dotted, triplet). The chosen note value is the duration of one LFO cycle.
- **LFO stays on the beat:** its phase follows the host's musical position, so jumps, loops and restarts keep it in time.
- **Three new modes:** Notch, Peak, LP 24 (24 dB/oct low-pass), next to LP, HP, BP.
- **Mode choice is a button group, never a continuous slider.**
- With LFO depth 0 and a mode of LP, HP or BP the Filter sounds exactly as today.

## Non-goals / anti-goals

- No LFO matrix, no routing, no LFO on any other effect or on the playing itself (decided 2026-11-24: dropped).
- No free-running Hz rate, no phase or retrigger control, no LFO on resonance, no per-region LFO settings
  (one parameter set per effect, as today), no Allpass mode, no host automation.
- Anti-goal: an LFO that drifts off the beat, or any audible click when switching mode, shape or rate.

## Approaches

- **Mode choice.** (A) Buttons in two rows, (B) a drop-down menu. Chosen by the user: **A**. All modes visible and
  one click each, matching the current three buttons; the drop-down saves space but hides the current mode.
- **Code structure** (builder-level, my call): the LFO is a small separate class in `engine/fx` that the Filter owns;
  the alternative of writing the LFO inline in the Filter is smaller but harder to test. Notch and Peak come from the
  existing state-variable filter's states; LP 24 is two cascaded stages.

## Behaviour

### Filter modes

- `FilterType` has six steps: 0 LP, 1 HP, 2 BP (unchanged), 3 Notch, 4 Peak, 5 LP 24. Default stays LP.
- Notch and Peak are built from the existing filter states (Notch = low + high; Peak = low − high). LP 24 cascades two
  stages. Resonance keeps its meaning and its range; no mode self-oscillates (as today), and no mode may produce
  unbounded levels. Gain staging of Peak and LP 24 is the builder's call, with the aim that switching modes at
  resonance 0.2 does not cause large level jumps.
- Changing the mode while audio plays must not click (a short crossfade or equivalent; the builder's call).

### LFO

- New parameters: **LFO Rate** (stepped, the 15 delay note values; default 1/4), **LFO Depth** (0–100 %, default 0),
  **LFO Shape** (stepped: Sine, Triangle, Saw down, Square; default Sine).
- **Depth** scales the swing in octaves around the Cutoff knob: 100 % = ±2 octaves (my call), applied in the log
  domain; the resulting cutoff is clamped to the filter's usual range (20 Hz to 0.45 × sample rate). Depth 0 = LFO off.
- **Shape:** all shapes are at their maximum at phase 0, so every shape peaks on the beat grid (my call). Saw down
  falls from the maximum over the cycle. Square is high for the first half, low for the second.
- **Phase:** phase = fractional part of (host position in beats ÷ cycle length in beats). Bar-based values use the
  host's beats per bar, like the Delay. While the host is stopped or has no position, the LFO keeps running at the
  last known tempo (like the Delay tail); it must resume without a jump when the host starts.
- The LFO acts on the Filter only while the Filter is on for the region; it has no state to ring out (the Filter has
  no tail). Both channels receive the same modulation.
- Abrupt shapes (Square) get a short slew so they do not click; how short is the builder's call, shorter than the
  10 ms cutoff smoothing so the chop stays rhythmic.

### UI

- Filter edit view in the FX panel uses two columns, like the Flanger. Left: mode buttons in two rows (LP HP BP /
  NOTCH PEAK LP24), then Cutoff and Resonance. Right: LFO Rate (stepped slider showing note names as the Delay time
  does), LFO Depth and the shape as four buttons (SINE TRI SAW SQR). Double-click resets a slider.
- Must fit the minimum window (800×600) and the FX panel width (360–460 px) without clipping; UI strings English.

### Persistence and compatibility

- The three new parameters are appended to the effect parameters (count 18 → 21) and saved wherever the others are
  (state XML, bank presets). Absent data means the defaults, so older presets and projects load unchanged and sound as
  before. `bank.json` goes to version **7**; banks 1..6 still load, 8+ are refused as too new. The state XML stays at
  version 1 with optional attributes only.
- An older plugin version that loads a newer project clamps an unknown mode (3..5) to its range and drops the LFO
  settings, in line with "older versions drop what they do not know".
- The preset fingerprint treats the new parameters like the other effect parameters.

## Constraints

- Audio thread: no allocation, no locks. Engine part JUCE-free in `engine/fx`, tested with Catch2.
- With depth 0 and a type of 0..2 the output is bit-identical to today (existing filter tests stay unchanged).
- VST3 validation (Steinberg validator + pluginval, strictness 5) stays green.

## Left to the builder

- Exact gain staging and Q split of Peak and LP 24; the crossfade on mode change; the slew of the LFO.
- How the host position reaches the Filter (the chain already receives tempo and bars each slice; position is added).
- The widget layout in the panel and the exact button text.

## Follow-ups after implementation

- Update the wiki Effects page and its screenshot, and the README's bank format number.
