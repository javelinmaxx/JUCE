#!/usr/bin/env python3
"""Focused square-wave verification of the real 2x transformer processing path."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

os.environ.setdefault('MPLCONFIGDIR', '/tmp/character-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

HERE = Path(__file__).resolve().parent
OUT = HERE / 'phase'
OUT.mkdir(exist_ok=True)
FS = 48000
AMPLITUDE = 0.001  # -60 dBFS peak: isolate linear phase from core saturation


def square(frequency, rate=FS):
    n = np.arange(rate * 2)
    # Exact integer-period squares in this measurement, with no floating edge jitter.
    assert rate % (2 * frequency) == 0
    return np.where(n % (rate // frequency) < rate // (2 * frequency), AMPLITUDE, -AMPLITUDE).astype(np.float32)


def build(directory, old=False):
    source = directory / 'Source'
    measure = directory / 'Measurements'
    source.mkdir(parents=True)
    measure.mkdir()
    for header in (HERE.parent / 'Source').glob('*.h'):
        shutil.copy(header, source)
    shutil.copy(HERE / 'render.cpp', measure)
    shutil.copy(HERE / 'baseline/transformer_before_phase.h' if old else
                HERE.parent / 'Source/OversampledTransformer.h', source / 'OversampledTransformer.h')
    if not old:
        shutil.copy(HERE.parent / 'Source/TransformerResponse.h', source)
    destination = directory / 'render.so'
    subprocess.run(['c++', '-std=c++17', '-O3', '-fPIC', '-shared',
                    str(measure / 'render.cpp'), '-o', str(destination)], check=True)
    lib = ctypes.CDLL(str(destination))
    lib.render.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int, ctypes.c_double,
                          ctypes.c_int] + [ctypes.c_float] * 6 + [ctypes.c_int, ctypes.c_float]
    lib.render.restype = None
    lib.latency_samples.argtypes = []
    lib.latency_samples.restype = ctypes.c_int
    return lib


def render(lib, signal, transformer=True, rate=FS):
    latency = lib.latency_samples()
    data = np.pad(signal.astype(np.float32), (0, latency))
    lib.render(data.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), len(data), rate,
               0, -18, 1, 10, 150, 0, 0, transformer, 0)
    assert np.isfinite(data).all(), 'Nonfinite output'
    # Compensate only the declared FIR latency; do not align away transformer phase.
    return data[latency:latency + len(signal)].astype(np.float64)


def design_response(frequency, rate):
    # Independent frequency-domain transfer of the specified loaded model.
    processing_rate = 2 * rate
    z = np.exp(-2j * np.pi * frequency / processing_rate)
    low_k = np.tan(np.pi * 8 / processing_rate)
    hp = (1 / (1 + low_k)) * (1 - z) / (1 + (low_k - 1) / (low_k + 1) * z)
    k = np.tan(np.pi * min(22000, processing_rate * .4) / processing_rate)
    norm = 1 / (1 + k / .8 + k * k)
    b0 = k * k * norm
    lp = (b0 + 2 * b0 * z + b0 * z * z) / (1 + 2 * (k * k - 1) * norm * z
         + (1 - k / .8 + k * k) * norm * z * z)
    return hp * lp

with tempfile.TemporaryDirectory(prefix='character-phase-') as tmp:
    current = build(Path(tmp) / 'current')
    before = build(Path(tmp) / 'before', old=True)
    assert current.latency_samples() == 64, 'Host latency changed'
    inputs = {f: square(f) for f in (50, 2000)}
    previous = {f: render(before, x) for f, x in inputs.items()}
    outputs = {f: render(current, x) for f, x in inputs.items()}
    clean = render(current, inputs[50], transformer=False)
    assert np.array_equal(clean, inputs[50]), 'Disabled transformer must be exact delayed clean audio'

    harmonics = np.arange(50, 18001, 100)
    x_fft = np.fft.rfft(inputs[50][-FS:])
    new_h = np.fft.rfft(outputs[50][-FS:])[harmonics] / x_fft[harmonics]
    old_h = np.fft.rfft(previous[50][-FS:])[harmonics] / x_fft[harmonics]
    expected = design_response(harmonics, FS)
    error = np.abs(np.angle(new_h / expected, deg=True))
    assert np.max(error) < .25, f'Phase differs from design: {np.max(error):.4f} deg'
    phase = np.unwrap(np.angle(new_h)) * 180 / np.pi
    old_phase = np.unwrap(np.angle(old_h)) * 180 / np.pi
    assert phase[0] > 0, 'Expected low-frequency phase lead'
    assert phase[-1] < -40, 'Expected high-frequency phase lag'

    def droop(y):
        start = y[FS + int(.001 * FS)]
        end = y[FS + int(.009 * FS)]
        return 100 * (1 - end / start)
    new_droop = droop(outputs[50])
    expected_droop = 100 * (1 - np.exp(-2 * np.pi * 8 * .008))
    assert abs(new_droop - expected_droop) < .5, 'Square-wave droop does not match LF pole'

    # Requested phase check at the other common host sample rates. This is a
    # focused measurement, not a rerun of the existing DSP/processor test suites.
    sample_rates = {}
    for rate in (44100, 96000):
        x = square(50, rate)
        y = render(current, x, rate=rate)
        bins = np.arange(50, min(18001, rate // 2), 100)
        h = np.fft.rfft(y[-rate:])[bins] / np.fft.rfft(x[-rate:])[bins]
        delta = np.max(np.abs(np.angle(h / design_response(bins, rate), deg=True)))
        assert delta < .25, f'Phase check failed at {rate} Hz'
        sample_rates[str(rate)] = float(delta)

metrics = {
    'sample_rate_hz': FS, 'input_peak_dbfs': -60, 'drive_db': 0, 'ratio': 1,
    'oversampling_factor': 2, 'compensated_latency_samples': 64,
    'lf_corner_hz': 8, 'hf_corner_hz': 22000, 'hf_q': .8,
    'phase_degrees_at_50hz': float(phase[0]),
    'phase_degrees_at_17950hz': float(phase[-1]),
    'max_design_phase_error_degrees_50_to_17950hz': float(np.max(error)),
    'other_sample_rate_phase_error_degrees': sample_rates,
    'square50_droop_percent_between_1_and_9_ms': new_droop,
    'previous_square50_droop_percent_between_1_and_9_ms': droop(previous[50]),
    'predicted_square50_droop_percent': float(expected_droop),
    'disabled_transformer_is_exact_delayed_input': True,
    'before_transformer_sha256': hashlib.sha256((HERE / 'baseline/transformer_before_phase.h').read_bytes()).hexdigest(),
    'source_sha256': {p: hashlib.sha256((HERE.parent / 'Source' / p).read_bytes()).hexdigest()
                      for p in ['CompressorDSP.h', 'OversampledTransformer.h', 'TransformerResponse.h', 'HighTilt.h']},
    'verification': 'passed',
}
(OUT / 'metrics.json').write_text(json.dumps(metrics, indent=2) + '\n')
np.savez_compressed(OUT / 'square_wave_measurements.npz', frequency_hz=harmonics,
    phase_degrees=phase, previous_phase_degrees=old_phase, magnitude_db=20*np.log10(np.abs(new_h)),
    square50_input=inputs[50], square50_output=outputs[50], square50_before=previous[50],
    square2000_input=inputs[2000], square2000_output=outputs[2000], square2000_before=previous[2000])
np.savetxt(OUT / 'phase_response.csv', np.column_stack([harmonics,phase,old_phase,20*np.log10(np.abs(new_h)),error]),
    delimiter=',', header='frequency_hz,current_phase_deg,previous_phase_deg,current_gain_db,phase_design_error_deg', comments='')

plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'axes.spines.top':False,
    'axes.spines.right':False,'axes.grid':True,'grid.alpha':.18,'lines.linewidth':1.8,
    'figure.facecolor':'#fafbfc','axes.facecolor':'white','savefig.facecolor':'#fafbfc'})
fig, axes = plt.subplots(2,2,figsize=(12,8.5))
fig.subplots_adjust(left=.075,right=.97,bottom=.11,top=.84,wspace=.25,hspace=.42)
fig.suptitle('Transformer phase / square-wave verification', x=.06, ha='left', y=.97,
             fontsize=21, fontweight='bold')
fig.text(.06,.91,'Actual DSP • 48 kHz host / 2× transformer • −60 dBFS peak • compression 1:1 • drive 0 dB',fontsize=10)
fig.text(.06,.87,'64-sample FIR delay removed; transformer phase remains. Both before/after traces use 2× oversampling.',fontsize=9,color='#526173')
colors={'clean':'#9ca3af','old':'#d55e00','new':'#0072b2'}
for f, ax, bounds, title in [(50,axes[0,0],(-2,22),'50 Hz square: LF droop'),
                            (2000,axes[0,1],(-.1,.45),'2 kHz square: edge shaping and HF lag')]:
    t = (np.arange(FS*2)-FS)/FS*1000
    mask=(t>=bounds[0])&(t<=bounds[1])
    ax.plot(t[mask],inputs[f][mask]/AMPLITUDE,color=colors['clean'],label='Input',drawstyle='steps-post')
    ax.plot(t[mask],previous[f][mask]/AMPLITUDE,'--',color=colors['old'],label='Previous simple filters')
    ax.plot(t[mask],outputs[f][mask]/AMPLITUDE,color=colors['new'],label='Loaded transformer model',
            marker='.' if f==2000 else None,markersize=4)
    ax.set(title=title,xlabel='Time relative to rising edge (ms)',ylabel='Amplitude / input peak',xlim=bounds)
    ax.legend(fontsize=8,loc='lower right' if f==50 else 'upper right')
ax=axes[1,0]
ax.semilogx(harmonics,old_phase,'--',color=colors['old'],label='Previous simple filters')
ax.semilogx(harmonics,phase,color=colors['new'],label='Measured from square harmonics')
ax.semilogx(harmonics[::12],np.angle(expected,deg=True)[::12],'o',ms=4,mfc='none',color='#009e73',label='Specified filter response')
ax.axhline(0,lw=1,color='#9ca3af')
ax.set(title='Phase from 50 Hz square-wave harmonics',xlabel='Frequency (Hz)',ylabel='Phase (degrees)',xlim=(50,18000))
ax.legend(fontsize=8)
ax=axes[1,1]
ax.semilogx(harmonics,20*np.log10(np.abs(old_h)),'--',color=colors['old'],label='Previous simple filters')
ax.semilogx(harmonics,20*np.log10(np.abs(new_h)),color=colors['new'],label='Loaded transformer model')
ax.axhline(0,lw=1,color='#9ca3af')
ax.set(title='Corresponding magnitude response',xlabel='Frequency (Hz)',ylabel='Gain (dB)',xlim=(50,18000))
ax.legend(fontsize=8)
fig.text(.06,.037,f'LF model: 8 Hz • HF model: 22 kHz, Q 0.8 • maximum phase error vs design: {np.max(error):.4f}°',fontsize=9,color='#526173')
fig.text(.06,.016,'Behavioral loaded-transformer approximation; no specific hardware calibration. Low-level squares isolate phase from saturation.',fontsize=9,color='#526173')
fig.savefig(OUT/'square_wave_phase.png',dpi=180)
fig.savefig(OUT/'square_wave_phase.svg')
fig.savefig(OUT/'transformer_phase_report.pdf')
plt.close(fig)
print(json.dumps(metrics,indent=2))
print('Focused square-wave verification passed; artifacts:',OUT)
