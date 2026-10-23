# YinYang Compressor — project memory and development log

## Confirmed product requirements

- One audio effect with selectable feed-forward FET, feedback VCA, variable-mu,
  and optical compressor styles.
- Optional transformer emulation at the output stage.
- Initial target: Intel Mac, macOS Catalina 10.15, Logic Pro X 10.5.7.
- Current user test machine: Apple Silicon arm64, reported macOS 26.6.2,
  Python 3.9.6 / DawDreamer 0.8.0. User confirmed VST3 and AU work on 2026-10-18.
- Required host formats: macOS VST3 and Audio Units (AU), architecture matching
  the host. Previous Intel-only build guidance was updated in chat for arm64.
- Transformer-generated distortion must roll off above 500 Hz; preserve clean
  high frequencies. Optional +6 dB sidechain high shelf with 1.5 kHz midpoint;
  no direct output EQ.
- The Linux cloud workspace supports implementation, DSP tests, and VST3 builds.
  AU compilation, `auval`, and Logic compatibility testing require macOS/Xcode.

## Implementation decisions

- Project: `projects/YinyangCompressor`, JUCE 9.0.4, C++17, CMake.
- Four detectors run continuously; smoothed model weights avoid hard switching.
- Stereo-linked gain, mono/stereo layouts, no lookahead, 64-sample reported latency
  after the 2× transformer update (original implementation: zero latency).
- Host automation/state recall through AudioProcessorValueTreeState.
- Controls: model, input, threshold, ratio, attack, release, knee, output, mix,
  detector high-pass, transformer drive/enable, bypass, and High Tilt.
- FET: fast peak feed-forward detection; VCA: peak feedback detection with ratio
  compensation and discrete-loop stability limits.
- Variable-mu: RMS feedback, progressively increasing ratio, level-dependent recovery.
- Optical: RMS feed-forward detection with fast and slow recovery components.
- Transformer: flux-dependent soft saturation, 500 Hz second-order low-pass
  on the nonlinear residual, bandwidth/phase shaping, and DC rejection after
  the mix/output controls. High Tilt acts in the detector branch after the HPF,
  before rectification and stereo linking, for all four compressor models.
- These are original behavioral approximations, not calibrated circuit models
  or exact reproductions of named hardware. Fixed 4× transformer oversampling is
  implemented; residual aliasing, reference measurement, and listening remain.

## Change log

### 2026-10-17 — environment and initial implementation

- Prepared signed Debian development dependencies in `/workspace/juce-env`.
- Built JUCE UnitTestRunner; 551 sections passed across Containers, JSON, XML,
  Files, Audio, and DSP. Repeated setup successfully.
- Built and dynamically loaded JUCE's bundled Linux VST3 example; verified
  exported entry points and module metadata. This was not a Logic/AU test.
- Saved environment installation/startup instructions, then recorded the confirmed
  Intel Catalina/Logic/AU target.
- Added the Character Compressor DSP engine, processor, native editor, CMake
  targets, DSP tests, and processor integration tests.
- Initial bypass tests exposed float smoothing stalling just below its target.
  Added a small endpoint snap so bypass reaches exact dry audio; rebuilt and
  reran affected DSP/processor tests successfully.
- Added macOS Intel AU build instructions, `auval` identifiers, and Logic
  installation steps in `README.md`.
- Created this repository log at the user's request, replacing the proposed
  Notion log. No Notion page was created.

### 2026-10-17 — measured characteristic plots

- Added an offline C++ bridge that measures the production DSP, plus a Python
  plotting script, numerical datasets, PNG/SVG figures, and a three-page PDF.
- Measured static DC and 1 kHz sine curves, attack/recovery, transformer harmonic
  content, small-signal bandwidth, and high-frequency aliasing at 48 kHz.
- At the documented 4:1 settings, FET and VCA attack crossings coincide at 5.75 ms;
  optical recovery to 10% takes approximately 1.146 s. Variable-mu shows transient
  overshoot and gentler steady-state compression.
- Confirmed substantial transformer aliasing at high drive: a 10 kHz tone at
  −6 dBFS peak / 18 dB drive produces an 18 kHz component at about −28.93 dBFS.
  Oversampling remains outstanding; the compressor DSP was not modified.
- See [Measurements/README.md](Measurements/README.md) for conditions, measured
  values, reproduction commands, and links to the plots.

### 2026-10-17 — fixed 2× transformer oversampling

- Implemented polyphase 2× interpolation and filtered decimation with 129-tap
  Blackman-windowed sinc FIRs. Transformer flux, bandwidth, and DC-block state
  run at twice the host rate. Coefficients are prepared outside the audio callback;
  processing uses fixed-size buffers and does not allocate.
- Added a constant 64-sample host latency and matching clean/bypass delay paths.
  Oversampling state stays warm while the transformer is disabled, so automation
  does not reset the filters or change the latency.
- Updated DSP/processor test expectations for compensated bypass, impulse delay,
  and oversampled block-partition invariance. Tests were **not rerun**, following
  the user's instruction; earlier passing results below apply to the 1× revision.
- Preserved the original alias spectrum and metrics in `Measurements/baseline/`.
  Updated the plot generator to compensate transport delay and compare 1×/2×.
  Measurements were **not rerun**; existing figures remain labeled as 1× baseline.
- Build validation: Linux VST3 compile/link and manifest generation passed.
  Build log: `/workspace/juce-env/character-2x-build.log`.
  Runtime behavior, measured alias improvement, and macOS/AU validation remain
  unverified for this revision.

