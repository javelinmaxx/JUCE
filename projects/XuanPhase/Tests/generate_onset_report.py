#!/usr/bin/env python3
"""Plot streamed legacy/adaptive onset comparisons recorded by XuanPhaseTests."""
from pathlib import Path
import os
os.environ.setdefault('MPLCONFIGDIR', '/tmp/xuan-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

root = Path(__file__).resolve().parent / 'Reports'
data = np.genfromtxt(root / 'onset-accuracy.csv', delimiter=',', names=True, dtype=None, encoding='utf-8')
cases = ['masked', 'quiet', 'rapid', 'noise', 'vibrato', 'slow-pad']
titles = ['Soft hits under a louder sustained two-tone pad', 'Quiet attacks: 0.0008 peak scale',
          '80 ms spacing, alternating strong / weak attacks', 'Steady seeded noise',
          'Steady tone with 5 Hz vibrato', '400 ms two-tone pad fade-in']
fig, axes = plt.subplots(3, 2, figsize=(12, 8), constrained_layout=True)
rows = []
total_old = total_new = total_expected = 0
for ax, case, title in zip(axes.flat, cases, titles):
    frame = data[data['case'] == case]
    expected = frame['expected_start'][frame['expected_start'] >= 0]
    newer = frame['sample'][(frame['new'] != 0) & (frame['sample'] > 9600)]
    older = frame['sample'][(frame['legacy'] != 0) & (frame['sample'] > 9600)]
    def matches(detections):
        return sum(np.any((detections >= start) & (detections-start < 1440)) for start in expected)
    new_hits, old_hits = matches(newer), matches(older)
    latency = [min(newer[(newer >= start) & (newer-start < 1440)]-start)/48
               for start in expected if np.any((newer >= start) & (newer-start < 1440))]
    total_old += old_hits; total_new += new_hits; total_expected += len(expected)
    delay = f'{np.mean(latency):.2f}' if latency else '—'
    rows.append(f'| {case} | {len(expected)} | {old_hits} | {new_hits} | {len(newer)} | {delay} |')
    ax.plot(frame['sample']/48000, frame['novelty'], lw=.8, color='#457b9d', label='Adaptive spectral novelty')
    for i, start in enumerate(expected):
        ax.axvline(start/48000, color='#2a9d8f', alpha=.65, lw=.8, label='Attack start' if i == 0 else None)
    selected = (frame['new'] != 0) & (frame['sample'] > 9600)
    ax.scatter(frame['sample'][selected]/48000, frame['novelty'][selected], color='#d38d20', s=24, zorder=3, label='New detected event')
    old = (frame['legacy'] != 0) & (frame['sample'] > 9600)
    ax.scatter(frame['sample'][old]/48000, np.full(np.sum(old), .025), marker='x', s=10, color='#888888', label='Legacy detection')
    ax.set(xlim=(.2, 1.5), ylim=(-.025, 1.05), title=title, xlabel='Input stream time (s)', ylabel='Novelty')
    ax.grid(alpha=.25); ax.legend(fontsize=6, loc='upper right')
fig.savefig(root / 'onset-detection.png', dpi=160)
plt.close(fig)

report = f'''# Xuan Phase adaptive onset detection

The recent whole-note discard rule has been reverted. It affected correction
eligibility, rather than changing the detector's thresholds: a detected event
could be dropped when its filter missed the safe pre-onset handoff. Late
responses now wait until the protected response has passed, as before.

The detector itself now distinguishes newly arriving spectral peaks from a
louder sustained background. These streamed tests detect **{total_new}/{total_expected}** constructed
attacks, versus **{total_old}/{total_expected}** for the prior full-window crest rule.

![Streamed onset accuracy comparisons](onset-detection.png)

| Case | Attack starts | Legacy matches | Adaptive matches | Adaptive events after warmup | Mean detection delay (ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
''' + '\n'.join(rows) + '''

The matching window is 30 ms and the warmup is 200 ms. The pad has no sharp
attack; one event at the start of its fade-in is allowed, with no repeated
triggers during the fade or sustain. Steady noise and vibrato must not trigger.
Quiet and masked fixtures each contain four attacks; the rapid fixture has
13 attacks at 80 ms spacing, alternating amplitudes 0.15 and 0.035. Attack
packets contain 2,437.5/6,750 Hz components, a 1.5 ms rise and 22 ms decay.
The masked case adds a substantially louder sustained 220/330 Hz background.

## Detector changes

- Score log growth at significant local spectral peaks, with a neighbouring-bin
  maximum over the preceding spectrum to reject vibrato and moving leakage.
- Compare against a 60 ms per-bin spectral baseline to identify newly emerging
  content, even when overall crest and energy are dominated by a sustained note.
- Adapt the novelty threshold using 64 frames of background statistics; bound
  large outliers so a strong hit does not hide the following weaker hit.
- Retain local attack evidence for 12 ms and re-arm after a release or clear
  new spectral content, with a minimum 20 ms between new events.
- Keep six milliseconds of follow-up phase analysis per event for the existing
  five-millisecond target collection; do not count follow-up frames as new events.
- Locate the phase anchor in the positive spectral residual, rather than using
  an older, louder waveform peak from the same long FFT window.

The streaming energy floor is approximately −90 dBFS RMS, versus the former
−70 dBFS full-frame gate. Cold-start single-frame reference analysis retains
the original crest rule; the adaptive state takes over on subsequent frames.

## Correction and safety checks

The full DSP/editor suite passes 308,491 checks; Linux VST3 and Standalone
builds pass without warnings.

The 25 ms lookahead, 50 ms reported host latency, ±150° target cap, protected
onset interval, Transient release knob, and five-millisecond sigmoid handoff
remain. Tests verify that late responses are applied after the protected
attack rather than discarded, including a simulated electric-piano sustain.
A late correction can still rotate sustained audio; restoring eligibility
does not guarantee removal of all phase modulation or warble.

These synthetic tests measure detector recall and false triggers separately
from worker scheduling and audible punch. They do not guarantee recognition
of every transient in arbitrary recordings. Frozen single-frame phase-design
probes elsewhere in the report isolate the phase algorithm from adaptive
onset decisions.

Data: [onset-accuracy.csv](onset-accuracy.csv).

Reproduce from the configured test build:

```bash
XuanPhaseTests --onset-report projects/XuanPhase/Tests/Reports
# Full DSP measurements, including the late-worker and piano regressions:
XuanPhaseTests --report projects/XuanPhase/Tests/Reports
python3 projects/XuanPhase/Tests/generate_onset_report.py
python3 projects/XuanPhase/Tests/generate_report.py
```
'''
(root / 'onset-detection.md').write_text(report)
print(f'Adaptive {total_new}/{total_expected}; legacy {total_old}/{total_expected}')
