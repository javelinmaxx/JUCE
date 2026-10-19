#!/usr/bin/env python3
"""Measure generated-harmonic filtering and the high shelf in production DSP."""
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
OUT = HERE / 'output_stage'
OUT.mkdir(exist_ok=True)
FS = 48000
LATENCY = 64


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-15))


def build(directory, before=False):
    source = directory / 'Source'
    measure = directory / 'Measurements'
    source.mkdir(parents=True)
    measure.mkdir()
    for header in (HERE.parent / 'Source').glob('*.h'):
        shutil.copy(header, source)
    if before:
        shutil.copy(HERE / 'baseline/transformer_before_frequency.h', source / 'OversampledTransformer.h')
    shutil.copy(HERE / 'render_output.cpp', measure)
    library = directory / 'render.so'
    subprocess.run(['c++', '-std=c++17', '-O3', '-shared', '-fPIC',
                    str(measure / 'render_output.cpp'), '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.render_output.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int,
        ctypes.c_double, ctypes.c_float, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.render_output.restype = None
    lib.render_detector_shelf.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int, ctypes.c_double]
    lib.render_detector_shelf.restype = None
    lib.render_sidechain.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int,
        ctypes.c_double, ctypes.c_int, ctypes.c_int, ctypes.c_float]
    lib.render_sidechain.restype = None
    return lib


def render(lib, x, rate=FS, drive=6, transformer=True, tilt=False, bypass=False):
    y = np.pad(np.asarray(x, dtype=np.float32), (0, LATENCY))
    lib.render_output(y.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), len(y),
                      rate, drive, transformer, tilt, bypass)
    assert np.isfinite(y).all(), 'Nonfinite output'
    return y[LATENCY:LATENCY+len(x)].astype(np.float64)


def tone(frequency, level=-12, rate=FS):
    return (10**(level/20) * np.sin(2*np.pi*frequency*np.arange(2*rate)/rate)).astype(np.float32)


def detector_shelf(lib, x, rate=FS):
    y = x.copy()
    lib.render_detector_shelf(y.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), len(y), rate)
    return y.astype(np.float64)


def sidechain(lib, x, model, tilt, rate=FS, highpass=0):
    y = np.pad(x, (0, LATENCY))
    lib.render_sidechain(y.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), len(y), rate, model, tilt, highpass)
    assert np.isfinite(y).all()
    return y[LATENCY:LATENCY+len(x)].astype(np.float64)


def spectrum(y, rate=FS):
    return np.fft.rfft(y[-rate:]) * 2 / rate


