# Zao Drive 80-phon contour test report

![Capped ISO target, measured magnitude, phase and group delay](grey-report.png)

The wet-driver input now uses the **ISO 226:2003 equal-loudness contour at
80 phons shifted down one octave**, moving its **0 dB reference to 750 Hz**,
with **upward gain capped at +3 dB**.
Only upward target gain is limited; negative target gains retain their ISO
values without inverting the contour. The frequency-shifted contour is derived from the standard; it is not
an unshifted ISO hearing contour at the applied frequencies. The phon selection defines the filter shape; the plugin does not
calibrate playback SPL or claim to set the listener's loudness to 80 phons.

## Target definition

The standard's 29 frequencies, `af`, `Lu` and `Tf` coefficients determine the
80-phon SPL curve. At each tabulated frequency:

```text
Af = 4.47e-3 × (10^(0.025 × 80) − 1.15) + (0.4 × 10^((Tf + Lu)/10 − 9))^af
Lp = (10/af) × log10(Af) − Lu + 94
gain(f) = min(+3 dB, Lp(2 × f) − Lp(1.5 kHz))
```

The original 1.5 kHz SPL reference is interpolated to approximately 83.412071 dB.
It now maps to 750 Hz in the applied contour. Uncapped relative gains are about
+35.578 dB at 10 Hz, -5.496 dB at 2 kHz and
+9.557 dB at 6.25 kHz. The upward values become +3 dB;
the former 4 kHz cut now appears at 2 kHz. SPL is interpolated
linearly in log frequency before applying the cap. Endpoints are held below
10 Hz and above 6.25 kHz after the shift; the source ISO table spans
20 Hz to 12.5 kHz.

Reference: *ISO 226:2003, Acoustics — Normal equal-loudness-level contours*.
The [target CSV](grey-iso80-target.csv) records the SPL, uncapped relative gain,
and capped target at every shifted ISO frequency (half its source frequency). An independent NumPy evaluation of
the standard's equation agrees with the C++ values within 1e-9 dB.

## FIR measurements

A Kaiser-windowed (beta 1), symmetric Type-I FIR implements the target. The
lighter window preserves the narrower shifted features at the existing tap
count and latency. It is
normalised at 750 Hz after windowing. If that normalization lifts the actual
response above +3 dB, a small blend with a pure delay bounds it while preserving
unity at 750 Hz and exact tap symmetry. This blends only enough to enforce the
existing cap. Coefficients are designed during preparation;
no coefficient design or allocation occurs in the audio callback. The short FIR
smooths the contour's fine details: maximum fit error is **1.043 dB**
from 10 Hz to the smaller of Nyquist and 6.25 kHz over the tested sample rates.
Maximum actual upward gain is **3.000 dB**, within the requested cap;
750 Hz remains at unity within 1e-5 dB.

Delay is unchanged: approximately 2.67 ms, or 128 samples for the 257-tap FIR at
48 kHz. Every measured impulse is exactly symmetric. After compensating its
centre delay, maximum residual phase error is **1.760e-13 radians**
across 8192 frequency points per rate.

| Sample rate | FIR taps | Added FIR samples | Added FIR ms | Total plugin samples | Gain at 1.575 kHz |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 8,000 | 43 | 21 | 2.625 | 91 | -4.62 dB |
| 32,000 | 171 | 85 | 2.656 | 155 | -4.53 dB |
| 44,100 | 237 | 118 | 2.676 | 188 | -4.58 dB |
| 48,000 | 257 | 128 | 2.667 | 198 | -4.55 dB |
| 96,000 | 513 | 256 | 2.667 | 326 | -4.54 dB |
| 192,000 | 1025 | 512 | 2.667 | 582 | -4.44 dB |

Total plugin latency includes the FIR, circuit oversampler delay and existing
64-sample transformer compensation. Clean, filtered-reference and bypass delays
remain aligned in all modes, Smart states and Mix settings.

## Regression coverage

C++ checks validate independent 80-phon SPL anchors, the 750 Hz reference and one-octave mapping, the
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
