# Dubcycling

VST3 plugin: two stereo loops, region-based rules that decide which part of loop 2 plays when loop 1 enters a region.

Status: edit view, audio engine and presets implemented. The plugin has stereo out and no audio input, so hosts list it as an instrument (put it on a MIDI track). MIDI is used in MIDI trigger mode and for the learned notes of the pass slots; otherwise it is ignored. Tested in Ableton Live on Windows (effects, beat grid and mix, MIDI trigger mode); the pass slots are not yet tested in a real host.

## Usage

- **Load loops:** "Load…" next to Loop 1 / Loop 2, or drag and drop an audio file (wav, mp3, flac, aif, aiff)
  onto a lane. Replacing a loop that has regions or rules asks first (its regions and all rules are deleted).
- **Bars:** length of each loop in bars; suggested from the file length and the host tempo on load.
  The combined cycle ("Cycle") is the least common multiple of both.
- **Regions:** double-click in a lane adds a region boundary, double-click on a boundary removes it, drag a
  boundary to move it. Rules move with their regions when a boundary is added or removed (merging two regions
  keeps the left region's rule).
- **Names, audition, playheads:** double-click a region's name (bottom left in the lane, or in the rule row of a Loop-2 region) to rename it (16 characters; empty = A, B, C). A right-click on a region plays it once (also with the host stopped; at the host's last tempo, mixed on top). A white line shows each loop's position; the Loop-2 region that sounds is tinted green.
- **Teaser:** the draw for the next Loop-1 region is made 150 ms before it happens, and Loop 2 shows the result building up (the coming region fades in, "SILENCE" dims the lane). What is shown is what plays, unless you edit or the transport jumps in between.
- **"Suggest":** suggests boundaries at transients (meant for drum loops); replaces the lane's boundaries
  after a confirmation.
- **Rules:** select a Loop-1 region (click) to edit its rule rows, one per Loop-2 region: the slider is the
  probability that this Loop-2 region plays when Loop 1 enters the selected region, the GATE/ONESHOT toggle
  sets the play mode (GATE repeats until Loop 1 leaves the region, ONESHOT plays the region once). "Silence"
  shows the remainder (probability that nothing new plays).
- **Reverse (REV):** the REV button next to GATE / ONE plays that Loop-2 region backwards for this mapping
  only (the same region can play forwards from another Loop-1 region). GATE plays it backwards and loops it
  backwards seamlessly until Loop 1 leaves the region; ONE plays it once from its end to its start, with the
  same fades as forwards. Tempo stays locked to the host and the pitch is unchanged (only time is reversed); the
  Loop-2 playhead moves backwards meanwhile. Right-click audition always plays forwards. A replayed pass uses the
  current REV setting of its mapping (like the mix). A change of REV is heard at the next start of that region, not mid-playback. REV is saved with projects and presets (bank format version
  5; older plugin versions refuse such banks, and a project re-saved by an older version loses the setting).
- **Presets and banks:** the top row has BANK and PRESET. A bank is a folder in `Documents/Dubcycling/Presets/<Bank>/` with `bank.json` and the used samples in `samples/`. SAVE overwrites the active preset, SAVE AS makes a variant, a name field asks for new names. Loading a preset with unsaved changes asks first. The "⋯" menu creates a bank, exports a bank as a zip (only the samples in use), imports a zip (never overwrites, a name clash becomes "Name (2)") and opens the folder. Presets cannot be deleted or renamed in the plugin yet. Saving a preset into an existing bank rewrites it as `bank.json` version 5, which older plugin versions then refuse to open.
- **Missing files:** when a saved project's loop file is gone, the plugin looks for a file with the same name
  and content (hash) in the folder of the other loop and the folder of the last loaded file (this session);
  regions, rules and bars are always kept. If it is not found, the lane says "File missing" and "RELINK" (or dropping a file)
  relinks it; relinking one loop also finds the other one if it was moved into the same folder.

