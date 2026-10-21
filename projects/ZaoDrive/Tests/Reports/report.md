# Zao Drive circuit test report

![Circuit waveforms and spectra](circuit-report.png)

## Numerical circuit accuracy

The four-node BJT DC solution is checked against a separate SciPy hybrid root
solve of the resistor network and documented transistor currents, initialized
independently of the C++ solution. Maximum node-voltage disagreement is
**7.239e-13 V**. This checks the nominal equations; it is not hardware calibration.

| Node | C++ model | Independent DC reference |
| --- | --- | --- |
| Base | 1.549234711 V | 1.549234711 V |
| Emitter | 0.889479543 V | 0.889479543 V |
| Collector | 4.999449778 V | 4.999449778 V |
| Emitter bypass | 0.701320409 V | 0.701320409 V |

Sustained 1.3 kHz input voltages of 0.035, 0.35, 3.5, and 12 V were exercised at
128, 192, and 384 kHz internal rates. These cover forward-active operation,
cutoff, saturation, and overload. Maximum BJT KCL residual was
**9.994e-11 A**; maximum op-amp equation residual
was **9.974e-11 V**. Op-amp output stayed within
its ±4 V rails. Antiparallel-diode symmetry was checked with inverse inputs.
Broadband ±12 V noise and abrupt polarity reversals also check convergence and
the op-amp's slew limit through transient overload.

## Overdrive and expected-output display

The upper plots use a −12 dBFS 1 kHz reference at Drive 0, 12, and 24 dB with
Output 0 dB, Mix 100%, Tone 18 kHz, and transformer Off. The BJT's asymmetric
clipping differs from the odd-symmetric op-amp feedback characteristic. At Drive
12 dB, the second harmonic is **-30.4 dBFS** for BJT and
**below −120 dBFS** for op-amp. The UI runs this same engine with the
current controls, using a fresh independent state and a settled reference input.

A 9 kHz op-amp input at Drive 24 dB tests the 21 kHz folded third harmonic.
The oversampled audio path reduces that component by
**25.1 dB** against the circuit run directly at 48 kHz.
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
equations. Maximum voltage disagreements are **2.235e-09 V** for shunt
and **3.630e-09 V** for crossover. Broadband ±12 V noise and abrupt
polarity reversals at 128, 192, and 384 kHz internal rates have maximum KCL
residuals of **9.991e-13 A** and
**9.997e-13 A**, respectively. Both
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
