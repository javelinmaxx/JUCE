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

### 2026-10-20 — response preview generator
- Added a dedicated `--response-previews` mode that renders four full-editor PNGs
  with distinct detector frequency/Q and Wideband shelf or Split bell settings.
- The preview mode skips processor checks so it can be used just to refresh the
  graph examples.

### 2026-10-20 — vertical layout and response scale
- Increased editor height by 15 px; shifted Detector, Attenuator, and Output
  sections down 20 px; extended the GR bar and plot by 20 px.
- Lowered the response plot by 10 px and changed its vertical axis to −18..0 dB.

### 2026-10-20 — GR panel alignment
- Extended the GR panel background down 20 px to align its bottom edge with the
  Detector section.

### 2026-10-20 — Smart spectral mode
- Added a three-position Wideband / Split-band / Smart toggle in the Attenuator panel. Smart
  uses an ebony palette while retaining the existing Detector and Attenuator
  frequency/Q controls.
- Added a rolling 1024-point FFT over 2–16 kHz. Detector Frequency weights the
  analysis band; Detector Q sets temporal spectrum smoothing. The difference
  between the current and smoothed spectra identifies narrow peaks, which are
  restored with narrow filter bands after broadband attenuation.
- Smart analysis updates every 256 samples and controls the existing audio
  filterbank, keeping the plug-in's reported latency at zero. Added a focused
  broadband-noise stability check.

### 2026-10-20 — Smart mode selector styling
- Restyled the three-position mode selector to match the Detector's round
  analog flip switch: head-on hex nut, circular flange, and metal lever. Wide,
  Split, and Smart labels remain beside their corresponding positions.
- Refreshed the full-editor preview.

### 2026-10-20 — detector/attenuator mock-signal report
- Tightened the three-position flip-switch lever travel so the handle sits
  closer to the centerline of its round mounting.
- Added `ShunDeEssTests --analysis-report <path>` to exercise Wideband,
  Split-band, and Smart with deterministic broadband noise, a low-frequency
  body tone, and a late high-frequency detail tone.
- The generated report records gain reduction, body/detail retention, and
  output level, plus detector rejection measured through its Listen path.
  The detector favored the selected 7.8 kHz tone by 31.5 dB; Smart retained
  71.9% of its detail tone versus 48.3% in Wideband while reducing the mock
  broadband input.
- The test asserts detector activity, Smart broadband attenuation, and better
  high-frequency detail retention than Wideband.

### 2026-10-20 — vertical Smart mode switch travel
- Changed the Wide / Split / Smart switch handle to stay vertical and travel
  along the centerline between the three labeled positions.
- Refreshed the full-editor preview.

### 2026-10-20 — match Detector switch hardware
- Matched the three-position selector's hex nut, round flange, bat-handle
  length, metal cap, and focus treatment to the Detector switch. Kept Wide,
  Split, and Smart arranged vertically beside their corresponding throws.
- Refreshed the full-editor preview.

### 2026-10-20 — clean vertical-handle preview
- Regenerated the editor preview from a clean build; the handle now visibly
  stays on the centerline through all three throws. Rebuilt VST3, Standalone,
  and test targets from the same clean objects.

### 2026-10-20 — measured Smart attenuation spectrum
- Extended the mock-signal report test to export the final 8192 samples of
  Smart-mode input and output as an FFT CSV, with the 7.8 kHz detail tone active.
- Added a measured spectrum panel plotting input, output, and output-minus-input
  across 2–16 kHz, alongside the existing detector, attenuator, and retention
  plots. The difference trace shows Smart's actual broad attenuation and narrow
  detail restoration from the processed mock signal.

### 2026-10-20 — Smart attenuator-Q and rolling spectrogram report
- Added a measured Attenuator Q sweep using the same Smart mock signal. The
  7.8 kHz detail retention changes from 81.0% at Q 0.5 to 67.6% at Q 4.0,
  while broadband attenuation rises from 9.1% to 21.4%; the report test checks
  that this Q-dependent detail change remains measurable.
- Replaced the fixed-spectrum report panel with a rolling FFT spectrogram of
  the actual Smart processor output for a four-second swept detail tone and two
  transient high-frequency bursts over noise and a 500 Hz body tone.
- Exported the rolling FFT frames to `smart-rolling-spectrogram.csv` for plot
  regeneration and inspection.

### 2026-10-20 — rolling attenuator-shape visualization
- Changed the rolling panel to map measured output-minus-input gain per FFT
  frame, using a zero-centered diverging scale so attenuation cuts and restored
  detail are visually distinct as the swept tone moves through the band.
- Added Q 0.5, 1.2, 2.8, and 4.0 processor runs to compare the moving shape.
- Phase inspection traced the low-Q positive ridge to the RBJ high-shelf slope
  exceeding its unity-safe range. Capped the slope at 1.0 and added a regression
  check that low-Q wideband attenuation does not boost its passband.
- The Smart restoration path also added a phase-rotated raw band-pass signal to
  dry audio, creating a comb-like ridge around detected detail. Replaced it with
  a controlled +6 dB peaking EQ blended by the existing peak-confidence weight.
- Added wideband shelf phase curves to the report for low, nominal, and high Q.

### 2026-10-20 — restore Smart band-pass restoration
- Restored Smart peak restoration to adding the original Q-shaped band-pass
  output to the dry signal. Kept the Q comparison and low-Q shelf passband fix.
- Updated the phase plot and regenerated report to include the 7.8 kHz
  restoration band-pass response.

### 2026-10-20 — pulsed sibilance in rolling mock input
- Added two smooth-envelope, high-pass noise pulses to each rolling Q test run
  to mimic sibilant consonants, while retaining the swept detail tone and
  narrow transient tones for comparison.
- Regenerated the four measured Q heatmaps and report from the new signal.