- **Effects:** Loop 2 runs through four inserts in this order: Bitcrush, Filter, Flanger, Reverb. Each insert has one
  parameter set (the knobs in the insert panel) that is shared by all Loop-2 regions; what differs per region is
  only whether an insert is on. The switches sit in the strip under the Loop-2 lane (one marker per region and
  insert) and in the panel (select a region to see its switches). Toggling the switch of the region that sounds
  is heard immediately. Reverb and Flanger tails ring out when a region ends or the host stops. Effects apply to
  Loop 2 only; Loop 1 is dry. 1-bit Bitcrush has three levels (-1, 0, +1): material peaking below half scale
  (-6 dBFS) becomes silent. Presets and projects store the effect settings. A project saved with effects and
  re-saved by an older plugin version loses its effect settings (older versions ignore the Fx data). The bank
  format is now `bank.json` version 2; older plugin versions refuse banks saved with the new one.

- **Beat grid:** the loop lanes show a grid in quarter-note beats: faint lines per beat, stronger lines per bar. The
  number of beats per bar is the numerator of the host time signature (4 without host info), so in 6/8 the grid has
  six lines per bar. Adding and dragging region boundaries always snaps to the grid; hold Shift for free placement.
  The "Suggest" (AUTO) boundaries snap to the grid as well. The lines are hidden when they would be too dense.
- **Mix:** every rule row has a MIX control for the balance of the two loops while that Loop-2 region plays. Centre =
  both loops at full level (as before), left = the Loop-2 region gets quieter, right = Loop 1 gets quieter, far right =
  only Loop 2 is heard. The mix applies only while that Loop-2 region plays; otherwise Loop 1 is back at full level.
  Auditioning (right-click) ignores the mix. The mix is stored per rule in projects and presets. The bank format is
  now `bank.json` version 3; older plugin versions refuse banks saved with the new one, and a project with mix
  values re-saved by an older plugin version loses them.

### Playing with MIDI

The toggle in the top bar switches between Autoplay (the default: the loops follow the host transport) and MIDI.
In MIDI mode the plugin is silent until a note is held: any note on any channel gates both loops, except the
notes learned for the pass slots (see below). Holding a note
starts both loops at the next host bar, and releasing the last held note stops them at the next bar with a short
(~5 ms) fade-out; effect tails ring out. With a stopped host the loops start at once, at the last known host tempo.
If the host loops a range without a bar line, a pending start or stop happens at the loop wrap. The mode is saved per plugin instance with the
project, not in presets, so loading a preset never changes it.

### Saving passes

A pass is one run of Loop 1 from its start to its wrap. While the plugin plays, it records which Loop-2 region
(or silence) entered at which point of the pass, including the random choices as they were made. The **last
finished pass** is kept: it is the most recent pass that ran completely (a stop, a transport jump or a MIDI
gate-off in the middle makes it incomplete). The **pad bar** along the bottom edge of the window works with it: a
control strip (PLAY, SAVE and LEARN on the left, CLEAR ALL on the right) and under it eight large pads, one per
slot, each with its own colour. The editor opens at 900x660 and can be made as small as 800x600.

- **SAVE** stores the last finished pass in the lowest free of the **8 slots**. It is disabled while there is no
  finished pass, when that pass is already saved, when all 8 slots are full and while a replay is on. Only passes
  finished after the last preset or project load can be saved.
- **Pads 1 to 8:** an empty pad is black with a dim number. A filled pad shows a dim version of its colour and,
  along its bottom, a thin strip of the stored pass: one segment per Loop-2 entry, as long as that region sounds,
  with gaps where Loop 2 is silent. A pad waiting to start has an amber frame; the playing pad lights up in its full
  colour. Clicking a filled pad **repeats that pass** until something else is triggered; an empty pad does nothing.
  **PLAY** (green while on) turns on the **chain**: all filled slots in order from 1 to 8, empty ones skipped, and the chain repeats. PLAY is
  lit while a slot or the chain is on; clicking it returns to normal random playing (this is also how you leave a
  slot replay). Every switch takes effect at the next pass start, so a replay always begins at the start of Loop 1;
  until then the random playing goes on. Replayed passes are not recorded.
- **Pass starts:** a pass starts at a Loop-1 cycle start on the song timeline (song position 0, then every Loop-1
  length). A host loop that does not start on a cycle start never completes a pass (nothing can be saved), and
  replay only (re)starts at cycle starts. A transport jump or a stop during a replay leaves Loop 2 silent until the
  next pass start; if PLAY was turned off meanwhile, random playing takes over at once.
