# HumRig

Vocal tune + mix plugin (AU / VST3) for **DaVinci Resolve Fairlight** on macOS.
Built for raw hummed / sung phone recordings: auto-tune (key + scale), EQ, compressor with GR meter,
de-esser, drive, reverb, delay and presets, in one window.

- Persian install & usage guide: [INSTALL_FA.md](INSTALL_FA.md)
- Built with JUCE 8 and Signalsmith Stretch (MIT). Pitch detection: YIN.

## Build
macOS builds are produced by GitHub Actions (`.github/workflows/build-macos.yml`): push to `main`, open the
run → *Artifacts* → `HumRig-macOS`.

Local (macOS or Linux):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target HumRigTests -j4
./build/HumRigTests_artefacts/Release/HumRigTests ./test-out     # headless DSP + processor tests, writes WAVs and editor.png
cmake --build build --target HumRig_VST3 HumRig_AU -j4           # AU is macOS-only
```

## Tests (`tests/Harness.cpp`)
Pitch detector accuracy, scale snapping, end-to-end correction of off-key synthetic hums (steady + glide, with noise),
Flex behaviour, latency == `setLatencySamples`, bypass latency equality, mono/stereo layouts, block sizes 1..2048,
no clicks / NaNs, mix chain, presets and state round-trip, editor snapshot.

## Licence note
JUCE is AGPLv3 / commercial. Fine for a public open-source repo; get a JUCE licence before selling binaries.
