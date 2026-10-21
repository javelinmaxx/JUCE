# Multi-Compressor

A JUCE-based audio plug-in repository with YinYang Compressor, SHUN DE-ESS,
Kuo Stereo, Zao Drive, Ren Transformer, and Kuo Velvet. YinYang provides feed-forward FET, feedback VCA, variable-mu,
and optical compression models, optional transformer coloration, and a
high-frequency sidechain tilt. Each project builds as VST3 for Windows and
macOS, and as an Audio Unit (AU) for macOS.

- Four selectable compressor models with stereo-linked gain reduction, plus
  an optional upward mode that boosts signal below threshold while leaving
  above-threshold signal at unity compressor gain.
- Optional output transformer with 4× oversampling, causal magnetic hysteresis,
  frequency-dependent phase response, and a 500 Hz / 12 dB-per-octave roll-off
  on generated distortion. The response uses a 20 Hz low corner and a 19 kHz,
  Q=2 high pole pair.
- Optional **SC High Tilt**: a +6 dB detector high shelf with a 1.5 kHz midpoint
  (+3 dB). It increases high-frequency compression sensitivity without directly
  EQing the output.
- Input/output gain, threshold, ratio, attack, release, knee, wet/dry mix,
  detector high-pass filter, transformer drive, and bypass.
- Mono/stereo operation, host automation, saved settings, and a fixed 64-sample
  latency reported to the host.
- Optional Mid/Side encoding before compression, with a Stereo Link control from
  independent channel detection (0%) to fully linked detection (100%).

The models are behavioral approximations for creative use, not calibrated
recreations of specific hardware. The plug-in currently appears in hosts as
**YinYang Compressor**, under **J-SONIC**. The build target names retain the
internal `CharacterCompressor` identifier.

## Compressor styles

The four modes share the same controls, but use different detection, compression
curves, and recovery behavior. Matching the knob positions will not necessarily
produce the same gain reduction or timing. These emulations focus on dynamics;
the optional transformer provides a separate coloration stage for any mode.

### FET — fast feed-forward control

FET compressors take their name from the field-effect transistor used for gain
control in analog designs. This mode uses a peak detector that reads the input
before gain reduction, with a fast attack and comparatively quick release.
It is a useful starting point for catching drum transients, controlling an
energetic vocal, or adding density with parallel compression. Use a slower
attack setting when you want more of the initial hit to pass through.

### VCA — feedback control

VCA refers to a voltage-controlled amplifier. This mode models a feedback
control loop: the detector senses the compressed level, so gain reduction
influences the next control decision. Peak detection and a longer recovery
than the FET mode provide another way to balance transient control and sustain.
Try it on drum groups, bass, or a mix bus when you want the dynamics of several
parts to move together; start with a modest ratio and light gain reduction.

### Variable-mu — progressive compression

Variable-mu compressors draw inspiration from tube circuits whose gain changes
with the control signal. Here, an RMS detector follows average signal energy
in a feedback loop, and compression becomes progressively stronger as the
level rises above threshold. The Ratio control sets an upper limit rather than
a fixed ratio throughout the curve. Recovery also slows with deeper gain
reduction. Try this mode for gentle bus control, sustained instruments, or
vocals where a gradual change in compression strength is useful.

### Optical — smooth leveling with recovery memory

Optical compressors are inspired by a light source and light-sensitive element
controlling gain. This mode uses RMS detection, a slower attack, and two
recovery components: one responds relatively quickly, while the other retains
memory of earlier compression. It is a useful starting point for leveling
vocals, bass, and acoustic instruments while allowing some initial articulation
through. Closely spaced notes can keep the slower recovery component engaged.

## Get the source and build scripts

