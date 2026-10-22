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
axes[0].set(ylabel='Phase relative to compensated 50 ms latency (degrees)', xlabel='Frequency (Hz)', title='Independent low/high rotation')
axes[1].set(ylabel='Magnitude (dB)', xlabel='Frequency (Hz)', title='Finite-FIR magnitude ripple')
axes[0].legend(fontsize=7)
for ax in axes: ax.grid(alpha=.25)
fig.savefig(root/'manual-phase.png', dpi=160); plt.close(fig)
fig, ax = plt.subplots(figsize=(10, 4), constrained_layout=True)
for key in ['depthminus100', 'depth0', 'depth50', 'depth100']:
    ax.plot(audio['sample']/48, audio[key], label=key.replace('depth', 'Auto Depth ').replace('minus','−')+'%', alpha=.8)
ax.set(xlabel='Time (ms), including reference latency', ylabel='Amplitude', title='Phase-guided alignment of a dispersed transient', xlim=(65,90))
ax.legend(); ax.grid(alpha=.25); fig.savefig(root/'transient-alignment.png', dpi=160); plt.close(fig)
# A separate case runs the actual background detector, handoff and crossfade;
# no target is injected and both manual rotations remain zero with unity gain.
live = np.genfromtxt(root/'auto-live.csv', delimiter=',', names=True)
curve = np.genfromtxt(root/'auto-phase.csv', delimiter=',', names=True)
fig, axes = plt.subplots(3, 1, figsize=(11, 9), constrained_layout=True)
for key, label in [('reference', 'Auto Off (50 ms reference)'), ('automatic', 'Live Auto, Depth 100%')]:
    axes[0].plot(live['sample']/48000, live[key], label=label, alpha=.75, linewidth=.7)
    rows = live[(live['sample'] >= 36864+2400+850) & (live['sample'] <= 36864+2400+1450)]
    axes[1].plot((rows['sample']-36864-2400)/48, rows[key], label=label, alpha=.8)
axes[0].set(xlabel='Stream time (seconds, including 50 ms latency)', ylabel='Amplitude', title='Live automatic phase correction: three dispersed hits')
axes[1].set(xlabel='Time relative to the last hit (ms, reference delay removed)', ylabel='Amplitude', title='Last hit after the worker has detected and applied correction')
# Unwrap before plotting so a +/-180 degree display wrap is not mistaken for
# a discontinuity in the complex frequency response.
axes[2].semilogx(curve['frequency'], np.unwrap(np.deg2rad(curve['target_deg']))*180/np.pi, label='Connected target')
axes[2].semilogx(curve['frequency'], np.unwrap(np.deg2rad(curve['measured_deg']))*180/np.pi, '--', label='Measured FIR', alpha=.8)
axes[2].set(xlabel='Frequency (Hz)', ylabel='Unwrapped rotation (degrees)', title='Detected-target phase response: neighboring bands remain connected')
for ax in axes: ax.grid(alpha=.25); ax.legend(fontsize=8)
fig.savefig(root/'auto-live.png', dpi=160); plt.close(fig)
live_rows=[]
for number, start in enumerate([4096,20480,36864], 1):
    rows=live[start+2400:start+2400+4096]
    before, after=rows['reference'], rows['automatic']
    p0,p1=np.max(abs(before)),np.max(abs(after))
    r0,r1=np.sqrt(np.mean(before**2)),np.sqrt(np.mean(after**2))
    live_rows.append(f'| {number} | {p0:.6f} | {p1:.6f} | {100*(p1/p0-1):.1f}% | {20*np.log10(r1/r0):+.4f} dB |')
import wave
for key, name in [('reference','auto-off.wav'),('automatic','auto-on.wav')]:
    with wave.open(str(root/name), 'wb') as wav:
        wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(48000)
        wav.writeframes((np.clip(live[key],-1,1)*32767).astype('<i2').tobytes())
def measurements(key):
    x=audio[key]
    return np.max(np.abs(x)), np.sqrt(np.mean(x*x))
