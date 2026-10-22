#!/usr/bin/env python3
"""Verify the capped ISO 226 80-phon target and actual FIR magnitude/phase."""
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
target = np.genfromtxt(root / "grey-iso80-target.csv", delimiter=",", names=True)
# Independent evaluation of the ISO 226:2003 coefficients/equation at 80 phons.
af = np.array([.532,.506,.480,.455,.432,.409,.387,.367,.349,.330,.315,.301,.288,.276,.267,.259,.253,.250,.246,.244,.243,.243,.243,.242,.242,.245,.254,.271,.301])
lu = np.array([-31.6,-27.2,-23,-19.1,-15.9,-13,-10.3,-8.1,-6.2,-4.5,-3.1,-2,-1.1,-.4,0,.3,.5,0,-2.7,-4.1,-1,.1,1.2,1.6,.5,-2.6,-7.1,-11.2,-10.7])
tf = np.array([78.5,68.7,59.5,51.1,44,37.5,31.5,26.5,22.1,17.9,14.4,11.4,8.6,6.2,4.4,3,2.2,2.4,3.5,1.7,-1.3,-4.2,-6,-5.4,-1.5,6,12.6,13.9,12.3])
a = 4.47e-3*(10**(.025*80)-1.15) + (.4*10**((tf+lu)/10-9))**af
spl = 10/af*np.log10(a)-lu+94
reference_spl = np.interp(np.log(750), np.log(target["frequency"]), spl)
reference = spl-reference_spl
assert np.max(abs(target["spl80"]-spl)) < 1e-9, "C++ ISO SPL differs from independent equation"
assert np.max(abs(target["uncapped_db"]-reference)) < 1e-9
assert np.max(abs(target["target_db"]-np.minimum(3, reference))) < 1e-9


def target_db(hz):
    unbounded = np.interp(np.log(np.maximum(hz, 10)), np.log(target["frequency"]), reference)
    return np.minimum(3, unbounded)


fig, axes = plt.subplots(2, 2, figsize=(12, 8), constrained_layout=True)
rows = []
max_phase_error = 0
max_response = -np.inf
max_fit_error = 0
for entry in latencies:
    rate = int(entry["rate"])
    h = data[data["rate"] == rate]["coefficient"]
    delay = int(entry["fir_samples"])
    assert np.array_equal(h, h[::-1]), "FIR must remain exactly symmetric"
    assert len(h) == 2*delay+1, "Reported delay must match the impulse centre"
    w, response = freqz(h, worN=8192)
    hz = w*rate/(2*np.pi)
    db = 20*np.log10(abs(response))
    compensated = response*np.exp(1j*w*delay)
    max_phase_error = max(max_phase_error, np.max(np.abs(np.angle(compensated))))
    max_response = max(max_response, np.max(db))
    assert compensated.real.min() > .4, "No hidden polarity reversals in the response"
    assert np.max(db) <= 3.00001, "Actual FIR exceeds the requested +3 dB ceiling"
    _, anchor = freqz(h, worN=[2*np.pi*750/rate])
    assert abs(20*np.log10(abs(anchor[0]))) < 1e-5, "750 Hz must remain at unity"
    valid = (hz >= 10) & (hz <= 6250)
    fit_error = np.max(abs(db[valid]-target_db(hz[valid])))
    max_fit_error = max(max_fit_error, fit_error)
    assert fit_error < 1.1, "Short FIR fit to ISO contour must remain within 1.1 dB"
    _, mid = freqz(h, worN=[2*np.pi*1575/rate])
    mid_db = 20*np.log10(abs(mid[0]))
    rows.append(f"| {rate:,} | {len(h)} | {delay} | {1000*delay/rate:.3f} | {int(entry['plugin_samples'])} | {mid_db:.2f} dB |")
    if rate in [44100, 48000, 96000, 192000]:
        axes[0, 0].semilogx(hz, db, label=f"{rate/1000:g} kHz")
        axes[0, 1].semilogx(hz[1:], np.angle(compensated[1:])*180/np.pi, label=f"{rate/1000:g} kHz")
    if rate == 48000:
        axes[1, 0].plot(np.arange(len(h)), h, color="#b67f35")
        axes[1, 0].axvline(delay, color="gray", linestyle=":", label="128-sample centre")
        group_delay = -np.gradient(np.unwrap(np.angle(response)), w)
        axes[1, 1].semilogx(hz[1:-1], group_delay[1:-1], color="#b67f35")

plot_hz = np.geomspace(20, 20000, 4096)
axes[0, 0].semilogx(plot_hz, target_db(plot_hz), "--", color="black", linewidth=1, label="80-phon target shifted down one octave / +3 dB cap")
axes[0, 0].semilogx(plot_hz, target_db(plot_hz/2), ":", color="gray", linewidth=1, label="Previous position / 1.5 kHz reference")
axes[0, 0].axhline(3, color="gray", linestyle=":", linewidth=.8)
axes[0, 0].set(xlim=(20, 20000), ylim=(-6.5, 3.5), xlabel="Hz", ylabel="Gain (dB)", title="One octave lower: 750 Hz reference, +3 dB upward cap")
axes[0, 0].legend(fontsize=8)
axes[0, 1].set(xlim=(20, 20000), xlabel="Hz", ylabel="Residual phase (degrees)", title="After matching the pure FIR delay")
axes[1, 0].set(xlabel="Sample", ylabel="Coefficient", title="Measured impulse response at 48 kHz")
axes[1, 0].legend()
axes[1, 1].set(xlim=(20, 20000), ylim=(127.99, 128.01), xlabel="Hz", ylabel="Group delay (samples)", title="Constant group delay at 48 kHz")
axes[1, 1].ticklabel_format(axis="y", style="plain", useOffset=False)
for ax in axes.flat:
    ax.grid(alpha=.25)
