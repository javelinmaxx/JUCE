#!/usr/bin/env python3
"""Plot measured SHUN detail-filter bandwidth and document the quiet spectrum test."""
from pathlib import Path
import csv
import os
os.environ.setdefault('MPLCONFIGDIR', '/tmp/shun-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import NullFormatter
import numpy as np

root = Path(__file__).resolve().parent / 'Reports'
curves = np.genfromtxt(root / 'detail-responses.csv', delimiter=',', names=True)
with (root / 'detail-bandwidth.csv').open() as stream:
    widths = list(csv.DictReader(stream))
fig, axes = plt.subplots(1, 2, figsize=(12, 4), constrained_layout=True)
for q in [.5, 1.2, 2.8, 4.]:
    rows = curves[np.isclose(curves['q'], q)]
    axes[0].semilogx(rows['frequency'], rows['digital_power'], label=f'Q {q:g}')
rows = curves[np.isclose(curves['q'], 2.8)]
axes[1].semilogx(rows['frequency'], rows['old_display_amplitude'], '--', label='Previous: analog amplitude approximation')
axes[1].semilogx(rows['frequency'], rows['digital_power'], label='Now: actual digital filter power')
axes[1].fill_between(rows['frequency'], rows['digital_power'], alpha=.18)
for ax, title in zip(axes, ['Actual DSP detail-band power at 48 kHz', 'Why the old gold shading looked wider (Q 2.8)']):
    ax.axhline(.5, color='grey', lw=.8, linestyle=':')
    ax.set(xlabel='Frequency (Hz)', ylabel='Normalized profile height', xlim=(2000, 16000), ylim=(0, 1.05), title=title)
    ax.set_xticks([2000, 4000, 6500, 8000, 12000, 16000], ['2k', '4k', '6.5k', '8k', '12k', '16k'])
    ax.xaxis.set_minor_formatter(NullFormatter())
    ax.grid(alpha=.25); ax.legend(fontsize=8)
fig.savefig(root / 'detail-bandwidth.png', dpi=160)
plt.close(fig)

table = []
for row in widths:
    if float(row['sample_rate']) == 48000:
        table.append(f"| {float(row['q']):g} | {float(row['low_hz']):,.1f} | {float(row['high_hz']):,.1f} | {float(row['width_hz']):,.1f} |")
report = '''# SHUN detail bandwidth and low-level spectrum

The detail filters are genuinely broad at low Q. The previous display made
those bands look broader again: it used an analog amplitude approximation
instead of the digital filters' power response. The audio filter bandwidth
has not changed in this update.

![Actual digital bandwidth and old-versus-new shading](detail-bandwidth.png)

## Measured bandwidth

These measurements use the coefficients of a filter actually selected by the
smart processor for a 6.5 kHz test tone at 48 kHz. Its selected FFT-bin centre
is 6,515.6 Hz. Band edges are the half-power points (−3 dB).

| Attenuator Q | Lower edge (Hz) | Upper edge (Hz) | Width (Hz) |
| --- | ---: | ---: | ---: |
''' + '\n'.join(table) + '''

The default Attenuator Q is 1.2, so a roughly 4.65 kHz band is expected here.
Q 4 narrows it to about 1.43 kHz. Both Detector Q and Attenuator Q already had
a maximum of 4; a shared limit now keeps their host ranges and DSP clamps
consistent. This update does not change defaults or de-essing behavior.

The old analog approximation also overestimated the digital bandwidth,
especially near Nyquist. Its amplitude tails made the gold area look wide:
half height corresponded to −6 dB. Gold shading now uses the exact digital
power response, where half height corresponds to −3 dB, and ends at Nyquist.
Actual low-level filter tails remain represented, with less visual emphasis.
Gold height is still an activity indicator weighted by the smart processor's
band strength, rather than another calibrated dB response curve.

## Low-level spectrum

The spectrum floor moves from **−84 to −120 dBFS**, adding 36 dB of display
range. The upper limit stays at 0 dBFS and the frequency span at 2–16 kHz.
The detector/attenuator curves retain their separate 0 to −18 dB scale.

![A −100 dBFS tone on the expanded spectrum scale](../../Previews/ResponseExamples/quiet-spectrum.png)

A −100 dBFS, 6.5 kHz tone, with the right channel opposite in polarity, must
render above the new floor. This was below the former display floor. Stereo
powers still combine after the FFT, preserving anti-phase visibility.

## Validation

- The plotted response matches the impulse response of each published audio
  biquad at three frequencies within 0.00002 absolute power error.
- Bandwidth tests cover Q 0.5/1.2/2.8/4 at 8/44.1/48/96 kHz and require
  monotonically narrower bands with higher Q.
- Both host Q controls expose a maximum of 4.
- The quiet-tone image check requires visible pixels above the plot baseline.
- Existing spectrum, bypass, audio-invariance, queue-overflow, editor scaling,
  tooltip and automation regressions remain part of the test suites.

Data: [detail-bandwidth.csv](detail-bandwidth.csv),
[detail-responses.csv](detail-responses.csv).

Reproduce:

```bash
ShunDeEsserTests --spectrum-previews /tmp/shun-previews
# Copy the two measured CSV files to this Reports directory, then:
python3 projects/ShunDeEsser/Tests/generate_spectrum_report.py
```
'''
(root / 'detail-bandwidth.md').write_text(report)