rows=[]
for key in ['depthminus100','depth0','depth50','depth100']:
    peak,rms=measurements(key)
    label=key[5:].replace('minus','−')
    rows.append(f'| {label}% | {peak:.6f} | {rms:.6f} | {20*np.log10(peak/rms):.3f} dB |')
targets = np.genfromtxt(root/'auto-targets.csv', delimiter=',', names=True)
release = np.genfromtxt(root/'auto-release.csv', delimiter=',', names=True)
wrap = lambda degrees: (degrees+180)%360-180
errors = [wrap(-targets['target_deg']), wrap(targets['positive_deg']-targets['target_deg']), wrap(targets['negative_deg']-targets['target_deg'])]
fig, axes = plt.subplots(1, 2, figsize=(12, 4), constrained_layout=True)
x = np.arange(len(targets))
for i, (name, error) in enumerate(zip(['Auto Off', 'Depth +100%', 'Depth −100%'], errors)):
    axes[0].bar(x+(i-1)*.24, error, width=.24, label=name)
axes[0].set(xticks=x, xticklabels=[f'{f:g}' for f in targets['frequency']], xlabel='Detected band (Hz)', ylabel='Residual phase error (degrees)', title='Phase error relative to the detected transient anchor')
axes[0].axhline(0, color='black', linewidth=.7); axes[0].legend(fontsize=8)
for ms in [40,200,800]:
    a=release[release['release_ms']==ms]
    axes[1].plot(a['sample']/48, a['rotation_deg'], label=f'Transient {ms} ms')
axes[1].set(xlabel='Stream time (ms, including 50 ms latency)', ylabel='Largest applied rotation (degrees)', title='Live worker: phase correction releases after one hit')
axes[1].legend(fontsize=8)
for ax in axes: ax.grid(alpha=.25)
fig.savefig(root/'auto-performance.png', dpi=160); plt.close(fig)
error_rows=[]
for name, error in zip(['Off', '+100%', '−100%'], errors):
    error_rows.append(f'| {name} | {np.mean(abs(error)):.3f}° | {np.max(abs(error)):.3f}° |')
release_rows=[]
for ms in [40,200,800]:
    a=release[release['release_ms']==ms]
    release_rows.append(f"| {ms} ms | {np.max(a['rotation_deg']):.3f}° | {a['rotation_deg'][-1]:.6f}° |")
performance = """## Alignment accuracy, reverse Depth, and Transient release

![Measured phase errors and release behavior](auto-performance.png)

At each detected spectral peak, residual error is the measured FIR rotation
minus the required rotation toward the common transient anchor, wrapped to
±180°. This directly measures phase alignment in addition to comparing peaks.
The deterministic response is frozen at full correction to isolate the alignment
objective from worker timing and release. Intermediate frequencies follow the
connected curve; this table measures the four detected peaks only.

| Auto Depth | Mean absolute residual error | Largest absolute residual error |
| --- | ---: | ---: |
"""+'\n'.join(error_rows)+"""

Negative Depth reverses the correction: on this constructed hit it doubles the
phase error and reduces the peak, while positive Depth moves the detected bands
toward the shared anchor. RMS remains approximately equal. This is a creative
reverse-alignment control, not a guarantee of lower peak for every input.

The release plot uses a separate **live-worker** single-hit stream, with Depth
−100% to also verify that negative settings activate analysis. It measures the
actual applied FIR response through audio telemetry, including kernel handoffs
and crossfades, at Transient 40/200/800 ms. Time follows processed audio samples,
so offline rendering speed does not change the envelope. Release is exponential:
after one selected time constant, 36.8% of the underlying target strength remains; after
about seven time constants, the response returns to manual-only processing.
The phase cap can hold a saturated response at its boundary until the target
strength has fallen far enough for the visible phase to start decreasing.
The release starts after the protected onset hold (lookahead + complete FIR
response + 8 ms safety margin), rather than the first sample of the hit. Rebuilds occur at up to 50 Hz during release and use the
existing 5 ms response crossfade; the plotted envelope includes that stepping.

| Transient | Largest applied rotation during test | Rotation at end of 1.25 s stream |
| --- | ---: | ---: |
"""+'\n'.join(release_rows)+"""

The short release must return below 1°; the long release must retain more
correction than the medium release. Other checks cover zero Depth, host bypass,
linked stereo, fixed latency, state recall of all eight parameters, 24-band
coverage into the high frequencies, and no repeated triggers on stationary noise.

These are reproducible synthetic cases, not a listening benchmark for all music.
The method prioritizes a detected transient anchor; conflicting components,
polyphonic material, low-frequency resolution, finite-FIR magnitude ripple,
and worker scheduling can limit improvement. Higher peak at unchanged RMS is a
punch indicator, not proof of perceived quality or reconstruction of an original
undispersed source.
"""
stereo = np.genfromtxt(root/'stereo-link.csv', delimiter=',', names=True)
stereo_live = np.genfromtxt(root/'stereo-link-live.csv', delimiter=',', names=True)
fig, axes = plt.subplots(2, 2, figsize=(12, 7), constrained_layout=True)
stereo_rows=[]
for link in [-100,-50,0,50,100]:
    a=stereo[stereo['link']==link]
    left,right=a['left'],a['right']
    spacing=abs(np.argmax(abs(left))-np.argmax(abs(right)))/48
    correlation=np.dot(left,right)/np.sqrt(np.dot(left,left)*np.dot(right,right))
    stereo_rows.append(f'| {link:+}% | {spacing:.3f} ms | {correlation:.4f} |')
