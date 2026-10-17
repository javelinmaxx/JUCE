# Character Compressor

A first working compressor plug-in with selectable **feed-forward FET**,
**feedback VCA**, **variable-mu**, and **optical** behavior, plus an optional
output transformer stage. Built with JUCE and C++17.

These are behavioral models, not measured recreations of particular hardware.
The transformer adds flux-dependent asymmetric saturation, high-frequency
shaping, and DC rejection. It currently runs at the host sample rate, so strong
drive can alias. It is a prototype for listening and refinement, not a release
claimed to match analog hardware.

## Controls and signal path

Input → stereo-linked compressor → wet/dry mix → output gain → optional
transformer → output. Bypass returns the original input, with a short transition
to avoid switching clicks. The mix's dry branch is the original input before
input gain. Output gain and transformer affect both branches of the mix.

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
  gain. Output gain can be used to drive the transformer harder as well.

All models remain running while selected model weights crossfade. Continuous
gain controls and toggle transitions are smoothed over approximately 20 ms.
Mono and stereo layouts are supported, with zero lookahead/reported latency.
Host automation and session state recall cover all 13 parameters. There is no
external sidechain, oversampling, automatic makeup gain, or preset browser yet.

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
