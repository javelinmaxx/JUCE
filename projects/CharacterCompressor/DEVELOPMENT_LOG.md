# Character Compressor — project memory and development log

## Confirmed product requirements

- One audio effect with selectable feed-forward FET, feedback VCA, variable-mu,
  and optical compressor styles.
- Optional transformer emulation at the output stage.
- Target: Intel Mac, macOS Catalina 10.15, Logic Pro X 10.5.7.
- Required host format: 64-bit Intel Audio Units (AU).
- The Linux cloud workspace supports implementation, DSP tests, and VST3 builds.
  AU compilation, `auval`, and Logic compatibility testing require macOS/Xcode.

## Implementation decisions

- Project: `projects/CharacterCompressor`, JUCE 9.0.4, C++17, CMake.
- Four detectors run continuously; smoothed model weights avoid hard switching.
- Stereo-linked gain, mono/stereo layouts, no lookahead, 64-sample reported latency
  after the 2× transformer update (original implementation: zero latency).
- Host automation/state recall through AudioProcessorValueTreeState.
- Controls: model, input, threshold, ratio, attack, release, knee, output, mix,
  detector high-pass, transformer drive/enable, and bypass.
- FET: fast peak feed-forward detection; VCA: peak feedback detection with ratio
  compensation and discrete-loop stability limits.
- Variable-mu: RMS feedback, progressively increasing ratio, level-dependent recovery.
- Optical: RMS feed-forward detection with fast and slow recovery components.
- Transformer: flux-dependent asymmetric soft saturation, bandwidth shaping,
  and DC rejection after the mix/output controls.
- These are original behavioral approximations, not calibrated circuit models
  or exact reproductions of named hardware. Fixed 2× transformer oversampling is
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
Processor checks cover all 13 parameter registrations, mono/stereo layout rules,
state recall, malformed-state rejection, actual processing, host bypass, zero
latency, empty buffers, and optional editor construction/rendering.

Build outputs and detailed test logs are outside the checkout under
`/workspace/juce-env/character-build` and `/workspace/juce-env/character-*.log`.
The inspected UI screenshot is `/workspace/juce-env/character-editor.png`.
Project source, documentation, and tests are versioned together under
`projects/CharacterCompressor`; generated binaries and machine-specific logs
remain outside the repository.

Not yet validated: macOS compilation, AU validation, Logic compatibility,
standalone runtime, listening quality, circuit/hardware accuracy, oversampled alias rejection,
or real-time CPU performance on the target Intel Mac. This is a testable first
implementation, not a production-release claim.

## Remaining product work

- Build/validate AU on macOS; test scan, playback, automation, state recall, and
  mono/stereo instances in Logic Pro X 10.5.7.
- Listen and tune attack/release, model transitions, optical recovery, and
  transformer coloration against the desired references.
- Run the updated tests and 2× aliasing measurements when requested; assess
  residual aliases and CPU usage before release.
- Replace provisional product/manufacturer identifiers before public release.
- Review JUCE licensing before distribution: modules are AGPLv3 or commercial;
  a commercial JUCE licence may be required.
