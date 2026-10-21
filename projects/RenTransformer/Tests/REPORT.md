# Ren Transformer validation

The processor tests exercise the production plugin and the original YinYang
transformer independently. No compressor or sidechain is instantiated by Ren.

| Feature | Verification |
| --- | --- |
| Transformer reuse | Compare every rendered sample with `character::OversampledTransformer` at 44.1, 48 and 96 kHz, with 0/6/18 dB Core Drive, 35% DC Bias and +3 dB Output; maximum allowed absolute error is 1e-6. |
| Output stage | With Core Off or Mix at zero, a stereo impulse returns at sample 64 with the selected Output gain. |
| Bypass | Both host bypass and the Bypass parameter return the original stereo impulse at sample 64 without Output gain. |
| CPU saving | The wrapper's running state becomes false after the Off fade and restarts after warming when enabled. |
| Host compatibility | Six exposed parameters survive binary state save/restore, and the host bypass parameter is registered. |
| Layouts and buffer sizes | Mono and stereo processing are exercised; 1/127/512-sample blocks must render the same signal within 1e-6. |
| Editor | Native control initialization, UI/host parameter changes and one-sentence tooltips are checked in the compact single-panel layout. |
| Scaling | Every interactive control remains inside the editor and passes pointer hit testing at 50% and 120%. |

The committed [native preview](../Previews/ren-transformer-tooltip.png) includes
a real JUCE tooltip. Build and preview reproduction commands are in the project
[README](../README.md). macOS AU and native Windows/macOS host validation require
those platforms.

## Results — 2026-10-21

Linux Release VST3 and Standalone builds passed without compiler warnings.
All 37 processor/DSP checks passed; the native editor run passed 67 checks
including tooltip rendering and both scaling limits. YinYang's three existing
CTest suites also passed. Windows workflows were not changed or run.

The compact editor measures 444 × 260 at 100% scale and places Core Drive,
DC Bias, Mix, and Output in a single row with 96-pixel spacing and symmetric
side margins. The Core switch is removed from the editor; the existing host
parameter and saved-state compatibility remain intact. Kuo Velvet's shared
editor helper validation also passed all 72 checks.
