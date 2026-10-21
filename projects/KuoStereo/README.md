# Kuo Stereo

Kuo Stereo is J-SONIC's stereo widener plug-in. It is a separate JUCE project
alongside YinYang Compressor and SHUN DE-ESS, with the same dark analog panel,
rotary controls, and amber accents. The [editor preview](Previews/kuo-stereo.png)
shows its three sections.

The [feature measurement report](Measurements/Report/REPORT.md) includes
synthetic audio, plots, and quantified results for adaptive Haas delay,
transient protection, Velvet widening, and the frequency display.

- **Smart Haas** searches six delays within Center Delay ± Variability,
  clamped to 3–30 ms. For each candidate, it compares the short-time spectrum
  of the original mid channel with the mid channel that would result from
  delaying the right channel. The least different candidate becomes the target.
  Max Rate limits movement toward that target, reducing pitch modulation and
  chorus-like motion. A zero Variability setting fixes the delay at Center
  Delay. Keep Transient uses the same onset detector as Velvet Width to
  temporarily shorten the effective Haas delay. At 0% it has no effect; at
  100% the delay reaches 0 ms during detected transients. After the shared
  32 ms hold, a 50 ms tap crossfade restores the normal delay without a rapid
  delay-time sweep. The shared onset detector compares fast and slow magnitudes in
  four frequency bands rather than the overall mid-channel level.
- **Velvet Width** adds a sparse, balanced-polarity convolution of the dry mid
  channel to the side channel. Transient Guard uses the shared mid-channel
  onset detector to remove the first 3 ms of an attack from the convolution
  input; existing sustained side texture continues while the attack stays focused.
- **Frequency Field** shows three goniometers fed by the processed stereo
  output, split below 250 Hz, from 250 Hz to 2.5 kHz, and above 2.5 kHz.
  Each display scales its points automatically so quieter bands remain visible.

The controls are Center Delay, Variability, Max Rate, Keep Transient, Velvet
Width, Transient Guard, Output, and Bypass. All are host parameters and survive
state recall.
The plug-in accepts stereo input and output, offers VST3 and Standalone builds,
and also builds as an Audio Unit on macOS. It reports no added host latency;
the delay and velvet paths keep up to about 30 ms of internal history.

From the repository root, build and test with:

```bash
cmake -S projects/KuoStereo -B build-kuo-stereo -DKUO_BUILD_TESTS=ON
cmake --build build-kuo-stereo --target KuoStereo_VST3 KuoStereoTests --parallel 4
ctest --test-dir build-kuo-stereo --output-on-failure
```

The Linux and macOS build requirements are the same as the other plug-ins in
the root [README](../../README.md).
