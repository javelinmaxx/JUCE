# Zao Drive Smart harmonic report

![Dissonance graph, content-dependent targets and measured wet spectra](smart-report.png)

The driver and clean input run in parallel. A linear-phase grey-noise-style
FIR shapes the driver feed using the 80-phon ISO contour with a +3 dB boost cap. The circuit output minus its aligned filtered
reference forms the wet residual; Tone and Smart process this residual, then Mix
adds it to the clean branch. Circuit gain/phase changes can remain in that
residual, so subtraction alone does not mathematically isolate only higher orders.
Smart protects existing input peaks and targets measured added harmonic orders
2–12 using a continuous approximation of the supplied 12-TET dissonance graph.
Drive, Bias, Knee, and the circuit equations stay unchanged.

## Measured audio

All four real circuit engines render C major (C4/E4/G4) and A minor (A3/C4/E4)
at 48 kHz, 0.1 FS peak per sine, Drive 24 dB, Tone 18 kHz, Mix 100%, Output
0 dB, transformer Off, with grey shaping enabled. A Hann-windowed 4096-sample frame determines the targets.
The actual causal filter bank then processes the recorded residual with that
steady-state analysis snapshot. The final 8192 samples are measured by Hann-windowed
Fourier projection at each selected harmonic frequency; these are measured
results, rather than the optimizer's predicted attenuation. Polyphonic leakage
and nearby harmonics can affect the measurements.

| Circuit | Chord | Target bands | Weighted harmonic energy reduction | Target harmonic energy retained |
| --- | --- | ---: | ---: | ---: |
| BJT CE | C major | 12 | 27.3% | 76.2% |
| BJT CE | A minor | 15 | 29.4% | 78.2% |
| Op-Amp feedback | C major | 16 | 30.4% | 74.8% |
| Op-Amp feedback | A minor | 16 | 35.1% | 70.8% |
| Diode shunt | C major | 16 | 30.4% | 74.7% |
| Diode shunt | A minor | 16 | 34.8% | 71.1% |
| Series crossover | C major | 1 | 49.7% | 50.3% |
| Series crossover | A minor | 4 | 21.0% | 80.2% |

The cost is the sum of target harmonic power times its interval dissonance;
retention is the power remaining at those targets, not total output loudness.
Consonant or input-overlapping frequencies are protected; there is no overall
drive reduction or normalization trick. Band cuts are bounded at 6 dB each.
The sampled graph is a musical heuristic, not a universal perceptual clarity metric.

The independent clean-branch reconstruction error is below 2e-5 FS in the CSV
round trip. The synthetic known-harmonic test additionally keeps the three
original chord tones within 0.15 dB, checks actual attenuation of added energy,
and confirms different targets for major and minor content.

## Real-time and host checks

The processor test feeds live circuit audio to the worker, exercises publication
and stale-result rejection, checks that Smart Off submits no FFT jobs, verifies
unchanged reported latency and unchanged manual controls, and saves/restores
Smart including legacy presets. Silence, noise, anti-phase stereo, consonance
anchors and bounded harmonic cuts are covered. New filter banks crossfade over
50 ms using a quintic smoothstep sigmoid with exact, flat endpoints. Analysis
refreshes at up to 20 times per second after the first full frame, retaining
the 4,096-sample window with overlapping 50 ms hops. Real-worker scheduling tests
verify the first full window and successive sample-accurate 50 ms submissions at
8/44.1/48/96/192 kHz. Audio tests compare both channels against independent
dry/filtered signals throughout the sigmoid fade and verify exact target-bank
output at 50 ms. A ready target starts on the next sample at the fade endpoint;
worker scheduling can add gaps if the target is not ready. New targets never
interrupt an unfinished fade. No FFT or circuit search runs in the audio callback.

Source data: [audio](smart-audio.csv), [target bands](smart-bands.csv),
[predicted scores](smart-scores.csv), [dissonance curve](smart-dissonance.csv).
The matching [PDF](smart-report.pdf) can be exported. See [SMART.md](../../SMART.md)
for the signal path, formulas and separation limits.
