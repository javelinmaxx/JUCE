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

### 2026-10-20 — attenuator controls and wideband shelf
- Combined detector frequency/bandwidth, attenuation frequency/Q, Split/Wideband
  mode, and detector Listen in an Attenuator section; moved Stereo Link into Output.
- Added independent attenuation filters: Wideband uses an adjustable high-shelf,
  and Split-band uses an adjustable bell centered on the attenuation frequency.
- Built the VST3 and processor tests. All 55 signal-path and editor checks pass;
  the preview was refreshed at 548 × 726.
