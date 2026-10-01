# APEX Test Registry

**Last verified:** 2026-07-26

## Test Framework

- **Framework:** JUCE `UnitTest` (via `Tests/APEXTests.jucer`)
- **Runner:** `Tests/Source/Main.cpp`
- **Build:** Visual Studio 2026 solution at `Tests/Builds/VisualStudio2026/`
- **Script:** `Scripts/run_apex_tests.ps1`

## Test Suites

### TEST-001 Smoke Tests
- **File:** `Tests/Source/Smoke/RunnerSmokeTests.cpp`
- **Category:** Smoke
- **Status:** PASS (builds and runs)

### TEST-002 Recording Writer Integrity
- **File:** `Tests/Source/Recording/RecordingWriterIntegrityTests.cpp`
- **Category:** Recording
- **Status:** PASS (builds and runs)

### TEST-003 Recording Identity Regression
- **File:** `Tests/Source/Recording/RecordingIdentityRegressionTests.cpp`
- **Category:** Recording
- **Status:** PASS (builds and runs)

### TEST-004 Live Record Waveform Regression
- **File:** `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`
- **Category:** Recording
- **Status:** PASS (builds and runs)

### TEST-005 Step Sequencer Deterministic
- **File:** `Tests/Source/StepSequencerTests/DeterministicTests.cpp`
- **Category:** StepSequencer
- **Status:** PASS (builds and runs)

### TEST-006 Step Sequencer PatternClip
- **File:** `Tests/Source/StepSequencerTests/PatternClipTests.cpp`
- **Category:** StepSequencer
- **Status:** PASS (builds and runs)

### TEST-007 Step Sequencer PatternModel
- **File:** `Tests/Source/StepSequencerTests/PatternModelTests.cpp`
- **Category:** StepSequencer
- **Status:** PASS (builds and runs)

### TEST-008 Step Sequencer PianoRoll
- **File:** `Tests/Source/StepSequencerTests/PianoRollTests.cpp`
- **Category:** StepSequencer
- **Status:** PASS (builds and runs)

### TEST-009 Step Sequencer Playback
- **File:** `Tests/Source/StepSequencerTests/PlaybackTests.cpp`
- **Category:** StepSequencer
- **Status:** PASS (builds and runs)

### TEST-010 Step Sequencer PolymeterSwing
- **File:** `Tests/Source/StepSequencerTests/PolymeterSwingTests.cpp`
- **Category:** StepSequencer
- **Status:** PASS (builds and runs)

### TEST-011 Step Sequencer StepEvent
- **File:** `Tests/Source/StepSequencerTests/StepEventTests.cpp`
- **Category:** StepSequencer
- **Status:** PASS (builds and runs)

### TEST-012 Arrangement AudioFileManagerShare
- **File:** `Tests/Source/Arrangement/AudioFileManagerShareTests.cpp`
- **Category:** Arrangement
- **Status:** PASS (builds and runs)

### TEST-013 Arrangement BladeSourceRenderContract
- **File:** `Tests/Source/Arrangement/BladeSourceRenderContractTests.cpp`
- **Category:** Arrangement
- **Status:** PASS (builds and runs)

### TEST-014 Arrangement BladeSplitPlanCore
- **File:** `Tests/Source/Arrangement/BladeSplitPlanCoreTests.cpp`
- **Category:** Arrangement
- **Status:** PASS (builds and runs)

### TEST-015 Arrangement BladeUndoPersistence
- **File:** `Tests/Source/Arrangement/BladeUndoPersistenceTests.cpp`
- **Category:** Arrangement
- **Status:** PASS (builds and runs)

### TEST-016 Arrangement ClipSplitCore
- **File:** `Tests/Source/Arrangement/ClipSplitCoreTests.cpp`
- **Category:** Arrangement
- **Status:** PASS (builds and runs)

### TEST-017 Diagnostics CallbackAuditCore
- **File:** `Tests/Source/Diagnostics/CallbackAuditCoreTests.cpp`
- **Category:** Diagnostics
- **Status:** PASS (builds and runs)

