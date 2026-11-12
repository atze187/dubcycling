# Reverse playback of Loop-2 regions — design

Date: 2026-11-12. Status: implemented.

## Intent

A Loop-2 region should be playable backwards. The setting belongs to a **mapping** (a Loop-1 region
triggers a Loop-2 region), not to the region, so the same Loop-2 region can play forwards from one
Loop-1 region and backwards from another.

## Decided with the user

- A **REV** toggle sits in each target row of the rule panel, next to GATE and 1-SHOT. It is
  independent of the mode.
- Reverse is a fixed switch per mapping (no chance); the random draw still only chooses which
  mapping fires.
- Timing and tempo stay locked to the host; effects, mix and everything else behave as before.
- The flag is stored with each mapping, so it travels with presets and banks; older presets and states
  load with reverse off.
- A replayed pass (pass slots) uses the **current** mapping's reverse setting, the same way it takes its
  mix from the current rules.

## Designer's calls (to be checked by the user)

- **Gate + reverse:** the region plays backwards and loops backwards seamlessly while Loop 1 stays in the
  triggering region (same start/stop/fade behaviour as forwards).
- **One-Shot + reverse:** the region plays once from its end to its start, then ends like a forwards
  one-shot (same end fade).
- Right-click audition of a region stays forwards (it has no mapping).
- The playhead of Loop 2 shows the source position and therefore moves backwards while a reversed voice
  plays.
- Bank format version 4 -> 5 (optional `"reverse"` per target, default false); a bank newer than 5 is
  still refused as too new.
- The state XML gets an optional per-target attribute (absent = false); `kStateVersion` stays 1.

## Non-goals

- No reverse for Loop 1, no random reverse probability, no per-region (instead of per-mapping) flag,
  no pitch change (audio is reversed in time only).

## Constraints

- Audio thread: no allocation, no locks. With every mapping forwards, output must stay bit-identical to
  today (existing Engine tests unchanged and green).
- UI strings English; the rule row must still fit at the minimum window size (800x600).
- VST3 validation stays green.