for ax,link in zip(axes.flat,[-100,0,100]):
    a=stereo[stereo['link']==link]
    ax.plot(a['sample']/48,a['left'],label='Left',alpha=.8)
    ax.plot(a['sample']/48,a['right'],label='Right',alpha=.8)
    ax.set(xlim=(64,83),xlabel='Time (ms, including reference latency)',ylabel='Amplitude',title=f'Stereo Link {link:+}%: identical input hits')
a=stereo_live[(stereo_live['sample']>=38000)&(stereo_live['sample']<44000)]
for key in ['left','right']: axes[1,1].plot((a['sample']-36864)/48,a[key],label=key.title(),alpha=.8)
axes[1,1].set(xlim=(64,85),xlabel='Time from last hit start (ms, including reference latency)',ylabel='Amplitude',title='Live worker at −100% Link: last repeated hit')
for ax in axes.flat: ax.grid(alpha=.25); ax.legend(fontsize=8)
fig.savefig(root/'stereo-link.png',dpi=160); plt.close(fig)
with wave.open(str(root/'stereo-link-live.wav'),'wb') as wav:
    wav.setnchannels(2); wav.setsampwidth(2); wav.setframerate(48000)
    wav.writeframes((np.clip(np.column_stack([stereo_live['left'],stereo_live['right']]),-1,1)*32767).astype('<i2').tobytes())
