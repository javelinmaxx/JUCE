#!/usr/bin/env python3
"""Plot upward transfer curves measured from the production compressor DSP."""
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
THRESHOLD = -18.0
RATIO = 4.0
MODELS = ['FET (feed-forward)', 'VCA (feedback)', 'Variable-mu', 'Optical']
COLORS = ['#0072b2', '#d55e00', '#009e73', '#8b58a5']
levels = np.arange(-48.0, 1.0, 1.0)

with tempfile.TemporaryDirectory(prefix='character-upward-plot-') as temporary:
    library_path = Path(temporary) / 'render_upward.so'
    subprocess.run(['c++', '-std=c++17', '-O3', '-shared', '-fPIC',
                    str(HERE / 'render_upward.cpp'), '-o', str(library_path)], check=True)
    library = ctypes.CDLL(str(library_path))
    library.render_upward.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int,
                                      ctypes.c_double, ctypes.c_int,
                                      ctypes.c_float, ctypes.c_float]
    library.render_upward.restype = None

    output_levels = np.empty((len(MODELS), len(levels)))
    for model in range(len(MODELS)):
        for index, level in enumerate(levels):
            signal = np.full(FS * 6, 10 ** (level / 20), dtype=np.float32)
            library.render_upward(signal.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
                                  len(signal), FS, model, THRESHOLD, RATIO)
            if not np.isfinite(signal).all():
                raise RuntimeError(f'Nonfinite output for {MODELS[model]} at {level} dBFS')
            output_levels[model, index] = 20 * np.log10(max(abs(float(np.mean(signal[-FS // 2:]))), 1e-12))

boost = output_levels - levels[None, :]
if np.any(boost[:, levels >= THRESHOLD] > 0.05):
    raise RuntimeError('Upward mode must not boost levels above threshold: '
                       + repr(boost[:, levels >= THRESHOLD].max(axis=1).tolist()))
if np.any(boost[:, levels < THRESHOLD] < -0.05):
    raise RuntimeError('Upward mode should boost levels below threshold')

np.savetxt(OUT / 'upward_compression.csv',
           np.column_stack([levels, output_levels.T, boost.T]), delimiter=',',
           header='input_dbfs,' + ','.join(f'{name}_output_dbfs' for name in MODELS)
           + ',' + ','.join(f'{name}_boost_db' for name in MODELS), comments='')
metadata = {
    'sample_rate_hz': FS,
    'threshold_dbfs': THRESHOLD,
    'ratio': RATIO,
    'knee_db': 0,
    'upward': True,
    'detector_highpass_hz': 0,
    'settling_duration_seconds': 6,
    'dsp_sha256': hashlib.sha256((HERE.parent / 'Source/CompressorDSP.h').read_bytes()).hexdigest(),
    'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=HERE, text=True).strip(),
    'measured_boost_db_at_minus30_dbfs': {
        MODELS[index]: float(boost[index, list(levels).index(-30)])
        for index in range(len(MODELS))
    },
}
(OUT / 'upward_compression.json').write_text(json.dumps(metadata, indent=2) + '\n')

plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10,
    'axes.spines.top': False, 'axes.spines.right': False, 'axes.grid': True,
    'grid.alpha': 0.2, 'lines.linewidth': 2, 'figure.facecolor': '#fafbfc',
    'axes.facecolor': 'white', 'savefig.facecolor': '#fafbfc'})
fig, axes = plt.subplots(1, 2, figsize=(12, 5.5))
fig.subplots_adjust(left=.075, right=.97, bottom=.16, top=.78, wspace=.28)
fig.suptitle('04  /  Upward compression', x=.06, ha='left', y=.96,
             fontsize=21, fontweight='bold')
fig.text(.06, .86,
         'Measured production C++ DSP • 48 kHz • threshold −18 dBFS • ratio 4:1 • hard knee • 100% wet',
         fontsize=9.5)
axes[0].plot(levels, levels, color='#9ca3af', linestyle='--', label='Unity / no compression')
for index, (name, color) in enumerate(zip(MODELS, COLORS)):
    axes[0].plot(levels, output_levels[index], color=color, label=name)
    axes[1].plot(levels, boost[index], color=color, label=name)
for axis in axes:
    axis.axvline(THRESHOLD, color='#666666', linestyle=':', linewidth=1.2)
    axis.axhline(0, color='#9ca3af', linewidth=1)
    axis.set_xlabel('Input level (dBFS)')
    axis.set_xlim(-48, 0)
axes[0].set(title='Steady-state input / output transfer', ylabel='Output level (dBFS)')
axes[0].legend(fontsize=8, loc='upper left')
axes[1].set(title='Upward gain below threshold', ylabel='Output gain over input (dB)', ylim=(-.3, 13))
axes[1].legend(fontsize=8, loc='upper right')
fig.text(.06, .035,
         'Each point uses a fresh DSP instance and a 6 s settled DC input. Detector HPF is disabled to expose the control law.',
         fontsize=9, color='#526173')
fig.savefig(OUT / '04_upward_compression.png', dpi=180)
fig.savefig(OUT / '04_upward_compression.svg')
plt.close(fig)
print(json.dumps(metadata, indent=2))
print(f'Upward compression plot and data saved in {OUT}')
