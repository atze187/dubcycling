# Dubcycling

VST3 plugin: two stereo loops, region-based rules that decide which part of loop 2 plays when loop 1 enters a region.
Loop 1 is the master, Loop 2 the slave. Rules can play a Loop-2 region forwards or backwards, with a mix and effects per
region, and a finished pass can be saved to one of eight slots and replayed by pad or MIDI note.

The plugin has stereo out and no audio input, so hosts list it as an instrument (put it on a MIDI track). Tested in
Ableton Live on Windows; macOS and Linux are built, unit-tested and validated in CI only.

## Documentation

The user documentation lives in the **[wiki](https://github.com/atze187/dubcycling/wiki)**:

- [Installation](https://github.com/atze187/dubcycling/wiki/Installation) and
  [Quick Start](https://github.com/atze187/dubcycling/wiki/Quick-Start)
- [User Interface](https://github.com/atze187/dubcycling/wiki/User-Interface),
  [Loops and Regions](https://github.com/atze187/dubcycling/wiki/Loops-and-Regions),
  [Rules](https://github.com/atze187/dubcycling/wiki/Rules) (probability, GATE / ONE, REV, MIX),
  [Effects](https://github.com/atze187/dubcycling/wiki/Effects)
- [Autoplay and MIDI Mode](https://github.com/atze187/dubcycling/wiki/Autoplay-and-MIDI-Mode),
  [Pass Slots](https://github.com/atze187/dubcycling/wiki/Pass-Slots) (pad bar, chain, MIDI learn)
- [Presets and Banks](https://github.com/atze187/dubcycling/wiki/Presets-and-Banks),
  [Host Integration](https://github.com/atze187/dubcycling/wiki/Host-Integration),
  [Troubleshooting](https://github.com/atze187/dubcycling/wiki/Troubleshooting)
- [Building from Source](https://github.com/atze187/dubcycling/wiki/Building-from-Source)

Binaries for Windows, macOS and Linux are on the [releases page](https://github.com/atze187/dubcycling/releases).

## Known limitations

- Tested in a real host only on Windows (Ableton Live); macOS and Linux are only built and unit-tested in CI.
- No undo.
- Effect parameters are not exposed for host automation, Flanger rate has no tempo sync, and a performance view is not
  implemented.
- Older plugin versions drop what they do not know (effects, mix, pass slots, REV) when they re-save a newer project,
  and refuse banks saved with a newer bank format (currently `bank.json` version 5).

## Build

JUCE 8.0.15 and Catch2 are fetched by CMake on the first configure. CMake >= 3.25 and Git are required on
every platform. Details, including the validators, are in the wiki page
[Building from Source](https://github.com/atze187/dubcycling/wiki/Building-from-Source).

### Windows

Prerequisite: Visual Studio with the "Desktop development with C++" workload.

    powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 all

Commands: `configure`, `build`, `test`, `validate`, `all` (option `-Config Debug|Release`, default Release). The script
wraps the presets `windows-x64` / `windows-release`; `validate` runs the Steinberg VST3 validator and pluginval
(strictness level 5). The plugin ends up in `build\plugin\Dubcycling_artefacts\Release\VST3\Dubcycling.vst3`.

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
      libxrender-dev libcurl4-openssl-dev mesa-common-dev libgl1-mesa-dev libgtk-3-dev libgtkmm-3.0-dev

    cmake --preset linux-x64
    cmake --build --preset linux-release --parallel
    xvfb-run -a ctest --preset linux-release     # the GUI tests need a display; xvfb provides one

Plugin: `build-linux/plugin/Dubcycling_artefacts/Release/VST3/Dubcycling.vst3`. MP3 files are decoded with
JUCE's built-in decoder on Linux (`JUCE_USE_MP3AUDIOFORMAT`).

### CI and releases

`.github/workflows/build.yml` builds, tests and uploads the `.vst3` bundle for Windows, macOS and Linux on every
push and pull request. `.github/workflows/release.yml` runs on a `v*` tag: it builds, tests and validates (Steinberg
validator and pluginval) on every platform and publishes `Dubcycling-<tag>-{win64,macos,linux}.zip`.

## License

AGPL-3.0, see `LICENSE`.