cap=np.genfromtxt(root/'phase-cap.csv',delimiter=',',names=True)
fig,ax=plt.subplots(figsize=(9,3.5),constrained_layout=True)
for key in ['left_deg','right_deg']: ax.semilogx(cap['frequency'],cap[key],label=key.replace('_deg','').title())
for limit in [-180,180]: ax.axhline(limit,color='black',linestyle='--',linewidth=.8)
ax.set(xlabel='Frequency (Hz)',ylabel='Requested correction (degrees)',ylim=(-195,195),title='Combined correction saturates at ±180° before FIR design')
ax.grid(alpha=.25);ax.legend();fig.savefig(root/'phase-cap.png',dpi=160);plt.close(fig)
cap_report="""
## Strict phase-correction limit

![DSP phase cap](phase-cap.png)

The final combined manual, automatic, and Stereo Link correction now saturates
at ±180° at every frequency **before** conversion to a complex FIR target.
A +200° request stops at +180°; it is not wrapped to −160°. The float limit is
chosen just below mathematical pi to avoid rounding +pi across the seam when
converting it to a complex value. No extra phase turns are used for separation.

The regression sweeps 20 Hz–20 kHz, manual −180/0/+180°, Auto Depth
−100/0/+100%, Stereo Link −100/−50/0/+50/+100%, and both channels. Every
combined correction must stay within ±pi, and positive/negative over-range
requests must retain their sign. The plot uses a deliberately over-range
synthetic target with maximum negative Link to show both saturated boundaries.
The finite FIR approximates the bounded target and can have small measured
phase ripple; the cap applies to the actual correction requested from its design.
Data: [phase-cap.csv](phase-cap.csv).

"""
ahead=np.genfromtxt(root/'lookahead.csv',delimiter=',',names=True)
fig,axes=plt.subplots(2,2,figsize=(12,6),constrained_layout=True)
ahead_rows=[]
# The full-buffer peak anchors the expected protected body; production guards
# use the detected per-onset anchor and include extra safety on both ends.
normal=ahead[ahead['late_worker']==0]
anchor=normal['sample'][np.argmax(abs(normal['reference']))]-2400
for late in [0,1]:
    a=ahead[ahead['late_worker']==late]
    t=a['sample']/48
    axes[0,late].plot(t,a['reference'],label='50 ms reference',alpha=.8)
    axes[0,late].plot(t,a['automatic'],label='Live Auto',alpha=.8)
    axes[0,late].set(xlim=((anchor+2400)/48-12,(anchor+2400)/48+12),xlabel='Stream time (ms)',ylabel='Amplitude',title='Normal worker' if late==0 else 'Deliberately late worker: stable fallback')
    axes[1,late].plot(t,a['filter_change'],label='Applied complex-response change')
    first,last=(anchor+1200)/48,(anchor+3120)/48
    axes[1,late].axvspan(first,last,color='green',alpha=.15,label='Checked onset interval')
    axes[1,late].set(xlabel='Stream time (ms)',ylabel='Largest change between meter snapshots',title='No filter retuning through the onset',xlim=(anchor/48-10,(anchor+6000)/48))
    protected=a[(a['sample']>=anchor+1200)&(a['sample']<anchor+3000)]
    ahead_rows.append(f"| {'Late' if late else 'Normal'} | {np.max(protected['filter_change']):.8f} |")
