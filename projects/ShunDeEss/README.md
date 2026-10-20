# SHUN DE-ESS

SHUN DE-ESS is a J-SONIC de-esser plug-in project in the Multi-Compressor
repository. It shares the analog panel, rotary control, switch, and meter styling
of YinYang Compressor while keeping its processor, host parameters, and bundle
identity independent.

The first build provides:

- Wideband attenuation and split-band attenuation, which leaves low frequencies
  intact.
- Adjustable sibilance frequency (2–12 kHz), detector bandwidth, threshold,
  and maximum reduction (0–18 dB).
- Attack, release, output, wet/dry mix, stereo linking, detector listen, and
  bypass controls.
- A timestamped three-second gain-reduction trace and a vertical GR meter.
- VST3 and Standalone targets; AU is also enabled when built on macOS.

The band-pass detector is normalized to unity at its selected center frequency.
Listen auditions its detector signal. The split-band crossover follows the selected sibilance
frequency. These controls are intended as a practical starting point and can be
refined against listening tests and measured material.

An editor preview is available at [Previews/shun-de-ess.png](Previews/shun-de-ess.png).

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

JUCE is provided by the parent repository. For system requirements, see the
root [README](../../README.md).
