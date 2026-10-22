# Xuan Phase test report

## Manual controls and latency

![Measured phase and magnitude](manual-phase.png)

The same FIR used by the processor is measured at 20, 50, 200, 1k, 5k and
12k Hz for opposite low/high settings, with a 1 kHz crossover. Phase is measured
relative to the 25 ms reference. The endpoints reach approximately ±180° at
20 Hz; intermediate-angle magnitude ripple is shown explicitly. The response
is a finite phase-rotation approximation, not a mathematically exact all-pass.

Impulse tests compare the actual hybrid convolution against its designed FIR,
check block-size invariance at 1 and 127 samples, stereo linking, and neutral
and bypass delays at 8/44.1/48/96/192 kHz. Host state recall covers all seven
parameters. Input/output latency stays round(sampleRate × 25 ms) samples.

## Transient spectral phase and Depth

![Synthetic transient alignment](transient-alignment.png)

Four Gaussian wave packets at 281.25, 843.75, 2437.5 and 6750 Hz have staggered
centres. Their measured spectrogram phases and slopes generate narrow-band
targets. The actual audio convolution then runs at Depth 0/50/100%.
This deterministic test freezes the detected target before rendering, separating
the phase algorithm's effect from real-time worker scheduling. The reference
delay is included in every trace. No output gain or loudness normalization is
applied; both RMS and crest are reported to expose magnitude changes.

| Auto Depth | Peak | RMS over the full 7,000-sample buffer | Crest factor |
| --- | ---: | ---: | ---: |
| 0% | 0.382388 | 0.028242 | 22.632 dB |
| 50% | 0.437323 | 0.028242 | 23.798 dB |
| 100% | 0.472587 | 0.028242 | 24.472 dB |

Depth 0 is sample-identical to manual-only processing. Full Depth must improve
the dispersed hit's peak by at least 5% in this test. Detected group-delay
corrections are bounded to ±25 ms; stationary noise must not repeatedly trigger
new targets. A separate streamed three-hit test exercises the real worker,
response handoff and stereo linking; host bypass is checked with nonzero
rotation and output gain. Peak improvement on this constructed signal is not a guarantee
for arbitrary recordings. Native editor checks cover parameter binding, hover
hints and the 50%–120% resize limits.

## Phase-field display

The native editor test streams repeated dispersed hits through the real processor
and worker, observes transient events and both signed timing directions, and
checks that non-empty image columns move from left to right. Green indicates an
early input band and red a late input band. The white curve uses the measured
complex FIR response: tests compare it with an independent DFT, ensure queued
responses do not appear before audio applies them, verify updates after handoff,
and check zero displayed phase under host bypass. Input analysis remains live
with Auto Off while the meter is visible, and stops when the editor is hidden.
The meter stays inside the editor at both scaling limits.

![Native editor with live transient history](../../Previews/xuan-phase.png)

Data: [phase.csv](phase.csv), [transient.csv](transient.csv).
