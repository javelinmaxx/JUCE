#!/usr/bin/env python3
"""Plot measured driver-minus-input residuals through the actual Smart filters."""
import os
from pathlib import Path
os.environ.setdefault("MPLCONFIGDIR", "/tmp/zao-matplotlib")
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

root = Path(__file__).resolve().parent / "Reports"
audio = np.genfromtxt(root / "smart-audio.csv", delimiter=",", names=True)
bands = np.genfromtxt(root / "smart-bands.csv", delimiter=",", names=True)
scores = np.genfromtxt(root / "smart-scores.csv", delimiter=",", names=True)
curve = np.genfromtxt(root / "smart-dissonance.csv", delimiter=",", names=True)
models = ["BJT CE", "Op-Amp feedback", "Diode shunt", "Series crossover"]
notes = [[261.6256, 329.6276, 391.9954], [220., 261.6256, 329.6276]]


def spectrum(x):
    w = np.hanning(len(x))
    return np.fft.rfftfreq(len(x), 1/48000), 20*np.log10(np.maximum(2*np.abs(np.fft.rfft(x*w))/sum(w), 1e-8))


def amplitude(x, hz):
    w = np.hanning(len(x))
    return 2*abs(np.sum(x*w*np.exp(-2j*np.pi*hz*np.arange(len(x))/48000)))/sum(w)


fig, axes = plt.subplots(3, 2, figsize=(12, 12), constrained_layout=True)
axes[0, 0].plot(curve["semitones"], curve["cost"], color="#d49846")
axes[0, 0].set(xlabel="12-TET semitones", ylabel="Dissonance cost", title="Continuous approximation of the supplied graph", xticks=range(13))
for chord in range(2):
    d = audio[(audio["model"] == 1) & (audio["chord"] == chord)]
    b = bands[(bands["model"] == 1) & (bands["chord"] == chord)]
    ax = axes[1, chord]
    for field, label in [("residual", "Driver − filtered reference"), ("smart_residual", "Smart wet residual")]:
        hz, db = spectrum(d[field]); ax.semilogx(hz, db, label=label, linewidth=1)
    for hz in notes[chord]:
        ax.axvline(hz, color="gray", alpha=.35, linestyle=":")
    ax.set(xlim=(150, 6500), ylim=(-95, -15), xlabel="Hz", ylabel="dBFS", title=["C major: generated wet spectrum", "A minor: generated wet spectrum"][chord]); ax.legend(fontsize=8)
    for band in b:
        ax.axvline(band["frequency"], color="#d49846", alpha=.2)
    ax = axes[2, chord]
    reductions = np.array([20*np.log10(max(amplitude(d["smart_residual"], hz), 1e-12)/max(amplitude(d["residual"], hz), 1e-12)) for hz in b["frequency"]])
    ax.scatter(b["frequency"], reductions, c=b["order"], cmap="viridis", vmin=2, vmax=12)
    ax.axhline(0, color="gray", linewidth=.8)
    ax.set(xscale="log", xlabel="Target Hz", ylabel="Measured amplitude change (dB)", title="Actual residual filtering (colour = harmonic order)")

ax = axes[0, 1]
for chord in range(2):
    b = bands[(bands["model"] == 1) & (bands["chord"] == chord)]
    ax.scatter(b["frequency"], b["gain_db"], label=["C major", "A minor"][chord])
ax.set(xscale="log", xlabel="Target Hz", ylabel="Band gain (dB)", title="Content changes the harmonic cuts; knobs stay fixed")
ax.legend()
for ax in axes.flat:
    ax.grid(alpha=.2)
fig.suptitle("Zao Drive Smart: separate clean and generated wet branches", fontsize=16)
for ext in ["png", "pdf"]:
    fig.savefig(root / f"smart-report.{ext}", dpi=160)
plt.close(fig)

