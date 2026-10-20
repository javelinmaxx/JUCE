# SHUN DE-ESS development log

### 2026-10-20 — initial plug-in project
- Created an independent JUCE plug-in target and parameter/state tree for SHUN
  DE-ESS, using the YinYang Compressor's analog-metal panels, rotary controls,
  animated switches, and three-second GR display.
- Implemented a band-pass sibilance detector, wideband and split-band attenuation,
  detector listen, stereo linking, wet/dry mix, output gain, and bypass.
- The VST3 target built successfully. Eighteen signal-path checks passed; the
  editor rendered at 548 × 726 and passed its layout and component checks. The
  committed PNG preview shows the default Wideband mode.
