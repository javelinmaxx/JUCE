# Multi-Compressor

Seven J-SONIC audio plugins for dynamics, de-essing, stereo width, overdrive,
transformer coloration, and phase alignment. Built with JUCE and C++17, with VST3 and Standalone
targets on Windows, macOS, and Linux, plus Audio Units on macOS.

The editors support 50%–120% resizing, one-sentence hover hints, host automation,
and session recall. Each plugin is an independent project under `projects/`;
the JUCE framework is included in this repository.

[Plugin collection](#plugin-collection) · [Measured examples](#measured-examples) ·
[Build and install](#build-and-install) · [Detailed build guide](docs/BUILDING.md)

## Plugin collection

| Plugin | Purpose | Main features |
| --- | --- | --- |
| [YinYang Compressor](projects/YinyangCompressor/README.md) | Dynamics | Four compressor models, Yin/Yang operation, sidechain shaping, transformer color |
| [SHUN DE-ESS](projects/ShunDeEss/README.md) | Sibilance control | Four attenuation modes, relative detection, separate detector and attenuator tuning |
| [Kuo Stereo](projects/KuoStereo/README.md) | Stereo widening | Adaptive Haas delay, Velvet convolution, spectral transient protection, three-band sound-field meters |
| [Zao Drive](projects/ZaoDrive/README.md) | Overdrive | Four circuit models, Smart harmonic shaping, Clear contour, waveform preview, Auto compensation |
| [Ren Transformer](projects/RenTransformer/README.md) | Transformer coloration | YinYang's transformer stage in a compact four-knob interface |
| [Kuo Velvet](projects/KuoVelvet/README.md) | Convolution widening | Kuo's Velvet stage with Width, Transient, Attack, and Output controls |
| [Xuan Phase](projects/XuanPhase/README.md) | Phase rotation | Independent low/high rotation, 25 ms lookahead, spectral transient alignment and Auto Depth |

### YinYang Compressor

![YinYang Compressor in FET mode, showing dynamics controls, gain-reduction history, sidechain, and transformer sections](projects/YinyangCompressor/Previews/fet.png)

Choose **FET**, **VCA**, **Variable-mu**, or **Optical** behavior to shape attacks,
sustain, and recovery. **Yang** reduces levels above threshold; **Yin** raises
quieter material toward it.

- Tune Threshold, Ratio, Knee, Attack, and Release, with **VariTransient** to
  adapt timing as downward gain reduction increases.
- Shape the detector with a high-pass filter and **SC High Tilt**; use stereo
  linking and Mid/Side operation to control the stereo response.
- Add optional oversampled transformer coloration with Core Drive and DC Bias,
  while monitoring the gain meter and scrolling history.

[Controls and signal path](projects/YinyangCompressor/README.md) ·
[Measurements](projects/YinyangCompressor/Measurements/README.md)

### SHUN DE-ESS

![SHUN DE-ESS with Relative detection, separate detector and attenuator controls, a response plot, and gain-reduction meter](projects/ShunDeEss/Previews/shun-relative.png)

Control sibilance with separate tuning for what the plugin detects and what it
attenuates. The response plot shows both curves beside the gain-reduction display.

- **Wideband** reduces the full signal; **Split-band** concentrates attenuation
  around the selected band.
- **Brilliant** restores detected spectral detail after smart attenuation;
  **Smooth** reverses that restoration to suppress peaks further.
- Select **Absolute** detection for a fixed threshold, **Listen** to audition
  the detector band, or **Relative** to compare its energy with the full signal.

[Controls and modes](projects/ShunDeEss/README.md) ·
[Detector and attenuator measurements](projects/ShunDeEss/Tests/Reports/detector-attenuator-mock-report.md)

### Kuo Stereo

![Kuo Stereo showing Smart Haas controls, Velvet Width controls, and low, mid, and high sound-field meters with band sliders](projects/KuoStereo/Previews/kuo-stereo.png)

Combine content-adaptive timing with convolution texture, then inspect the
resulting stereo field across three frequency bands.

- **Smart Haas** chooses signed delay times in 0.2 ms steps within Center Delay
  ± Variability, scoring the mid spectrum and useful side energy. Max Rate
  limits delay movement; zero Variability switches the search off to save CPU.
- **Dynamic** shortens Haas delay around detected transients. **Velvet Width**
  adds sparse convolution texture, with spectral **Transient** protection and
  adjustable **Attack** to keep onsets focused.
- Three goniometers show low, mid, and high stereo activity. Their adjacent
  0%–200% sliders set how much of each band feeds Velvet.

[Controls and processing](projects/KuoStereo/README.md) ·
[Audio examples and measurements](projects/KuoStereo/Measurements/Report/REPORT.md)

### Zao Drive

![Zao Drive showing circuit selection, Smart and Clear buttons, a triangle-wave output preview, transformer controls, and Auto output compensation](projects/ZaoDrive/Previews/zao-auto.png)

Explore **BJT common-emitter**, **op-amp feedback**, **Schottky diode shunt**,
and **series-diode crossover** overdrive. The first knob follows the circuit:
Bias for BJT, Input for op-amp, and Knee for the two passive diode modes.

- **Smart** analyzes generated harmonics against the input using a 12-TET
  dissonance model, then filters selected wet components. It updates at up to
  **20 Hz**, with **50 ms sigmoid crossfades** between filter banks.
- **Clear** applies a linear-phase 80-phon equal-loudness contour shifted down
  an octave and lowered so it adds no positive gain. Tone and Mix shape the
  separate driver-minus-reference wet residual.
- **Expected Output** renders a 1 kHz triangle through the current processing.
  **Auto** derives fixed RMS compensation from that reference; Output adds or
  subtracts gain from the matched level. An optional YinYang transformer adds
  another stage of coloration.

[Circuit models](projects/ZaoDrive/CIRCUITS.md) ·
[Smart signal path](projects/ZaoDrive/SMART.md) ·
[Auto measurements](projects/ZaoDrive/Tests/Reports/auto-report.md)

### Ren Transformer

![Ren Transformer's compact panel with Core Drive, DC Bias, Mix, and Output knobs and a hover hint](projects/RenTransformer/Previews/ren-transformer-tooltip.png)

Use YinYang's transformer independently: **Core Drive** sets saturation,
**DC Bias** introduces asymmetry, **Mix** blends coloration, and **Output**
sets the level entering the transformer. The shared stage uses 4× oversampling,
magnetic hysteresis, and a frequency-dependent transformer response.

[Controls and signal path](projects/RenTransformer/README.md) ·
[Processor checks](projects/RenTransformer/Tests/REPORT.md)

### Kuo Velvet

![Kuo Velvet's compact panel with Width, Transient, Attack, and Output knobs and a hover hint](projects/KuoVelvet/Previews/kuo-velvet-tooltip.png)

Add Kuo's convolution width with four controls: **Width**, **Transient**,
**Attack**, and **Output**. Width scales the added side signal while preserving
the input mid before output gain. The shared spectral onset detector protects
attacks; this compact version omits Haas search, meters, and band controls.

[Controls and processing](projects/KuoVelvet/README.md) ·
[Processor checks](projects/KuoVelvet/Tests/REPORT.md)

### Xuan Phase

![Xuan Phase with independent Low Phase and High Phase knobs, Crossover, Auto Depth, Transient, and Stereo Link](projects/XuanPhase/Previews/xuan-phase.png)

Rotate the low and high frequencies independently with a combined ±150° cap and a smooth
adjustable crossover, **25 ms correction lookahead**, and **50 ms** total latency. **Auto** detects
clear transients and uses their spectrogram phase and phase slopes to align
narrow frequency bands; signed **Auto Depth** scales or reverses that correction
independently of the manual settings, and **Transient** sets its release time.
**Stereo Link** blends from shared correction through independent channel
processing to opposing transient shifts.
The side spectrogram moves left to right, colouring early bands green and late
bands red, with white left-channel and gold right-channel traces for applied phase.

[Controls and signal path](projects/XuanPhase/README.md) ·
[Phase and transient measurements](projects/XuanPhase/Tests/Reports/REPORT.md) ·
[Auto alignment performance](projects/XuanPhase/Tests/Reports/AUTO_ALIGNMENT.md)

## Measured examples

### Four approaches to compression

![Measured YinYang compression curves comparing FET, VCA, Variable-mu, and Optical models at the same settings](projects/YinyangCompressor/Measurements/plots/01_compression_curves.png)

At the same threshold, ratio, knee, and timing settings, the four models produce
different transfer and gain-reduction curves. These measurements use 48 kHz,
a −18 dBFS threshold, 4:1 ratio, and 6 dB knee, with the detector HPF and
transformer off. The [measurement report](projects/YinyangCompressor/Measurements/README.md)
also covers attack, recovery, upward compression, and transformer harmonics.

### Velvet widening a mono signal

![Left-right scatter plots showing a dry mono signal as a diagonal line and a wider stereo field with Velvet at 100 percent and Haas removed](projects/KuoStereo/Measurements/Report/mono-velvet-only.png)

The dry mono signal has identical left and right channels, forming a diagonal
line. With Velvet at 100%, its added side signal spreads the stereo field.
Haas is removed in this test, isolating the convolution's contribution. The
[feature report](projects/KuoStereo/Measurements/Report/REPORT.md) includes
transient tests, adaptive Haas measurements, and reproducible audio examples.

The models are developed for creative audio processing. Individual project
documents describe their equations, assumptions, and measurement conditions.

## Build and install

Use **CMake 3.22+**, a **C++17 compiler**, and the platform's JUCE dependencies.
The source includes JUCE; no separate framework checkout is needed. Start from
the repository root:

```bash
git clone --branch master https://github.com/javelinmaxx/Multi-Compressor.git
cd Multi-Compressor
```

**macOS — build all seven plugins as VST3 and AU:** install full Xcode and CMake,
then run:

```bash
./build-macos-all.sh
```

Use `--arch universal` for Apple Silicon and Intel, or add `--install` to copy
the completed bundles into your user plugin folders. The script builds without
installing by default. See `./build-macos-all.sh --help` for all options.

**Windows — build VST3:** the manual
[All Plugins - Windows VST3 workflow](https://github.com/javelinmaxx/Multi-Compressor/actions/workflows/all-plugins-windows.yml)
packages all seven plugins, including Xuan Phase. For a local YinYang build, use
[`build-windows-vst3.ps1`](build-windows-vst3.ps1) with Visual Studio 2022's C++
tools. The [build guide](docs/BUILDING.md#windows-vst3) covers local commands,
workflow artifacts, and installation.

**One project — CMake:** for example, build Zao Drive's native VST3:

```bash
cmake -S projects/ZaoDrive -B build/zao -DCMAKE_BUILD_TYPE=Release -DZAO_BUILD_TESTS=OFF
cmake --build build/zao --config Release --target ZaoDrive_VST3 --parallel 4
```

The complete bundle is under `build/zao/ZaoDrive_artefacts/Release/VST3/`.
Use each project's own target name when building another plugin. Linux builds
require the [JUCE system dependencies](docs/Linux%20Dependencies.md).

| Platform | Plugin installation folder |
| --- | --- |
| Windows VST3 | `C:\Program Files\Common Files\VST3\` |
| macOS VST3 | `~/Library/Audio/Plug-Ins/VST3/` |
| macOS AU | `~/Library/Audio/Plug-Ins/Components/` |
| Linux VST3 | `~/.vst3/` |

Copy the entire `.vst3` or `.component` bundle, then rescan plugins in your DAW.
Build a binary for the host's operating system and architecture.

[Full build and installation instructions](docs/BUILDING.md) include architecture
options, individual project targets, manual CMake commands, and AU validation.
Each project's README lists its processor tests, measurement tools, and editor
preview commands.

## License

Built with [JUCE](https://juce.com/). JUCE modules are available under AGPLv3 or
commercial terms; see [LICENSE.md](LICENSE.md) for the framework license and
dependency details.
