# HumRig

Vocal tune + mix plugin (AU / VST3) for **DaVinci Resolve Fairlight** on macOS.
Built for raw hummed / sung phone recordings: auto-tune (key + scale) and a full vocal chain in nine tabs
(Home, Clean, EQ, Compress, De-ess, Color, Space, Stereo, Master): expander, 6-band EQ with live curve, full compressor,
band de-esser, saturation + exciter, reverb/delay with pre-delay, filters and ducking, doubler/width, look-ahead limiter and LUFS meter, plus presets.

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
no clicks / NaNs, mix chain, presets and state round-trip, editor snapshots of every tab,
and per-module checks for the limiter, BS.1770 loudness, EQ bands, compressor, de-esser, expander, saturation, space and stereo tools.
A golden regression (Linux) proves the v0.2 sound is unchanged when the new controls are at their defaults.

## Licence note
JUCE is AGPLv3 / commercial. Fine for a public open-source repo; get a JUCE licence before selling binaries.
