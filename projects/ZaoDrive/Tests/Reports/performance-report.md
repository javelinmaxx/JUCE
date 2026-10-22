# Zao Drive CPU optimization report

Measured 2026-10-22 against commit `3a63a7c8a3`, using Linux x86-64,
GCC 14.2 and a CMake Release build. All results are local wall-clock timings;
cloud scheduling varies, so these are not promised DAW CPU-meter reductions.

These measurements describe optimization revision `b8b88eaff6`, before the
subsequent one-octave contour shift. See [the current contour report](grey-report.md)
for its updated frequency response.

## Processing measurements

Each case processes 0.25 seconds of independent stereo tones plus periodic
impulses at 48, 96 or 192 kHz. Engine cases use 128-sample buffers, all four
circuits, default Drive/Mix/Output, with Tone, BJT Bias and diode Knee changes
halfway through. Smart and transformer are Off. FIR cases measure the standalone
stereo contour filter. Preparation, signal generation and file writes are
excluded from timings. Each value is the median of three trials, repeated in
three alternating before/after executable runs, then median-aggregated.

Representative 96 kHz results (milliseconds to process 0.25 seconds):

| Path | Before | After | Time reduction |
| --- | ---: | ---: | ---: |
| Contour FIR only | 11.904 | 4.381 | 63.2% |
| BJT CE | 144.392 | 97.492 | 32.5% |
| OPAMP FB | 61.882 | 52.233 | 15.6% |
| DIODE SHUNT | 78.903 | 60.999 | 22.7% |
| CROSSOVER | 81.814 | 64.282 | 21.4% |

[Full results at all three rates](performance-benchmark.csv) and
[individual runs](performance-runs.csv) include every case. These measurements
cover the processing engine; they do not quantify editor or Smart analysis CPU.
The FIR optimization retains double accumulation, the same impulse response,
linear phase and latency. Oversampling remains 4×; convergence tolerances and
iteration budgets remain unchanged. The transformer and Smart DSP are unchanged.

## Audio and behavior checks

The before/after captures contain 840,000 float samples across all cases.
Peak absolute error was 4.40627e-18 FS; RMS error was
4.85448e-21 FS. This is numerical agreement for these test
signals, with additional existing tests covering overload, noise, transient
reversals, alias rejection, stereo isolation, automation and bypass.

All processor tests passed. The native editor/Smart preview run passed **528
checks**, including the added reduced-matrix/reference comparison, Schottky
current and derivative parity, worker cancellation/latest-request handling,
reused-preview/fresh-engine agreement across four modes, and continuous
waveform/scale interpolation including retargeting mid-transition.
Linux Release VST3 and Standalone targets built successfully. Native macOS and
Windows builds were not run.

## Preview behavior

The display reuses a prepared, independent engine on a low-priority background
worker. Only visible editors submit changed targets, at most five per second.
Pending requests replace older ones; stale work is cancelled between blocks.
An idle preview worker waits for notification rather than polling. Small locks
connect only the UI and preview worker; the audio callback never takes them.

The waveform and its scale ease over 160 ms at 30 frames per second. New results
start from the currently displayed shape, so frequent updates do not jump.
Only the preview panel repaints during animation. The peak label follows the
displayed waveform. The first completed frame appears directly.

## Reproduction

Build and run `ZaoDrivePerformanceBench` as described in the project README;
its optional file argument captures concatenated raw float audio, ordered by
48/96/192 kHz, FIR then circuit modes 0–3, left then right channel per case.
Run the same benchmark source against both versions on an otherwise idle
machine. No timing threshold is used in the automated tests.

For the visible editor check on bare Linux Xvfb:

```bash
xvfb-run -a python3 projects/ZaoDrive/Tests/with_x11_atoms.py \
  build-zao/ZaoDriveTests --editor /tmp/zao-smart.png --smart-preview
```
