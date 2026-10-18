# Character Compressor

A first working compressor plug-in with selectable **feed-forward FET**,
**feedback VCA**, **variable-mu**, and **optical** behavior, plus an optional
output transformer stage. Built with JUCE and C++17.

These are behavioral models, not measured recreations of particular hardware.
The transformer adds flux-dependent saturation. Its generated distortion is
isolated as a nonlinear residual, filtered by a **500 Hz fourth-order Butterworth
low-pass (24 dB/octave)**, and added back to the clean signal. This preserves clean
high frequencies while reducing harmonics above 500 Hz; the cutoff is gradual.
The combined signal then passes through a causal loaded
transformer response: an 8 Hz high-pass models low-frequency phase lead and
droop, while a 22 kHz second-order low-pass (Q 0.8) models high-frequency phase
lag from leakage inductance and winding capacitance. These are provisional
design values. The transformer runs at fixed 2× the host sample rate, with
FIR interpolation and decimation filters. This is intended to reduce aliasing;
strong drive can still produce residual aliases. It is a prototype for listening and refinement, not a release
claimed to match analog hardware.

## Controls and signal path

Input → stereo-linked compressor → wet/dry mix → output gain → optional
transformer → output. Bypass returns the original input delayed by the same 64 samples as the
processing path, with a short transition to avoid switching clicks. The mix's dry branch is the original input before
input gain. Output gain and transformer affect both branches of the mix.

The internal detector branch is input gain → detector HPF → optional High Tilt
→ rectification/stereo linking → compression control. High Tilt affects all
four models through this branch; it does not EQ the audio output.

- **FET:** fast peak feed-forward control. Attack time is scaled to one quarter
  of the common attack setting; release is scaled to 70%.
- **VCA:** peak feedback control, sensing the compressed signal before output
  gain. Compensated control slope gives the selected steady-state ratio.
- **Variable-mu:** RMS feedback, progressively increasing ratio, and recovery
  that slows as gain reduction increases. Ratio is a maximum, not a fixed slope.
- **Optical:** RMS feed-forward control with a slow recovery memory in addition
  to the main envelope. Attack is twice the common attack setting.
- **Detector HPF:** filters only the compression detector. Set to 0 Hz to disable.
- **Transformer drive:** increases saturation without increasing small-signal
  gain. Generated distortion rolls off above 500 Hz. The existing 0–18 dB
  drive range and 6 dB default are retained. Output gain can drive the transformer harder as well.
- **SC High Tilt:** sidechain high shelf, off by default. The 1.5 kHz
  midpoint has +3 dB gain, rising smoothly to +6 dB at high frequencies (shelf
  slope S = 1). It follows the detector HPF and increases high-frequency
  compression sensitivity. It changes the output only through gain reduction,
  with no direct effect at ratio 1:1. Switching crossfades the detector signal
  over approximately 20 ms. Older sessions restore High Tilt as off.

All models remain running while selected model weights crossfade. Continuous
gain controls and toggle transitions are smoothed over approximately 20 ms.
Mono and stereo layouts are supported, with no lookahead and a fixed
64-sample latency (1.33 ms at 48 kHz), reported to the host for compensation.
The transformer, clean output, and bypass paths retain this latency even when
the transformer is disabled. Both resampling filters have 129 taps; only the
transformer stage runs at 2×, and its filter coefficients use that higher rate.
Host automation and session state recall cover all 14 parameters. Existing
parameter IDs/order are retained; High Tilt is appended with AU version hint 2. There is no
external sidechain, oversampling selector, automatic makeup gain, or preset browser yet.

## Measured characteristics

See [the measurement report](Measurements/README.md) for compression curves,
attack/recovery plots, transformer harmonics and frequency response, and the
previous 1× high-drive aliasing measurements. Those plots are the baseline
from before the 2× change, not a validation of the new code. Figures are available
as PNG, SVG, and PDF. Per the user’s request, the test suites and measurement
sweep were not rerun after the 2× implementation.

The subsequent [square-wave phase verification](Measurements/phase/README.md)
records the previous 2× transformer revision with 50 Hz and 2 kHz squares. At 48 kHz,
measured phase is +8.96° at 50 Hz and −65.93° at 17.95 kHz after removing
the fixed FIR delay. Focused checks also pass at 44.1 and 96 kHz; the full
test suites remain unrerun.

