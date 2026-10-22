# Xuan Phase

Xuan Phase is J-SONIC's manual and transient-guided phase rotator. It offers
independent low/high rotation, a smooth adjustable crossover, signed Auto Depth,
Transient release, Output, and a fixed **25 ms reference latency** in a 50%–120% scalable editor.
The 840 × 490 default panel includes a vertical phase-field meter beside the controls.

![Xuan Phase native editor](Previews/xuan-phase.png)

## Manual rotation and the 25 ms reference

Low Phase and High Phase range from −180° to +180°. Crossover, from 80 Hz to
4 kHz, smoothly blends the two phase settings; at the crossover the requested
angle is their average. Positive angles advance phase relative to the delayed
reference, and negative angles retard it. Both stereo channels use one response.

A 20 Hz cycle lasts 50 ms, so 180° at 20 Hz corresponds to 25 ms. The plugin
reports `round(sampleRate × 0.025)` samples to the host: 1,200 samples at 48 kHz.
Its real FIR spans ±25 ms around that reference, making phase advances causal
within the available lookahead. Neutral controls return only the reference
delay, as does host/plugin bypass. Rotated transients can move or spread around
the reference; reported latency remains fixed, rather than tracking every
frequency's group delay.

The desired positive-frequency response is unit magnitude with phase
`low × w(f) + high × (1 − w(f))`, where `w(f) = 1 / (1 + (f / crossover)^4)`.
The worker builds a real, conjugate-symmetric FIR and tapers its outer edges.
This finite response approximates an ideal phase-only rotator: magnitude ripple
is measurable, especially near 20 Hz at intermediate angles. DC and Nyquist
must be real; they cannot carry an arbitrary quadrature rotation. See the
[measured report](Tests/Reports/REPORT.md) rather than assuming an exact all-pass.

## Transient alignment

Auto analyzes overlapping input spectrogram frames on a separate worker.
Positive spectral flux, an energy rise, and transient crest distinguish clear
onsets from stationary noise. Local spectral peaks define up to 24 phase-band anchors. Neighboring local
phase/delay curves join with smoothstep interpolation, with angles unwrapped
across ±180°; only the outer edges taper to neutral. Peak selection reserves
coverage across logarithmic regions up to 20 kHz (or 45% of sample rate), then
fills spare slots with the strongest remaining peaks to avoid low-frequency bias. Their complex phase and locally unwrapped phase slopes
estimate how wave packets can meet the strongest transient sample. Relative
band-delay targets are limited to ±25 ms.

**Auto Depth** ranges from −100% to +100% and scales only these additional
corrections: 0% leaves the manual response unchanged, +100% applies the full
detected target, and negative values reverse the correction away from alignment.
A shared stereo reference preserves the relationship between channels; nearly cancelling
anti-phase input uses the left channel as the analysis reference.

**Transient** sets the exponential correction release from 20 to 1,000 ms
(default 200 ms): 36.8% remains after one time constant, counted in audio samples
after the last detected onset. New onsets refresh the correction; it decays
back toward the manual response between hits, and returns fully once below 0.1%.
Release responses update at up to 50 Hz through the existing 5 ms kernel fades.
Auto Off or Depth 0% stops automatic response updates; a visible meter retains
lower-rate input analysis for manual use, which stops when the editor is hidden.
The first part of a new onset must
be observed before a target can be computed, so the improvement depends on the
source and worker scheduling; the phase objective is a punch-oriented heuristic.

The audio thread performs a short SIMD FIR head and a partitioned FFT tail.
The tail starts after the head, adding **no extra block latency**. A three-slot
handoff transfers immutable responses from the worker, and a 5 ms sigmoid
crossfade warms both responses on the same signal history. No kernel design,
heap allocation, or waiting for a worker occurs in the audio callback. Output
changes are smoothed over 20 ms. Auto analysis is dormant when Off.

## Phase Field meter

The side panel shows three seconds of input spectrogram history on a logarithmic
frequency axis. **New data enters at the left and moves right** toward older
history. Brighter pixels mean more input energy; **green** marks frequency bands
arriving before the detected transient anchor, and **red** marks those arriving
after it. Colours describe observed input timing, so they also work with Auto Off.
Timing colors interpolate between adjacent detected bands; spectral energy
remains unchanged, so genuine gaps in the input still appear dark.
A small display hold makes short onsets visible without changing the audio detector.

The **white curve** in the 56 px track at the right shows applied rotation
relative to the 25 ms reference, from −180° to +180° across its track. It is measured from the
active FIR, including manual rotation, Auto Depth, response crossfades, and bypass;
it does not jump ahead to an unapplied worker target. Guides mark 0°, ±45°,
±90°, and ±135°, with matching line styles in the key below the track; positive rotation is right of center and negative is left.
The history is 168 px wide, and the readout reports the largest absolute
rotation. The lower strip shows input peak activity in white and detected
transient activity in amber.

The meter shares the background spectral analysis. Fixed-size atomic snapshots
transfer input and applied-response data without GUI locks in the audio callback.
No display FFT runs on the audio thread. Rendering and scrolling run at 30 Hz,
with stale input cleared after 200 ms and blank columns for missed UI frames.
Closing the editor clears its history; sample-rate changes clear incompatible
frequency history. Audio processing and the fixed latency are unchanged.

The [Auto alignment performance report](Tests/Reports/AUTO_ALIGNMENT.md)
includes a live three-hit Auto case, measured phase errors, positive/negative
Depth, short/long release curves, and matched-level listening examples.
The phase display rounds the curve with shape-preserving cubic interpolation
and splits at ±180° wraps; Output sits at the far right beside Transient.

## Build and test

From the repository root:

```bash
cmake -S projects/XuanPhase -B build/xuan -DCMAKE_BUILD_TYPE=Release
cmake --build build/xuan --config Release --target XuanPhase_VST3 XuanPhase_Standalone XuanPhaseTests --parallel 3
ctest --test-dir build/xuan -C Release --output-on-failure
```

On headless Linux, run CTest through `xvfb-run -a` to provide a display for JUCE.

macOS also provides `XuanPhase_AU`; `build-macos-all.sh` discovers this project
automatically. Windows can build `XuanPhase_VST3` with Visual Studio 2022 using
the same configure/build pattern and `--config Release`; the existing Windows
workflows are unchanged. For plugin-only builds set `-DXUAN_BUILD_TESTS=OFF`.

Generate measurements and a native preview on Linux:

```bash
xvfb-run -a build/xuan/XuanPhaseTests --report "$PWD/projects/XuanPhase/Tests/Reports"
xvfb-run -a python3 projects/ZaoDrive/Tests/with_x11_atoms.py \
  build/xuan/XuanPhaseTests --editor "$PWD/projects/XuanPhase/Previews/xuan-phase.png"
python3 projects/XuanPhase/Tests/generate_report.py
```

The editor capture uses the existing X11 window-atom helper from Zao Drive's
tests so the preview exercises a real desktop window on a bare Xvfb display.

[Hover hints](TOOLTIPS.md) cover every control. See the repository
[build guide](../../docs/BUILDING.md) for dependencies and installation paths.
