# Dubcycling

VST3 plugin: two stereo loops, region-based rules that decide which part of loop 2 plays when loop 1 enters a region.

Status: scaffold (empty plugin, stereo out, no input).

## Build (Windows)

    powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 all

Commands: `configure`, `build`, `test`, `all`. The plugin ends up in
`build\plugin\Dubcycling_artefacts\Release\VST3\Dubcycling.vst3`.

Requires Visual Studio (C++ tools) and CMake >= 3.25. JUCE and Catch2 are fetched by CMake.

## License

AGPL-3.0, see `LICENSE`.