rows = []
for model in range(4):
    for chord in range(2):
        d = audio[(audio["model"] == model) & (audio["chord"] == chord)]
        b = bands[(bands["model"] == model) & (bands["chord"] == chord)]
        old = np.array([amplitude(d["residual"], hz)**2 for hz in b["frequency"]])
        new = np.array([amplitude(d["smart_residual"], hz)**2 for hz in b["frequency"]])
        # Recover the exact graph-derived cost from the requested gain law.
        costs = -b["gain_db"]/18
        reduction = 100*(1-np.sum(new*costs)/np.sum(old*costs))
        retention = 100*np.sum(new)/np.sum(old)
        assert np.isfinite(reduction) and reduction > 0, "Measured dissonance-weighted harmonic energy must decrease"
        dry_error = np.max(np.abs((d["smart_output"]-d["smart_residual"])-d["input"]))
        assert dry_error < 2e-5, "Clean branch must be independent of the wet harmonic processing"
        rows.append(f"| {models[model]} | {['C major', 'A minor'][chord]} | {len(b)} | {reduction:.1f}% | {retention:.1f}% |")

report = f"""# Zao Drive Smart harmonic report

![Dissonance graph, content-dependent targets and measured wet spectra](smart-report.png)

The driver and clean input run in parallel. A linear-phase grey-noise-style
FIR shapes the driver feed using the 80-phon ISO contour with a +3 dB boost cap. The circuit output minus its aligned filtered
reference forms the wet residual; Tone and Smart process this residual, then Mix
adds it to the clean branch. Circuit gain/phase changes can remain in that
residual, so subtraction alone does not mathematically isolate only higher orders.
Smart protects existing input peaks and targets measured added harmonic orders
2–12 using a continuous approximation of the supplied 12-TET dissonance graph.
Drive, Bias, Knee, and the circuit equations stay unchanged.

## Measured audio

All four real circuit engines render C major (C4/E4/G4) and A minor (A3/C4/E4)
at 48 kHz, 0.1 FS peak per sine, Drive 24 dB, Tone 18 kHz, Mix 100%, Output
0 dB, transformer Off, with grey shaping enabled. A Hann-windowed 4096-sample frame determines the targets.
The actual causal filter bank then processes the recorded residual with that
steady-state analysis snapshot. The final 8192 samples are measured by Hann-windowed
Fourier projection at each selected harmonic frequency; these are measured
results, rather than the optimizer's predicted attenuation. Polyphonic leakage
and nearby harmonics can affect the measurements.

| Circuit | Chord | Target bands | Weighted harmonic energy reduction | Target harmonic energy retained |
| --- | --- | ---: | ---: | ---: |
{chr(10).join(rows)}

The cost is the sum of target harmonic power times its interval dissonance;
retention is the power remaining at those targets, not total output loudness.
Consonant or input-overlapping frequencies are protected; there is no overall
drive reduction or normalization trick. Band cuts are bounded at 6 dB each.
The sampled graph is a musical heuristic, not a universal perceptual clarity metric.

The independent clean-branch reconstruction error is below 2e-5 FS in the CSV
round trip. The synthetic known-harmonic test additionally keeps the three
original chord tones within 0.15 dB, checks actual attenuation of added energy,
and confirms different targets for major and minor content.

## Real-time and host checks

The processor test feeds live circuit audio to the worker, exercises publication
and stale-result rejection, checks that Smart Off submits no FFT jobs, verifies
unchanged reported latency and unchanged manual controls, and saves/restores
Smart including legacy presets. Silence, noise, anti-phase stereo, consonance
anchors and bounded harmonic cuts are covered. New filter banks crossfade over
50 ms using a quintic smoothstep sigmoid with exact, flat endpoints. Analysis
refreshes at up to 20 times per second after the first full frame, retaining
the 4,096-sample window with overlapping 50 ms hops. Real-worker scheduling tests
verify the first full window and successive sample-accurate 50 ms submissions at
8/44.1/48/96/192 kHz. Audio tests compare both channels against independent
dry/filtered signals throughout the sigmoid fade and verify exact target-bank
output at 50 ms. A ready target starts on the next sample at the fade endpoint;
worker scheduling can add gaps if the target is not ready. New targets never
interrupt an unfinished fade. No FFT or circuit search runs in the audio callback.

Source data: [audio](smart-audio.csv), [target bands](smart-bands.csv),
[predicted scores](smart-scores.csv), [dissonance curve](smart-dissonance.csv).
The matching [PDF](smart-report.pdf) can be exported. See [SMART.md](../../SMART.md)
for the signal path, formulas and separation limits.
"""
(root / "smart-report.md").write_text(report)
print("\n".join(rows))
print(f"Wrote {root / 'smart-report.md'}")
