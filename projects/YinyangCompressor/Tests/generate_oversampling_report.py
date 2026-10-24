#!/usr/bin/env python3
"""Plot OversamplingTests' measured, fundamental-normalized alias levels."""
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

folder = Path(__file__).resolve().parent / 'Reports'
with (folder / 'oversampling.csv').open() as source:
    rows = list(csv.DictReader(source))
plt.rcParams.update({'text.color': '#ece7df', 'axes.labelcolor': '#ece7df',
                     'xtick.color': '#ece7df', 'ytick.color': '#ece7df',
                     'font.size': 10})
fig, ax = plt.subplots(figsize=(9, 4.8), facecolor='#19181b')
ax.set_facecolor('#242228')
x = np.arange(4)
for offset, field, color, label in [(-.18, 'native_alias_dbc', '#8e9299', 'Compressor at 1×'),
                                     (.18, 'oversampled_alias_dbc', '#dca45c', 'Whole path at 4×')]:
    values = [float(row[field]) for row in rows]
    ax.bar(x+offset, values, width=.34, color=color, label=label)
    for at, value in zip(x+offset, values):
        ax.text(at, value-4, f'{value:.1f}', ha='center', va='top', fontsize=9)
ax.set_xticks(x, ['FET', 'VCA', 'Variable-mu', 'Optical'])
ax.set_ylim(-145, 0)
ax.set_ylabel('Folded harmonic power / fundamental (dBc; lower is better)')
ax.set_title('Yinyang Compressor — aliasing with Transformer Off', color='#ece7df', pad=14)
ax.grid(axis='y', alpha=.15)
ax.set_axisbelow(True)
ax.legend(facecolor='#242228', labelcolor='#ece7df', loc='upper right')
fig.text(.5, .02, '48 kHz • 10 kHz tone • 20:1 • 0.05 ms attack • 10 ms release • 0 dB knee',
         ha='center', color='#bfb6a9', fontsize=9)
fig.tight_layout(rect=(0, .04, 1, 1))
fig.savefig(folder / 'oversampling.png', dpi=160, facecolor=fig.get_facecolor())
