# Kuo Stereo

Zero Width suspends the shared Velvet convolution tap reads after its Width
fade reaches silence, while its onset detector and input history stay live for
Haas protection and reactivation. Bypass keeps both Haas search and Velvet hot.

Kuo Stereo is J-SONIC's stereo widener plug-in. It is a separate JUCE project
alongside YinYang Compressor and SHUN DE-ESS, with the same dark analog panel,
rotary controls, and amber accents. The [editor preview](Previews/kuo-stereo.png)
shows its three sections.
The sound field displays are 182 px tall in the default 548 × 721 editor.
The editor resizes from 50% to 120% with a fixed aspect ratio.

The [feature measurement report](Measurements/Report/REPORT.md) includes
synthetic audio, plots, and quantified results for adaptive Haas delay,
transient protection, Velvet widening, and the frequency display.

- **Smart Haas** searches every 0.2 ms choice within Center Delay ± Variability,
  clamped to −30–30 ms (up to 121 choices). Positive values delay
  the right channel; negative values delay the left channel, so the right
  arrives earlier relative to it. At 0 ms, neither channel is delayed by Haas.
  For each candidate, it compares the original mid spectrum with the spectrum
  that would result from that signed delay and rewards moderate side energy.
  This prevents a 0 ms delay from always winning solely because its mid is
  unchanged. A 2048-sample analysis window, 250 ms score averaging, and a
  small switching margin let the target respond to content with some movement.
  Each analysis pass checks four rotating 0.6 ms survey points and the current
  choice. Fine 0.2 ms neighborhoods alternate every other pass, so each is
  checked about every 171 ms at 48 kHz. At most seven candidates are FFT-scored
  per pass; the broad survey covers the full range over several passes. The
  analysis runs every 2048 samples on a low-priority worker using a copied,
  preallocated input snapshot. Its result reaches the audio path on a later
  block. Zero Variability disables the search entirely and displays OFF.
  The active delay moves linearly toward each target at the selected Max Rate.
  A zero Variability setting fixes the target at Center Delay. Dynamic
  uses the same onset detector as Velvet Width to
  temporarily shorten the effective Haas delay. At 0% it has no effect; at
  100% the Haas delay reaches 0 ms during detected transients. A 1.5 ms
  lookahead lets its 1 ms tonal-onset crossfade finish before the attack
  reaches the output; sharp impulses remain sample-aligned. After a 10 ms
  hold, an exponential tap crossfade decays to 1% protection over 65 ms,
  restoring the normal delay without a rapid delay-time sweep. The shared onset
  detector compares fast and slow magnitudes in four frequency bands and
  requires a substantial overall attack or a strong rise in one band.
- **Velvet Width** splits its sparse, balanced-polarity convolution of the dry
  mid into left and right tap signals, then cross-subtracts them at the outputs.
  Its additions cancel in the output mid channel, leaving the Haas-processed
  mid unchanged. Low, Mid, and High sliders beside the meters
  control how much of each 250 Hz / 2.5 kHz input band reaches the convolution.
  All three range from 0% to 200% and default to 100%. Transient uses the shared mid-channel
  onset detector to attenuate only detected onset samples before convolution;
  its depth fades toward zero near the detector threshold to avoid abrupt
  switches. Existing sustained side texture continues while the attack stays
  focused. Attack controls how quickly attenuation reaches full depth,
  up to 10 ms. Values below 0.5 ms use a 0.5 ms de-click ramp on tonal onsets;
  high-crest transients use the selected attack, including immediate 0 ms.
- **Frequency Field** shows three goniometers fed by the processed stereo
  output, split below 250 Hz, from 250 Hz to 2.5 kHz, and above 2.5 kHz.
  Each display scales its points automatically so quieter bands remain visible.

The controls are Center Delay, Variability, Max Rate, Dynamic, Velvet
Width, Transient, Attack, Low/Mid/High Velvet, Output, and Bypass. All are host
parameters and survive state recall.
The plug-in accepts stereo input and output, offers VST3 and Standalone builds,
and also builds as an Audio Unit on macOS. It reports 1.5 ms of lookahead
latency to the host; the delay and velvet paths share that latency.

From the repository root, build and test with:

```bash
cmake -S projects/KuoStereo -B build-kuo-stereo -DKUO_BUILD_TESTS=ON
cmake --build build-kuo-stereo --target KuoStereo_VST3 KuoStereoTests --parallel 4
ctest --test-dir build-kuo-stereo --output-on-failure
```

The Linux and macOS build requirements are the same as the other plug-ins in
the root [README](../../README.md).
