# Kuo Stereo

Kuo Stereo is J-SONIC's stereo widener plug-in. It is a separate JUCE project
alongside YinYang Compressor and SHUN DE-ESS, with the same dark analog panel,
rotary controls, and amber accents. The [editor preview](Previews/kuo-stereo.png)
shows its three sections.

- **Smart Haas** searches six delays between 3 ms and Max Delay. For each
  candidate, it compares the short-time spectrum of the original mid channel
  with the mid channel that would result from delaying the right channel.
  The least different candidate becomes the target; the actual delay glides
  toward it to avoid clicks. Haas Mix blends the delayed right channel.
- **Velvet Width** adds a sparse, balanced-polarity convolution of the dry mid
  channel to the side channel. Transient Guard detects mid-channel onsets,
  holds the protection through the convolution tail, and temporarily reduces
  the added width so attacks stay focused.
- **Frequency Field** shows three goniometers fed by the processed stereo
  output, split below 250 Hz, from 250 Hz to 2.5 kHz, and above 2.5 kHz.
  Each display scales its points automatically so quieter bands remain visible.

The controls are Haas Mix, Max Delay, Velvet Width, Transient Guard, Output,
and Bypass. All are host parameters and survive state recall. The plug-in
accepts stereo input and output, offers VST3 and Standalone builds, and also
builds as an Audio Unit on macOS. It reports no added host latency; the delay
and velvet paths keep up to about 30 ms of internal history.

From the repository root, build and test with:

```bash
cmake -S projects/KuoStereo -B build-kuo-stereo -DKUO_BUILD_TESTS=ON
cmake --build build-kuo-stereo --target KuoStereo_VST3 KuoStereoTests --parallel 4
ctest --test-dir build-kuo-stereo --output-on-failure
```

The Linux and macOS build requirements are the same as the other plug-ins in
the root [README](../../README.md).