- **What a pass stores:** only which Loop-2 region (or silence) entered when, as a fraction of the pass, and its
  play mode (GATE or ONESHOT). No audio, no copies of effects or mix: these come from the current region
  settings. A slot therefore **plays what still fits**: after a tempo, bars or boundary change the entries play at
  the same point of the pass, and an entry whose Loop-2 region no longer exists (or whose loop is gone) is skipped.
- The small **x** in a filled pad's top-right corner clears it, **CLEAR ALL** clears every slot (no undo). Clearing the slot that is playing lets
  the current pass finish; then the plugin goes back to random playing (or, in the chain, to the next filled slot).
- The slots are part of the **preset**: saving a preset stores them, loading one replaces them (and turns replay
  off). A project stores them too. The bank format is now `bank.json` version 4; older plugin versions refuse
  banks saved with it, and a project with saved passes re-saved by an older plugin version loses them.
- **MIDI learn:** click **LEARN**, then click a pad, SAVE or PLAY; it shows "..." (waiting for note; the strip also
  says "Waiting for note: slot 8") until you play a MIDI note, which is then bound to it. Any note on any channel
  counts; velocity and length are ignored. A note is bound to one target only: learning it again moves it. In Learn
  mode the pads show their note in a small label bottom-right and SAVE/PLAY show it as their text (the tooltips
  name it at any time); clicking a waiting target again only cancels the wait (its note stays). To remove a note,
  **right-click** the pad or button in Learn mode. The pads' x is inactive in Learn mode. Click LEARN again to leave Learn mode (an armed learn is
  cancelled). A bound note triggers its target like a click: a slot note repeats that slot (ignored while the slot
  is empty), the PLAY note turns the chain on (only while a slot is filled) or any replay off, the SAVE note
  saves. Bound notes also act while the host bypasses the plugin. Learned notes are stored **per plugin
  instance** with the project, **not** in presets or banks, so loading a preset never changes them. Slots and
  learned notes work in both Autoplay and MIDI mode; in MIDI mode a learned note **no longer opens the gate**,
  every other note still does, and the gate still decides whether anything sounds.

### Known limitations

- Tested in a real host only on Windows (Ableton Live), and not yet with the pass slots; macOS and Linux are only
  built and unit-tested in CI.
- No undo.
- Effect parameters are not exposed for host automation, Flanger rate has no tempo sync, and a performance view is not implemented.

## Build

JUCE 8.0.15 and Catch2 are fetched by CMake on the first configure. CMake >= 3.25 and Git are required on
every platform.

### Windows

Prerequisite: Visual Studio with the "Desktop development with C++" workload.

    powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 all

Commands: `configure`, `build`, `test`, `all` (option `-Config Debug|Release`, default Release). The script wraps
the presets `windows-x64` / `windows-release`. The plugin ends up in
`build\plugin\Dubcycling_artefacts\Release\VST3\Dubcycling.vst3`.

### macOS

Prerequisites: macOS 11 or newer, Xcode command line tools, Ninja.

    cmake --preset macos-universal
    cmake --build --preset macos-release --parallel
    ctest --preset macos-release

Universal binary (arm64 + x86_64), deployment target 11.0. Plugin:
`build-macos/plugin/Dubcycling_artefacts/Release/VST3/Dubcycling.vst3`.

### Linux

Prerequisites: a C++20 compiler, Ninja and the JUCE system libraries. On Debian/Ubuntu:

    sudo apt-get install ninja-build xvfb libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
      libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev \
      libxrender-dev libcurl4-openssl-dev mesa-common-dev libgl1-mesa-dev

    cmake --preset linux-x64
    cmake --build --preset linux-release --parallel
    xvfb-run -a ctest --preset linux-release     # the GUI tests need a display; xvfb provides one

Plugin: `build-linux/plugin/Dubcycling_artefacts/Release/VST3/Dubcycling.vst3`. MP3 files are decoded with
JUCE's built-in decoder on Linux (`JUCE_USE_MP3AUDIOFORMAT`).

### CI and test status

`.github/workflows/build.yml` builds, tests and uploads the `.vst3` bundle for Windows, macOS and Linux on every
push and pull request. The macOS and Linux builds have so far only been built in CI and have not been tested on
real hardware or in real hosts. The Windows build has been tested in Ableton Live (not yet the pass slots).

## License

AGPL-3.0, see `LICENSE`.