### 2026-10-17 — transformer phase response and square-wave verification

- Replaced the simple transformer bandwidth/DC filters with a causal loaded
  response: an 8 Hz first-order high-pass and 22 kHz second-order low-pass,
  Q 0.8. This approximates magnetizing inductance, leakage inductance, and
  winding capacitance; values are design choices, not hardware measurements.
- Added `Source/TransformerResponse.h` and integrated it after core saturation
  at the 2× processing rate. State resets with the transformer; coefficients
  are prepared outside the audio callback. Host latency remains 64 samples.
- Added `Measurements/verify_transformer_phase.py` and preserved the previous
  2× transformer header for before/after comparison. Measured actual DSP using
  50 Hz and 2 kHz squares at −60 dBFS peak, 1:1 compression, and 0 dB drive.
- After removing only the fixed FIR delay, measured +8.957° at 50 Hz and
  −65.934° at 17.95 kHz. Maximum phase error versus the specified filter
  response was 0.0172° at 48 kHz, 0.0232° at 44.1 kHz, and 0.0027° at 96 kHz.
  The 50 Hz square drooped 33.040% between 1 and 9 ms (prediction: 33.110%).
  Transformer-disabled output remained the exact delayed input.
- Saved plots, PDF, numerical data, and source hashes in
  [Measurements/phase](Measurements/phase/README.md). PDF parsing and page
  rendering succeeded. Earlier 1× measurement artifacts remain unchanged.
- Linux VST3 compile/link and manifest generation passed; build log is
  `/workspace/juce-env/character-phase-build.log`. Only the requested focused
  square-wave checks ran; full DSP/processor suites were not rerun.

### 2026-10-18 — frequency-dependent distortion and optional High Tilt

- User confirmed the preceding macOS VST3 and AU builds work. Recorded their
  current arm64 Mac environment; this report does not validate the new revision.
- Measured the previous transformer at −12 dBFS peak / 6 dB drive: THD stayed
  near 2% from 250 Hz through 5 kHz, so the requested roll-off was absent.
- User clarified that generated harmonics should be filtered above 500 Hz.
  Added a fourth-order Butterworth low-pass to the nonlinear residual at 2×,
  preserving the clean path and the existing transformer phase response.
  Drive range/default remain 0–18 dB / 6 dB. The −3 dB corner is 500 Hz and
  attenuation rises at 24 dB/octave; it is not a hard spectral cutoff.
- Measured fifth-harmonic attenuation of −3.0103 dB at 500 Hz, −24.1087 dB
  at 1 kHz, and −48.2116 dB at 2 kHz. At the baseline input/drive settings,
  revised THD is 2.1422% at 100 Hz, 0.02209% at 500 Hz, and 0.001420% at 1 kHz.
- Initially added High Tilt in the output path (corrected to the sidechain
  below after user clarification): +6 dB high shelf, S = 1,
  1.5 kHz midpoint (+3 dB), default off, approximately 20 ms switching fade.
  Measured +3.0000 dB at 1.5 kHz and +5.9982 dB at 10 kHz.
- Appended `highTilt` as the 14th host parameter with AU version hint 2,
  preserving old parameter IDs/order. Saved state includes the toggle; older
  sessions explicitly reset it to off. Updated the native editor and tooltip.
- Focused measurements passed at 44.1, 48, and 96 kHz. Disabled output stages
  and global bypass remain exact delayed input. Latency remains 64 samples.
  Linux VST3 build and 15 focused processor checks passed, including host
  parameter/state migration, automation, stereo matching, and editor rendering.
- Added plots, CSVs, PDF, source hashes, and reproduction instructions in
  [Measurements/output_stage](Measurements/output_stage/README.md). PDF parsing
  and page rendering succeeded. Preserved the previous transformer header.
- Full DSP/processor suites were not rerun, following the user's earlier
  instruction. Updated their parameter count and harmonic-probe expectations
  for future runs. Focused logs are `/workspace/juce-env/character-output-*.log`.
  New macOS binaries and listening validation remain to be done on the Mac.

### 2026-10-18 — corrected High Tilt routing to the sidechain

- User clarified that High Tilt belongs in the compressor sidechain and must
  not directly EQ the output. Moved the shelf after the detector HPF, before
  rectification/stereo linking, feeding all four existing gain-control models.
  Removed the shelf from the audio output path. Its +6 dB gain, 1.5 kHz midpoint,
  smoothing, parameter ID, and saved-state compatibility are retained.
- Renamed the UI toggle to `SC HIGH TILT +6 dB` and the host parameter display
  name to `Sidechain high tilt`; updated the tooltip, signal-path documentation,
  measurement script, and regenerated report to reflect the corrected routing.
- Focused measurements confirm sample-identical output with tilt on/off at
  ratio 1:1, both with and without the transformer. At input −12 dBFS peak,
  threshold −24 dBFS, ratio 4:1, and knee 0 dB, 10 kHz gain reduction increases
  by 4.448 dB (FET), 4.373 dB (VCA), 3.993 dB (variable-mu), and 4.499 dB
  (optical). The change at 100 Hz stays below 0.003 dB. The enabled detector
  HPF also works with High Tilt. The transformer roll-off is unchanged.
