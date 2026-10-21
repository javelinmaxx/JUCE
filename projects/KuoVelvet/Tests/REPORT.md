# Kuo Velvet validation

Kuo Stereo and Kuo Velvet use the same production `VelvetStage`. Kuo Velvet has
no Haas processor, FFT search, or search thread.

| Feature | Verification |
| --- | --- |
| Original sound | Compare 49,152 channel samples against audio captured from Kuo Stereo at commit `b5297a76ba`, before the extraction, at 44.1/48/96 kHz. |
| Transient and Attack | The reference combines noise, a tone, stereo differences, impulses, full protection, different band levels and an automated Attack change; maximum allowed absolute error is 1e-6. |
| Transient protection | A centered impulse must produce at least 100× less convolution side energy with Transient at 100% and Attack at 0 ms than with protection disabled; 5 ms Attack admits more of the onset. |
| Noise widening | Centered mono noise produces side energy, including after settling with Transient at 100%. |
| Mid preservation | At unity Output gain, output mid must match the delayed input within 1e-6, including at 200% band levels. |
| Band levels | Zeroing all bands removes added side; raising all from 100% to 200% doubles side amplitude (four times energy). |
| Width | Zero Width produces no added side energy. |
| Bypass and lookahead | Host and parameter bypass retain the original signal level and 1.5 ms delay, independently of Output gain. |
| Host compatibility | All eight exposed parameters survive binary state save/restore; 1/127/512-sample host blocks render the same signal within 1e-6. |
| Editor | Native control initialization, UI/host parameter changes, one-sentence tooltips and visible tooltip rendering are checked. |
| Scaling | Every interactive control remains inside the editor and passes pointer hit testing at 50% and 120%. |

The audio fixture's provenance and signal settings are documented in
[Fixtures/README.md](Fixtures/README.md). Kuo Stereo's processor and editor
regressions are also run after extracting the shared stage. The committed
[native preview](../Previews/kuo-velvet-tooltip.png) includes a real JUCE tooltip.
Reproduction commands are in the project [README](../README.md). macOS AU and
native Windows/macOS host validation require those platforms.

## Results — 2026-10-21

Linux Release VST3 and Standalone builds passed without compiler warnings.
All 33 processor/DSP checks passed; the native editor run passed 72 checks
including tooltip rendering and both scaling limits. Maximum sample error
against the pre-refactor fixture was 4.99992e-11 at 44.1 kHz, 4.99981e-11 at
48 kHz and 4.99985e-11 at 96 kHz, consistent with CSV rounding.
Kuo Stereo's existing CTest suite and native editor run (312 checks) also passed.
Windows workflows were not changed or run.
