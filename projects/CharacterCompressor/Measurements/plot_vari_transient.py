#!/usr/bin/env python3
"""Measure and plot production attack/release with VariTransient off and on."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

os.environ.setdefault('MPLCONFIGDIR', '/tmp/character-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

HERE = Path(__file__).resolve().parent
OUT = HERE / 'plots'
OUT.mkdir(exist_ok=True)
FS = 48000
MODELS = ['FET (feed-forward)', 'VCA (feedback)', 'Variable-mu', 'Optical']
COLORS = ['#0072b2', '#d55e00', '#009e73', '#8b58a5']
SETTINGS = [(0, 'VariTransient 0%'), (400, 'VariTransient 400%')]
plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10,
    'axes.spines.top': False, 'axes.spines.right': False, 'axes.grid': True,
    'grid.alpha': 0.2, 'lines.linewidth': 2, 'figure.facecolor': '#fafbfc',
    'axes.facecolor': '#ffffff', 'savefig.facecolor': '#fafbfc'})

with tempfile.TemporaryDirectory(prefix='character-vari-transient-') as temp:
    library = Path(temp) / 'render.so'
    subprocess.run(['c++', '-std=c++17', '-O3', '-shared', '-fPIC',
                    str(HERE / 'render_vari_transient.cpp'), '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.render_vari_transient.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int,
                                          ctypes.c_double, ctypes.c_int, ctypes.c_float]
    lib.render_vari_transient.restype = None
    lib.latency_samples.argtypes = []
    lib.latency_samples.restype = ctypes.c_int
    latency = lib.latency_samples()

    # A low DC level, 1 s above threshold, then low again exposes the envelope
    # transitions without carrier ripple. Include enough tail for Optical recovery.
    time = np.arange(int(3.5 * FS)) / FS
    input_signal = np.full(time.size, 10 ** (-36 / 20), dtype=np.float32)
    input_signal[(time >= 0.1) & (time < 1.1)] = 10 ** (-6 / 20)
    trace = np.empty((4, 2, time.size), dtype=np.float32)
    metrics = {}
    for model, name in enumerate(MODELS):
        for setting, (amount, _) in enumerate(SETTINGS):
            output = np.pad(input_signal, (0, latency)).astype(np.float32)
            lib.render_vari_transient(output.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
                                      len(output), FS, model, amount)
            output = output[latency:latency + time.size]
            gr = 20 * np.log10(np.maximum(input_signal, 1e-12)) \
                 - 20 * np.log10(np.maximum(np.abs(output), 1e-12))
            trace[model, setting] = gr
            attack = gr[(time >= 0.1) & (time < 1.1)]
            recovery = gr[time >= 1.1]
            steady = float(np.mean(attack[-FS // 10:]))
            attack_crossing = np.flatnonzero(attack >= steady * 0.9)
            release_crossing = np.flatnonzero(recovery <= recovery[0] * 0.1)
            metrics[f'{name} / {amount}%'] = {
                'steady_gr_db': steady,
                'attack_to_90_percent_ms': float(attack_crossing[0] / FS * 1000)
                    if attack_crossing.size else None,
                'release_to_10_percent_ms': float(release_crossing[0] / FS * 1000)
                    if release_crossing.size else None,
            }

fig, axes = plt.subplots(1, 2, figsize=(12, 5.8))
fig.subplots_adjust(left=.075, right=.98, bottom=.13, top=.79, wspace=.22)
fig.suptitle('06  /  VariTransient attack and release', x=.06, ha='left',
             fontsize=21, fontweight='bold', y=.96)
fig.text(.06, .865,
         'Measured production C++ DSP • 48 kHz • 4:1 • attack 10 ms • release 150 ms • 3 dB GR knee (3–6 dB)',
         fontsize=9.5)
for model, (name, color) in enumerate(zip(MODELS, COLORS)):
    for setting, (amount, label) in enumerate(SETTINGS):
        style = '-' if amount else '--'
        alpha = 1.0 if amount else .72
        attack_mask = (time >= .1) & (time <= .25)
        axes[0].plot((time[attack_mask] - .1) * 1000, trace[model, setting, attack_mask],
                     linestyle=style, color=color, alpha=alpha,
                     label=f'{name} · {label}' if model == 0 else None)
        # Legend entries for each model and setting are added explicitly below.
        axes[1].plot((time[time >= 1.1] - 1.1) * 1000,
                     trace[model, setting, time >= 1.1],
                     linestyle=style, color=color, alpha=alpha)

# Explicit compact legends: model color in the first row, setting style in the second.
from matplotlib.lines import Line2D
model_handles = [Line2D([0], [0], color=c, lw=2, label=n) for n, c in zip(MODELS, COLORS)]
setting_handles = [Line2D([0], [0], color='#333333', lw=2, linestyle='--', label='0%'),
                   Line2D([0], [0], color='#333333', lw=2, linestyle='-', label='400%')]
axes[0].legend(handles=model_handles, loc='lower right', fontsize=8, framealpha=.92)
axes[1].legend(handles=setting_handles, title='VariTransient', loc='upper right', fontsize=8)
axes[0].axhline(3, color='#777777', lw=1, linestyle=':', alpha=.8)
axes[0].axhline(6, color='#777777', lw=1, linestyle=':', alpha=.8)
axes[0].text(2, 3.15, '3 dB trigger', fontsize=8, color='#555555')
axes[0].text(2, 6.15, '6 dB full effect', fontsize=8, color='#555555')
axes[0].set(title='Attack after the upward step', xlabel='Time after step (ms)',
            ylabel='Gain reduction (dB)', xlim=(0, 150), ylim=(-.2, 10))
axes[1].set(title='Recovery after the downward step', xlabel='Time after step (ms)',
            ylabel='Gain reduction (dB)', xlim=(0, 2400), ylim=(-.2, 10))
fig.text(.06, .035,
         'Solid: 400% (4× slower attack, up to ¼ release). Dashed: 0%. Upward compression disabled.',
         fontsize=9)
for ext in ('png', 'svg'):
    figure_path = OUT / f'06_vari_transient.{ext}'
    fig.savefig(figure_path, dpi=180 if ext == 'png' else None,
                bbox_inches='tight')
    if ext == 'svg':
        figure_path.write_text('\n'.join(line.rstrip() for line in figure_path.read_text().splitlines()) + '\n')
plt.close(fig)

np.savez_compressed(OUT / 'vari_transient.npz', time_s=time, gain_reduction_db=trace)
import csv
with (OUT / 'vari_transient.csv').open('w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['model', 'vari_transient_percent', 'time_seconds', 'gain_reduction_db'])
    for model, name in enumerate(MODELS):
        for setting, (amount, _) in enumerate(SETTINGS):
            writer.writerows((name, amount, f'{t:.8f}', f'{gr:.8f}')
                             for t, gr in zip(time[::48], trace[model, setting, ::48]))

metadata = {
    'sample_rate_hz': FS, 'threshold_dbfs': -18, 'ratio_control': 4,
    'attack_control_ms': 10, 'release_control_ms': 150, 'knee_db': 6,
    'vari_transient_percent': [0, 400], 'detector_hpf_hz': 0,
    'oversampling_factor': 4, 'latency_samples_removed': latency,
    'stimulus': 'DC step -36 dBFS to -6 dBFS at 0.1 s, back to -36 dBFS at 1.1 s',
    'dsp_sha256': {name: hashlib.sha256((HERE.parent / 'Source' / name).read_bytes()).hexdigest()
                   for name in ['CompressorDSP.h', 'OversampledTransformer.h',
                                'TransformerResponse.h', 'HighTilt.h']},
    'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=HERE, text=True).strip(),
    'metrics': metrics,
}
(OUT / 'vari_transient.json').write_text(json.dumps(metadata, indent=2) + '\n')
print(json.dumps(metrics, indent=2))
print(f'Wrote {OUT / "06_vari_transient.png"} and SVG, CSV, NPZ, JSON')
