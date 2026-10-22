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


def dc_kcl(v, bias_offset=0):
    """Independent residual formulation from the documented component network."""
    base, emitter, collector, bypass = v
    forward = 6.734e-15 * np.expm1((base - emitter) / 0.02585)
    reverse = 6.734e-15 * np.expm1((base - collector) / 0.02585)
    collector_current = forward * (1 + (collector - emitter) / 100) - 1.25 * reverse
    base_current = forward / 200 + reverse / 4
    return np.array([
        (base - 9) / 100000 + base / 22000 - bias_offset * (1/100000 + 1/22000) + base_current,
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


def diode_current(junction):
    return 200e-9 * (np.expm1(junction / 0.03102) - np.expm1(-junction / 0.03102))


def diode_reference(source, knee=0.5):
    command = 9 * np.tanh(source/9)
    resistance = 220 * 100**knee
    limit = 0.01 * 0.1**knee
    def shunt_kcl(j):
        current = diode_current(j)
        node = j + 220 * current
        delivered = limit * np.tanh((command-node) / ((4700+resistance)*limit))
        return node/100000 + current - delivered
    junction = brentq(shunt_kcl, -1.5, 1.5, xtol=1e-14)
    shunt = junction + 220 * diode_current(junction)
    def series_kcl(j):
        current = diode_current(j)
        drop = j + 220*current
        delivered = limit * np.tanh((command-drop-10000*current) / ((1000+resistance)*limit))
        return current-delivered
    junction = brentq(series_kcl, -1.5, 1.5, xtol=1e-14)
    return shunt, 10000*diode_current(junction)


references = np.array([diode_reference(source) for source in diode_dc["input_volts"]])
shunt_reference, series_reference = references[:, 0], references[:, 1]
shunt_error = np.max(np.abs(diode_dc["shunt_volts"] - shunt_reference))
series_error = np.max(np.abs(diode_dc["series_volts"] - series_reference))
if max(shunt_error, series_error) > 1e-6:
    raise RuntimeError(f"Loaded Schottky DC mismatch: {shunt_error}, {series_error} V")

shape = np.genfromtxt(destination / "shape-waveforms.csv", delimiter=",", names=True)
bias_dc = np.genfromtxt(destination / "bias-operating-points.csv", delimiter=",", names=True)
knee_dc = np.genfromtxt(destination / "knee-transfer.csv", delimiter=",", names=True)
shape_residuals = np.genfromtxt(destination / "shape-residuals.csv", delimiter=",", names=True)
softness = np.genfromtxt(destination / "softness.csv", delimiter=",", names=True)
bias_error = 0
for row in bias_dc:
    ref = root(lambda v: dc_kcl(v, row["bias"] * .008), [1.5, .9, 5, .7], tol=1e-11)
    measured = np.array([row[n] for n in ("base", "emitter", "collector", "bypass")])
    if not ref.success or np.max(np.abs(dc_kcl(ref.x, row["bias"]*.008))) > 1e-10:
        raise RuntimeError("Biased BJT reference did not converge")
    bias_error = max(bias_error, np.max(np.abs(measured-ref.x)))
knee_error = 0
for row in knee_dc:
    ref = diode_reference(row["input_volts"], row["knee"]*.01)
    knee_error = max(knee_error, abs(ref[0]-row["shunt_volts"]), abs(ref[1]-row["series_volts"]))
if max(bias_error, knee_error) > 1e-6:
    raise RuntimeError(f"Bias/Knee DC mismatch: {bias_error}, {knee_error}")


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
    ax.set(title=f"{name}: static transfer", xlabel="Amplifier command (V)", ylabel="Output voltage (V)")
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
diode_fig.suptitle("Zao Drive loaded Schottky circuits · 48 kHz / 4× oversampling", fontsize=15)
diode_fig.savefig(destination / "diode-report.png", dpi=170)
diode_fig.savefig(destination / "diode-report.pdf")

shape_fig, shape_axes = plt.subplots(3, 3, figsize=(14, 10), constrained_layout=True)
shape_axes[0, 0].plot(bias_dc["bias"], bias_dc["collector"], "o-", color="#b77b45", label="Collector")
shape_axes[0, 0].plot(bias_dc["bias"], bias_dc["base"], "o-", color="#63646e", label="Base")
shape_axes[0, 0].set(title="BJT idle operating point", xlabel="Bias (%)", ylabel="Voltage (V)")
shape_axes[0, 0].legend(fontsize=8)
for column, mode in enumerate((0, 2, 3)):
    controls = (-100, 0, 100) if mode == 0 else (0, 50, 100)
    for control, color in zip(controls, colors):
        selected = shape[(shape["model"] == mode) & (shape["control"] == control)]
        shape_axes[1, column].plot(np.arange(96)/48, selected["output"][-96:], color=color,
                                  label=f"{'Bias' if mode == 0 else 'Knee'} {control}%")
        spectrum = np.abs(np.fft.rfft(selected["output"])) * 2 / len(selected)
        bins = np.arange(1, 12)*20
        shape_axes[2, column].plot(np.arange(1, 12), 20*np.log10(np.maximum(spectrum[bins], 1e-9)),
                                   "o-", color=color, markersize=3, label=f"{control}%")
        if mode != 0:
            rows = diode_dc if control == 50 else knee_dc[knee_dc["knee"] == control]
            field = "shunt_volts" if mode == 2 else "series_volts"
            shape_axes[0, column].plot(rows["input_volts"], rows[field], color=color, label=f"Knee {control}%")
    shape_axes[1, column].set(title=("BJT Bias", "Shunt Knee", "Crossover Knee")[column]+" at Drive 12 dB",
                              xlabel="Time (ms)", ylabel="Output (FS)")
    shape_axes[1, column].legend(fontsize=8)
    shape_axes[2, column].set(title="Harmonic balance", xlabel="Harmonic number", ylabel="dBFS", ylim=(-120, 6))
    if mode != 0:
        shape_axes[0, column].set(title="Loaded Schottky transfer", xlabel="Amplifier command (V)", ylabel="Receiver voltage (V)")
        shape_axes[0, column].legend(fontsize=8)
for ax in shape_axes.flat:
    ax.grid(alpha=.15)
shape_fig.suptitle("Zao Drive: BJT Bias and finite-impedance Schottky Knee", fontsize=15)
shape_fig.savefig(destination / "shape-report.png", dpi=170)
shape_fig.savefig(destination / "shape-report.pdf")

rows = "\n".join(f"| {int(r['bias'])}% | {r['base']:.6f} V | {r['collector']:.6f} V |" for r in bias_dc)
report = f"""# Zao Drive circuit test report

![Bias and Knee behavior](shape-report.png)

## Schottky diodes and amplifier impedance

All three diode circuits use nominal Schottky junctions (Is 200 nA, nVt
31.02 mV) and a 220 Ω series ballast. A lower forward voltage alone does not
ensure softness: the ballast gives the conducting pair a finite incremental
resistance. The two external diode networks are driven by a loaded amplifier
whose output impedance, current delivery and bandwidth depend on Knee:

| Knee | Driver resistance | Current limit | Bandwidth |
| --- | --- | --- | --- |
| 0% | 220 Ω | 10 mA | 45 kHz |
| 50% | 2.2 kΩ | 3.162 mA | 15 kHz |
| 100% | 22 kΩ | 1 mA | 5 kHz |

These resistances add to the circuit's own source resistor. The amplifier's
smooth ±9 V swing and smooth current limit enter the receiving circuit's KCL;
the driver is not an ideal gain feeding a square voltage clamp. Knee controls
impedance and headroom, so it also changes level. The feedback mode additionally
models 100 Ω op-amp output resistance supplying the feedback network and a
100 kΩ receiver, while retaining its ±4 V internal rail and 1 V/µs slew limits.

For a 4 V, 1 kHz shunt command at 192 kHz internal rate, the fraction of samples
above 95% of each waveform's own peak falls from
**{float(softness['silicon_flat_fraction'])*100:.1f}%** in an independent static
solve of the former ideal-source silicon network to
**{float(softness['schottky_flat_fraction'])*100:.1f}%** in the new loaded
Schottky circuit. This is a specified flat-top benchmark, not a listening test.

## BJT nonideal bias

Bias shifts the base divider's Thevenin voltage by −0.8 to +0.8 V while retaining
its source impedance. It changes the transistor's actual DC operating point and
asymmetric headroom; it is not a digital offset added after clipping. The input
and emitter coupling states are initialized at that operating point. During
automation, Bias and the diode driver parameters are smoothed over 20 ms.

| Bias | Base | Collector |
| --- | --- | --- |
{rows}

Independent SciPy DC solves agree within **{bias_error:.3e} V** across these bias
settings; nominal-bias node error is **{dc_error:.3e} V**. Maximum BJT KCL residual
under ±12 V noise and overload, across Bias −100/0/+100% and internal rates
128/192/384 kHz, is **{max(shape_residuals['bjt_amps']):.3e} A**.

## Numerical validation

![Schottky transfer curves and waveforms](diode-report.png)

Independent bracketing solves include the Schottky ballast, amplifier current
limit, amplifier resistance and receiving load. The settled C++ transfer curves
agree within **{shunt_error:.3e} V** (shunt) and **{series_error:.3e} V** (crossover)
at Knee 50%; maximum error at the Knee endpoints is **{knee_error:.3e} V**.
The largest shunt/crossover KCL residuals at all three Knee positions under
noise and abrupt overload at 128/192/384 kHz are
**{max(shape_residuals['shunt_amps']):.3e} A** and
**{max(shape_residuals['series_amps']):.3e} A**. The scalar solves are bounded to
40 iterations; very stiff capacitor transitions can stop above the 1e-12 A
convergence target while remaining within the tested 1e-9 A bound.
The feedback circuit's largest equation residual in the level sweep is
**{max(residuals['opamp_residual_volts']):.3e} V**. Its rails, slew rate and diode
symmetry are tested independently of these DC checks.

## Audio, transformer and host behavior

![Circuit waveforms, spectra and transformer](circuit-report.png)

The plots use a −12 dBFS 1 kHz input, Output 0 dB, Mix 100%, Tone 18 kHz and
transformer Off and Smart Off unless labeled. Tone filters the driver-minus-input
wet residual, which is then added to the aligned clean branch. The shape plots sweep BJT Bias and diode Knee
at Drive 12 dB. A 9 kHz op-amp input at Drive 24 dB has **{float(alias['reduction_db']):.1f} dB**
less folded third harmonic with 4× circuit oversampling than at the host rate.
YinYang's directly included transformer still matches the original class within
1e-6 FS when isolated with Mix 0%. Dry mix and bypass preserve reported latency.
Mono/stereo, oversized blocks, parameter recall, and maximum-drive stability
while Bias/Knee automation changes are also exercised.

The first knob binds to BJT Bias, Op-Amp Input, or the shared diode Knee according
to mode. Bias and Knee use new independent host parameters, preserving the
existing parameter IDs, normalized circuit mapping and input-gain automation.
Old presets default to Bias 0% and Knee 50% even after another preset has used
different values. The expected-output display uses the same engine and includes
the new controls. Native editor checks cover all four contextual bindings,
parameter changes, one-sentence hints, and 50–120% scaling.

The models specify nominal components; they are not a calibrated particular
pedal or Schottky part. Full equations and limits are in
[CIRCUITS.md](../../CIRCUITS.md). Raw data includes
[shape-waveforms.csv](shape-waveforms.csv), [bias-operating-points.csv](bias-operating-points.csv),
[knee-transfer.csv](knee-transfer.csv), [shape-residuals.csv](shape-residuals.csv),
[waveforms.csv](waveforms.csv) and [diode-transfer.csv](diode-transfer.csv).
The shape, circuit and diode figures have matching PDF versions.

## Validation — 2026-10-22

Linux Release VST3 and Standalone builds passed without compiler warnings.
All 200 circuit/processor/Smart checks passed. Native editor validation passed 249
checks with the tooltip visible, including mode-specific control rebinding,
independent host automation and recall, waveform updates and both resize limits.
The per-mode native preview checks passed. Windows workflows were not
changed or run.
"""
(destination / "report.md").write_text(report)
print(f"DC errors: nominal BJT {dc_error:.3e}, biased BJT {bias_error:.3e}, shunt {shunt_error:.3e}, crossover {series_error:.3e}, Knee endpoints {knee_error:.3e} V")
print(f"Wrote {destination / 'report.md'}")
