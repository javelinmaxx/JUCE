# Zao Drive 80-phon contour test report

![Capped ISO target, measured magnitude, phase and group delay](grey-report.png)

The wet-driver input now uses the **ISO 226:2003 equal-loudness contour at
80 phons**, referenced to **0 dB at 1 kHz**, with **upward gain capped at +3 dB**.
Negative gains keep their ISO values; there is no overall depth compression or
inversion of the contour. This replaces the former bounded inverse A-weighting
approximation. The phon selection defines the filter shape; the plugin does not
calibrate playback SPL or claim to set the listener's loudness to 80 phons.

## Target definition

The standard's 29 frequencies, `af`, `Lu` and `Tf` coefficients determine the
80-phon SPL curve. At each tabulated frequency:

```text
Af = 4.47e-3 × (10^(0.025 × 80) − 1.15) + (0.4 × 10^((Tf + Lu)/10 − 9))^af
Lp = (10/af) × log10(Af) − Lu + 94
gain = min(+3 dB, Lp(f) − Lp(1 kHz))
```

The 1 kHz SPL reference is approximately 80.012075 dB. Uncapped relative gains
are about +38.978 dB at 20 Hz, −2.096 dB at 4 kHz and +12.957 dB at 12.5 kHz.
The upward values become +3 dB; the 4 kHz cut is retained. SPL is interpolated
linearly in log frequency before applying the cap. Endpoints are held below
20 Hz and above 12.5 kHz, where this ISO table provides no additional values.

Reference: *ISO 226:2003, Acoustics — Normal equal-loudness-level contours*.
The [target CSV](grey-iso80-target.csv) records the SPL, uncapped relative gain,
and capped target at every ISO frequency. An independent NumPy evaluation of
the standard's equation agrees with the C++ values within 1e-9 dB.

## FIR measurements

A Kaiser-windowed (beta 3), symmetric Type-I FIR implements the target. It is
normalised at 1 kHz after windowing. Coefficients are designed during preparation;
no coefficient design or allocation occurs in the audio callback. The short FIR
smooths the contour's fine details: maximum fit error is **0.879 dB**
from 20 Hz to the smaller of Nyquist and 12.5 kHz over the tested sample rates.
Maximum actual upward gain is **2.647 dB**, within the requested cap;
1 kHz remains at unity within 1e-5 dB.

Delay is unchanged: approximately 2.67 ms, or 128 samples for the 257-tap FIR at
48 kHz. Every measured impulse is exactly symmetric. After compensating its
centre delay, maximum residual phase error is **1.760e-13 radians**
across 8192 frequency points per rate.

| Sample rate | FIR taps | Added FIR samples | Added FIR ms | Total plugin samples | Gain at 3.15 kHz |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 8,000 | 43 | 21 | 2.625 | 91 | -1.98 dB |
| 32,000 | 171 | 85 | 2.656 | 155 | -1.98 dB |
| 44,100 | 237 | 118 | 2.676 | 188 | -1.97 dB |
| 48,000 | 257 | 128 | 2.667 | 198 | -1.97 dB |
| 96,000 | 513 | 256 | 2.667 | 326 | -1.97 dB |
| 192,000 | 1025 | 512 | 2.667 | 582 | -1.98 dB |

Total plugin latency includes the FIR, circuit oversampler delay and existing
64-sample transformer compensation. Clean, filtered-reference and bypass delays
remain aligned in all modes, Smart states and Mix settings.

## Regression coverage

C++ checks validate independent 80-phon SPL anchors, the 1 kHz reference, the
unscaled negative gains, the +3 dB target and actual-response ceilings, endpoint
handling, impulse/streaming agreement, direct-convolution equivalence through
history wraps and stereo isolation from 8–192 kHz. End-to-end impulse tests
verify exact dry-mix and host-bypass timing at 44.1/48/96 kHz. All four circuits
are compared across 17-sample and 513-sample host blocks. Existing nonlinear
convergence, Smart adaptation, state recall and transformer parity tests pass.

The contour shapes only the driver feed. The wet residual subtracts its aligned
filtered reference, then Tone and Smart operate on that residual before adding
it to the flat clean branch. Circuit gain and phase can still leave fundamental
energy in the residual; their existing phase behavior is separate from the FIR's
constant delay. The alias benchmark uses the same shaped excitation for both
circuit versions.

Data: [impulses](grey-impulse.csv), [latencies](grey-latency.csv),
[ISO target](grey-iso80-target.csv). A matching [PDF](grey-report.pdf) is available.

Regenerate from the repository root:

```bash
build-zao/ZaoDriveTests --grey-report "$PWD/projects/ZaoDrive/Tests/Reports"
python3 projects/ZaoDrive/Tests/generate_grey_report.py
```
