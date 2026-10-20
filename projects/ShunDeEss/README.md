# SHUN DE-ESS

SHUN DE-ESS is a J-SONIC de-esser plug-in project in the Multi-Compressor
repository. It shares the analog panel, rotary control, switch, and meter styling
of YinYang Compressor while keeping its processor, host parameters, and bundle
identity independent.

The first build provides:

- Wideband, Split-band, and Smart modes. Smart analyzes a rolling 2–16 kHz FFT,
  attenuates spectrally flat content, and restores detected peaks with narrow bands.
- Independent detector frequency/bandwidth and attenuation frequency/Q controls,
  plus threshold and maximum reduction (0–18 dB).
- Attack, release, output, stereo linking, detector listen, and bypass controls.
- A timestamped three-second gain-reduction trace and a vertical GR meter.
- VST3 and Standalone targets; AU is also enabled when built on macOS.

The band-pass detector is normalized to unity at its selected center frequency.
Listen auditions its detector signal. In Smart mode, Detector Frequency weights
the FFT analysis region and Detector Q controls temporal spectrum smoothing;
higher Q smooths less. Attenuator Frequency sets the broad cut center, while
Attenuator Q controls the width of the narrow peak-restoration bands. The FFT
updates in overlapping windows and drives the zero-latency filterbank.

An editor preview is available at [Previews/shun-de-ess.png](Previews/shun-de-ess.png).
Four examples show the live response plot with different detector/attenuator
settings: [Wideband default](Previews/ResponseExamples/wideband-default.png),
[Wideband high shelf](Previews/ResponseExamples/wideband-high-shelf.png),
[Split tight bell](Previews/ResponseExamples/split-tight-bell.png), and
[Split wide bell](Previews/ResponseExamples/split-wide-bell.png), plus
[Smart spectral](Previews/ResponseExamples/smart-spectral.png).

## Build

From the repository root, configure and build the VST3:

```bash
cmake -S projects/ShunDeEss -B build-shun-deess -DSHUN_BUILD_TESTS=OFF
cmake --build build-shun-deess --target ShunDeEss_VST3 --parallel 4
```

To include the processor checks, leave tests enabled (the default) and run:

```bash
cmake -S projects/ShunDeEss -B build-shun-deess
cmake --build build-shun-deess --target ShunDeEssTests --parallel 4
ctest --test-dir build-shun-deess --output-on-failure
```

Regenerate the response examples without running the processor checks:

```bash
build-shun-deess/ShunDeEssTests --response-previews projects/ShunDeEss/Previews/ResponseExamples
```

JUCE is provided by the parent repository. For system requirements, see the
root [README](../../README.md).
