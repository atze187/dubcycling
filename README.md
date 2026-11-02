# Dubcycling

VST3 plugin: two stereo loops, region-based rules that decide which part of loop 2 plays when loop 1 enters a region.

Status: scaffold (empty plugin, stereo out, no input).

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

    sudo apt-get install ninja-build xvfb libasound2-dev libjack-jackd2-dev libfreetype-dev \
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
