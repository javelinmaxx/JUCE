# Kuo Stereo feature measurement report

This report measures the actual `KuoStereoProcessor` at 48 kHz in 256-sample blocks.
Signals are synthetic and deterministic; no listening or host test is implied.
The measurement executable and this plotting script reproduce every figure and sample.

## Smart Haas: content adaptation and spectral preservation

Four consecutive 2 s mono segments contain either a 100, 500, or 1350 Hz tone
plus low-level white noise, or broad white noise alone. Center is 12 ms,
Variability is ±6 ms, Max Rate is 10 ms/s, and Velvet Width is 0%.
The fixed comparison uses the same processor with Variability at 0 ms.
The first 0.5 s of each segment is excluded from the steady-state table.
The error is the mean of weighted absolute log FFT-magnitude ratios
between input and processed mid, over 512-sample Hann windows; lower is better.

![Smart Haas delay and spectrum comparison](smart-haas.png)

| Content | Most common target | Median active delay | Adaptive error | Fixed 12 ms error |
| --- | ---: | ---: | ---: | ---: |
| 100 Hz + noise | 10.8 ms | 10.80 ms | 0.345 | 0.352 |
| 500 Hz + noise | 18.0 ms | 11.39 ms | 0.492 | 0.250 |
| 1350 Hz + noise | 15.6 ms | 15.23 ms | 0.396 | 0.316 |
| broad noise | 15.6 ms | 12.61 ms | 0.593 | 0.569 |

The selected target changes with content and stays inside 6–18 ms; the largest observed
active-delay step is approximately 10.0 ms/s (10 ms/s setting). The spectral comparison
also exposes a limitation: adaptation does **not** consistently improve the
processed mid spectrum against a fixed 12 ms delay in these signals.
Broad noise makes the target fluctuate among candidates. This result demonstrates
content response, but does not validate a universal spectral-preservation claim.
The 500 Hz case is especially poor: the fixed 12 ms delay is six tone
periods, but the six-candidate grid omits 12 ms and alternates between
other period-matched targets. Rate-limited travel between them passes
through delays that colour the tone. Candidate-grid coverage and
temporal averaging merit investigation before claiming reliable improvement.

## Keep Transient and Velvet Transient Guard

A 1.0-amplitude onset with a short decaying noise burst is placed in a
0.002-amplitude white-noise bed. Haas Center is 12 ms and Variability is 0 ms.
Keep Transient is compared at 0% and 100% with Velvet Width at 0%.
Separately, Velvet Width is 100% and Transient Guard is compared at 0% and
100%; the Haas-only side signal is subtracted to isolate Velvet's added side.

![Transient timing and side-tail protection](transients.png)

- At onset, the right sample is 0.0011 with Keep 0% and 0.9995 with Keep 100% (input 0.9995).
- Velvet-added side RMS from 4–28 ms is 0.03363 at Guard 0% and 0.00110 at Guard 100% (96.7% reduction).
- The protected Haas path reaches 0 ms during the onset and returns after
  the shared hold and tap crossfade; normal Haas stays at 12 ms.

## Velvet Width: widening a mono signal

A 1.5 s mono, low-pass-filtered white-noise excerpt is processed with
the same fixed 12 ms Haas delay: Velvet Width at 0%, at 100% with
Guard 0%, and at 100% with Guard 100%.
The Haas-only render is the baseline; the change between renders is Velvet.

![Mono noise stereo scatter comparison](mono-width.png)

- Side RMS rises from 0.04476 to 0.05488 (22.6% increase).
- Velvet itself adds 0.03158 side RMS while the mid difference between renders is 0.0000000 RMS.
- With Guard 100%, side RMS is 0.05488 and the Velvet-added side RMS is 0.03158; sustained width remains audible.
- Left/right correlation changes from 0.003 to -0.198.
- Listen to [mono input](mono-input.wav), [Haas only](mono-haas-only.wav),
  [Haas + Velvet 100%](mono-velvet-100.wav), and
  [Haas + Velvet 100% + Guard 100%](mono-velvet-guard-100.wav).

## Frequency Field: split goniometer

An 80 Hz, 700 Hz, and 5 kHz stereo tone mix feeds the processor. The three
plots are the actual 96-point scope buffers read from the processor, using
its below-250 Hz, 250 Hz–2.5 kHz, and above-2.5 kHz splits.

![Three frequency-split goniometers](frequency-field.png)

| Band | Points | Left RMS | Right RMS |
| --- | ---: | ---: | ---: |
| low | 96 | 0.1766 | 0.1765 |
| mid | 96 | 0.1305 | 0.1291 |
| high | 96 | 0.0726 | 0.0709 |

All bands receive nonzero processed stereo points; this verifies the visual
data path, not calibrated filter separation.

## Reproduce

From the repository root, after configuring `projects/KuoStereo` with
`-DKUO_BUILD_TESTS=ON`:

```bash
cmake --build build-kuo-stereo --target KuoStereoFeatureMeasurements --parallel 4
./build-kuo-stereo/KuoStereoFeatureMeasurements /tmp/kuo-feature-data
python3 projects/KuoStereo/Measurements/generate_report.py /tmp/kuo-feature-data
```

The plotting step requires NumPy and Matplotlib. Output goes to
`projects/KuoStereo/Measurements/Report` unless a second path is supplied.
