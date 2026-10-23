# Zao Drive

Zao Drive is the fourth J-SONIC plugin in this repository, with a common-emitter
BJT overdrive, an op-amp feedback diode clipper, a diode shunt clipper, and
series-diode crossover distortion. It shares Yinyang, Shun,
and Kuo's ebony panels, amber controls, flip switch, and 50%–120% scalable UI.

![Zao Drive](Previews/zao-drive.png)

An [op-amp mode preview](Previews/zao-opamp.png) shows the feedback clipper with
the output transformer enabled; [Diode Shunt](Previews/zao-shunt.png) and
[Crossover](Previews/zao-crossover.png) previews show the two passive circuits.

The **Circuit** knob selects BJT CE, OPAMP FB, DIODE SHUNT, or CROSSOVER.
The first knob changes with the mode:

| Mode | First knob | Behavior |
| --- | --- | --- |
| BJT CE | Bias | Shifts the base-bias voltage by up to ±0.8 V around the nominal operating point. |
| OPAMP FB | Input | Adjusts input level from −24 to +24 dB. |
| DIODE SHUNT / CROSSOVER | Knee | Varies driver impedance, current limit and bandwidth from firm to soft. |

Drive adds up to 36 dB of excitation. **Clear**, beside Smart, toggles the
linear-phase ISO 226:2003 80-phon contour shifted down one octave. Its entire
previous curve is lowered by 3 dB: the maximum is now 0 dB, and the shifted
750 Hz reference is −3 dB. Clear starts On and is recalled with presets;
older presets load On to retain their contour path. Off fades to a flat,
delay-matched feed and then stops convolution, saving CPU. Re-enabling warms
the FIR before a 20 ms fade; latency stays constant.
The circuit output minus the aligned filtered reference forms a separate wet
residual, which Tone filters from 1.2–18 kHz. Mix adds that residual to the clean input;
Output sets the level entering the optional transformer.

**Auto**, in the Output section, matches the RMS of the preview's input and
processed output using a fixed correction after the transformer, then applies
the Output knob as an offset from that matched level. At 0 dB it matches RMS,
at +6 dB it is 6 dB louder, and at −6 dB it is 6 dB quieter. It uses the
same −12 dBFS, 1 kHz triangle reference waveform shown in Expected Output. There is no
live RMS detector, averaging or automatic reaction to the music: the correction
stays constant between reference updates, preserving the input's dynamics.
Every parameter change notifies the shared reference worker, even with the
editor closed. Control-driven recalculation has no polling delay or fixed rate
limit: if changes arrive during a render, the next render uses the latest
settings immediately. Completed snapshots keep updating the gain during
continuous adjustments; unchanged settings do not rerender. Smart-only updates
follow the 50 ms Smart cadence with Auto On, or retain a 200 ms display-only
limit with Auto Off to save CPU. Only changes to the correction fade over 20 ms;
this avoids clicks without continuously modulating the gain. Auto defaults Off,
is saved with presets and follows host automation. Existing presets load Off.
The Output knob still controls gain entering the transformer; Auto compensates
its resulting reference level before adding the same knob value as a final trim.
This retains the transformer coloration while allowing manual level adjustment. Correction is bounded to ±36 dB, with a silent
reference guard. See the [Auto report](Tests/Reports/auto-report.md) and
[enabled preview](Previews/zao-auto.png).

**Smart**, beside the circuit header, compares the input and generated wet
spectra using the supplied 12-TET dissonance curve. It suppresses clashing
harmonic components directly while leaving the manual circuit settings intact.
Analysis runs on a worker at up to 20 updates per second after the first full
4,096-sample window; successive windows overlap with a 50 ms hop. Filter changes
use endpoint-smooth sigmoid crossfades over that same 50 ms interval, and Smart adds
no latency. It defaults Off and saves CPU when Off. See [SMART.md](SMART.md)
for the signal path and separation limits, the [measured report](Tests/Reports/smart-report.md),
and the [Smart preview](Previews/zao-smart.png).