### TEST-018 Device Capability Enumeration (device.capabilities.v1)
- **File:** `Tests/Source/Device/DeviceCapabilityTests.cpp`
- **Category:** Device
- **Status:** PASS (builds and runs)
- **Coverage:** professional rate set exact/ascending; buffer ladder incl. 32; device∩professional rate intersection (oddball rates excluded, 0.01 Hz tolerance); device∩ladder buffer intersection; `32 (experimental)` labeling + parse-back

### TEST-019 Device Config Validation (device.config-validation.v1)
- **File:** `Tests/Source/Device/DeviceCapabilityTests.cpp`
- **Category:** Device
- **Status:** PASS (builds and runs)
- **Coverage:** advisory mode on empty lists; supported values pass; unsupported rate/buffer rejected with explicit reason; 32 rejected when unreported, accepted when reported; 48000.5 not aliased to 48000

### TEST-020 Callback Audit Period + Drain (callback-audit-period.v1)
- **File:** `Tests/Source/Diagnostics/CallbackAuditCoreTests.cpp`
- **Category:** Diagnostics
- **Status:** PASS (builds and runs)
- **Coverage:** nominal period ticks for all 7 professional rates x 7 buffer sizes (device-derived); spot-exact deadlines (48k/64=13333 ticks … 192k/32=1666); invalid-input zero; adaptive drain interval bounds (48k/32≈0.341 s, 192k/32≈0.085 s, floor 0.05, cap 5.0); ring capacity exposure

### TEST-021 DrumSampler Rate (drumsampler.rate.v1)
- **File:** `Tests/Source/DrumSampler/DrumSamplerRateTests.cpp`
- **Category:** DrumSampler
- **Status:** PASS (builds and runs)
- **Coverage:** pad pitch preserved 44.1k file at 44.1/48/88.2/96/176.4/192k engine (both resampling directions); duration-in-seconds preserved on 32-sample blocks; ADSR ms-invariance; engine prepare propagation; legacy 44.1k default without prepare

### TEST-022 Clip Timing Defaults (clip.timing-defaults.v1)
- **File:** `Tests/Source/Arrangement/ClipTimingDefaultTests.cpp`
- **Category:** Arrangement
- **Status:** PASS (builds and runs)
- **Coverage:** 8 s default MIDI clip at all professional rates; legacy 352800 without rate; seconds-preserving setSampleRate; tick-extent stability; same-rate no-op; non-positive rate ignored; 96k ticks<->samples round-trip

### TEST-023 LUFS K-Weighting Rates (metering.lufs-kweighting-rates.v1)
- **File:** `Tests/Source/Diagnostics/LufsMeterCoreTests.cpp`
- **Category:** Diagnostics
- **Status:** PASS (builds and runs)
- **Coverage:** 48k published BS.1770 coefficients verbatim (1e-12); prototype derivation reproduces published 48k within 5e-4; oracle match at 6 derived rates (1e-9); -23 dBFS (RMS) 1 kHz sine reads -23.0±0.3 LUFS at all 7 rates x {32,256} blocks

### TEST-024 Project Sample-Rate Reconcile (project.sample-rate-reconcile.v1)
- **File:** `Tests/Source/Project/ProjectSampleRateReconcileTests.cpp`
- **Category:** Project
- **Status:** PASS (builds and runs)
- **Coverage:** same-rate reopen (none); 44.1->48 / 48->96 / 96->48 factors; legacy-no-metadata never reinterpreted; corrupt-zero metadata treated as legacy; unavailable device rate never fabricated; scalePosition rounding; clip timeline seconds-preserving reconcile; >=1-sample length clamp; MIDI seconds+tick extent preserved with clip rate updated; invalid factor/rate no-op

## Missing Test Coverage

| Area | Status |
|---|---|
| AudioEngine process() | NO TEST |
| Transport state machine | NO TEST |
| Routing graph topology | NO TEST |
| Routing snapshot publication | NO TEST |
| PDC / plugin latency | NO TEST |
| Automation system | NO TEST |
| Offline export | NO TEST |
| Project persistence | NO TEST |
| Crash recovery | NO TEST |
| Plugin hosting | NO TEST |
| Bus routing | NO TEST |
| Control room monitoring | NO TEST |
| Live input monitoring | NO TEST |
| MIDI playback | NO TEST |
| Time/Pitch DSP | NO TEST |
| Tape stop | NO TEST |
| Click/metronome | NO TEST |
| Mixer | NO TEST |