This repository includes the JUCE framework and six independent plug-in
projects: [`projects/CharacterCompressor`](projects/CharacterCompressor),
[`projects/ShunDeEss`](projects/ShunDeEss),
[`projects/KuoStereo`](projects/KuoStereo),
[`projects/ZaoDrive`](projects/ZaoDrive),
[`projects/RenTransformer`](projects/RenTransformer), and
[`projects/KuoVelvet`](projects/KuoVelvet). Zao Drive models a common-emitter BJT
amplifier, an op-amp feedback diode clipper, a diode shunt clipper, and
series-diode crossover distortion, with an expected-output display
and YinYang's optional transformer stage.
[`Ren Transformer`](projects/RenTransformer/README.md) isolates YinYang's transformer
and output controls, and [`Kuo Velvet`](projects/KuoVelvet/README.md) isolates
Kuo Stereo's Velvet widening with spectral transient protection and band levels.
All six projects share an analog visual style while keeping independent processors and
build targets. You do not need another JUCE download. All build commands below
run from the repository root.

Build SHUN DE-ESS, Kuo Stereo, and Zao Drive with CMake using their
[SHUN](projects/ShunDeEss/README.md), [Kuo](projects/KuoStereo/README.md), and
[Zao](projects/ZaoDrive/README.md)
instructions. The platform scripts below build YinYang Compressor; the Windows
GitHub workflow builds YinYang Compressor and SHUN DE-ESS.

Download the development branch with Git:

```bash
git clone --branch feature/character-compressor https://github.com/javelinmaxx/Multi-Compressor.git
cd Multi-Compressor
```

Build requirements common to all platforms: **CMake 3.22 or newer**, Git, and
an appropriate C++17 compiler. The commands below build Release plug-ins and
disable the test targets. Each platform needs its own binary; a macOS VST3
cannot load on Windows.

Build scripts are included at the repository root:

| Script | Builds |
| --- | --- |
| [`build-windows-vst3.ps1`](build-windows-vst3.ps1) | Windows x64 VST3 |
| [`build-macos.sh`](build-macos.sh) | macOS VST3, AU, or both; Apple Silicon, Intel, or universal |

Both scripts locate the project relative to their own location, stop if a build
fails, and print the finished bundle paths. They do not run test suites. Build
directories are excluded from Git.

## 1. Build Windows VST3

### Build on Windows

Install **Visual Studio 2022** with the **Desktop development with C++** workload,
including the MSVC compiler, Windows SDK, and CMake tools. Open **Developer
PowerShell for VS 2022**, navigate to the repository root, and run:

```powershell
.\build-windows-vst3.ps1
```

Use `-Parallel 8` to change the job count. If your PowerShell policy blocks the
script, you can allow it for this invocation only:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-windows-vst3.ps1
```

The equivalent direct CMake commands are:

```powershell
cmake -S projects/CharacterCompressor -B build-character-win -G "Visual Studio 17 2022" -A x64 -DCHARACTER_BUILD_TESTS=OFF '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>'

cmake --build build-character-win --config Release --target CharacterCompressor_VST3 --parallel 4
```

The complete Windows x64 plug-in bundle is:

```text
build-character-win\CharacterCompressor_artefacts\Release\VST3\YinYang Compressor.vst3
```

Copy the entire `.vst3` folder, including its contents, into:

```text
C:\Program Files\Common Files\VST3\
```

Windows may request administrator permission. Restart your Windows DAW and
rescan its plug-ins. The build links the MSVC runtime statically.

### Build with GitHub Actions

A Windows computer is not required to compile either plug-in. GitHub builds
YinYang Compressor and SHUN DE-ESS in separate Windows jobs:

1. Open [Windows VST3 builds](https://github.com/javelinmaxx/Multi-Compressor/actions/workflows/character-compressor-windows.yml).
2. Enable Actions if GitHub prompts you, then open a successful run for the branch.
3. Under **Artifacts**, download **YinYang-Compressor-Windows-x64-VST3** and
   **SHUN-DE-ESS-Windows-x64-VST3**.
4. Extract each ZIP. Install both complete `.vst3` folders in the Windows
   plug-in location above.

Source/configuration pushes to `feature/character-compressor` or `master`
automatically trigger the workflow. Once the workflow is on the repository's
default branch, **Run workflow** also lets you start a build manually. You can
rerun an existing run using **Re-run all jobs**. Artifacts are retained for
30 days. Testing the resulting plug-in still requires a Windows host.

## 2. Build macOS VST3

Install **Xcode** and open it once to finish setup and accept its licence.
Install **CMake 3.22 or newer** compatible with your macOS version. Ensure
`cmake` is available in Terminal. If needed, select your Xcode installation:

```bash
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
```

For an **Apple Silicon Mac**, run from the repository root:

```bash
./build-macos.sh vst3 --arch arm64 --install
```

This builds and installs the VST3 in your user plug-in folder. Omit `--install`
to build without copying it. Use `--arch x86_64` for Intel or `--arch universal`
for both architectures. Run `./build-macos.sh --help` for all options.

To build manually instead:

```bash
cmake -S projects/CharacterCompressor -B build-character-mac-arm64 -G Xcode \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCHARACTER_BUILD_TESTS=OFF

