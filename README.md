# Dubcycling

VST3 plugin: two stereo loops, region-based rules that decide which part of loop 2 plays when loop 1 enters a region.

Status: edit view and audio engine implemented (stereo out, no input); not yet tested in a real host.

## Usage

- **Load loops:** "Load…" next to Loop 1 / Loop 2, or drag and drop an audio file (wav, mp3, flac, aif, aiff)
  onto a lane. Replacing a loop that has regions or rules asks first (its regions and all rules are deleted).
- **Bars ("Takte"):** length of each loop in bars; suggested from the file length and the host tempo on load.
  The combined cycle ("Zyklus") is the least common multiple of both.
- **Regions:** double-click in a lane adds a region boundary, double-click on a boundary removes it, drag a
  boundary to move it. Rules move with their regions when a boundary is added or removed (merging two regions
  keeps the left region's rule).
- **"Vorschlag":** suggests boundaries at transients (meant for drum loops); replaces the lane's boundaries
  after a confirmation.
- **Rules:** select a Loop-1 region (click) to edit its rule rows, one per Loop-2 region: the slider is the
  probability that this Loop-2 region plays when Loop 1 enters the selected region, the GATE/ONESHOT toggle
  sets the play mode (GATE repeats until Loop 1 leaves the region, ONESHOT plays the region once). "Stille"
  shows the remainder (probability that nothing new plays).
- **Missing files:** when a saved project's loop file is gone, the plugin looks for a file with the same name
  and content (hash) in the folder of the other loop and the folder of the last loaded file (this session);
  regions, rules and bars are always kept. If it is not found, the lane says "Datei fehlt" and "Neu zuordnen…" (or dropping a file)
  relinks it; relinking one loop also finds the other one if it was moved into the same folder.

### Known limitations

- Not tested in a real host yet on any platform; macOS and Linux are only built and unit-tested in CI.
- No undo.
- The effects slot and a performance view are not implemented.

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
real hardware or in real hosts. The Windows build has not been tested in a real host either.

## License

AGPL-3.0, see `LICENSE`.
