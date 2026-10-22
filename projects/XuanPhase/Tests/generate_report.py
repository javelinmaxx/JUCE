#!/usr/bin/env python3
"""Plot actual Xuan FIR phase measurements and the dispersed-transient test."""
from pathlib import Path
import os
os.environ.setdefault('MPLCONFIGDIR', '/tmp/xuan-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
root = Path(__file__).resolve().parent / 'Reports'
phase = np.genfromtxt(root/'phase.csv', delimiter=',', names=True)
audio = np.genfromtxt(root/'transient.csv', delimiter=',', names=True)
fig, axes = plt.subplots(1, 2, figsize=(12, 4), constrained_layout=True)
for degrees in [-180, -90, 0, 90, 180]:
    rows = phase[phase['setting'] == degrees]
    axes[0].semilogx(rows['frequency'], rows['phase_deg'], 'o-', label=f'Low {degrees:+} / High {-degrees:+} deg')
    axes[1].semilogx(rows['frequency'], rows['gain_db'], 'o-')
axes[0].set(ylabel='Phase relative to 25 ms reference (degrees)', xlabel='Frequency (Hz)', title='Independent low/high rotation')
axes[1].set(ylabel='Magnitude (dB)', xlabel='Frequency (Hz)', title='Finite-FIR magnitude ripple')
axes[0].legend(fontsize=7)
for ax in axes: ax.grid(alpha=.25)
fig.savefig(root/'manual-phase.png', dpi=160); plt.close(fig)
fig, ax = plt.subplots(figsize=(10, 4), constrained_layout=True)
for key in ['depth0', 'depth50', 'depth100']:
    ax.plot(audio['sample']/48, audio[key], label=key.replace('depth', 'Auto Depth ')+'%', alpha=.8)
ax.set(xlabel='Time (ms), including reference latency', ylabel='Amplitude', title='Phase-guided alignment of a dispersed transient', xlim=(40,65))
ax.legend(); ax.grid(alpha=.25); fig.savefig(root/'transient-alignment.png', dpi=160); plt.close(fig)
def measurements(key):
    x=audio[key]
    return np.max(np.abs(x)), np.sqrt(np.mean(x*x))
rows=[]
for key in ['depth0','depth50','depth100']:
    peak,rms=measurements(key)
    rows.append(f'| {key[5:]}% | {peak:.6f} | {rms:.6f} | {20*np.log10(peak/rms):.3f} dB |')
(root/'REPORT.md').write_text('''# Xuan Phase test report

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
'''+ '\n'.join(rows)+'''

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
''')
print('\n'.join(rows))
