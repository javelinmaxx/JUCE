#!/usr/bin/env python3
"""Independently verify the implemented FIR's measured response and phase."""
import os
from pathlib import Path
os.environ.setdefault("MPLCONFIGDIR", "/tmp/zao-matplotlib")
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.signal import freqz

root = Path(__file__).resolve().parent / "Reports"
data = np.genfromtxt(root / "grey-impulse.csv", delimiter=",", names=True)
latencies = np.genfromtxt(root / "grey-latency.csv", delimiter=",", names=True)
fig, axes = plt.subplots(2, 2, figsize=(12, 8), constrained_layout=True)
rows = []
max_phase_error = 0
for entry in latencies:
    rate = int(entry["rate"])
    h = data[data["rate"] == rate]["coefficient"]
    delay = int(entry["fir_samples"])
    assert np.array_equal(h, h[::-1]), "FIR must remain exactly symmetric"
    assert len(h) == 2*delay+1, "Reported delay must match the impulse centre"
    w, response = freqz(h, worN=8192)
    hz = w*rate/(2*np.pi)
    compensated = response*np.exp(1j*w*delay)
    max_phase_error = max(max_phase_error, np.max(np.abs(np.angle(compensated))))
    assert compensated.real.min() > .4, "No hidden polarity reversals in the response"
    _, mid = freqz(h, worN=[2*np.pi*2500/rate])
    mid_db = 20*np.log10(abs(mid[0]))
    rows.append(f"| {rate:,} | {len(h)} | {delay} | {1000*delay/rate:.3f} | {int(entry['plugin_samples'])} | {mid_db:.2f} dB |")
    if rate in [44100, 48000, 96000, 192000]:
        axes[0, 0].semilogx(hz, 20*np.log10(abs(response)), label=f"{rate/1000:g} kHz")
        axes[0, 1].semilogx(hz[1:], np.angle(compensated[1:])*180/np.pi, label=f"{rate/1000:g} kHz")
    if rate == 48000:
        axes[1, 0].plot(np.arange(len(h)), h, color="#b67f35")
        axes[1, 0].axvline(delay, color="gray", linestyle=":", label="128-sample centre")
        group_delay = -np.gradient(np.unwrap(np.angle(response)), w)
        axes[1, 1].semilogx(hz[1:-1], group_delay[1:-1], color="#b67f35")

axes[0, 0].set(xlim=(30, 20000), ylim=(-6.6, .4), xlabel="Hz", ylabel="Gain (dB)", title="Wet-driver input: grey-noise-style contour")
axes[0, 0].legend()
axes[0, 1].set(xlim=(30, 20000), xlabel="Hz", ylabel="Residual phase (degrees)", title="After matching the pure FIR delay")
axes[1, 0].set(xlabel="Sample", ylabel="Coefficient", title="Measured impulse response at 48 kHz")
axes[1, 0].legend()
axes[1, 1].set(xlim=(30, 20000), ylim=(127.99, 128.01), xlabel="Hz", ylabel="Group delay (samples)", title="Constant group delay at 48 kHz")
axes[1, 1].ticklabel_format(axis="y", style="plain", useOffset=False)
for ax in axes.flat:
    ax.grid(alpha=.25)
fig.suptitle("Zao Drive: linear-phase grey pre-emphasis", fontsize=16)
for ext in ["png", "pdf"]:
    fig.savefig(root / f"grey-report.{ext}", dpi=160)
plt.close(fig)
assert max_phase_error < 1e-10, f"Unexpected compensated phase: {max_phase_error} rad"
report = f"""# Zao Drive grey-filter test report

![Magnitude, compensated phase, impulse symmetry and group delay](grey-report.png)

The wet-driver input uses a fixed grey-noise-style contour: a bounded inverse
A-weighting approximation with about 6 dB of midrange reduction around 2–4 kHz,
returning toward unity at bass and upper-treble frequencies. Grey noise depends
on the chosen equal-loudness contour and listening level; this is an explicit,
gentle approximation rather than a universal calibrated grey-noise standard.

A Blackman-windowed, symmetric Type-I FIR implements that contour. Coefficients
are computed during preparation, with no coefficient design or allocation in
the audio callback. The sample-rate-dependent tap count preserves approximately
2.67 ms of delay. At 48 kHz this is 257 taps with a 128-sample group delay.
The runtime exploits tap symmetry and uses independent stereo histories.

## Measurements

The table and plots come from the actual C++ FIR coefficients, independently
analysed with SciPy. Every impulse is exactly symmetric, its response remains
positive after compensating the centre delay, and the maximum residual phase
error is **{max_phase_error:.3e} radians** across 8192 frequency points per rate.

| Sample rate | FIR taps | Added FIR samples | Added FIR ms | Total plugin samples | Gain at 2.5 kHz |
| ---: | ---: | ---: | ---: | ---: | ---: |
{chr(10).join(rows)}

Total plugin latency includes the FIR, circuit oversampler's integer delay and
the existing 64-sample transformer compensation. The processor reports this
complete latency to the host in all modes, Smart states, Mix settings and bypass.

## Signal alignment and regression coverage

The clean branch retains the original input spectrum and receives a pure delay
matching FIR plus circuit latency. The residual subtracts the aligned **filtered
reference** from the driver output. Tone and Smart process only that residual
before it is added to the clean branch; this prevents the new filter from being
inserted as an EQ on the clean branch. Circuit gain and phase can still leave
fundamental energy in the residual. The circuit, Tone, Smart and transformer
retain their existing phase behavior; the new FIR adds only a constant delay.

C++ checks cover impulse/streaming agreement, direct-convolution equivalence
through history wraps, stereo isolation, DC gain, midrange attenuation and phase
from 8–192 kHz. End-to-end impulse tests verify exact dry-mix and host-bypass
timing at 44.1/48/96 kHz. All four circuits are compared across 17-sample and
513-sample host blocks to check reference alignment through block boundaries.
Existing nonlinear convergence, Smart adaptation, saved-state and transformer
parity tests also pass. The alias comparison feeds both circuit versions the
same grey-shaped input, so reduced excitation does not inflate that comparison.

Data: [impulse responses](grey-impulse.csv), [latencies](grey-latency.csv).
A matching [PDF](grey-report.pdf) is available.

Regenerate from the repository root:

```bash
build-zao/ZaoDriveTests --grey-report "$PWD/projects/ZaoDrive/Tests/Reports"
python3 projects/ZaoDrive/Tests/generate_grey_report.py
```
"""
(root / "grey-report.md").write_text(report)
print("\n".join(rows))
print(f"Maximum compensated phase error: {max_phase_error:.3e} rad")