The **Expected Output** display renders a −12 dBFS, 1 kHz triangle reference through a
separate instance of the actual audio engine. Gray is the delayed input and amber
is the predicted output, including Clear, Auto, Tone, Mix, Output, bypass, and transformer
settings. It is a reference waveform, not a live oscilloscope or a prediction of
arbitrary input material. The labeled plot scale fits both input and output,
starting at ±0.25 FS to keep passive diode behavior visible. Rendering runs on a
dedicated background worker that reuses its prepared filters and oversampler,
without sharing circuit state or locks with the audio callback. The processor and editor share this worker; every control change requests an
update, with no fixed render-rate cap. New changes during rendering coalesce
into the next latest snapshot, rather than building a queue. Smart-only updates
follow the 20 Hz analysis cadence with Auto On and remain limited to five
times per second for the display alone with Auto Off. It also runs with the editor
closed while Auto is enabled, keeping audio and preview calibration identical. Waveforms and the display scale ease between results over
160 ms at 30 animation frames per second; new targets continue from the current
displayed shape rather than jumping.

The optional **Transformer Stage** directly includes Yinyang's
[`OversampledTransformer`](../YinyangCompressor/Source/OversampledTransformer.h)
class. Core Drive and DC Bias have the same behavior, including hysteresis,
frequency response, and filtering of generated distortion. The transformer
stops processing once its Off fade finishes; re-enabling warms its delay before
fading in. Circuit processing uses a separate 4× oversampler. Host latency is the
grey FIR delay (128 samples at 48 kHz), integer circuit oversampler delay,
plus the transformer's fixed 64 samples and
stays constant with every model, Clear, Auto, Mix, bypass, or transformer switching.

The clean branch stays flat and is delayed to match the FIR. The FIR's linear
phase prevents additional frequency-dependent phase rotation when the branches
merge; circuit and transformer coloration still have their own phase response.
See the [grey filter report](Tests/Reports/grey-report.md) for measured magnitude,
phase and latency across sample rates.

All controls have one-sentence hover hints, listed in [TOOLTIPS.md](TOOLTIPS.md).

## Circuit models

These are numerical circuit models with explicit nominal component values, not
generic transistor-shaped transfer curves. Their accuracy is relative to the
specified equations and components; they are not calibrated reproductions of a
particular transistor, pedal, or op-amp IC. See [CIRCUITS.md](CIRCUITS.md) for the
topologies, device parameters, integration method, and model limits.

- **BJT CE:** a 9 V biased common-emitter stage, with source impedance, input
  coupling capacitor, loaded bias divider, collector resistor, split emitter
  resistors, and emitter bypass capacitor. A charge-based Ebers–Moll transistor
  includes forward/reverse current gain, both junctions, Early modulation,
  voltage-dependent depletion capacitance, and diffusion charge. Newton solves
  all four circuit nodes at every oversampled step. Its physical inversion is
  corrected at the output so parallel mixing retains polarity.
- **OPAMP FB:** a non-inverting amplifier with a 4.7 kΩ ground leg and 47 kΩ
  feedback leg. Ballasted antiparallel Schottky diodes and a 51 pF capacitor sit inside the
  feedback loop. The solve includes 100 dB open-loop gain, a 3 MHz gain-bandwidth
  product, 100 Ω output impedance, a 100 kΩ receiver, ±4 V internal rails and a
  1 V/µs slew limit.
- **DIODE SHUNT:** a 4.7 kΩ source feeds a 100 kΩ load with ballasted antiparallel Schottky
  diodes shunting the output to ground. Quiet signals pass through; higher
  voltages flatten into diode-limited peaks.
