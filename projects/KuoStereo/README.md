# Kuo Stereo

Kuo Stereo is J-SONIC's stereo widener plug-in. It is a separate JUCE project
alongside YinYang Compressor and SHUN DE-ESS, with the same dark analog panel,
rotary controls, and amber accents. The [editor preview](Previews/kuo-stereo.png)
shows its three sections.

The [feature measurement report](Measurements/Report/REPORT.md) includes
synthetic audio, plots, and quantified results for adaptive Haas delay,
transient protection, Velvet widening, and the frequency display.

- **Smart Haas** searches 30 delay candidates within Center Delay ± Variability,
  clamped to −30–30 ms, including Center Delay itself. Positive values delay
  the right channel; negative values delay the left channel, so the right
  arrives earlier relative to it. At 0 ms, neither channel is delayed by Haas.
  For each candidate, it compares the original mid spectrum with the spectrum
  that would result from that signed delay. A 2048-sample analysis window,
  averaged candidate scores, and a switching margin keep the least different
  target stable.
  The active delay follows a cubic sigmoid toward each target; Max Rate limits
  its steepest movement, reducing pitch modulation and chorus-like motion.
  A zero Variability setting fixes the target at Center Delay. Keep Transient
  uses the same onset detector as Velvet Width to
  temporarily shorten the effective Haas delay. At 0% it has no effect; at
  100% the Haas delay reaches 0 ms during detected transients. A 1.5 ms
  lookahead lets its 1 ms tonal-onset crossfade finish before the attack
  reaches the output; sharp impulses remain sample-aligned. After a 10 ms
  hold, an exponential tap crossfade decays to 1% protection over 65 ms,
  restoring the normal delay without a rapid delay-time sweep. The shared onset
  detector compares fast and slow magnitudes in four frequency bands rather
  than the overall mid-channel level.
- **Velvet Width** adds a sparse, balanced-polarity convolution of the dry mid
  channel to the side channel. Transient Guard uses the shared mid-channel
  onset detector to attenuate only detected onset samples before convolution;
  its depth fades toward zero near the detector threshold to avoid abrupt
  switches. Existing sustained side texture continues while the attack stays
  focused. Guard Attack controls how quickly attenuation reaches full depth,
  up to 10 ms. Values below 0.5 ms use a 0.5 ms de-click ramp on tonal onsets;
  high-crest transients use the selected attack, including immediate 0 ms.
- **Frequency Field** shows three goniometers fed by the processed stereo
  output, split below 250 Hz, from 250 Hz to 2.5 kHz, and above 2.5 kHz.
  Each display scales its points automatically so quieter bands remain visible.

The controls are Center Delay, Variability, Max Rate, Keep Transient, Velvet
Width, Transient Guard, Guard Attack, Output, and Bypass. All are host
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
