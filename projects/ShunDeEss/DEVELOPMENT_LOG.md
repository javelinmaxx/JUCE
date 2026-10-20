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

### 2026-10-20 — separate detector and attenuator panels
- Split the controls into independent Detector and Attenuator panels, aligned
  to YinYang Compressor's two stacked lower-left sections, with a wider 24 px
  gap to make the grouping unambiguous.
- Rebalanced the panel heights and control rows to preserve readable headings,
  align Output with Attenuator, and keep the shared bottom edge.

### 2026-10-20 — suite-aligned vertical layout
- Shifted the SHUN panels and meter to YinYang Compressor's 30 px content offset
  and restored the 726 px editor height.
- Matched the lower panel positions and 18 px bottom margin so the interface
  fits without clipping.

### 2026-10-20 — title and frame spacing refinement
- Shifted the interface panels, meter, and controls up another 20 px to tighten
  the title gap and increase the lower frame margin.

### 2026-10-20 — switch alignment
- Vertically centered the Detector and Attenuator flip switches against their
  respective knob rows and refreshed the editor preview.

### 2026-10-20 — remove output mix control
- Removed the wet/dry Mix control and parameter; de-essing now always applies
  the detector-controlled attenuation, with output gain and bypass retained.
- Recentered Output and Stereo Link in the output section.

### 2026-10-20 — output control sizing
- Increased the Output and Stereo Link knobs to match the Detector knob bounds.

### 2026-10-20 — secondary knob sizing
- Standardized all secondary knobs to the Attenuator controls' 76 × 64 bounds.

### 2026-10-20 — control alignment refinement
- Restored DET Q and ATT Q to their prior positions and lowered both switches
  by 3 px to align with the knob rows.

### 2026-10-20 — compressor panel spacing
- Aligned Release horizontally with Range and moved Attack/Release up by 15 px.
- Shortened the Compressor panel by 15 px. With Detector unchanged, the gap
  between panels is now 27 px, up from 12 px (15 px of additional open space).

### 2026-10-20 — additional Compressor panel reduction
- Shortened the Compressor panel by another 35 px with the other panels fixed.
  The gap before Detector is now 62 px; Attack and Release retain their position.

### 2026-10-20 — additional Compressor panel reduction
- Shortened the Compressor panel by another 30 px with the other panels fixed.
  The gap before Detector is now 92 px; Attack and Release retain their position.

### 2026-10-20 — main control spacing
- Moved Threshold, Range, Attack, and Release up by 10 px and shortened the
  Compressor panel by 10 px. The gap before Detector is now 102 px.

### 2026-10-20 — GR display orientation
- Reversed the GR meter and trace vertically so 0 dB is at the top and stronger
  reduction descends.

### 2026-10-20 — restore GR history direction
- Restored the history timeline to left-to-right: −3 s on the left and NOW on
  the right. The vertical GR orientation remains top-down.

### 2026-10-20 — detector/attenuator response display
- Added a live frequency-response plot in the open space between Compressor and
  Detector, showing the detector band-pass and active attenuation shape.
- The logarithmic frequency axis spans 2–16 kHz to cover both frequency controls;
  curves follow the current frequency, Q, mode, and maximum-cut settings.
