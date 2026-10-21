#!/usr/bin/env python3
"""Plot real Zao renders and independently solve its specified DC network."""
import os
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", "/tmp/zao-matplotlib")
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.optimize import root

destination = Path(__file__).resolve().parent / "Reports"
data = np.genfromtxt(destination / "waveforms.csv", delimiter=",", names=True)
dc = np.genfromtxt(destination / "operating-point.csv", delimiter=",", names=True)
residuals = np.genfromtxt(destination / "solver-residuals.csv", delimiter=",", names=True)
alias = np.genfromtxt(destination / "alias-check.csv", delimiter=",", names=True)


def dc_kcl(v):
    """Independent residual formulation from the documented component network."""
    base, emitter, collector, bypass = v
    forward = 6.734e-15 * np.expm1((base - emitter) / 0.02585)
    reverse = 6.734e-15 * np.expm1((base - collector) / 0.02585)
    collector_current = forward * (1 + (collector - emitter) / 100) - 1.25 * reverse
    base_current = forward / 200 + reverse / 4
    return np.array([
        (base - 9) / 100000 + base / 22000 + base_current,
        (emitter - bypass) / 220 - collector_current - base_current,
        (collector - 9) / 4700 + collector_current,
        (bypass - emitter) / 220 + bypass / 820,
    ])


reference = root(dc_kcl, [1.5, 0.9, 5, 0.7], tol=1e-11)
if not reference.success or np.max(np.abs(dc_kcl(reference.x))) > 1e-10:
    raise RuntimeError("Independent DC reference failed to converge")
actual = np.array([float(dc[name]) for name in ("base", "emitter", "collector", "bypass")])
dc_error = np.max(np.abs(actual - reference.x))
if dc_error > 1e-6:
    raise RuntimeError(f"DC reference mismatch: {dc_error} V")


def signal(model, drive, transformer=0):
    selected = (data["model"] == model) & (data["drive"] == drive) & (data["transformer"] == transformer)
    return data[selected]


plt.rcParams.update({"font.size": 10, "axes.spines.top": False, "axes.spines.right": False})
fig, axes = plt.subplots(3, 2, figsize=(12, 10), constrained_layout=True)
colors = ["#63646e", "#b77b45", "#9c3e29"]
names = ["BJT common emitter", "Op-amp feedback clipper"]
harmonics = {}
for model in range(2):
    ax = axes[0, model]
    for drive, color in zip((0, 12, 24), colors):
        rows = signal(model, drive)
        t = np.arange(96) / 48
        ax.plot(t, rows["output"][-96:], color=color, label=f"Drive {drive} dB")
    ax.plot(t, rows["input"][-96:], "--", color="#999999", linewidth=0.8, label="Input")
    ax.set(title=f"{names[model]}: predicted output", xlabel="Time (ms)", ylabel="Output (FS)")
    ax.legend(fontsize=8, ncol=2)
    ax.grid(alpha=0.15)

    rows = signal(model, 12)
    spectrum = np.abs(np.fft.rfft(rows["output"])) * 2 / len(rows)
    frequency = np.fft.rfftfreq(len(rows), 1 / 48000)
    harmonic_bins = np.array([np.argmin(np.abs(frequency - k * 1000)) for k in range(1, 16)])
    levels = 20 * np.log10(np.maximum(spectrum[harmonic_bins], 1e-10))
    harmonics[model] = levels
    axes[1, model].bar(np.arange(1, 16), levels + 120, bottom=-120, color="#b77b45", width=0.55)
    axes[1, model].set(title="Harmonics at Drive 12 dB", xlabel="Harmonic number", ylabel="dBFS", ylim=(-120, 6))
    axes[1, model].grid(axis="y", alpha=0.15)

    for enabled, color in ((0, "#777777"), (1, "#b77b45")):
        rows = signal(model, 12, enabled)
        axes[2, model].plot(t, rows["output"][-96:], color=color, label="Transformer On" if enabled else "Transformer Off")
    axes[2, model].set(title="Shared YinYang transformer: Core 12 dB / Bias 30%", xlabel="Time (ms)", ylabel="Output (FS)")
    axes[2, model].legend(fontsize=8)
    axes[2, model].grid(alpha=0.15)