def thd(spec, frequency, rate=FS):
    # H2-H10, limited to bins below Nyquist; high-frequency values can reach
    # floating-point/alias floors and are not a claim of a strict band limit.
    bins = np.arange(2, min(10, (rate//2-1)//frequency)+1) * frequency
    return 100 * np.linalg.norm(spec[bins]) / abs(spec[frequency])


def lowpass_db(frequencies, rate):
    # Independent second-order Butterworth magnitude, prewarped at 500 Hz.
    ratio = np.tan(np.pi*frequencies/(2*rate)) / np.tan(np.pi*500/(2*rate))
    return -10 * np.log10(1 + ratio**4)


with tempfile.TemporaryDirectory(prefix='character-output-') as tmp:
    current = build(Path(tmp)/'current')
    before = build(Path(tmp)/'before', before=True)
    frequencies = np.unique(np.r_[np.round(np.geomspace(30, 5000, 28)).astype(int),
                                  [50, 100, 250, 500, 1000, 2000, 5000]])
    drives = [0, 6, 12, 18]
    thds = np.empty((len(drives), len(frequencies)))
    baseline = np.empty(len(frequencies))
    for i, frequency in enumerate(frequencies):
        x = tone(frequency)
        baseline[i] = thd(spectrum(render(before, x)), frequency)
        for j, drive in enumerate(drives):
            thds[j, i] = thd(spectrum(render(current, x, drive=drive)), frequency)
    print('THD sweep finished', flush=True)

    # Fixed 100 Hz stimulus isolates harmonic filtering from changes in the
    # fundamental. Compare absolute harmonic amplitudes, not dBc/THD ratios.
    x = tone(100, level=-6)
    old_spec = spectrum(render(before, x, drive=12))
    new_spec = spectrum(render(current, x, drive=12))
    # This symmetric steady-state stimulus produces odd harmonics. Even bins
    # sit at the numerical floor and cannot provide a meaningful filter ratio.
    harmonics = np.arange(3, 20, 2)*100
    attenuation = db(new_spec[harmonics] / old_spec[harmonics])
    expected = lowpass_db(harmonics, FS)
    audible = (db(old_spec[harmonics]) > -100) & (db(new_spec[harmonics]) > -115)
    attenuation_error = float(np.max(np.abs(attenuation[audible]-expected[audible])))
    assert attenuation_error < .15, 'Generated harmonics do not follow the 500 Hz residual filter'
    assert abs(attenuation[list(harmonics).index(500)] + 3.0103) < .05

    # Probe the fifth harmonic of separate tones to keep the measured 500 Hz,
    # 1 kHz and 2 kHz distortion bins well above the floating-point floor.
    probe_attenuation = {}
    for fundamental in [100, 200, 400]:
        probe = tone(fundamental, level=-6)
        old = spectrum(render(before, probe, drive=12))[5*fundamental]
        new = spectrum(render(current, probe, drive=12))[5*fundamental]
        measured = float(db(new/old))
        prediction = float(lowpass_db(5*fundamental, FS))
        assert abs(measured-prediction) < .05, 'Fifth-harmonic roll-off mismatch'
        probe_attenuation[str(5*fundamental)] = measured

    # Measure the actual detector shelf class. At ratio 1:1 the complete audio
    # path must remain bit-identical with tilt on/off, even with transformer on.
    shelf_frequencies = np.unique(np.r_[np.round(np.geomspace(20, 20000, 45)).astype(int),
                                        [100, 500, 1500, 10000]])
    shelf = []
    output_delta = []
    for frequency in shelf_frequencies:
        x = tone(frequency, level=-60)
        plain = spectrum(x)[frequency]
        y = detector_shelf(current, x)
        shelf.append(float(db(spectrum(y)[frequency]/plain)))
        for transformer in [False, True]:
            off = render(current, x, transformer=transformer)
            on = render(current, x, transformer=transformer, tilt=True)
            assert np.array_equal(off, on), 'Sidechain tilt directly altered audio at ratio 1:1'
        output_delta.append(float(db(spectrum(on)[frequency]/spectrum(off)[frequency])))
    shelf = np.array(shelf)
    assert abs(shelf[list(shelf_frequencies).index(1500)]-3) < .01
    assert abs(shelf[-1]-6) < .01
    assert abs(shelf[0]) < .01
    assert np.max(np.abs(output_delta)) < .00001
    assert np.all(np.diff(shelf) > -0.001), 'Shelf should rise monotonically'

    # Compare current vs previous small-signal response with shelf off. The new
    # filter must not low-pass the clean high-frequency signal.
    clean_response_delta = {}
    for frequency in [1000, 5000, 10000, 18000]:
        x = tone(frequency, level=-60)
        a = spectrum(render(before, x))[frequency]
        b = spectrum(render(current, x))[frequency]
        delta = float(db(b/a))
        assert abs(delta) < .01, 'Clean HF response changed'
        clean_response_delta[str(frequency)] = delta

    other_rates = {}
    for rate in [44100, 96000]:
        x = tone(100, level=-6, rate=rate)
        a = spectrum(render(before, x, rate=rate, drive=12), rate)
        b = spectrum(render(current, x, rate=rate, drive=12), rate)
        delta = db(b[harmonics]/a[harmonics])
        mask = (db(a[harmonics]) > -100) & (db(b[harmonics]) > -115)
        error = float(np.max(np.abs(delta[mask]-lowpass_db(harmonics, rate)[mask])))
        assert error < .15, f'Harmonic filter mismatch at {rate}'
        x = tone(1500, level=-60, rate=rate)
        y = detector_shelf(current, x, rate=rate)
        shelf_mid = float(db(spectrum(y, rate)[1500]/spectrum(x, rate)[1500]))
        assert abs(shelf_mid-3) < .01
        other_rates[str(rate)] = {'harmonic_attenuation_max_error_db': error, 'shelf_midpoint_db': shelf_mid}

    # Verify the routing through each compressor's actual gain-control path.
    # Same input and parameters, fresh engines, only the sidechain switch differs.
    sidechain_results = {}
    for model, name in enumerate(['FET', 'VCA', 'Variable-mu', 'Optical']):
        measurements = {}
        for frequency in [100, 1500, 10000]:
            x = tone(frequency)
            off = sidechain(current, x, model, False)
            on = sidechain(current, x, model, True)
            extra_gr = float(10*np.log10(np.sum(off[-FS:]**2)/np.sum(on[-FS:]**2)))
            measurements[str(frequency)] = extra_gr
        assert abs(measurements['100']) < .05, f'Unexpected bass sensitivity: {name}'
        assert measurements['10000'] > measurements['1500'] > .5, f'Tilt does not drive sidechain: {name}'
        # Verify composition with the existing detector HPF.
        x = tone(10000)
        off = sidechain(current, x, model, False, highpass=80)
        on = sidechain(current, x, model, True, highpass=80)
        with_hpf = float(10*np.log10(np.sum(off[-FS:]**2)/np.sum(on[-FS:]**2)))
        assert with_hpf > 1, f'Tilt lost with detector HPF: {name}'
        measurements['10000_with_hpf80'] = with_hpf
        sidechain_results[name] = measurements

    x = tone(1000)
    assert np.array_equal(render(current, x, transformer=False), x), 'Disabled stages change audio'
    assert np.array_equal(render(current, x, tilt=True, bypass=True), x), 'Bypass must bypass both stages'
    # Drive should increase low-frequency distortion throughout the knob range.
    assert np.all(np.diff(thds[:, list(frequencies).index(50)]) > 0)

metrics = {
    'sample_rate_hz': FS, 'oversampling_factor': 2, 'latency_samples': LATENCY,
    'thd_input_peak_dbfs': -12, 'thd_harmonics': 'H2-H10 below Nyquist',
    'distortion_filter': '500 Hz second-order Butterworth (12 dB/octave), nonlinear residual only',
    'harmonic_comparison': {'input_hz': 100, 'peak_dbfs': -6, 'drive_db': 12,
        'max_attenuation_error_db': attenuation_error,
        'fifth_harmonic_probe_attenuation_db': probe_attenuation},
    'thd_percent_drive6': {str(f): {'before': float(baseline[list(frequencies).index(f)]),
        'after': float(thds[1,list(frequencies).index(f)])} for f in [50,100,250,500,1000,2000,5000]},
    'shelf_gain_db': {str(f): float(shelf[list(shelf_frequencies).index(f)]) for f in [100,500,1500,10000]},
    'shelf_location': 'sidechain after detector HPF, before rectification and stereo linking',
    'ratio1_output_identical_with_tilt_on_off': True,
    'sidechain_extra_gain_reduction_db': sidechain_results,
    'clean_hf_response_change_db': clean_response_delta,
    'other_sample_rates': other_rates,
    'disabled_stages_exact_delayed_input': True, 'bypass_exact_delayed_input': True,
    'source_sha256': {name: hashlib.sha256((HERE.parent/'Source'/name).read_bytes()).hexdigest()
        for name in ['CompressorDSP.h','OversampledTransformer.h','TransformerResponse.h','HighTilt.h']},
    'before_transformer_sha256': hashlib.sha256((HERE/'baseline/transformer_before_frequency.h').read_bytes()).hexdigest(),
    'verification': 'passed',
}
(OUT/'metrics.json').write_text(json.dumps(metrics, indent=2)+'\n')
np.savetxt(OUT/'thd.csv', np.column_stack([frequencies,baseline,thds.T]), delimiter=',',
    header='input_frequency_hz,before_drive6_thd_percent,drive0_thd_percent,drive6_thd_percent,drive12_thd_percent,drive18_thd_percent', comments='')
np.savetxt(OUT/'harmonics.csv', np.column_stack([harmonics,db(old_spec[harmonics]),db(new_spec[harmonics]),attenuation,expected]), delimiter=',',
    header='harmonic_frequency_hz,before_dbfs,after_dbfs,measured_attenuation_db,expected_attenuation_db', comments='')
np.savetxt(OUT/'shelf.csv', np.column_stack([shelf_frequencies,shelf,output_delta]), delimiter=',',
    header='frequency_hz,detector_shelf_gain_db,ratio1_output_change_db', comments='')

plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'axes.spines.top':False,
    'axes.spines.right':False,'axes.grid':True,'grid.alpha':.18,'lines.linewidth':1.8,
    'figure.facecolor':'#fafbfc','axes.facecolor':'white','savefig.facecolor':'#fafbfc'})
fig, axes = plt.subplots(2,2,figsize=(12,8.5))
fig.subplots_adjust(left=.08,right=.97,bottom=.11,top=.84,wspace=.28,hspace=.46)
fig.suptitle('Transformer distortion / high-tilt verification', x=.06, ha='left', y=.97, fontsize=21, fontweight='bold')
fig.text(.06,.91,'Production C++ DSP • 48 kHz • 2× transformer • compression 1:1 • fixed 64-sample delay removed',fontsize=10)
fig.text(.06,.87,'500 Hz / 12 dB per octave filtering applies to generated distortion; the clean high-frequency signal remains.',fontsize=9,color='#526173')
colors=['#0072b2','#d55e00','#009e73','#8b58a5']
ax=axes[0,0]
ax.loglog(frequencies,baseline,'--',color='#9299a3',label='Before / 6 dB drive')
for i,d in enumerate(drives):
    ax.loglog(frequencies,thds[i],color=colors[i],label=f'{d} dB drive')
ax.axvline(500,color='#aaa',lw=1)
ax.set(title='Distortion vs input frequency / −12 dBFS peak',xlabel='Input frequency (Hz)',ylabel='THD H2–H10 (%)',ylim=(1e-5,100))
ax.legend(fontsize=8,ncol=2)
ax=axes[0,1]
ax.semilogx(harmonics,expected,color='#9299a3',label='Specified 500 Hz low-pass')
ax.semilogx(harmonics[audible],attenuation[audible],'o',ms=4,color=colors[0],label='100 Hz tone / odd harmonics')
ax.semilogx([int(f) for f in probe_attenuation], list(probe_attenuation.values()),
    'x',ms=6,color=colors[1],label='Fifth harmonic of 100 / 200 / 400 Hz')
ax.axvline(500,color='#aaa',lw=1)
ax.set(title='Harmonic filtering / −6 dBFS, drive 12 dB',xlabel='Generated harmonic frequency (Hz)',ylabel='After / before amplitude (dB)')
ax.legend(fontsize=8)
ax=axes[1,0]
ax.semilogx(shelf_frequencies,shelf,color=colors[0],label='Sidechain detector shelf')
ax.semilogx(shelf_frequencies,output_delta,'--',color=colors[2],label='Audio output change at ratio 1:1')
ax.axvline(1500,color='#aaa',lw=1)
ax.axhline(6,color='#aaa',lw=1)
ax.plot(1500,3,'o',color=colors[0])
ax.set(title='Sidechain High Tilt / −60 dBFS peak',xlabel='Frequency (Hz)',ylabel='Gain (dB)',ylim=(-.3,6.5))
ax.legend(fontsize=8)
ax=axes[1,1]
ax.plot(harmonics,db(old_spec[harmonics]),'o--',color='#9299a3',label='Before')
ax.plot(harmonics,db(new_spec[harmonics]),'o-',color=colors[1],label='Filtered distortion')
ax.axvline(500,color='#aaa',lw=1)
ax.set(title='Odd harmonics / 100 Hz, −6 dBFS, drive 12 dB',xlabel='Generated harmonic frequency (Hz)',ylabel='Harmonic amplitude (dBFS)',ylim=(-130,-20))
ax.legend(fontsize=8)
fig.text(.06,.04,'Sidechain shelf: 1.5 kHz midpoint (+3 dB), +6 dB plateau. No direct output EQ. Tilt off in distortion measurements.',fontsize=9,color='#526173')
fig.text(.06,.017,'Roll-off is gradual, not a brick-wall cutoff. Very low THD values include floating-point and residual alias limits.',fontsize=9,color='#526173')
fig.savefig(OUT/'output_stage.png',dpi=180)
fig.savefig(OUT/'output_stage.svg')
fig.savefig(OUT/'output_stage_report.pdf')
plt.close(fig)
print(json.dumps(metrics,indent=2))
print('Focused output-stage verification passed:',OUT)
