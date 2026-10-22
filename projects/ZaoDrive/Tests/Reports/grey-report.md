# Zao Drive Clear contour test report

![Clear On/Off response, prior contour level, phase and group delay](grey-report.png)

**CLEAR**, beside SMART, enables the octave-shifted 80-phon contour. It defaults
On, saves its state with presets and automation, and loads On for older presets
that previously always applied the contour. Off uses a flat feed with the same
latency. The expected-output display follows the switch.

## Attenuation-only target

The previous contour is lowered by **3 dB everywhere**, preserving its shape
and one-octave frequency shift. Its former +3 dB plateaus become **0 dB**;
the 750 Hz reference becomes **−3 dB**, and its former 4 kHz valley remains
shifted to 2 kHz. This does not merely flatten the formerly boosted regions.

The source ISO 226:2003 coefficients define SPL at 80 phons:

```text
Af = 4.47e-3 × (10^(0.025 × 80) − 1.15) + (0.4 × 10^((Tf + Lu)/10 − 9))^af
Lp = (10/af) × log10(Af) − Lu + 94
Clear gain(f) = min(0 dB, Lp(2 × f) − Lp(1.5 kHz) − 3 dB)
```

The source SPL reference is 83.412071 dB. Target gain is −3 dB at
750 Hz and approximately -8.496 dB at 2 kHz. Log-frequency
interpolation and the shifted endpoint holds below 10 Hz and above 6.25 kHz
remain unchanged. The [target CSV](grey-iso80-target.csv) records source SPL,
uncapped shifted/offset gain and the applied target. Independent NumPy ISO
equations agree with the C++ values within 1e-9 dB.

This is an intentionally shifted, level-adjusted filter derived from the ISO
contour; it does not calibrate playback SPL or reproduce an unshifted hearing
contour at the applied frequencies.

## FIR measurements

The symmetric Type-I FIR uses the same tap count, Kaiser beta 1, and approximately
2.67 ms delay. It is normalized to **−3 dB at 750 Hz**, not to unity. When needed,
a blend with a −3 dB pure delay enforces the actual 0 dB response ceiling while
preserving that reference gain and symmetry. Coefficient design happens only
during preparation; processing retains the SIMD implementation.

Maximum measured upward gain is **-0.000099 dB** across 8192 frequency
points per sample rate and must be at most 0 dB. The reference is within
1e-5 dB of −3 dB. Maximum fit error is **1.043 dB** from 10 Hz to
the smaller of Nyquist and 6.25 kHz. Every measured impulse is exactly symmetric;
maximum delay-compensated phase error is **1.761e-13 radians**.

| Sample rate | FIR taps | FIR samples | FIR ms | Total plugin samples | Gain at 1.575 kHz |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 8,000 | 43 | 21 | 2.625 | 91 | -7.62 dB |
| 32,000 | 171 | 85 | 2.656 | 155 | -7.53 dB |
| 44,100 | 237 | 118 | 2.676 | 188 | -7.58 dB |
| 48,000 | 257 | 128 | 2.667 | 198 | -7.55 dB |
| 96,000 | 513 | 256 | 2.667 | 326 | -7.54 dB |
| 192,000 | 1025 | 512 | 2.667 | 582 | -7.44 dB |

## Toggle and integration checks

Clear crossfades over 20 ms between shaped and flat feeds with identical delay.
On reactivation, it warms a complete FIR history before fading in. Off stops
convolution after the fade. The pure delay remains active so host latency,
dry mixing and the circuit-minus-reference residual stay aligned.

C++ tests verify exact Off impulses, On agreement with an independent FIR,
stereo isolation, smooth DC transitions, reactivation/warmup, and convolution
suspension at 8/44.1/48/96/192 kHz. Automated toggles also preserve the exact
end-to-end dry signal and host-reported latency. Existing tests cover direct
convolution equivalence, all four circuits, overload, alias reduction and the
shared transformer. Clear participates in Smart signatures, invalidating stale
spectral decisions. State tests cover Off round trips and legacy-On migration;
native editor tests cover button placement, host automation and preview updates.

The contour shapes only the driver feed. Both its shaped and unshaped feeds
have the FIR delay; the wet residual subtracts the matching circuit-latency-aligned
reference before Tone and Smart, then rejoins the flat clean branch. The output
overdrive can still generate gain and harmonics; the no-boost constraint applies
to the Clear contour itself.

Data: [impulses](grey-impulse.csv), [latencies](grey-latency.csv),
[target](grey-iso80-target.csv). A [PDF](grey-report.pdf) is also available.

Regenerate from the repository root:

```bash
build-zao/ZaoDriveTests --grey-report "$PWD/projects/ZaoDrive/Tests/Reports"
python3 projects/ZaoDrive/Tests/generate_grey_report.py
```