- Linux VST3 build and 23 focused processor checks passed, including state,
  host automation, ratio-1 transparency, stronger HF compression, unchanged
  dry path, stereo matching, latency, and native editor rendering. Full suites
  were not rerun. Logs: `/workspace/juce-env/character-sidechain-*.log`.

### 2026-10-18 — Windows VST3 builds from GitHub Actions

- Added `.github/workflows/character-compressor-windows.yml` so the user can
  obtain a Windows build from their Mac. Uses the `windows-2022` hosted runner,
  Visual Studio 2022, x64, Release configuration, and a static MSVC runtime.
- Builds only `CharacterCompressor_VST3` with tests disabled, checks the
  expected Windows binary exists, and uploads the entire VST3 bundle as the
  `Character-Compressor-Windows-x64-VST3` artifact, retained for 30 days.
- Source/configuration pushes to the compressor branch or `master` trigger a
  build. Manual dispatch is also configured; GitHub's Run workflow button
  becomes available once the workflow is on the default branch.
- Added download, installation, and rerun instructions to the project README.
  YAML/configuration checked locally; Windows build result is recorded by the
  GitHub Actions run. No DSP/processor suites were rerun for this workflow.

### 2026-10-18 — Multi-Compressor repository README

- Replaced the upstream framework landing page with a Multi-Compressor project
  description and feature overview, retaining JUCE attribution/licensing links.
- Added separate Windows VST3, macOS VST3, and macOS AU build/install sections,
  including Windows builds through GitHub Actions for users working on a Mac.
- Updated clone/Actions URLs to `javelinmaxx/Multi-Compressor`. Documented
  Apple Silicon, Intel, and universal options while retaining the actual
  `CharacterCompressor` targets and `Character Compressor` bundle names.
- Checked the documented paths/targets against CMake and the Windows workflow.
  Documentation-only change; no builds or test suites were rerun.

### 2026-10-18 — root build scripts

- Added `build-macos.sh` for VST3, AU, or both, with architecture/deployment
  options and optional installation into the user's plug-in folders.
- Added `build-windows-vst3.ps1` for a Release Windows x64 VST3 with static
  runtime linkage and explicit native-command error handling.
- Both scripts locate the source relative to the script, disable test targets,
  verify the expected bundle exists, and print its path. Root build folders
  are ignored by Git. The Windows Actions workflow now invokes the same script
  and triggers when it changes.
- Added script commands to all three README build sections. Checked Bash
  syntax/help and workflow YAML locally; actual macOS/Windows execution requires
  those platforms. No plug-in builds or full test suites were rerun.

### 2026-10-18 — compressor-style introductions

- Added introductions for FET feed-forward, VCA feedback, variable-mu, and
  optical modes to the root README, including detector/recovery behavior and
  suggested starting uses. Distinguished dynamics modeling from the separate
  transformer coloration stage.
- Checked the descriptions against the current DSP implementation, including
  variable-mu's progressive ratio and optical recovery memory. Documentation
  only; no builds or test suites were rerun.

### 2026-10-19 — DSP implementation comments

- Added inline explanations to the compressor's parameter setup, sidechain,
  stereo linking, model-specific detectors/ratios/timing, knee, wet/dry path,
  bypass delay, and meters. Added comments to the oversampled transformer,
  residual filter, high shelf, and modeled transformer response.
- Comments describe implemented behavior and relevant equations; DSP behavior
  and parameter identifiers were not changed. No builds or tests were run.

### 2026-10-19 — causal transformer hysteresis and revised response corners

