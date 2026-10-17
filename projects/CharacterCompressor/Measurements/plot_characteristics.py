#!/usr/bin/env python3
"""Measure the production C++ engine and export reproducible figures/data."""
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
from matplotlib.backends.backend_pdf import PdfPages
import numpy as np

HERE = Path(__file__).resolve().parent
OUT = HERE / 'plots'
OUT.mkdir(exist_ok=True)
FS = 48000
NAMES = ['FET (feed-forward)', 'VCA (feedback)', 'Variable-mu', 'Optical']
COLORS = ['#0072b2', '#d55e00', '#009e73', '#8b58a5']
STYLES = ['-', '--', '-', ':']
plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10,
    'axes.spines.top': False, 'axes.spines.right': False, 'axes.grid': True,
    'grid.alpha': 0.18, 'lines.linewidth': 2, 'figure.facecolor': '#fafbfc',
    'axes.facecolor': '#ffffff', 'savefig.facecolor': '#fafbfc'})

def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-12))

with tempfile.TemporaryDirectory(prefix='character-measure-') as temp:
    library = Path(temp) / 'render.so'
    subprocess.run(['c++', '-std=c++17', '-O3', '-shared', '-fPIC',
                    str(HERE / 'render.cpp'), '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.render.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int,
        ctypes.c_double, ctypes.c_int] + [ctypes.c_float] * 6 + [ctypes.c_int, ctypes.c_float]
    lib.render.restype = None
    lib.latency_samples.argtypes = []
    lib.latency_samples.restype = ctypes.c_int
    latency = lib.latency_samples()

    def render(signal, model=0, transformer=False, drive=6, ratio=4):
        # Flush and remove fixed transport delay for correctly aligned envelope
        # and waveform plots. Host-facing audio retains the reported delay.
        output = np.pad(np.asarray(signal, dtype=np.float32), (0, latency))
        lib.render(output.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), len(output),
                   FS, model, -18, ratio, 10, 150, 6, 0, transformer, drive)
        assert np.isfinite(output).all(), 'DSP produced nonfinite samples'
        return output[latency:latency + len(signal)].astype(np.float64)

    print('Measuring static curves...', flush=True)
    levels = np.arange(-48., 1., 1.)
    static = np.empty((4, len(levels)))
    sine_gr = np.empty_like(static)
    t = np.arange(FS * 2) / FS
    for m in range(4):
        for i, level in enumerate(levels):
            amplitude = 10 ** (level / 20)
            y = render(np.full(len(t), amplitude), model=m)
            static[m, i] = db(np.mean(y[-FS // 4:]))
            x = amplitude * np.sin(2 * np.pi * 1000 * t)
            y = render(x, model=m)
            sine_gr[m, i] = db(np.sqrt(np.mean(x[-FS // 4:] ** 2))) - db(np.sqrt(np.mean(y[-FS // 4:] ** 2)))
        print(f'  {NAMES[m]}', flush=True)

    print('Measuring attack/release...', flush=True)
    # DC level step isolates gain control from sine-carrier ripple and peak/RMS offsets.
    time = np.arange(FS * 4) / FS
    hot = (time >= 0.1) & (time < 1.1)
    x = np.where(hot, 10 ** (-6 / 20), 10 ** (-36 / 20))
    envelope = np.array([db(x) - db(render(x, model=m)) for m in range(4)])
    metrics = {}
    for m, name in enumerate(NAMES):
        attack = envelope[m, int(0.1 * FS):int(1.1 * FS)]
        settled = float(np.mean(attack[-FS // 10:]))
        recovery = envelope[m, int(1.1 * FS):]
        first_attack = np.flatnonzero(attack >= settled * 0.9)
        first_release = np.flatnonzero(recovery <= recovery[0] * 0.1)
        metrics[name] = {'steady_step_reduction_db': settled,
            'attack_to_90_percent_ms': float(first_attack[0] / FS * 1000) if len(first_attack) else None,
            'release_to_10_percent_ms': float(first_release[0] / FS * 1000) if len(first_release) else None}

    print('Measuring transformer...', flush=True)
    drives = [0, 6, 12, 18]
    tone = 10 ** (-6 / 20) * np.sin(2 * np.pi * 1000 * t)
    iron = np.array([render(tone, ratio=1, transformer=True, drive=d) for d in drives])
    harmonics = np.empty((4, 12))
    for i, y in enumerate(iron):
        spectrum = np.abs(np.fft.rfft(y[-FS:])) * 2 / FS
        harmonics[i] = db(spectrum[np.arange(1, 13) * 1000] / spectrum[1000])
    # Integer-Hz probes and a 1 s analysis window avoid FFT leakage.
    frequencies = np.unique(np.round(np.geomspace(10, 20000, 100)).astype(int))
    response = []
    for f in frequencies:
        probe = 0.001 * np.sin(2 * np.pi * f * t)  # -60 dBFS peak, effectively linear
        y = render(probe, ratio=1, transformer=True, drive=6)
        fundamental = np.abs(np.fft.rfft(y[-FS:]))[f] * 2 / FS
        response.append(float(db(fundamental / 0.001)))
    response = np.array(response)
    high_tone = 10 ** (-6 / 20) * np.sin(2 * np.pi * 10000 * t)
    high_output = render(high_tone, ratio=1, transformer=True, drive=18)
    alias_spectrum = db(np.abs(np.fft.rfft(high_output[-FS:])) * 2 / FS)
    metrics['Transformer'] = {'drive18_1khz_third_harmonic_dbc': float(harmonics[-1, 2]),
        'drive18_10khz_alias18khz_dbfs': float(alias_spectrum[18000]),
        'drive18_10khz_fundamental_dbfs': float(alias_spectrum[10000])}

np.savez_compressed(OUT / 'measurements.npz', levels_dbfs_peak=levels,
    dc_output_dbfs=static, sine_gain_reduction_db=sine_gr, time_s=time,
    step_gain_reduction_db=envelope, drives_db=drives,
    transformer_tone_output=iron, harmonic_dbc=harmonics,
    response_frequency_hz=frequencies, response_gain_db=response,
    alias_spectrum_dbfs=alias_spectrum)
np.savetxt(OUT / 'static_curves.csv', np.column_stack([levels, static.T, sine_gr.T]),
    delimiter=',', header='input_dbfs_peak,FET_dc,VCA_dc,Mu_dc,Optical_dc,FET_sine_GR,VCA_sine_GR,Mu_sine_GR,Optical_sine_GR', comments='')
metadata = {'sample_rate_hz': FS, 'threshold_dbfs': -18, 'ratio_control': 4,
    'attack_control_ms': 10, 'release_control_ms': 150, 'knee_db': 6,
    'detector_hpf_hz': 0, 'input_output_gain_db': 0, 'mix': 1,
    'oversampling_factor': 2, 'compensated_latency_samples': latency,
    'dsp_sha256': {name: hashlib.sha256((HERE.parent / 'Source' / name).read_bytes()).hexdigest()
                   for name in ['CompressorDSP.h', 'OversampledTransformer.h', 'TransformerResponse.h']},
    'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=HERE, text=True).strip(),
    'metrics': metrics}
(OUT / 'metrics.json').write_text(json.dumps(metadata, indent=2) + '\n')

subtitle = 'Measured C++ DSP • 48 kHz • threshold −18 dBFS • ratio 4:1 • knee 6 dB • attack 10 ms / release 150 ms'
figures = []

def finish(fig, name, note):
    fig.text(0.06, 0.018, note, fontsize=9, color='#526173')
    fig.savefig(OUT / f'{name}.png', dpi=180)
    fig.savefig(OUT / f'{name}.svg')
    figures.append(fig)

fig, axes = plt.subplots(1, 2, figsize=(12, 5.4))
fig.subplots_adjust(left=.075, right=.97, bottom=.18, top=.77, wspace=.25)
fig.suptitle('01  /  Compression curves', x=.06, ha='left', fontsize=21, fontweight='bold', y=.97)
fig.text(.06, .87, subtitle, fontsize=9.4)
axes[0].plot(levels, levels, color='#a5aab1', lw=1, label='Unity')
for m in range(4):
    axes[0].plot(levels, static[m], STYLES[m], color=COLORS[m], label=NAMES[m])
    axes[1].plot(levels, sine_gr[m], STYLES[m], color=COLORS[m], label=NAMES[m])
for ax in axes:
    ax.axvline(-18, color='#999999', ls=':', lw=1)
    ax.set_xlabel('Input level (dBFS peak)')
    ax.set_xlim(-48, 0)
axes[0].set(title='Steady DC transfer — control-law measurement', ylabel='Output level (dBFS)')
axes[1].set(title='1 kHz sine — measured RMS gain reduction', ylabel='Gain reduction (dB)')
axes[0].legend(fontsize=8, loc='upper left')
axes[1].legend(fontsize=8, loc='upper left')
finish(fig, '01_compression_curves', 'Detector HPF and transformer off. Each point: 2 s settling run; final 250 ms measured. FET / optical DC curves overlap.')

fig, axes = plt.subplots(1, 3, figsize=(13, 5.4))
fig.subplots_adjust(left=.065, right=.98, bottom=.18, top=.77, wspace=.30)
fig.suptitle('02  /  Attack and recovery', x=.06, ha='left', fontsize=21, fontweight='bold', y=.97)
fig.text(.06, .87, subtitle, fontsize=9.4)
for m in range(4):
    axes[0].plot(time[::48], envelope[m, ::48], STYLES[m], color=COLORS[m], label=NAMES[m])
    mask = (time >= .1) & (time <= .2)
    axes[1].plot((time[mask] - .1)*1000, envelope[m, mask], STYLES[m], color=COLORS[m])
    mask = time >= 1.1
    axes[2].plot((time[mask][::48]-1.1)*1000, envelope[m, mask][::48], STYLES[m], color=COLORS[m])
axes[0].axvspan(.1, 1.1, color='#c7cbd0', alpha=.18)
axes[0].set(title='DC level: −36 → −6 → −36 dBFS', xlabel='Time (s)', ylabel='Gain reduction (dB)', xlim=(0,4))
axes[0].legend(fontsize=8)
axes[1].set(title='Attack close-up', xlabel='Time after upward step (ms)', xlim=(0,100))
axes[2].set(title='Recovery close-up', xlabel='Time after downward step (ms)', xlim=(0,2500))
for ax in axes:
    ax.set_ylim(-.15, 10)
finish(fig, '02_attack_release', 'DC isolates envelope dynamics; detector HPF and transformer off. Knob values are not measured settling times.')

fig, axes = plt.subplots(2, 2, figsize=(12, 8.7))
fig.subplots_adjust(left=.075, right=.97, bottom=.10, top=.85, wspace=.25, hspace=.44)
fig.suptitle('03  /  Output transformer character', x=.06, ha='left', fontsize=21, fontweight='bold', y=.97)
fig.text(.06, .91, 'Measured C++ DSP • 48 kHz • 2× transformer • compression disabled (1:1) • 0 dB input/output gain', fontsize=10)
a = axes[0,0]
segment = np.arange(FS*2-96, FS*2)
a.plot((segment-segment[0])/FS*1000, tone[segment], color='#a5aab1', lw=1, label='Clean input')
for i,d in enumerate(drives):
    a.plot((segment-segment[0])/FS*1000, iron[i,segment], color=COLORS[i], label=f'{d} dB drive')
a.set(title='1 kHz at −6 dBFS peak: waveform shaping', xlabel='Time (ms)', ylabel='Amplitude')
a.legend(fontsize=8, ncol=2)
a = axes[0,1]
for i,d in enumerate(drives):
    a.plot(np.arange(1,13), harmonics[i], 'o-', color=COLORS[i], ms=3, label=f'{d} dB drive')
a.set(title='1 kHz harmonics relative to output fundamental', xlabel='Harmonic number', ylabel='Level (dBc)', ylim=(-120,5), xticks=np.arange(1,13))
a.legend(fontsize=8)
a = axes[1,0]
a.semilogx(frequencies, response, color=COLORS[0])
a.axhline(0, color='#a5aab1', ls='--', lw=1)
a.set(title='Small-signal response: −60 dBFS, 6 dB drive', xlabel='Frequency (Hz)', ylabel='Fundamental gain (dB)', xlim=(10,20000), ylim=(-8,1))
a = axes[1,1]
f = np.arange(len(alias_spectrum))
a.plot(f/1000, alias_spectrum, color=COLORS[3], lw=1)
baseline_path = HERE / 'baseline/alias_spectrum.csv'
if baseline_path.exists():
    baseline = np.loadtxt(baseline_path, delimiter=',', skiprows=1)
    a.plot(baseline[:,0]/1000, baseline[:,1], color='#a5aab1', lw=1, alpha=.65, label='Previous 1×')
    a.plot(f/1000, alias_spectrum, color=COLORS[3], lw=1, label='Current 2×')
    a.legend(fontsize=8)
a.set(title='10 kHz at −6 dBFS peak, 18 dB drive', xlabel='Frequency (kHz)', ylabel='Output spectral level (dBFS)', xlim=(0,24), ylim=(-120,0))
a.annotate('18 kHz alias bin', xy=(18,max(-115,alias_spectrum[18000])), xytext=(11,-14), fontsize=9,
           arrowprops={'arrowstyle':'->','color':'#526173'})
finish(fig, '03_transformer', 'Spectra: final 1 s of coherent 2 s tones. 2× oversampling reduces aliasing; residual products remain possible. Fixed delay removed.')
with PdfPages(OUT / 'character_compressor_report.pdf') as pdf:
    for fig in figures:
        pdf.savefig(fig)
for fig in figures:
    plt.close(fig)
print(json.dumps(metrics, indent=2), flush=True)
print(f'Figures, raw data, and PDF saved to {OUT}', flush=True)
