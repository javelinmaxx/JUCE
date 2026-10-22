# Xuan Phase

Xuan Phase is J-SONIC's manual and transient-guided phase rotator. It offers
independent low/high rotation, a smooth adjustable crossover, Auto Depth,
Output, and a fixed **25 ms reference latency** in a 50%–120% scalable editor.

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
onsets from stationary noise. Local spectral peaks define up to 24 narrow
Gaussian phase bands. Their complex phase and locally unwrapped phase slopes
estimate how wave packets can meet the strongest transient sample. Relative
band-delay targets are limited to ±25 ms.

**Auto Depth** scales only these additional corrections: 0% leaves the manual
response unchanged, and 100% applies the full detected target. A shared stereo
reference preserves the relationship between channels; nearly cancelling
anti-phase input uses the left channel as the analysis reference. Targets hold
between detected onsets, allowing repeated hits to retain their alignment.
Auto Off or Depth 0% suspends spectral jobs. The first part of a new onset must
be observed before a target can be computed, so the improvement depends on the
source and worker scheduling; the phase objective is a punch-oriented heuristic.

The audio thread performs a short SIMD FIR head and a partitioned FFT tail.
The tail starts after the head, adding **no extra block latency**. A three-slot
handoff transfers immutable responses from the worker, and a 5 ms sigmoid
crossfade warms both responses on the same signal history. No kernel design,
heap allocation, or waiting for a worker occurs in the audio callback. Output
changes are smoothed over 20 ms. Auto analysis is dormant when Off.

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
xvfb-run -a build/xuan/XuanPhaseTests --editor "$PWD/projects/XuanPhase/Previews/xuan-phase.png"
python3 projects/XuanPhase/Tests/generate_report.py
```

[Hover hints](TOOLTIPS.md) cover every control. See the repository
[build guide](../../docs/BUILDING.md) for dependencies and installation paths.
