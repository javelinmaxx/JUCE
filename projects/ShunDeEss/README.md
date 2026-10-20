# SHUN DE-ESS

SHUN DE-ESS is a J-SONIC de-esser plug-in project in the Multi-Compressor
repository. It shares the analog panel, rotary control, switch, and meter styling
of YinYang Compressor while keeping its processor, host parameters, and bundle
identity independent.

The first build provides:

- Four modes on a rotary selector: Wideband, Split-band, Brilliant, and Smooth.
  Brilliant and Smooth analyze a rolling 2–16 kHz FFT and attenuate spectrally
  flat content. Brilliant restores detected detail peaks with narrow bands;
  Smooth reverses that restoration to suppress those peaks further.
- Independent detector frequency/bandwidth and attenuation frequency/Q controls,
  plus threshold and maximum reduction (0–18 dB).
- Attack, release, output, stereo linking, detector listen, and bypass controls.
- A timestamped three-second gain-reduction trace and a vertical GR meter.
- VST3 and Standalone targets; AU is also enabled when built on macOS.

The band-pass detector is normalized to unity at its selected center frequency.
Listen auditions its detector signal. In Brilliant and Smooth, Detector Frequency
weights the FFT analysis region and Detector Q controls temporal spectrum
smoothing; higher Q smooths less. Attenuator Frequency sets the broad cut center,
while Attenuator Q controls the width of the narrow detail bands. The FFT updates
in overlapping windows and drives the zero-latency filterbank. Existing sessions
retain their Wideband, Split-band, or former Smart (now Brilliant) setting.

An editor preview is available at [Previews/shun-de-ess.png](Previews/shun-de-ess.png).
Six examples show the live response plot with different detector/attenuator
settings: [Wideband default](Previews/ResponseExamples/wideband-default.png),
[Wideband high shelf](Previews/ResponseExamples/wideband-high-shelf.png),
[Split tight bell](Previews/ResponseExamples/split-tight-bell.png), and
[Split wide bell](Previews/ResponseExamples/split-wide-bell.png), plus
[Brilliant spectral](Previews/ResponseExamples/brilliant-spectral.png) and
[Smooth spectral](Previews/ResponseExamples/smooth-spectral.png).

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

Run the deterministic detector/attenuator mock-signal test and write its
Markdown report, measured Brilliant attenuator-Q and detector-Q sweeps, phase plot,
and rolling attenuator-shape heatmaps at Q 0.5, 1.2, 2.8, and 4.0. The rolling
signal includes gated high-pass noise pulses to mimic sibilant consonants and
compares pulse attenuation with the quiet baseline:

```bash
build-shun-deess/ShunDeEssTests --analysis-report projects/ShunDeEss/Tests/Reports/detector-attenuator-mock-report.md
```

The committed example report is at
[Tests/Reports/detector-attenuator-mock-report.md](Tests/Reports/detector-attenuator-mock-report.md).
Its plotted versions are available as [PNG](Tests/Reports/detector-attenuator-mock-report.png)
and [PDF](Tests/Reports/detector-attenuator-mock-report.pdf). Regenerate both from
the report and measured spectrum data with:

```bash
python3 projects/ShunDeEss/Tests/generate_analysis_report.py
```

The generator also writes `Tests/Reports/wide-split-pulse-test-report.png` and
`Tests/Reports/smart-detector-q-report.png`, with matching PDF reports.

JUCE is provided by the parent repository. For system requirements, see the
root [README](../../README.md).