fig.suptitle("Zao Drive circuit validation · 48 kHz / 4× circuit oversampling", fontsize=15)
fig.savefig(destination / "circuit-report.png", dpi=170)
fig.savefig(destination / "circuit-report.pdf")

rows = "\n".join(f"| {name} | {actual[i]:.9f} V | {reference.x[i]:.9f} V |"
                 for i, name in enumerate(("Base", "Emitter", "Collector", "Emitter bypass")))
opamp_second = (f"{harmonics[1][1]:.1f} dBFS" if harmonics[1][1] > -120 else "below −120 dBFS")
report = f"""# Zao Drive circuit test report

![Circuit waveforms and spectra](circuit-report.png)

## Numerical circuit accuracy

The four-node BJT DC solution is checked against a separate SciPy hybrid root
solve of the resistor network and documented transistor currents, initialized
independently of the C++ solution. Maximum node-voltage disagreement is
**{dc_error:.3e} V**. This checks the nominal equations; it is not hardware calibration.

| Node | C++ model | Independent DC reference |
| --- | --- | --- |
{rows}

Sustained 1.3 kHz input voltages of 0.035, 0.35, 3.5, and 12 V were exercised at
128, 192, and 384 kHz internal rates. These cover forward-active operation,
cutoff, saturation, and overload. Maximum BJT KCL residual was
**{max(residuals['bjt_residual_amps']):.3e} A**; maximum op-amp equation residual
was **{max(residuals['opamp_residual_volts']):.3e} V**. Op-amp output stayed within
its ±4 V rails. Antiparallel-diode symmetry was checked with inverse inputs.
Broadband ±12 V noise and abrupt polarity reversals also check convergence and
the op-amp's slew limit through transient overload.

## Overdrive and expected-output display

The upper plots use a −12 dBFS 1 kHz reference at Drive 0, 12, and 24 dB with
Output 0 dB, Mix 100%, Tone 18 kHz, and transformer Off. The BJT's asymmetric
clipping differs from the odd-symmetric op-amp feedback characteristic. At Drive
12 dB, the second harmonic is **{harmonics[0][1]:.1f} dBFS** for BJT and
**{opamp_second}** for op-amp. The UI runs this same engine with the
current controls, using a fresh independent state and a settled reference input.

A 9 kHz op-amp input at Drive 24 dB tests the 21 kHz folded third harmonic.
The oversampled audio path reduces that component by
**{float(alias['reduction_db']):.1f} dB** against the circuit run directly at 48 kHz.
This comparison includes the oversampled path's post-circuit Tone filter.

## Transformer and host behavior

The lower plots toggle the directly reused YinYang transformer at Core Drive
12 dB and DC Bias 30%. A separate sample-for-sample test isolates the transformer
with Mix 0% and verifies agreement with the original YinYang class below 1e-6 FS.
Dry mix and bypass impulse tests verify the reported integer latency and stereo
isolation. Maximum-drive processing remains finite at 44.1, 48, and 96 kHz,
including blocks larger than prepareToPlay's block size. Host state round trips
retain the circuit and transformer settings. Editor tests verify named model
initialization, parameter updates, waveform changes, tooltips, and resizing.

These are simulated circuit tests. The model limits are documented in
[CIRCUITS.md](../../CIRCUITS.md); no full SPICE transient or physical-device
calibration is claimed. Raw renders are in [waveforms.csv](waveforms.csv),
[solver-residuals.csv](solver-residuals.csv), and
[operating-point.csv](operating-point.csv). A printable figure is available as
[PDF](circuit-report.pdf).
"""
(destination / "report.md").write_text(report)
print(f"Independent DC error: {dc_error:.3e} V")
print(f"Wrote {destination / 'report.md'}")
