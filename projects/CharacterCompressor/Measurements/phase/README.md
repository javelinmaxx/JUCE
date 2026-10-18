# Transformer phase verified with square waves

[View the plot](square_wave_phase.png) · [PDF report](transformer_phase_report.pdf)
· [Vector plot](square_wave_phase.svg) · [Numerical results](metrics.json)

The production DSP revision before the subsequent distortion-filter/high-shelf
change was measured with 50 Hz and 2 kHz square waves,
at −60 dBFS peak, compression ratio 1:1, transformer drive 0 dB, and 48 kHz
host sample rate. Low-level input isolates the small-signal response from
saturation. Both before/after traces use 2× oversampling. Only the fixed
64-sample FIR transport delay is removed; transformer phase remains intact.

The loaded-transformer approximation uses an 8 Hz first-order high-pass for
magnetizing-inductance behavior and a 22 kHz second-order low-pass, Q 0.8,
for leakage-inductance/winding-capacitance behavior. Both operate at 2× the
host rate. These design values have not been calibrated to physical hardware.

| Focused check | Result |
| --- | ---: |
| Phase at 50 Hz | +8.957° |
| Phase at 17.95 kHz | −65.934° |
| Maximum phase error versus specified filter response, 50 Hz–17.95 kHz | 0.0172° |
| 50 Hz square droop, 1–9 ms after rising edge | 33.040% |
| Predicted droop from the 8 Hz pole | 33.110% |
| Maximum phase error at 44.1 kHz host rate | 0.0232° |
| Maximum phase error at 96 kHz host rate | 0.0027° |
| Transformer disabled | Exact delayed input |
| Reported transport latency | 64 samples |

Phase comes from output/input FFT ratios at the odd harmonics of the 50 Hz
square, using the final second of a two-second render. The 2 kHz square exposes
edge shaping and settling. Waveform overshoot and ringing include the combined
resampling filters and transformer response. Delay compensation exposes some
FIR pre-ringing before the displayed edge; the actual processing remains causal.

The previous 2× filter implementation is preserved in
`../baseline/transformer_before_phase.h`. Source hashes are recorded in
`metrics.json`; `phase_response.csv` and `square_wave_measurements.npz` preserve
the measured results. The original 1× compressor plots are unchanged.

Reproduce from the repository root with C++17, Python, NumPy and Matplotlib:

```bash
python3 projects/CharacterCompressor/Measurements/verify_transformer_phase.py
```

This command runs only the requested square-wave verification and regenerates
this report. The full DSP/processor suites and aliasing sweep were not rerun.

For measurements of the newer output-stage revision, see
[distortion roll-off and High Tilt](../output_stage/README.md). Rerunning the
phase script measures the current source and replaces the figures in this folder.