The latest [output-stage measurements](Measurements/output_stage/README.md)
verify the 500 Hz distortion roll-off and High Tilt at 44.1, 48, and 96 kHz.
At −12 dBFS peak and 6 dB drive, THD falls from 2.14% at 100 Hz to 0.0221%
at 500 Hz and 0.00142% at 1 kHz. These tests use compression 1:1 and High Tilt off.
The detector shelf measures +3.00 dB at 1.5 kHz and +6.00 dB at 10 kHz.
At ratio 1:1, toggling High Tilt leaves the output sample-identical with the
transformer both on and off. With compression active it increases HF gain
reduction in all four models.

The user confirmed that the preceding VST3 and AU builds work on their Mac.
The new output-stage revision still needs a macOS rebuild and listening check.

## Build and test in this cloud workspace

From the JUCE repository root:

```bash
. /workspace/juce-env/activate.sh
cmake -S projects/CharacterCompressor -B /workspace/juce-env/character-build \
    -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /workspace/juce-env/character-build \
    --target CharacterCompressor_VST3 CharacterDSPTests CharacterProcessorTests --parallel 4
ctest --test-dir /workspace/juce-env/character-build --output-on-failure
```

The optional native editor check needs a display. In this workspace:

```bash
mkdir -p /workspace/juce-env/cache
XDG_CACHE_HOME=/workspace/juce-env/cache xvfb-run -a \
    /workspace/juce-env/character-build/CharacterProcessorTests --editor
```

Linux artifact:
`/workspace/juce-env/character-build/CharacterCompressor_artefacts/Debug/VST3/Character Compressor.vst3`.
Linux VST3 binaries cannot load in Logic on macOS.

## Build VST3 and AU for the current Apple Silicon Mac

Use a current compatible Xcode installation and CMake 3.22 or newer. From the
JUCE repository root, use a separate build directory from any Intel build:

```bash
cmake -S projects/CharacterCompressor -B build-character-arm64 -G Xcode \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCHARACTER_BUILD_TESTS=OFF
cmake --build build-character-arm64 --config Release \
    --target CharacterCompressor_VST3 CharacterCompressor_AU --parallel 4
```

Bundles appear in `build-character-arm64/CharacterCompressor_artefacts/Release/`
under `VST3/Character Compressor.vst3` and `AU/Character Compressor.component`.
Replace your installed copies with these complete bundles and restart the host.
DawDreamer requires an absolute bundle path and matching Python/plug-in architecture.
Logic loads the AU. These commands build without running tests.

## Build an Intel Audio Unit for Catalina / Logic Pro X 10.5.7

Use a Mac with Xcode command-line tools and CMake 3.22 or newer. On Catalina,
use an Xcode version supported by both the OS and JUCE (the checkout documents
Xcode 12.4 as its minimum). A newer build Mac can also target Catalina; verify
the resulting binary on the actual target Mac. The macOS path is provided but
cannot be tested from this Linux workspace.

From the JUCE repository root, with this project present:

```bash
cmake -S projects/CharacterCompressor -B build-character-mac -G Xcode \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15
cmake --build build-character-mac --config Release \
    --target CharacterCompressor_AU
```

The AU is at:
`build-character-mac/CharacterCompressor_artefacts/Release/AU/Character Compressor.component`.
To install a new development build in your user plug-in folder:

```bash
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components"
ditto "build-character-mac/CharacterCompressor_artefacts/Release/AU/Character Compressor.component" \
    "$HOME/Library/Audio/Plug-Ins/Components/Character Compressor.component"
auval -v aufx Chc1 Chra
```

Restart Logic and check Plug-in Manager. Add **Audio FX → Audio Units →
Character Audio → Character Compressor** to a mono or stereo audio track.
Start with low monitor levels, transformer off, output at 0 dB, and adjust
threshold while watching gain reduction. Confirm bypass, all four models,
transformer, automation, session save/reopen, and mono/stereo playback.

If building this folder separately, provide the JUCE checkout with
`-DJUCE_SOURCE_DIR=/path/to/JUCE`. Provisional manufacturer `Chra`, plug-in
code `Chc1`, and bundle ID `audio.character.compressor` should be finalized
before distributing builds; changing them later can break session identity.

## Development records and licensing

See [DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md) for requirements, decisions, actual
validation results, and remaining work. JUCE modules are licensed under AGPLv3
or commercial terms. A commercial JUCE licence may be required for distribution;
see the repository's [LICENSE.md](../../LICENSE.md).
