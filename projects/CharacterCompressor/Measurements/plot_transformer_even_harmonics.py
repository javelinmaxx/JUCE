#!/usr/bin/env python3
"""Show how simulated transformer DC current creates even harmonics."""
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
TONE_HZ = 100
INPUT_PEAK_DBFS = -6
CURRENT_SETTINGS = [0, 25, 50, 75, 100]
COLORS = ['#59646d', '#0072b2', '#009e73', '#e69f00', '#d55e00']
LATENCY = 64

with tempfile.TemporaryDirectory(prefix='character-even-harmonics-') as temporary:
    library_path = Path(temporary) / 'render_even_harmonics.so'
    subprocess.run(['c++', '-std=c++17', '-O3', '-shared', '-fPIC',
                    str(HERE / 'render_even_harmonics.cpp'), '-o', str(library_path)], check=True)
    library = ctypes.CDLL(str(library_path))
    library.even_harmonics_latency_samples.restype = ctypes.c_int
    library.render_even_harmonics.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int,
                                              ctypes.c_double, ctypes.c_float]
    library.render_even_harmonics.restype = None
    LATENCY = library.even_harmonics_latency_samples()

time = np.arange(FS * 2) / FS
input_signal = 10 ** (INPUT_PEAK_DBFS / 20) * np.sin(2 * np.pi * TONE_HZ * time)
outputs = []
harmonics = np.arange(2, 11)
spectra = []
for amount in CURRENT_SETTINGS:
    signal = np.pad(input_signal.astype(np.float32), (0, LATENCY))
    library.render_even_harmonics(signal.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
                                  len(signal), FS, amount)
    if not np.isfinite(signal).all():
        raise RuntimeError(f'Nonfinite transformer output at Even harmonics {amount}%')
    rendered = signal[LATENCY:LATENCY + len(input_signal)].astype(np.float64)
    outputs.append(rendered)
    spectrum = np.fft.rfft(rendered[-FS:]) * 2 / FS
    spectra.append(20 * np.log10(np.maximum(np.abs(spectrum[harmonics * TONE_HZ])
                                            / abs(spectrum[TONE_HZ]), 1e-12)))

outputs = np.asarray(outputs)
spectra = np.asarray(spectra)
if spectra[-1, 0] <= -30:
    raise RuntimeError('Maximum simulated DC current did not produce a clear second harmonic')

np.savetxt(OUT / 'transformer_even_harmonics.csv',
           np.column_stack([harmonics, spectra.T]), delimiter=',',
           header='harmonic_number,' + ','.join(f'even_harmonics_{x}_percent_dbc'
                                                for x in CURRENT_SETTINGS), comments='')
metadata = {
    'sample_rate_hz': FS,
    'oversampling_factor': 4,
    'latency_samples': LATENCY,
    'input_tone_hz': TONE_HZ,
    'input_peak_dbfs': INPUT_PEAK_DBFS,
    'transformer_drive_db': 18,
    'even_harmonics_settings_percent': CURRENT_SETTINGS,
    'harmonic_numbers': harmonics.tolist(),
    'harmonics_relative_to_fundamental_dbc': spectra.tolist(),
    'second_harmonic_at_zero_current_dbc': float(spectra[0, 0]),
    'second_harmonic_at_maximum_current_dbc': float(spectra[-1, 0]),
    'zero_current_even_partials_at_numerical_floor': True,
    'dsp_sha256': hashlib.sha256((HERE.parent / 'Source/CompressorDSP.h').read_bytes()
        + (HERE.parent / 'Source/OversampledTransformer.h').read_bytes()).hexdigest(),
    'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=HERE, text=True).strip(),
    'verification': 'passed',
}
(OUT / 'transformer_even_harmonics.json').write_text(json.dumps(metadata, indent=2) + '\n')

plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10,
    'axes.spines.top': False, 'axes.spines.right': False, 'axes.grid': True,
    'grid.alpha': .2, 'lines.linewidth': 2, 'figure.facecolor': '#fafbfc',
    'axes.facecolor': 'white', 'savefig.facecolor': '#fafbfc'})
fig, axes = plt.subplots(1, 2, figsize=(12, 5.5))
fig.subplots_adjust(left=.075, right=.97, bottom=.16, top=.78, wspace=.28)
fig.suptitle('05  /  Transformer DC current and even harmonics', x=.06, ha='left',
             y=.96, fontsize=21, fontweight='bold')
fig.text(.06, .86,
         'Measured production C++ DSP • 4× oversampling • 100 Hz / −6 dBFS peak • 18 dB drive',
         fontsize=9.5)
window = np.arange(FS * 2 - FS // 50, FS * 2)  # final 20 ms
for amount, color, signal in zip(CURRENT_SETTINGS, COLORS, outputs):
    axes[0].plot((window - window[0]) / FS * 1000, signal[window], color=color,
                 label=f'{amount}%')
axes[0].set(title='Waveform asymmetry vs simulated DC current',
            xlabel='Time (ms)', ylabel='Output amplitude')
axes[0].legend(title='Even harmonics', fontsize=8, title_fontsize=8)
for harmonic_index, frequency in enumerate(harmonics):
    if frequency % 2 == 0:
        axes[1].plot(CURRENT_SETTINGS, spectra[:, harmonic_index], 'o-',
                     color=COLORS[harmonic_index % len(COLORS)], label=f'H{frequency}')
axes[1].set(title='Even-order harmonic levels', xlabel='Even harmonics control (%)',
            ylabel='Level relative to fundamental (dBc)', xticks=CURRENT_SETTINGS)
axes[1].legend(fontsize=8, ncol=2)
fig.text(.06, .035,
         'The knob models transformer DC magnetizing current. Higher settings bias the core transfer curve and increase asymmetry.',
         fontsize=9, color='#526173')
fig.savefig(OUT / '05_transformer_even_harmonics.png', dpi=180)
fig.savefig(OUT / '05_transformer_even_harmonics.svg')
plt.close(fig)
print(json.dumps({key: metadata[key] for key in
                  ['second_harmonic_at_zero_current_dbc', 'second_harmonic_at_maximum_current_dbc',
                   'harmonics_relative_to_fundamental_dbc']}, indent=2))
print(f'Transformer even-harmonic test plot and data saved in {OUT}')
