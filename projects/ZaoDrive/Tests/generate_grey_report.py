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
reference = spl-reference_spl-3
assert np.max(abs(target["spl80"]-spl)) < 1e-9, "C++ ISO SPL differs from independent equation"
assert np.max(abs(target["uncapped_db"]-reference)) < 1e-9
assert np.max(abs(target["target_db"]-np.minimum(0, reference))) < 1e-9


def target_db(hz):
    unbounded = np.interp(np.log(np.maximum(hz, 10)), np.log(target["frequency"]), reference)
    return np.minimum(0, unbounded)


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
    assert compensated.real.min() > .3, "No hidden polarity reversals in the response"
    assert np.max(db) <= 0, "Clear FIR must never produce positive frequency gain"
    _, anchor = freqz(h, worN=[2*np.pi*750/rate])
    assert abs(20*np.log10(abs(anchor[0]))+3) < 1e-5, "750 Hz must be lowered to minus three dB"
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
axes[0, 0].semilogx(plot_hz, target_db(plot_hz), "--", color="black", linewidth=1, label="Clear On: octave-shifted target, 0 dB ceiling")
axes[0, 0].semilogx(plot_hz, target_db(plot_hz)+3, ":", color="gray", linewidth=1, label="Previous contour level / +3 dB ceiling")
axes[0, 0].axhline(0, color="gray", linestyle="-.", linewidth=.8, label="Clear Off: flat feed")
axes[0, 0].set(xlim=(20, 20000), ylim=(-9.5, 3.5), xlabel="Hz", ylabel="Gain (dB)", title="CLEAR: contour lowered 3 dB, no positive gain")
axes[0, 0].legend(fontsize=8)
axes[0, 1].set(xlim=(20, 20000), xlabel="Hz", ylabel="Residual phase (degrees)", title="After matching the pure FIR delay")
axes[1, 0].set(xlabel="Sample", ylabel="Coefficient", title="Measured impulse response at 48 kHz")
axes[1, 0].legend()
axes[1, 1].set(xlim=(20, 20000), ylim=(127.99, 128.01), xlabel="Hz", ylabel="Group delay (samples)", title="Constant group delay at 48 kHz")
axes[1, 1].ticklabel_format(axis="y", style="plain", useOffset=False)
for ax in axes.flat:
    ax.grid(alpha=.25)
fig.suptitle("Zao Drive Clear: optional, attenuation-only contour", fontsize=16)
for ext in ["png", "pdf"]:
    fig.savefig(root / f"grey-report.{ext}", dpi=160)
plt.close(fig)
assert max_phase_error < 1e-10, f"Unexpected compensated phase: {max_phase_error} rad"
report = f"""# Zao Drive Clear contour test report

![Clear On/Off response, prior contour level, phase and group delay](grey-report.png)

**CLEAR**, beside SMART, enables the octave-shifted 80-phon contour. It defaults
On, saves its state with presets and automation, and loads On for older presets
that previously always applied the contour. Off uses a flat feed with the same
latency. The expected-output display follows the switch.

## Attenuation-only target

The previous contour is lowered by **3 dB everywhere**, preserving its shape
and one-octave frequency shift. Its former +3 dB plateaus become **0 dB**;
the 750 Hz reference becomes **−3 dB**, and its former 4 kHz valley remains
shifted to 2 kHz. This does not merely flatten the formerly boosted regions.

The source ISO 226:2003 coefficients define SPL at 80 phons:

```text
Af = 4.47e-3 × (10^(0.025 × 80) − 1.15) + (0.4 × 10^((Tf + Lu)/10 − 9))^af
Lp = (10/af) × log10(Af) − Lu + 94
Clear gain(f) = min(0 dB, Lp(2 × f) − Lp(1.5 kHz) − 3 dB)
```

The source SPL reference is {reference_spl:.6f} dB. Target gain is −3 dB at
750 Hz and approximately {reference[23]:+.3f} dB at 2 kHz. Log-frequency
interpolation and the shifted endpoint holds below 10 Hz and above 6.25 kHz
remain unchanged. The [target CSV](grey-iso80-target.csv) records source SPL,
uncapped shifted/offset gain and the applied target. Independent NumPy ISO
equations agree with the C++ values within 1e-9 dB.

This is an intentionally shifted, level-adjusted filter derived from the ISO
contour; it does not calibrate playback SPL or reproduce an unshifted hearing
contour at the applied frequencies.

## FIR measurements

The symmetric Type-I FIR uses the same tap count, Kaiser beta 1, and approximately
2.67 ms delay. It is normalized to **−3 dB at 750 Hz**, not to unity. When needed,
a blend with a −3 dB pure delay enforces the actual 0 dB response ceiling while
preserving that reference gain and symmetry. Coefficient design happens only
during preparation; processing retains the SIMD implementation.

Maximum measured upward gain is **{max_response:.6f} dB** across 8192 frequency
points per sample rate and must be at most 0 dB. The reference is within
1e-5 dB of −3 dB. Maximum fit error is **{max_fit_error:.3f} dB** from 10 Hz to
the smaller of Nyquist and 6.25 kHz. Every measured impulse is exactly symmetric;
maximum delay-compensated phase error is **{max_phase_error:.3e} radians**.

| Sample rate | FIR taps | FIR samples | FIR ms | Total plugin samples | Gain at 1.575 kHz |
| ---: | ---: | ---: | ---: | ---: | ---: |
{chr(10).join(rows)}

## Toggle and integration checks

Clear crossfades over 20 ms between shaped and flat feeds with identical delay.
On reactivation, it warms a complete FIR history before fading in. Off stops
convolution after the fade. The pure delay remains active so host latency,
dry mixing and the circuit-minus-reference residual stay aligned.

C++ tests verify exact Off impulses, On agreement with an independent FIR,
stereo isolation, smooth DC transitions, reactivation/warmup, and convolution
suspension at 8/44.1/48/96/192 kHz. Automated toggles also preserve the exact
end-to-end dry signal and host-reported latency. Existing tests cover direct
convolution equivalence, all four circuits, overload, alias reduction and the
shared transformer. Clear participates in Smart signatures, invalidating stale
spectral decisions. State tests cover Off round trips and legacy-On migration;
native editor tests cover button placement, host automation and preview updates.

The contour shapes only the driver feed. Both its shaped and unshaped feeds
have the FIR delay; the wet residual subtracts the matching circuit-latency-aligned
reference before Tone and Smart, then rejoins the flat clean branch. The output
overdrive can still generate gain and harmonics; the no-boost constraint applies
to the Clear contour itself.

Data: [impulses](grey-impulse.csv), [latencies](grey-latency.csv),
[target](grey-iso80-target.csv). A [PDF](grey-report.pdf) is also available.

Regenerate from the repository root:

```bash
build-zao/ZaoDriveTests --grey-report "$PWD/projects/ZaoDrive/Tests/Reports"
python3 projects/ZaoDrive/Tests/generate_grey_report.py
```
"""
(root / "grey-report.md").write_text(report)
print("\n".join(rows))
print(f"ISO fit error: {max_fit_error:.3f} dB; maximum gain: {max_response:.3f} dB; compensated phase: {max_phase_error:.3e} rad")
