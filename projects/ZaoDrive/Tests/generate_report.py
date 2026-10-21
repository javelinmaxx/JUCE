#!/usr/bin/env python3
"""Plot real Zao renders and independently solve its specified DC network."""
import os
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", "/tmp/zao-matplotlib")
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.optimize import root, brentq

destination = Path(__file__).resolve().parent / "Reports"
data = np.genfromtxt(destination / "waveforms.csv", delimiter=",", names=True)
dc = np.genfromtxt(destination / "operating-point.csv", delimiter=",", names=True)
residuals = np.genfromtxt(destination / "solver-residuals.csv", delimiter=",", names=True)
alias = np.genfromtxt(destination / "alias-check.csv", delimiter=",", names=True)
diode_dc = np.genfromtxt(destination / "diode-transfer.csv", delimiter=",", names=True)
diode_residuals = np.genfromtxt(destination / "diode-residuals.csv", delimiter=",", names=True)


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


def diode_current(voltage):
    return 2e-9 * (np.expm1(voltage / 0.049115) - np.expm1(-voltage / 0.049115))


shunt_reference = []
series_reference = []
for source in diode_dc["input_volts"]:
    shunt_reference.append(brentq(lambda v: (v - source) / 4700 + v / 100000 + diode_current(v),
                                  -1.5, 1.5, xtol=1e-14))
    drop = brentq(lambda d: d + 11000 * diode_current(d) - source, -1.5, 1.5, xtol=1e-14)
    series_reference.append((source - drop) * 10000 / 11000)
shunt_error = np.max(np.abs(diode_dc["shunt_volts"] - shunt_reference))
series_error = np.max(np.abs(diode_dc["series_volts"] - series_reference))
if max(shunt_error, series_error) > 1e-6:
    raise RuntimeError(f"Passive diode DC mismatch: {shunt_error}, {series_error} V")


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

diode_fig, diode_axes = plt.subplots(3, 2, figsize=(12, 10), constrained_layout=True)
for column, (mode, name, measured, reference_curve) in enumerate((
    (2, "Diode shunt clipper", diode_dc["shunt_volts"], shunt_reference),
    (3, "Series-diode crossover", diode_dc["series_volts"], series_reference),
)):
    ax = diode_axes[0, column]
    ax.plot(diode_dc["input_volts"], measured, color="#b77b45", label="C++ circuit")
    ax.plot(diode_dc["input_volts"][::12], np.asarray(reference_curve)[::12], ".", color="#333333", label="Independent DC root")
    ax.set(title=f"{name}: static transfer", xlabel="Input voltage (V)", ylabel="Output voltage (V)")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.15)
    for drive, color in zip((0, 12, 24), colors):
        rows = signal(mode, drive)
        t = np.arange(96) / 48
        diode_axes[1, column].plot(t, rows["output"][-96:], color=color, label=f"Drive {drive} dB")
    diode_axes[1, column].plot(t, rows["input"][-96:], "--", color="#999999", linewidth=0.8, label="Input")
    diode_axes[1, column].set(title="1 kHz reference output", xlabel="Time (ms)", ylabel="Output (FS)")
    diode_axes[1, column].legend(fontsize=8, ncol=2)
    diode_axes[1, column].grid(alpha=0.15)
    rows = signal(mode, 24)
    spectrum = np.abs(np.fft.rfft(rows["output"])) * 2 / len(rows)
    frequency = np.fft.rfftfreq(len(rows), 1 / 48000)
    bins = [np.argmin(np.abs(frequency - k * 1000)) for k in range(1, 16)]
    levels = 20 * np.log10(np.maximum(spectrum[bins], 1e-10))
    diode_axes[2, column].bar(np.arange(1, 16), levels + 120, bottom=-120, color="#b77b45", width=0.55)
    diode_axes[2, column].set(title="Harmonics at Drive 24 dB", xlabel="Harmonic number", ylabel="dBFS", ylim=(-120, 6))
    diode_axes[2, column].grid(axis="y", alpha=0.15)
diode_fig.suptitle("Zao Drive passive diode circuits · 48 kHz / 4× oversampling", fontsize=15)
diode_fig.savefig(destination / "diode-report.png", dpi=170)
diode_fig.savefig(destination / "diode-report.pdf")

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

## Diode shunt and series crossover modes

![Passive diode transfer curves and waveforms](diode-report.png)

The shunt pair limits peaks across its load, while the series pair creates a
central dead zone before conducting. Both are passive networks, so their
levels differ from the active amplifier modes. The same Input, Drive, Tone,
Output, Mix, and transformer controls remain available.
Both passive modes use ×4 input conditioning; the static plots specify source
voltage after that gain, while waveform plots use the common digital reference.

For DC source voltages from −3 to +3 V, independent SciPy bracketing solves
check the settled C++ outputs against the documented resistor and Shockley
equations. Maximum voltage disagreements are **{shunt_error:.3e} V** for shunt
and **{series_error:.3e} V** for crossover. Broadband ±12 V noise and abrupt
polarity reversals at 128, 192, and 384 kHz internal rates have maximum KCL
residuals of **{max(diode_residuals['shunt_residual_amps']):.3e} A** and
**{max(diode_residuals['series_residual_amps']):.3e} A**, respectively. Both
networks retain odd symmetry, including their capacitor states.

All four circuit positions are checked through host state recall, automation,
and maximum-drive processing. A two-mode legacy state is also loaded after a
diode-mode session to verify that it restores the original Op-Amp selection.
The selector uses the original normalized two-choice parameter plus a diode-pair
flag, preserving existing BJT/Op-Amp automation. The expected-output display
uses these same models and scales to keep the passive circuits visible.

Raw diode checks are in [diode-transfer.csv](diode-transfer.csv) and
[diode-residuals.csv](diode-residuals.csv); the diode plots also have a
[PDF version](diode-report.pdf).

These are simulated circuit tests. The model limits are documented in
[CIRCUITS.md](../../CIRCUITS.md); no full SPICE transient or physical-device
calibration is claimed. Raw renders are in [waveforms.csv](waveforms.csv),
[solver-residuals.csv](solver-residuals.csv), and
[operating-point.csv](operating-point.csv). A printable figure is available as
[PDF](circuit-report.pdf).
"""
(destination / "report.md").write_text(report)
print(f"Independent DC error: {dc_error:.3e} V")
print(f"Diode reference errors: shunt {shunt_error:.3e} V, crossover {series_error:.3e} V")
print(f"Wrote {destination / 'report.md'}")
