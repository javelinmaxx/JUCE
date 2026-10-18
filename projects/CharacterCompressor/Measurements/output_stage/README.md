# Transformer distortion roll-off and High Tilt

[Plots](output_stage.png) · [PDF](output_stage_report.pdf) · [SVG](output_stage.svg)
· [Results and source hashes](metrics.json)

The production C++ DSP now filters generated transformer distortion above
500 Hz. It subtracts the clean input from the saturation output, filters that
nonlinear residual through a fourth-order Butterworth low-pass, and adds it
back to the clean input. All of this runs at 2× before the existing transformer
phase/bandwidth response and decimation. This is a 24 dB/octave roll-off with
−3.01 dB at 500 Hz, not a brick-wall cutoff. The residual includes saturation's
change to the fundamental as well as generated harmonics; its filtering can
also affect the large-signal fundamental gain/phase around the corner.

The user explicitly chose filtering generated harmonics, rather than limiting
which input frequencies can drive the saturator. Higher-frequency inputs can
still generate low-frequency intermodulation products on complex signals.
The original 0–18 dB drive range, 6 dB default, and flux memory remain.

## Distortion measurement

48 kHz host rate, 2× transformer, mono, compression 1:1, unity input/output gain,
High Tilt off. Every tone runs for two seconds; the final second provides
coherent FFT bins. The fixed 64-sample delay is removed. The previous 2× phase
model is preserved in `../baseline/transformer_before_frequency.h`.

At −12 dBFS peak input and 6 dB drive, measured THD (H2–H10 below Nyquist):

| Input frequency | Previous THD | Revised THD |
| --- | ---: | ---: |
| 50 Hz | 2.4202% | 2.4113% |
| 100 Hz | 2.1849% | 2.1422% |
| 250 Hz | 2.0177% | 0.3710% |
| 500 Hz | 1.9851% | 0.02209% |
| 1 kHz | 1.9797% | 0.001420% |
| 2 kHz | 1.9902% | 0.000137% |

Thus distortion is frequency dependent. Low-frequency saturation remains,
while higher generated harmonics are attenuated. Near the numerical floor,
THD does not continue decreasing monotonically; these figures are not THD+N,
hardware measurements, or an assurance of zero aliasing.

To verify the filter separately from changes in fundamental level, compare
absolute fifth-harmonic amplitudes before/after at −6 dBFS peak and 12 dB drive:

| Input tone | Generated harmonic | Measured attenuation |
| --- | --- | ---: |
| 100 Hz | 500 Hz | −3.0103 dB |
| 200 Hz | 1 kHz | −24.1087 dB |
| 400 Hz | 2 kHz | −48.2116 dB |

A separate 100 Hz harmonic series agrees with the specified digital filter
within 0.0037 dB at 48 kHz for bins above the measurement floor. Corresponding
checks pass at 44.1 kHz (0.0065 dB maximum error) and 96 kHz (0.00013 dB).
Even harmonics of this symmetric steady-state stimulus sit near the numerical
floor, so the filter-ratio plot uses measurable odd harmonics.

## High Tilt

The switch enables a +6 dB high shelf in the compressor's sidechain, after the
detector HPF and before rectification/stereo linking. Its midpoint is 1.5 kHz
(+3 dB), with RBJ shelf slope S = 1. The detector shelf runs continuously and
crossfades with the unfiltered detector signal over approximately 20 ms. It
defaults off; automation/state saving are supported. Old sessions without the
new parameter restore it as off. It affects all four gain-control models and
does not directly EQ the audio path. Reported latency remains 64 samples.

Measured at −60 dBFS peak, 48 kHz:

| Frequency | Detector shelf gain |
| --- | ---: |
| 100 Hz | +0.0001 dB |
| 500 Hz | +0.0780 dB |
| 1.5 kHz | +3.0000 dB |
| 10 kHz | +5.9982 dB |

The shelf measurement uses the production `HighTilt` class directly. The 1.5 kHz
midpoint also passes at 44.1 and 96 kHz. End-to-end renders at ratio 1:1 confirm
sample-identical output with tilt on/off, with either transformer setting. With
compression active (4:1, threshold −24 dBFS, knee 0 dB, input −12 dBFS peak),
High Tilt increases HF gain reduction in all four models while leaving bass
sensitivity essentially unchanged. Tests also cover an enabled 80 Hz detector
HPF. Per-model results are in `metrics.json`.

| Model | Extra reduction at 1.5 kHz | Extra reduction at 10 kHz |
| --- | ---: | ---: |
| FET | 2.247 dB | 4.448 dB |
| VCA | 2.218 dB | 4.373 dB |
| Variable-mu | 1.958 dB | 3.993 dB |
| Optical | 2.250 dB | 4.499 dB |

These are rendered output RMS differences at the settings above, including each
model's envelope behavior. Extra reduction at 100 Hz is below 0.003 dB.

With High Tilt off, clean
small-signal response from 1–18 kHz differs from the prior transformer by less
than 0.00001 dB, confirming that the residual filter does not low-pass clean highs.
High Tilt changes output only through compression gain reduction; it does not
boost the transformer's output or generated harmonics directly.

## Reproduce

From the repository root, with C++17, Python, NumPy, and Matplotlib:

```bash
python3 projects/CharacterCompressor/Measurements/verify_output_stage.py
```

This regenerates the plots, PDF, `metrics.json`, and the THD/harmonic/shelf CSVs.
Only focused output-stage verification ran. The prior phase report and 1×
compression plots remain historical records; the full DSP/processor suites
were not rerun. The Linux VST3 build and 23 focused processor checks passed,
covering the new parameter, automation, stereo matching, saved/legacy state,
latency, dry-path transparency, additional HF compression, and editor rendering.
Updated macOS builds need user listening checks.
