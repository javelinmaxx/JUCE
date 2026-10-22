# Kuo Velvet validation

Kuo Velvet uses the shared production `VelvetStage` with full-band convolution
input and the three-band input filters disabled. The spectral transient detector
is retained. The compact 444 × 260 interface has Width, Transient, Attack and
Output in one row, with Bypass in the header. Meter rendering, meter DSP,
band-level parameters and the editor repaint timer have been removed.

| Feature | Verification |
| --- | --- |
| Original sound | Compare 49,152 channel samples against the original Kuo Stereo processor at commit `b5297a76ba`, with all band levels at 100%, at 44.1/48/96 kHz; maximum allowed absolute error is 1e-6. |
| Transient and Attack | The reference combines noise, a tone, stereo differences, impulses, full protection and an automated Attack change. |
| Transient protection | A centered impulse must produce at least 100× less convolution side energy with Transient at 100% and Attack at 0 ms than with protection disabled; 5 ms Attack admits more of the onset. |
| Noise widening | Centered mono noise produces side energy, including after settling with Transient at 100%. |
| Mid preservation | At unity Output gain, output mid must match the delayed input within 1e-6. |
| Width | Zero Width produces no added side energy. |
| Bypass and lookahead | Host and parameter bypass retain the original signal level and 1.5 ms delay, independently of Output gain. |
| Host compatibility | Five exposed parameters survive binary state save/restore; 1/127/512-sample host blocks render the same signal within 1e-6. |
| Older presets | Retired band parameters are absent; their saved values are ignored and removed on resave, while Width, Transient, Attack, Output and Bypass recall correctly. An old preset with all bands at zero still widens using the full-band signal. |
| Editor | Native control initialization, UI/host parameter changes, one-sentence tooltips and visible tooltip rendering are checked. |
| Scaling | Every interactive control remains inside the editor and passes pointer hit testing at 50% and 120%. |

The original audio reference was recaptured with unity band levels; its provenance
and settings are documented in [Fixtures/README.md](Fixtures/README.md).
The committed [native preview](../Previews/kuo-velvet-tooltip.png) includes a real
JUCE tooltip. Reproduction commands are in the project [README](../README.md).
macOS AU and native Windows/macOS host validation require those platforms.

## Results — 2026-10-22

Linux Release VST3 and Standalone builds passed without compiler warnings.
All 35 processor/DSP checks passed; the native editor run passed 65 checks
including tooltip rendering and both scaling limits. Maximum sample error
against the original unity-band reference was 1.49468e-8 at 44.1 kHz,
1.49493e-8 at 48 kHz and 1.49504e-8 at 96 kHz, within the 1e-6 tolerance;
the small differences reflect bypassing the unity-band filter recombination.
Kuo Stereo's CTest suite and native editor run (312 checks) passed with its
existing band controls enabled. Ren Transformer's shared editor helper run
also passed all 67 checks. Windows workflows were not changed or run.