- **CROSSOVER:** ballasted antiparallel Schottky diodes sit in series between a 1 kΩ source
  and a 10 kΩ load. Their forward-voltage drop creates a central dead zone, with
  real leakage and capacitive feedthrough instead of a hard mathematical gate.
  Drive brings more of the signal above that conduction region. Both passive
  circuits use Shockley currents and trapezoidal capacitor companions, with a
  fixed ×4 input-conditioning gain before a finite-current, finite-bandwidth
  amplifier. At Knee 50% its output resistance is 2.2 kΩ, current limit 3.162 mA
  and pole 15 kHz. All diode models use nominal Schottky junctions (200 nA Is,
  31.02 mV nVt) with 220 Ω series ballast for a softer conducting knee.

All four circuits keep independent left/right states. Parameters are smoothed;
changing circuit crossfades over 10 ms, and inactive circuits then stop
processing. Only active circuits receive per-sample parameter updates and enter
the processing loop; the preview and Auto renderer use the same engine. A mode
that wakes again resets its stored circuit state before fading in. No allocation,
mutex, file I/O, or preview rendering occurs in the
audio callback.

Existing two-mode sessions and normalized `model` automation remain compatible:
the original 0/1 parameter still selects BJT/Op-Amp. The added `diodeCircuit`
parameter selects the diode pair, where the same 0/1 parameter selects
Shunt/Crossover. The UI combines both parameters into one four-position knob. Bias and Knee
are appended as independent parameters, so switching the first knob retains
each control's saved value. Older states default to Bias 0% and Knee 50%.
The legacy Input parameter retains its gain behavior for saved presets and host
automation in every mode; its knob is visible in Op-Amp mode.

## CPU optimization

The circuit solvers reuse junction exponentials and accepted transistor charges;
the BJT solve eliminates its passive bypass node before solving the remaining
three nodes with partial pivoting. Iteration limits, convergence tolerances,
physical circuit equations, and 4× oversampling are unchanged. The contour FIR
uses SIMD double-precision accumulation where supported, with a scalar fallback
and the same coefficients, phase and latency. Tone coefficients update during
automation smoothing and are cached while the control is stationary.

See the [performance report](Tests/Reports/performance-report.md) for benchmark
conditions, before/after timings and audio comparison. To run the diagnostic
benchmark in a Release build:

```bash
cmake --build build-zao --target ZaoDrivePerformanceBench --parallel 2
build-zao/ZaoDrivePerformanceBench /tmp/zao-audio.bin
```

The optional argument captures raw stereo float samples for version comparisons;
timings are CSV on standard output and are not pass/fail test limits.

## Build and test

From the repository root:

```bash
cmake -S projects/ZaoDrive -B build-zao -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-zao --target ZaoDrive_VST3 ZaoDrive_Standalone ZaoDriveTests --parallel 2
ctest --test-dir build-zao --output-on-failure
```

The VST3 bundle is under `build-zao/ZaoDrive_artefacts/Release/VST3/`.
AU is enabled when configuring on macOS. Use an appropriate CMake generator
and toolchain for Windows/macOS; the Windows workflow is unchanged.

Capture a native editor preview on Linux with the Xvfb helper (it supplies the
window-manager atoms needed for a real window on a bare virtual display):

```bash
xvfb-run -a python3 projects/ZaoDrive/Tests/with_x11_atoms.py build-zao/ZaoDriveTests --editor /tmp/zao-drive.png
xvfb-run -a python3 projects/ZaoDrive/Tests/with_x11_atoms.py build-zao/ZaoDriveTests --editor /tmp/zao-tooltip.png --tooltip
```

The tests check circuit residuals through cutoff/saturation, diode shunt limiting,
series-diode dead-zone behavior, current conservation,
op-amp rail limits and symmetry, harmonic differences, host state recall, latency,
bypass/dry exactness, stereo isolation, and sample-for-sample transformer parity
with Yinyang. Editor checks cover the waveform display, host controls, hover hints,
and both resize limits.

Generate the [circuit report](Tests/Reports/report.md) and standalone plots:

```bash
build-zao/ZaoDriveTests --report "$PWD/projects/ZaoDrive/Tests/Reports"
python3 projects/ZaoDrive/Tests/generate_report.py
```

Report plotting needs NumPy, SciPy, and Matplotlib; building the plugin does not.