fig.suptitle("Zao Drive: 80-phon contour shifted down one octave", fontsize=16)
for ext in ["png", "pdf"]:
    fig.savefig(root / f"grey-report.{ext}", dpi=160)
plt.close(fig)
assert max_phase_error < 1e-10, f"Unexpected compensated phase: {max_phase_error} rad"
report = f"""# Zao Drive 80-phon contour test report

![Capped ISO target, measured magnitude, phase and group delay](grey-report.png)

The wet-driver input now uses the **ISO 226:2003 equal-loudness contour at
80 phons shifted down one octave**, moving its **0 dB reference to 750 Hz**,
with **upward gain capped at +3 dB**.
Only upward target gain is limited; negative target gains retain their ISO
values without inverting the contour. The frequency-shifted contour is derived from the standard; it is not
an unshifted ISO hearing contour at the applied frequencies. The phon selection defines the filter shape; the plugin does not
calibrate playback SPL or claim to set the listener's loudness to 80 phons.

## Target definition

The standard's 29 frequencies, `af`, `Lu` and `Tf` coefficients determine the
80-phon SPL curve. At each tabulated frequency:

```text
Af = 4.47e-3 × (10^(0.025 × 80) − 1.15) + (0.4 × 10^((Tf + Lu)/10 − 9))^af
Lp = (10/af) × log10(Af) − Lu + 94
gain(f) = min(+3 dB, Lp(2 × f) − Lp(1.5 kHz))
```

The original 1.5 kHz SPL reference is interpolated to approximately {reference_spl:.6f} dB.
It now maps to 750 Hz in the applied contour. Uncapped relative gains are about
{reference[0]:+.3f} dB at 10 Hz, {reference[23]:+.3f} dB at 2 kHz and
{reference[28]:+.3f} dB at 6.25 kHz. The upward values become +3 dB;
the former 4 kHz cut now appears at 2 kHz. SPL is interpolated
linearly in log frequency before applying the cap. Endpoints are held below
10 Hz and above 6.25 kHz after the shift; the source ISO table spans
20 Hz to 12.5 kHz.

Reference: *ISO 226:2003, Acoustics — Normal equal-loudness-level contours*.
The [target CSV](grey-iso80-target.csv) records the SPL, uncapped relative gain,
and capped target at every shifted ISO frequency (half its source frequency). An independent NumPy evaluation of
the standard's equation agrees with the C++ values within 1e-9 dB.

## FIR measurements

A Kaiser-windowed (beta 1), symmetric Type-I FIR implements the target. The
lighter window preserves the narrower shifted features at the existing tap
count and latency. It is
normalised at 750 Hz after windowing. If that normalization lifts the actual
response above +3 dB, a small blend with a pure delay bounds it while preserving
unity at 750 Hz and exact tap symmetry. This blends only enough to enforce the
existing cap. Coefficients are designed during preparation;
no coefficient design or allocation occurs in the audio callback. The short FIR
smooths the contour's fine details: maximum fit error is **{max_fit_error:.3f} dB**
from 10 Hz to the smaller of Nyquist and 6.25 kHz over the tested sample rates.
Maximum actual upward gain is **{max_response:.3f} dB**, within the requested cap;
750 Hz remains at unity within 1e-5 dB.

Delay is unchanged: approximately 2.67 ms, or 128 samples for the 257-tap FIR at
48 kHz. Every measured impulse is exactly symmetric. After compensating its
centre delay, maximum residual phase error is **{max_phase_error:.3e} radians**
across 8192 frequency points per rate.

| Sample rate | FIR taps | Added FIR samples | Added FIR ms | Total plugin samples | Gain at 1.575 kHz |
| ---: | ---: | ---: | ---: | ---: | ---: |
{chr(10).join(rows)}

Total plugin latency includes the FIR, circuit oversampler delay and existing
64-sample transformer compensation. Clean, filtered-reference and bypass delays
remain aligned in all modes, Smart states and Mix settings.

## Regression coverage

C++ checks validate independent 80-phon SPL anchors, the 750 Hz reference and one-octave mapping, the
unscaled negative gains, the +3 dB target and actual-response ceilings, endpoint
handling, impulse/streaming agreement, direct-convolution equivalence through
history wraps and stereo isolation from 8–192 kHz. End-to-end impulse tests
verify exact dry-mix and host-bypass timing at 44.1/48/96 kHz. All four circuits
are compared across 17-sample and 513-sample host blocks. Existing nonlinear
convergence, Smart adaptation, state recall and transformer parity tests pass.

The contour shapes only the driver feed. The wet residual subtracts its aligned
filtered reference, then Tone and Smart operate on that residual before adding
it to the flat clean branch. Circuit gain and phase can still leave fundamental
energy in the residual; their existing phase behavior is separate from the FIR's
constant delay. The alias benchmark uses the same shaped excitation for both
circuit versions.

Data: [impulses](grey-impulse.csv), [latencies](grey-latency.csv),
[ISO target](grey-iso80-target.csv). A matching [PDF](grey-report.pdf) is available.

Regenerate from the repository root:

```bash
build-zao/ZaoDriveTests --grey-report "$PWD/projects/ZaoDrive/Tests/Reports"
python3 projects/ZaoDrive/Tests/generate_grey_report.py
```
"""
(root / "grey-report.md").write_text(report)
print("\n".join(rows))
print(f"ISO fit error: {max_fit_error:.3f} dB; maximum gain: {max_response:.3f} dB; compensated phase: {max_phase_error:.3e} rad")
