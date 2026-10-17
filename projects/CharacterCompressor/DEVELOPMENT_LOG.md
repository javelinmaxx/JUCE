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
- Stereo-linked gain, mono/stereo layouts, no lookahead, zero reported latency.
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
  or exact reproductions of named hardware. Transformer oversampling is not yet
  implemented; strong drive can alias. Reference measurement and listening remain.

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

## Validation status

Validated in the Linux cloud instance on 2026-10-17:

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
standalone runtime, listening quality, circuit/hardware accuracy, alias rejection,
or real-time CPU performance on the target Intel Mac. This is a testable first
implementation, not a production-release claim.

## Remaining product work

- Build/validate AU on macOS; test scan, playback, automation, state recall, and
  mono/stereo instances in Logic Pro X 10.5.7.
- Listen and tune attack/release, model transitions, optical recovery, and
  transformer coloration against the desired references.
- Add oversampling/aliasing measurements for the nonlinear output stage.
- Replace provisional product/manufacturer identifiers before public release.
- Review JUCE licensing before distribution: modules are AGPLv3 or commercial;
  a commercial JUCE licence may be required.