for ax in axes.flat: ax.grid(alpha=.25);ax.legend(fontsize=7)
fig.savefig(root/'lookahead.png',dpi=160);plt.close(fig)
ahead_report="""## Lookahead and onset stability

![Live lookahead and delayed-worker regression](lookahead.png)

The input now has 25 ms of actual delay **before** phase convolution, while
analysis continues to see the raw input. Together with the 25 ms FIR center,
reported host latency is 50 ms at every setting, including bypass and Auto Off.
The entire finite response can extend to approximately 75 ms after input.

Auto gathers 5 ms of context and latches the detected target once per onset.
Repeated detections from overlapping FFT frames do not repeatedly redesign it.
Target strength holds through the lookahead and full FIR response plus an
8 ms safety margin; Transient release starts after that hold. An onset more
than 40 ms later can begin a new collection. Responses arriving too late for
the pre-onset 5 ms crossfade are held until the protected hit has passed.

The test streams a dispersed hit through the real worker with Transient at
its fastest 20 ms setting. It records changes to the actual applied complex
FIR response, and requires it to stay fixed through an interval containing
the delayed transient peak. A second run deliberately pauses the worker until
its safe pre-onset deadline has passed: the hit must retain the previous,
neutral response, and correction must recover after the protected interval.
No audio callback waits for the worker.

| Worker | Largest filter change in checked onset interval |
| --- | ---: |
"""+'\n'.join(ahead_rows)+"""

The zero changes demonstrate removal of kernel modulation in this constructed
onset interval; static phase rotation can still reshape or ring on a transient.
A late hit may receive less correction instead of a warbling crossfade.
Closely spaced transients, manual automation, and arbitrary recordings are not
promised to match this isolated synthetic case. Additional checks cover exact
50 ms neutral/bypass latency at 8/44.1/48/96/192 kHz, the entire reported tail,
and safe transition boundaries at each rate.
Data: [lookahead.csv](lookahead.csv).

"""
stereo_report="""## Stereo Link: shared, independent, and separating correction

![Measured Stereo Link comparisons](stereo-link.png)

Stereo Link replaces Output; processing gain is unity. The first three plots
feed identical dispersed hits to both channels through the actual convolution
engine at 100% Auto Depth, using a frozen detected target. At +100%, both
channels receive exactly the same correction; at 0%, each uses its own target
(which happens to be identical in this example). At negative values, opposing
transient-guided timing shifts aim to separate the channels, but the final
combined correction is now strictly capped to ±180° at every frequency.
The previous full-band 5 ms separation is no longer guaranteed: at high
frequencies, opposing +180°/−180° limits are equivalent, so the largest
negative Link value can produce less peak spacing than an intermediate value.

| Stereo Link | Absolute channel peak spacing | L/R correlation over complete buffer |
| --- | ---: | ---: |
"""+'\n'.join(stereo_rows)+"""

A separate regression uses a right-channel hit shifted by 80 samples. At 0%,
its output must equal its own independently analyzed and convolved reference,
and changing the shared target must not affect either output. At +100%, the
measured channel filters must be identical even when their individual targets
differ. A live test also pairs a left transient with a steady right sine: at
0% the right correction stays neutral, while at +100% both channels receive
the same correction. Intermediate positive values blend toward the shared
phase response.

The final subplot runs the real background worker at −100% Link on repeated
identical hits with the default 200 ms release, starting with no preloaded
target. It verifies that independent analysis and response handoff actually
apply differing channel corrections within the phase limit; release,
scheduling, and the cap determine the resulting peak spacing. Listen: [live stereo separation](stereo-link-live.wav).

Mono input layouts disable the opposing shift. Auto Off and zero Auto Depth
also disable separation. Channel kernels crossfade together, retain block-size
invariance, and the white/gold traces are checked against independent DFTs of
the corresponding FIR. Negative Link deliberately changes the stereo phase
relationship and may change the mono sum; it is a widening effect rather than
a promise of maximum separation for every transient source.
"""
(root/'REPORT.md').write_text('''# Xuan Phase test report

## Manual controls and latency

![Measured phase and magnitude](manual-phase.png)

The same FIR used by the processor is measured at 20, 50, 200, 1k, 5k and
12k Hz for opposite low/high settings, with a 1 kHz crossover. Phase is measured
relative to the compensated 50 ms latency (25 ms input lookahead plus
the 25 ms FIR center). The endpoints reach approximately ±180° at
20 Hz; intermediate-angle magnitude ripple is shown explicitly. The response
is a finite phase-rotation approximation, not a mathematically exact all-pass.

Impulse tests compare the actual hybrid convolution against its designed FIR,
check block-size invariance at 1 and 127 samples, stereo linking, and neutral
and bypass delays at 8/44.1/48/96/192 kHz. Host state recall covers all eight
parameters. Input/output latency stays round(sampleRate × 50 ms) samples.

## Transient spectral phase and Depth

![Synthetic transient alignment](transient-alignment.png)

Four Gaussian wave packets at 281.25, 843.75, 2437.5 and 6750 Hz have staggered
centres. Their measured spectrogram phases and slopes generate narrow-band
targets. The actual audio convolution then runs at Depth −100/0/50/100%.
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
rotation and negative Stereo Link. Peak improvement on this constructed signal is not a guarantee
for arbitrary recordings. Native editor checks cover parameter binding, hover
hints and the 50%–120% resize limits.

## Live automatic phase-shifting case

![Live Auto audio and connected band response](auto-live.png)

This additional case feeds three repeated dispersed hits into the **real processor
and analysis worker** at 48 kHz in 128-sample blocks, starting with no detected
targets. Auto is On at 100% Depth with the default 200 ms Transient release;
Low Phase and High Phase are zero, gain is unity, and Stereo Link is +100%.
The block loop gives the background worker 3 ms per block; scheduling can affect
the first hit. Auto Off is the identical input delayed by the fixed 50 ms.
No target is preloaded, and no gain compensation or normalization is applied.

| Hit | Auto Off peak | Live Auto peak | Peak increase | RMS change |
| --- | ---: | ---: | ---: | ---: |
'''+ '\n'.join(live_rows)+'''

Each RMS window spans 4,096 samples including the complete hit and response tail.
Tests require the last hit's peak to improve by at least 5% while RMS stays within
2% of the reference. This demonstrates automatic phase shifting on a deliberately
dispersed signal; arbitrary source material may improve less or lose peak level.
Listen at the same playback level: [Auto Off](auto-off.wav), [Live Auto](auto-on.wav).

The response subplot measures the deterministic detected-target FIR used in the
Depth test above, rather than pretending a time-varying stream has one fixed
transfer function. Adjacent targets now interpolate their local phase/delay
curves with a smoothstep blend instead of fading to zero between Gaussian peaks.
Angles unwrap before interpolation across +/-180 degrees; only the outside edges
taper to neutral. Regression checks cover gaps, wrapped angles, continuity at
every detected target, and connected timing colors. Spectrogram energy still
shows real spectral gaps, since absent input frequencies must remain dark.

A separate broadband-onset regression contains over 24 spectral peaks and
requires the 24 selected targets to include both sub-1 kHz and above-16 kHz
content. Selection reserves the strongest peak in each logarithmic region,
then fills remaining slots by strength, rather than taking the first 24
low-frequency peaks. Coverage now extends to 20 kHz (limited to 45% of the
sample rate).

'''+performance+cap_report+ahead_report+stereo_report+'''

## Phase-field display

The native editor test streams repeated dispersed hits through the real processor
and worker, observes transient events and both signed timing directions, and
checks that non-empty image columns move from left to right across the
three-second history. The history is 168 px wide beside a 56 px applied-phase
track, with styled guides and a key for 0°, ±45°, ±90°, ±135°, and the ±180° boundaries. Green indicates an
early input band and red a late input band. The white left-channel and gold right-channel curves use the measured
complex FIR responses: tests compare it with an independent DFT, ensure queued
responses do not appear before audio applies them, verify updates after handoff,
and check zero displayed phase under host bypass. Input analysis remains live
with Auto Off while the meter is visible, and stops when the editor is hidden.
The meter stays inside the editor at both scaling limits. Its phase trace uses
shape-preserving cubic interpolation between measured points, without overshoot,
and splits at the ±180° wrap instead of drawing a line across the entire track.
Additional tests feed ±180°, ±200°, ±540°, and +900° responses to the actual
plot helper, check that wrapping preserves their complex phase, flatten the
cubic curves to verify they remain within the track, and reject lines joining
+179° directly to −179°. The plot is clipped to the labeled ±180° limits;
the DSP now saturates the combined correction to ±180° before FIR design,
rather than retaining extra unwrapped turns.
Stereo Link sits at the far right of the three-knob Auto row beside Transient.

A hosted-editor regression reproduces AU EditorCompHolder's lifecycle: a
visible child editor is added before its parent's native peer exists, then the
parent is attached and shown without changing the child's visible flag. This
previously left the meter blank with its timer stopped. The test requires live
history pixels and automatic analysis startup, then checks parent-only
hide/show, peer detach/reattach, and destruction while the host container lives.
These run on Linux/Xvfb against JUCE's real desktop peer and timers; Logic Pro
and macOS AU rendering still require validation on a Mac.

![Native editor with live transient history](../../Previews/xuan-phase.png)

Data: [phase.csv](phase.csv), [transient.csv](transient.csv), [auto-live.csv](auto-live.csv), [auto-phase.csv](auto-phase.csv).
''')
print('\n'.join(rows))

report=(root/'REPORT.md').read_text()
auto=report[report.index('## Transient spectral phase and Depth'):report.index('## Phase-field display')]
(root/'AUTO_ALIGNMENT.md').write_text('# Xuan Phase automatic alignment performance\n\n' +
    'Measured at 48 kHz using the production FIR, convolution engine, and analysis worker.\n' +
    'Run `XuanPhaseTests --report <this directory>` then `python3 ../generate_report.py` to reproduce.\n\n' + auto +
    '\nData: [transient](transient.csv), [live audio](auto-live.csv), [phase targets](auto-targets.csv), [release](auto-release.csv).\n')