- Magnetic hysteresis is path-dependent: the core's magnetic state lags the
  applied field, leaving remanence and distinct rising/falling branches (a B–H
  loop). Background: Jiles and Atherton, “Theory of ferromagnetic hysteresis,”
  *Journal of Magnetism and Magnetic Materials* 61 (1986), 48–60,
  [doi:10.1016/0304-8853(86)90066-1](https://doi.org/10.1016/0304-8853(86)90066-1).
- Added a per-channel causal scalar play operator at the 2× transformer rate:
  `M[n] = clamp(M[n−1], H[n]−r, H[n]+r)`. Its state is bounded by a drive-scaled
  coercive band and changes branch only when the current field leaves that band.
  The remembered magnetization shifts the nonlinear field; normalization
  preserves small-signal gain. Reset clears the memory. This compact behavioral
  approximation is not a full Jiles–Atherton model or hardware calibration.
- Changed the loaded-transformer response corners from 8 Hz / 22 kHz, Q 0.8 to
  20 Hz / 19 kHz, Q 2.0. The bilinear-transform filters remain causal and run
  at the oversampled rate. No parameter IDs or host controls changed.
- The earlier phase and output-stage plots document the prior response settings
  and pre-hysteresis DSP; the focused square-wave phase report has now been
  regenerated for the current implementation.

### 2026-10-19 — upward compression mode

- Added an automatable `upward` toggle, off by default and appended as the 15th
  parameter (AU version hint 3). Older saved sessions restore it as off.
- In upward mode, the compressor gain curve is 0 dB above threshold and adds
  gain below threshold according to the selected ratio. The knee smooths the
  transition; Attack and Release continue to shape gain movement. Negative
  gain-reduction state represents positive makeup gain, and the editor meter
  switches its label to `GAIN` while the mode is selected.
- Feedback models use the input detector while upward mode is engaged; this
  keeps the below-threshold boost curve from being re-triggered by their own
  output gain. No test suite was run for this change.

### 2026-10-19 — upward-mode editor colors and meter scale

- Normal mode now uses blue control accents and an orange gain-reduction meter.
  Turning on Upward swaps the accents: orange controls and a blue upward-gain
  meter. The meter shows labeled 5 dB ticks from 0 to 30 dB, and its caption
  distinguishes gain from gain reduction.
- The Linux Debug VST3 target compiled and linked; `git diff --check` passed.
  Test suites were not run.

### 2026-10-19 — transformer distortion-filter slope

- Reduced the 500 Hz generated-distortion low-pass from two cascaded
  second-order Butterworth sections (fourth order, 24 dB/octave) to one
  second-order Butterworth section (12 dB/octave). The clean signal remains
  outside this filter, and the −3 dB corner remains 500 Hz.
- Updated the measurement script's expected response and metadata. Existing
  output-stage plots and measurements remain historical data from the earlier
  24 dB/octave version; they were not regenerated. No test suites were run.

Current revision checks (2026-10-19): focused square-wave phase measurement
passed at 48, 44.1, and 96 kHz; maximum design-response phase error was 0.024°
at 48 kHz through 17.95 kHz. The Linux Debug VST3 target compiled and linked.
Full DSP/processor test suites and distortion/aliasing sweeps were not run.

### 2026-10-19 — standalone Upward tests and transformer saturation check

- Added a separate `CharacterUpwardCompression` CTest executable. Its 40 checks
  cover the upward curves for all four models, unity above threshold, the
  variable-mu progressive ratio, 1:1, wet/dry and bypass behavior, and all five
  supported sample rates. The standalone test compiled with `g++` and passed;
  CTest registration could not be run because CMake is unavailable here. The
  processor suite retains its host-parameter Upward integration checks.
- Reviewed the transformer result that appeared weak at 1 kHz. The nonlinear
  residual is intentionally low-passed at 500 Hz, which strongly attenuates
  the 3 kHz harmonic from a 1 kHz tone. Repeated the saturation measurement at
  100 Hz, below that corner: at −6 dBFS peak, THD measured 1.374% / 6.169% /
  15.191% / 19.265% for 0 / 6 / 12 / 18 dB drive; the 18 dB third harmonic
  measured −14.70 dBc. This confirms drive-dependent saturation in the
  low-frequency test band.
- Changed the transformer waveform/harmonic plot test tone to 100 Hz and
  refreshed the plot data. Added a DSP regression check requiring the
  normalized 100 Hz third harmonic at 18 dB drive to exceed the 0 dB-drive
  result by at least 4×. The direct DSP executable passed all 115 checks.

## Validation status

Prior 1× revision validated in the Linux cloud instance on 2026-10-17
(the tests below were not rerun after the 2× change):

| Check | Result |
| --- | --- |
| Debug CMake build: DSP tests, processor tests, VST3 | Passed |
| CTest: 2 suites | Both passed, 0 failed |
| DSP suite | 67 checks passed |
| Processor suite | 28 checks passed |
| Processor suite with native editor rendering and PNG output under Xvfb | 33 checks passed (includes the same 28 processor checks) |
| VST3 dynamic loading | Passed; three required entry points found |
| Generated VST3 metadata | Audio module and component controller classes verified |

DSP checks cover steady-state FET/VCA ratios, the variable-mu progressive curve,
optical steady-state behavior, unity ratio, stereo linking, block-size invariance,
dry mix, exact bypass, automation transitions, transformer harmonics/DC rejection,
reset, invalid inputs, and finite output at extreme settings from 8–192 kHz.
Processor checks cover all 20 parameter registrations, mono/stereo layout rules,
state recall, malformed-state rejection, actual processing, host bypass,
64-sample latency, empty buffers, and optional editor construction/rendering.

Build outputs and detailed test logs are outside the checkout under
`/workspace/juce-env/yinyang-build` and `/workspace/juce-env/character-*.log`.
The inspected UI screenshot is `/workspace/juce-env/character-editor.png`.
Project source, documentation, and tests are versioned together under
`projects/YinyangCompressor`; generated binaries and machine-specific logs
remain outside the repository.

The user reports the preceding VST3/AU revision works on their Mac; cloud
validation of macOS compilation, formal AU validation, and Logic compatibility
remains unavailable. Not yet validated for the latest output-stage revision:
macOS builds, Logic compatibility,
standalone runtime, listening quality, circuit/hardware accuracy, oversampled alias rejection,
or real-time CPU performance on the target Intel Mac. This is a testable first
implementation, not a production-release claim.

## Remaining product work

- Rebuild the latest VST3/AU on macOS; test scan, playback, automation, state recall, and
  mono/stereo instances in Logic Pro X 10.5.7.
- Listen and tune attack/release, model transitions, optical recovery, and
  transformer coloration against the desired references.
- Run the updated tests and 4× aliasing measurements when requested; assess
  residual aliases and CPU usage before release.
- Replace provisional product/manufacturer identifiers before public release.
- Review JUCE licensing before distribution: modules are AGPLv3 or commercial;
  a commercial JUCE licence may be required.

### 2026-10-19 — transformer filter Q settings and regenerated measurements

- Set the transformer low-frequency second-order high-pass to 20 Hz, Q 1.0,
  and its 19 kHz low-pass to Q 1.2. Both causal biquads run at the existing
  2× processing rate.
- Regenerated the characteristic, square-wave phase, and output-stage reports.
  The phase model agrees with measured DSP within 0.017° from 50 Hz to 17.95
  kHz; the 50 Hz square-wave droop is 73.70% from 1–9 ms. Phase checks also
  pass at 44.1 and 96 kHz. The output-stage report verifies the updated
  small-signal response against the specified filter transfer functions.
- The transformer saturation plot continues to use the requested 100 Hz test
  tone; at 18 dB drive its third harmonic is approximately −15.00 dBc.
- Updated transformer response documentation and the focused measurement
  results. The complete JUCE/CMake build was unavailable in this environment;
  focused C++ DSP and measurement-script verification is being run directly.

### 2026-10-19 — 4× transformer oversampling and Even harmonics control

- Increased fixed transformer oversampling from 2× to 4×. Expanded the
  Blackman-windowed interpolation/decimation filters from 129 to 257 taps to
  preserve the transition width; their combined group delay keeps host latency
  at 64 samples. Updated phase/output verification to use the 4× processing
  rate and regenerated the characteristic, output-stage, and phase plots.
- Added the automatable `Even harmonics` knob (0–100%, default 0%) to simulate
  transformer DC magnetizing current. The current biases the core transfer
  curve, creating asymmetry and even-order distortion while the zero setting
  preserves the prior response. Older sessions default the new parameter off.
- Added the dedicated 100 Hz transformer test plot, showing the waveform and
  H2/H4/H6/H8/H10 as the control increases. At 18 dB drive and −6 dBFS input,
  H2 rises from the numerical floor at 0% to −17.27 dBc at 100%.
- CTest passed all three suites (DSP, processor, upward compression); the
  editor integration passed 253 checks, including rendering the new knob. The
  Linux Debug VST3 target built successfully. Phase, output-stage, upward, and
  transformer even-harmonic measurements all regenerated successfully.

### 2026-10-19 — dedicated upward-compression plot
- Added a standalone measurement script and C++ bridge that render the current
  production compressor with Upward enabled across input levels for all four
  models. The resulting figure shows input/output transfer and gain below the
  −18 dBFS threshold at a 4:1 ratio; CSV and JSON results accompany the PNG/SVG.
- Verified the expected unity response above threshold and gain increase below
  threshold. At −30 dBFS, measured boost is approximately 9 dB for FET, VCA,
  and Optical, and 7.86 dB for Variable-mu's progressive curve.

### 2026-10-19 — GR-dependent VariTransient control

- Added an automatable 0–400% VariTransient control, defaulting to 0%. Above
  3 dB downward gain reduction, the envelope-time change blends in through a
  smooth 3 dB knee and reaches full effect at 6 dB GR. At 400%, attack reaches
  4× its configured time and release is reduced to one quarter. Upward
  compression is unaffected.
- Appended the host parameter with AU version hint 5, preserved earlier
  automation indices, and added a small editor knob with an explanatory tooltip.
  Older sessions default the new control to 0%.
- Added processor registration, editor, state-migration, attack, and release
  regression checks. All three CTest suites passed (DSP, processor, and upward
  compression); focused output-stage checks passed, and the editor rendered with
  the new knob visible (257 processor/editor checks passed). The Linux Debug
  VST3 target built and linked successfully.

### 2026-10-19 — VariTransient plot and refreshed UI previews

- Added a standalone measurement bridge and Python plotting script for attack
  and release with VariTransient at 0% and 400%, across all four models. The
  plot marks the 3 dB trigger and 6 dB full-effect level; PNG, SVG, CSV, NPZ,
  and JSON metrics are saved under `Measurements/plots/`.
- At 400%, measured FET attack to 90% increased from 5.75 ms to 17.71 ms and
  recovery to 10% decreased from 241.81 ms to 176.85 ms. The other models show
  the same slower-attack/faster-recovery direction; exact measurements are in
  `Measurements/README.md` and `plots/vari_transient.json`.
- Regenerated all eight model/Upward UI preview PNGs so each includes the
  current secondary controls, including Even harmonics and VariTransient.
- Measurement generation completed successfully and the preview renderer
  validated all eight palettes and files.

### 2026-10-19 — compressor editor control layout

- Reordered the primary controls to Threshold, Knee, Ratio, and placed
  VariTransient between Attack and Release. Shortened the GR history panel and
  moved Output, Mix, and Stereo Link below it.
- Grouped transformer enable, Iron Drive, and Even harmonics in a titled
  Transformer Stage panel. Changed all five animated switches to vertical
  tracks and arranged the remaining switches along the lower edge.
- Editor integration passed 260 checks and the VST3 target built. Updated
  previews were rendered for all eight model/Upward palette combinations; the
  preview renderer verified each image's palette colors and saved all files.

### 2026-10-19 — paired compressor controls and centered toggles
- Reordered compressor controls into Threshold/Ratio and Attack/Release pairs,
  with Knee and VariTransient at the ends of their respective rows.
- Centered each switch pivot on its section's knob row and tightened the
  switch hit areas to keep the controls inside their panels. Rebuilt the VST3
  and regenerated all eight previews; no full test suite was run.

### 2026-10-19 — head-on nut and narrower control layout
- Changed the retaining nut to a regular, head-on hexagon and relabeled the
  Mid/Side switch positions M/S and L/R. Hid the redundant function headers on
  the Transformer and Mid/Side switches.
- Narrowed the editor from 580 to 548 pixels, tightened the left control panels,
  and moved the meter and output section inward. Updated preview sizing and its
  meter sample point. Rebuilt the VST3 and rendered all eight previews; no full
  test suite was run.

### 2026-10-19 — clean switch nut and updated control legends
- Removed the protruding faux screw heads that made the toggle retaining nut
  read like a winged plate; made the inset hex face opaque for a clean hex and
  circular-washer stack.
- Renamed Iron Drive to Core Drive and Even Harmonics to DC Bias. Mid/Side
  switch legends now read MS (on) and LR (off).
- Rebuilt the VST3 and regenerated all eight UI previews; visually inspected
  the switch close-up and full panel. No full test suite was run.

### 2026-10-19 — round toggle bezel
- Changed the switch hardware from an oval plate to a circular bezel and washer,
  nested in a compact hex retaining nut to match the supplied reference image.
- Rebuilt the VST3 and regenerated all eight previews with the focused UI
  renderer. No full test suite was run.

### 2026-10-19 — panel toggle with two-sided ON/OFF legend
- Reworked the vertical switches around a metal bat-handle toggle reference:
  layered mounting washer, visible pivot hardware, steel lever, and rounded cap.
- Kept each control's function label above the hardware and placed ON/OFF on
  opposite sides of the lever travel, with state-aware label emphasis.
- GitHub public asset search did not surface a suitable downloadable model, so
  the supplied switch photo guided the vector rendering. Rebuilt the VST3 and
  rendered all eight UI previews; no full test suite was run.

### 2026-10-19 — theme-specific high-contrast knob faces
- Changed knob face gradients for all four simulation themes in both standard
  and Upward modes: cool metallic blue-gray for VCA, pale steel for FET,
  deep green for light Variable Mu, and rose/charcoal for Optical.
- Rebuilt the VST3 and rendered all eight previews with the focused UI renderer;
  visually checked dark, light, and Upward palettes. No full test suite run.

### 2026-10-19 — balanced knobs and compact flip switches
- Restored a restrained beveled knob rim and subtle face shading while leaving
  out glossy highlights and plastic shine.
- Replaced the vertical switch plungers with smaller animated pivoting levers
  on metal escutcheons. Reduced their layout footprints and the Bypass button.
- Rebuilt the VST3 and regenerated all eight model/Upward previews with the
  focused preview renderer; no full test suite was run.

### 2026-10-19 — compact header and studio-lit brushed aluminum
- Removed the IN/OUT dBFS label, moved Input into the Compressor header as a
  miniature control, and changed Bypass into a labeled push button with a
  state-responsive glow.
- Replaced uniform metal scanlines with irregular, fine horizontal brush marks
  and a directional specular sheen. Added themed upper-left spill light and a
  soft chassis vignette to evoke a sidelit studio compressor.
- Flattened rotary controls by removing stacked face bevels, plastic shine,
  cast shadows, and grip grooves; retained a thin position arc and indicator.
  Simplified switch tracks and thumbs to flat fills with fine outlines.
- Rebuilt the VST3 and UI preview target. Regenerated all eight previews; the
  focused renderer verified their dimensions and model-specific GR colors.
  No full test suite was run.

### 2026-10-19 — denser control layout
- Reduced the editor to 580×810 and shortened the Compressor and Sidechain
  sections. Reduced Sidechain and Transformer control sizes and tightened row
  spacing throughout the surface.
- Aligned the bottom of Transformer Stage with the Output section at 792 px.
  Kept VariTransient the same height as Knee and retained the inline, frameless
  Upward control and frameless toggles.
- Rebuilt the VST3 and processor preview target. All eight palette previews
  rendered and passed the focused UI check; no full test suite was run.

### 2026-10-19 — compact rectangular layout and inline switches
- Resized the editor to 580×850, extended the GR display from 400 to 480 px,
  and moved its history plot, readout, and output controls to use the added
  height. Reduced Transformer Stage to a compact 174 px section.
- Matched VariTransient's control height to Knee and added fine horizontal
  brushed-metal striations to the main Compressor panel.
- Removed enclosing switch frames. Upward is now a small inline status button
  beside the Compressor heading; other toggles retain only their switch tracks.
- The VST3 and preview targets built. All eight mode previews regenerated and
  passed the palette-aware UI preview check. No full test suite was run.

### 2026-10-19 — analog surface and illuminated meter styling
- Applied the UI UX Pro Max skeuomorphism guidance to the existing compressor
  surface: layered panel edges, subtle grain, shaded metal knobs, calibrated
  scale ticks, and tactile switch housings retain each simulation palette.
- Added signal-responsive, layered color halos to the GR bar and five-second
  history trace. Kept the trace core and numeric readout legible on dark meter
  glass across light and dark compressor themes.
- Re-rendered all eight model/Upward previews and updated their image checks to
  allow for the intentional gradient and luminous blending.
- The VST3 and processor editor targets built successfully. The eight-theme
  preview check passed. Editor capture reported 266 processor checks passed;
  the full DSP and Upward ctest suites were not run for this visual-only change.

### 2026-10-19 — sidechain controls and compressor UI hierarchy

- Reduced Knee and VariTransient knob sizes. Moved Upward into the Compressor
  section with Threshold/Knee/Ratio, placed Bypass beside the plug-in title,
  and kept VariTransient between Attack and Release.
- Added a Sidechain section containing Detector HPF, SC High Tilt, and Mid/Side.
  SC High Tilt is now a 0–12 dB knob, defaulting to 0 dB. The previous boolean
  host parameter remains for old sessions; enabled states migrate to 6 dB.
- Grouped Transformer, Iron Drive, and Even harmonics in Transformer Stage;
  regenerated all eight previews with the final layout.
- Built the Linux VST3. The full three-suite CTest run passed; editor layout
  checks passed (266), focused output-stage checks passed (30), and output-stage
  measurements regenerated with verification passed.

### 2026-10-19 — Threshold and Ratio dial scales
- Enlarged the Threshold and Ratio dials, added graduated parameter values and
  units around their faces, and removed their numeric readout boxes. Shortened
  the Threshold caption to “THRES” and moved the Attack/Release row down to
  preserve spacing within the Compressor panel.
- Regenerated all eight model/Upward UI previews. The VST3 and focused preview
  renderer built and passed; no full test suite was run.

### 2026-10-19 — clearer dial values and compressor mode button
- Increased the Threshold/Ratio value label size and contrast, and gave each
  marking more room around the enlarged dial face. Moved Knee to the second
  control row so Threshold and Ratio can use the available width.
- Replaced the small Upward indicator with a Bypass-style illuminated button:
  it reads “COMPRESSOR” in normal mode and “UPWARD” when engaged. Repositioned
  the compact Input control to clear the new button.
- Rebuilt the VST3 and preview target; all eight model/Upward previews rendered
  and passed the focused UI check. No full test suite was run.

### 2026-10-19 — Compressor header mode control
- Moved the Upward mode interaction onto the Compressor section heading and
  removed the separate adjacent button. The header reads “COMPRESSOR” in its
  default mode and “UPWARD” when engaged.
- Rebuilt the VST3 and regenerated all eight mode previews; the focused preview
  check passed. The change remains uncommitted as requested.

### 2026-10-19 — main dial caption spacing and scale placement
- Added clearance between the Compressor header button and the THRES/RATIO
  captions, then adjusted the second control row to keep the panel within bounds.
- Kept the scale values on the circular rotary arc and sized their text bounds
  to the label strings, reducing the inward shifts at the dial edges.
- Rebuilt the VST3 and regenerated all eight mode previews; focused preview
  validation passed. Changes remain uncommitted and unpushed.

### 2026-10-19 — balanced lower stages and three-row compressor controls
- Matched Transformer Stage to Sidechain at 128 px, including matching 80×78
  knob bounds. Evenly spaced both sections’ two knobs and toggle around shared
  control centers.
- Expanded the Compressor panel to 358 px and arranged its controls in three
  rows: Threshold/Ratio, Attack/Release, then Knee/VariTransient. Knee and
  VariTransient now match Attack and Release in size.
- Rebuilt the VST3 and regenerated all eight mode previews; the focused UI
  preview check passed. The changes remain uncommitted and unpushed.

### 2026-10-19 — compacted compressor and aligned stage bottoms
- Reduced the Threshold/Ratio knob diameter by about 15%, shortened the
  Compressor panel, and lifted the lower stages. Matched the Sidechain bottom
  to the GR panel bottom and kept Transformer Stage level with Output.
- Rebuilt the VST3 and preview target; all eight mode previews regenerated and
  passed the focused UI preview check. No full test suite was run.

### 2026-10-19 — consistent lower-section spacing
- Reduced the GR panel height and shortened Sidechain, Transformer Stage, and
  Output to 116 px. Lower sections now use consistent 12 px gaps and share
  aligned bottoms; reduced the overall editor height to keep the bottom margin
  consistent as well.
- Lowered the Compressor mode button and matched its text size to section
  headers. Reduced lower-stage control heights to fit the shorter panels.
- Updated preview dimensions and regenerated all eight UI previews. The VST3
  and focused preview validation passed; no full test suite was run.

### 2026-10-19 — compact model selector and value readouts
- Removed the model-specific detector description and moved the model selector
  down into the vacated space. Reduced the Threshold and Ratio controls by a
  further 10% while preserving their parameter ranges and values.
- Aligned the GR panel's top edge with the Compressor panel and verified that
  Output and Transformer Stage share the same top edge. Reduced every slider
  value display font by 15% and moved its readout 3 px closer to the control.
- Rebuilt the VST3 and regenerated all eight mode previews. No full test suite
  was run; focused editor checks passed (262 checks).

### 2026-10-19 — Threshold/Ratio value displays and matched small dials
- Removed the marked scales around Threshold and Ratio and restored their
  numeric value boxes. Shifted Threshold right so its center aligns with
  Attack and Knee.
- Enlarged Attack, Release, Knee, and VariTransient to match the Sidechain
  knobs' drawable dial height while keeping their value boxes clear of adjacent
  rows.
- Rebuilt the VST3 and regenerated all eight previews. Focused editor checks
  passed (262 checks); no full test suite was run.

### 2026-10-19 — compacted top spacing and knob captions
- Removed the gap between the model selector and the Compressor/GR panels by
  moving the complete control layout upward 10 px. Preserved the 12 px section
  gaps and the 18 px bottom margin.
- Shifted knob captions 3 px closer to their controls while leaving panel and
  section margins unchanged.

### 2026-10-19 — model selector spacing and Output heading
- Restored a 10 px gap between the model selector and the GR/Compressor panels,
  while moving the selector up 10 px to tighten its spacing below the plug-in
  title.
- Matched the Output heading to the other section headers and lowered its
  controls to leave clear space below the heading.

### 2026-10-19 — raised the four secondary Compressor controls
- Moved Attack, Release, Knee, and VariTransient upward by 10 px, carrying
  their captions and value readouts with the knobs. Preserved the section size
  and surrounding panel layout.

### 2026-10-19 — main knob captions and Input label alignment
- Lowered the THRES and RATIO captions by 10 px. Moved the INPUT caption closer
  to its knob and aligned their vertical centers.
- Added a focused editor geometry check for the Input label. The focused editor
  checks passed (263 checks); the VST3 build succeeded.

### 2026-10-19 — darker Upward palettes and revised GR accents
- Darkened the Upward surfaces for FET, VCA, Variable-mu, and Optical. The
  preview check now compares each Upward compressor panel and background to
  its normal-mode counterpart and verifies both are darker.
- Replaced FET's gold GR accent with dark purple and made white the VCA's normal
  control accent as well as its GR color.
- Rebuilt the VST3 and regenerated all eight previews. The palette-aware preview
  check passed; no full test suite was run.

### 2026-10-19 — YinYang name, modes, and VCA colors
- Renamed the plug-in product and editor to YinYang Compressor. The upward
  compression mode is now labeled Yin; normal compression is Yang.
- Made the normal VCA panels white with dark text and blue controls. The VCA Yin
  palette keeps its dark panels and uses white knob faces.
- Updated the Windows artifact names and user-facing build instructions.

### 2026-10-19 — shortened editor title
- Changed the editor heading to “YIN/YANG COMP” for a more compact title.

### 2026-10-19 — balanced title and section spacing
- Moved the editor title down 20 px so its visible gap to the model selector matches
  the 10 px gap between the selector and the main sections.

### 2026-10-19 — J-SONIC header and company metadata
- Changed the company display and plug-in metadata to J-SONIC, moved the company
  line down 20 px, and aligned Bypass vertically with the plug-in title.
- Removed the blank top band by moving the company line to the top edge.

### 2026-10-19 — compacted the vertical editor layout
- Restored the J-SONIC line to its prior vertical location, shifted the remaining
  interface elements up 10 px, and shortened the editor by 10 px to preserve the
  lower margin.
- Increased the full-layout upward shift by 20 px and reduced the editor height
  by another 20 px, keeping the bottom margin consistent.
- Changed primary text in the FET Yin palette from warm off-white to pure white.
- Set the slider value-display default to white as well, removing the warm tint
  from FET Yin knob captions and readouts.
- Set the FET Yin secondary label and knob-marking color to white too.

### 2026-10-19 — verified M/S decoding to L/R
- Added a bypassed round-trip DSP check. It caught an inverted side-channel
  polarity in the encoder; corrected the matrix so the inverse decode restores
  the original L/R channel order after the 64-sample latency.

### 2026-10-19 — capped Yin gain and shortened the GR display
- Capped upward gain at 12 dB and restricted the ratio knob/DSP to 1:1–2:1 in
  Yin, restoring 1:1–20:1 in Yang. Reduced the GR display to 0–18 dB and its
  timestamped history to three seconds. The 125 DSP checks and 205 focused
  processor/editor checks passed; all eight previews were regenerated.

### 2026-10-19 — finer ratio steps and Yin/Yang range scaling
- Changed Ratio to 0.01 increments. Switching modes now preserves the knob's
  normalized position across their ranges, so Yin 2:1 maps to Yang 20:1.
- Added focused checks for the parameter increment and the Yin-to-Yang endpoint
  mapping.

### 2026-10-20 — Yin gain meter and time-control allocation
- Flipped the Yin meter to rise from its 0 dB baseline, with a -6 to +12 dB
  scale and a +12 dB ceiling. Yang retains its downward 0–18 dB GR scale.
- Swapped the internal Attack and Release time values in Yin while keeping the
  slider attachments, captions, and host automation assignments unchanged.

### 2026-10-22 — YinyangCompressor project rename

- Renamed the project directory, CMake/plugin targets, processor/editor classes,
  test executables and test option to use Yinyang naming. Updated shared
  transformer includes, build scripts, Windows workflow rename references,
  and documentation. Earlier log entries retain their historical target names.
- Kept the YinYang Compressor product name, bundle/manufacturer/plugin IDs,
  parameter IDs, and `CharacterCompressorState` root for DAW session compatibility.
- Existing generated `build-character-*` archives predate the rename; configure
  a fresh build directory rather than reusing those CMake caches.
- Validation: fresh Linux Debug VST3/Standalone builds; all three renamed
  CTest suites and native editor checks passed. Ren Transformer and Zao Drive
  rebuilt and passed their processor tests. Both macOS build scripts passed
  mocked bundle/target checks; native macOS and Windows builds were not run.
  Generated host product/plugin IDs match the previous compressor build.

### 2026-10-23 — consistent collection naming
- Standardized the visible name as Yinyang Compressor, with YinyangCompressor
  as its directory/build target prefix and YinyangCompressorState as its state root.
- Changed the bundle ID to audio.jsonic.yinyangcompressor; the old identity is
  not retained as an alias. The editor heading spells out YINYANG COMPRESSOR.
- Renamed test binaries to YinyangCompressorTests, YinyangCompressorDSPTests,
  and YinyangCompressorUpwardCompressionTests. Updated scripts and documentation.
- Shun is now Shun De-Esser / ShunDeEsser across folder, targets, classes,
  state root, bundle ID, documentation, and packaging references.
- All processors obtain their host-visible name from CMake's JucePlugin_Name.
- Earlier dated entries above retain their historical names and decisions.