cmake --build build-character-mac-arm64 --config Release \
  --target CharacterCompressor_VST3 --parallel 4
```

Install the complete bundle into your user plug-in folder:

```bash
mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3"
ditto \
  "build-character-mac-arm64/CharacterCompressor_artefacts/Release/VST3/Character Compressor.vst3" \
  "$HOME/Library/Audio/Plug-Ins/VST3/Character Compressor.vst3"
```

Restart your VST3 host or rescan its plug-ins. For DawDreamer, provide the
absolute path to the `.vst3` bundle and use Python with the same architecture
as the plug-in. Logic Pro uses the AU version described below.

**Intel Mac:** substitute `x86_64` for `arm64`, use a separate directory such as
`build-character-mac-intel`, and update the build/install paths accordingly.
For a Catalina target, set `CMAKE_OSX_DEPLOYMENT_TARGET=10.15`; building directly
on Catalina requires a compatible toolchain (JUCE documents Xcode 12.4 and
macOS 10.15.4 as minimum build requirements). Verify on the target Mac.

**Universal build:** with a compatible Xcode SDK, set
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`, keep the deployment target at `11.0`,
and use a separate build directory. This produces a bundle for both architectures
on macOS 11 or later. Do not reuse an Intel build directory for an arm64 build.

## 3. Build macOS AU

Use the same Xcode and CMake requirements as the macOS VST3 build. For an
**Apple Silicon Mac**, run from the repository root:

```bash
./build-macos.sh au --arch arm64 --install
```

To build and install **both AU and VST3** together, use:

```bash
./build-macos.sh all --arch arm64 --install
```

To build the AU manually instead:

```bash
cmake -S projects/CharacterCompressor -B build-character-mac-arm64 -G Xcode \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCHARACTER_BUILD_TESTS=OFF

cmake --build build-character-mac-arm64 --config Release \
  --target CharacterCompressor_AU --parallel 4
```

Install the complete `.component` bundle:

```bash
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components"
ditto \
  "build-character-mac-arm64/CharacterCompressor_artefacts/Release/AU/Character Compressor.component" \
  "$HOME/Library/Audio/Plug-Ins/Components/Character Compressor.component"
```

The Intel and universal architecture substitutions from the macOS VST3 section
apply here as well. AU and VST3 can share a build directory when their
architecture and deployment settings match.

Restart Logic Pro and check **Plug-in Manager**. On an audio track, select
**Audio FX → Audio Units → J-SONIC → YinYang Compressor**.
To run Apple's AU validator separately:

```bash
auval -v aufx Chc1 Chra
```

---

Further information: [plug-in documentation](projects/CharacterCompressor/README.md),
[development log](projects/CharacterCompressor/DEVELOPMENT_LOG.md), and
[distortion/sidechain measurements](projects/CharacterCompressor/Measurements/output_stage/README.md).

Built with [JUCE](https://juce.com/). JUCE modules are available under AGPLv3 or
commercial terms; a commercial JUCE licence may be required for distribution.
See [LICENSE.md](LICENSE.md) for the framework licence and dependency details.
