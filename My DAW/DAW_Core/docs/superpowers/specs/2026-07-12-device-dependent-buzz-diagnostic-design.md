# Device-Dependent Buzz Diagnostic Design

Date: 2026-07-12

## Purpose

Find the first APEX audio boundary that introduces the signal-dependent buzz heard with an Apollo Solo, prove or reject the master soft-clip hypothesis, and use that evidence to implement one device-independent fix.

The user will not inspect code or logs. The diagnostic build must collect and label evidence automatically. The user only runs four short listening tests and reports `buzz` or `clean`.

## Observed Behavior

- Apollo Solo in APEX: imported playback, live monitoring, and recorded-clip playback buzz.
- Apollo Solo in Reaper and FL Studio on the same computer: clean.
- Built-in Legion Go audio in APEX: clean.
- APEX with Apollo selected but transport stopped, tracks unarmed, and monitoring disabled: clean.
- Changing the Apollo buffer size does not noticeably change the buzz character.
- The issue is present across source types, so it is not limited to WAV. MP3, FLAC, AIFF, OGG, WAV, and other supported files are decoded before entering the shared engine/master/device path.

## Verified Source Facts

### Master hypothesis

1. `MasterOversamplingCore::upsample` interpolates toward the next sample within a callback block, but repeats the current sample at the final sample because it has no next-block state.
2. `MasterOversamplingCore::downsample` averages each oversampled group back into one sample.
3. `MasterBusEngine::processBlock` routes all playback and monitored audio through `MasterCeilingCore`.
4. `MasterCeilingCore` defaults to `SoftClip`.
5. `ProjectManager::buildState` writes `ceilingMode = "SoftClip"` rather than the live setting, and no corresponding restore logic exists.

This mechanism can create one signal-dependent discontinuity per callback. It is a hypothesis, not a conclusion, because it is not obviously device-specific.

### Device path

- APEX clears all active device outputs before rendering.
- JUCE's ASIO backend presents compact arrays of active channel pointers.
- `GuardedAsioDevice` forwards the original JUCE callback and does not repack audio buffers.
- The current normal device reopen path propagates the device's actual sample rate and block size into engine preparation.
- APEX currently lacks final output sanitation after master, click, control-room, and drum processing.
- APEX does not collect callback deadline, CPU-load, or xrun evidence.
- APEX opens up to 64 inputs and copies every active input twice per callback.
- Some engine paths can allocate or lock during real-time processing, including the forensic logger.

## Version-Control Baseline

Repository root: `C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core`

Baseline commit: `27ec3fa` (`chore: establish project baseline`)

This commit was created before diagnostic source changes and is the restoration authority. Its message differs from the later requested wording because the baseline had already been created when that amendment arrived; the commit contents and timing satisfy the requirement.

Verified ignore behavior:

- `Builds/VisualStudio2026/x64/` output, including `.exe`, `.obj`, and `.pdb`, is ignored.
- Real source under `Builds/VisualStudio2026/ArrangementEditor/`, including `TimePitchDSPCore.h` and `ArrangementViewCore.cpp`, is tracked.
- The existing `signalsmith-stretch` repository remains a gitlink. Its pre-existing untracked `signalsmith-linear/` directory is ignored for parent-repository status reporting and is not modified.

## Goals

- Produce an unambiguous Normal-versus-Master-Bypass comparison.
- Capture the first stage where clean samples become corrupted.
- Keep instrumentation real-time safe.
- Make every report self-labeling by device and mode.
- Cover playback, live monitoring, recording playback, and every supported decoded file format.
- Preserve existing fixes and unrelated behavior.
- Build and verify a Debug executable for diagnosis.
- After diagnosis, remove scaffolding and prove restoration with Git before implementing the permanent fix.

## Non-Goals

- Do not copy Tracktion Engine or other GPL source into APEX.
- Do not replace APEX's device architecture during diagnosis.
- Do not ship the diagnostic bypass as a production feature.
- Do not apply multiple speculative fixes in one build.
- Do not perform file I/O, dynamic allocation, string formatting, or logging from the audio callback.

## Diagnostic Architecture

### 1. Runtime mode

The diagnostic build has two runtime modes:

- `Normal`: default on every launch; master ceiling behavior is unchanged.
- `Master Bypass`: enabled only by explicit user action; `MasterCeilingCore::process` returns without interpolation, clipping, or downsampling.

The mode is never persisted. Relaunching always returns to `Normal`.

A Debug-only on-screen control displays `MASTER: NORMAL` or a prominent `MASTER BYPASS` indicator. The control is the only way to engage bypass. A non-real-time startup/mode-change record is also written to the diagnostic report. Capture directory names include the mode, so Normal and Bypass runs cannot be confused.

### 2. Device-open snapshot

At `audioDeviceAboutToStart`, collect on the non-processing start path:

- device name and device type;
- requested setup and actual sample rate/block size;
- available, requested, and active input/output channel names and masks;
- engine sample rate/block size after preparation;
- selected diagnostic mode, which must be `Normal` at startup;
- driver xrun count when supported.

The report records values but does not change device setup.

### 3. Real-time telemetry

The audio callback writes fixed-size records into preallocated storage using atomics only. Each record includes:

- callback sequence and delivered sample count;
- input/output callback channel counts;
- elapsed callback time and calculated callback budget;
- current driver xrun count when querying it is callback-safe, otherwise a post-run delta;
- peak and non-finite counts at each signal boundary;
- resample/pitch path and effective ratio where available;
- active Normal/Bypass mode identifier.

No `Logger`, mutex, allocation, or formatted string is permitted in this path. Human-readable reports are generated after capture stops.

### 4. Boundary captures

Preallocate enough stereo float storage for 12 seconds at the actual device sample rate. Capture these boundaries:

1. `TAP-IN`: first two active hardware inputs, or an explicit no-input marker.
2. `TAP-ENGINE`: output from `AudioEngine` before `MasterBusEngine`.
3. `TAP-MASTER`: output after master inserts, fader, ceiling, and dither.
4. `TAP-CONTROL`: output after `ControlRoomEngine`.
5. `TAP-FINAL`: exact first two output channels returned to JUCE after all contributors, including click and drum processing.

Copies are bounded by preallocated capacity. When full, capture stops rather than wrapping or allocating. Transport stop schedules WAV/report writing on a background or message-thread path, never on the audio callback.

Each run gets a new directory named with timestamp, sanitized device name, and mode. Existing captures are never overwritten.

### 5. Master bypass

The bypass is a minimal diagnostic branch in `MasterCeilingCore`; it does not restructure the class. Because processing is in-place, bit-exact bypass means returning before the ceiling stage mutates samples. The rest of the master, control-room, and device path remains active.

The visible indicator and report mode must derive from the same atomic state used by the audio processor.

## Run Matrix

Use the same project, source, approximate playback region, sample rate, and level for all runs:

1. Run A: Apollo Solo, Normal, play about 10 seconds.
2. Run A2: Apollo Solo, Master Bypass, play the same region about 10 seconds.
3. Run B: built-in Legion Go audio, Normal, play the same region about 10 seconds.
4. Run B2: built-in Legion Go audio, Master Bypass, play the same region about 10 seconds.

The user reports only `buzz` or `clean` for each run. After the user says testing is complete, generated files are read directly from the shared workspace.

At least one Apollo run must also exercise live monitoring because live input bypasses file decoding and confirms whether the shared output path is responsible.

## Interpretation

### A buzzes and A2 is clean

The ceiling/oversampling stage is causally implicated. Compare `TAP-ENGINE` and `TAP-MASTER` for block-edge discontinuities and compare A with A2. Check whether Run A input is already corrupted; if so, the ceiling may amplify another defect rather than originate it.

The permanent change then replaces the block-local fake oversampling with a continuous, properly filtered implementation, fixes ceiling-mode persistence, and adds block-partition invariance tests.

### A and A2 both buzz

The oversampler is exonerated as the sole cause. Use the first boundary where A differs from clean Run B:

- corrupted `TAP-IN`: input/device acquisition or unintended input routing;
- clean engine but corrupted master: master inserts, ceiling, dither, or master gain;
- clean master but corrupted control: control-room processing;
- clean control but corrupted final: click/drum/final callback contribution;
- clean `TAP-FINAL` while hardware playback buzzes: JUCE/device handoff, channel mapping, or driver-boundary behavior;
- callback overruns/xrun growth: real-time workload and scheduling.

Only the identified component advances to permanent-fix design.

### B2 regresses

Any new buzz or changed audio in B2 is a diagnostic defect. Stop and correct the bypass/capture implementation before interpreting Apollo results.

## Error Handling

- Capture allocation failure disables WAV capture, preserves audio processing, and records the failure outside the callback.
- Unsupported xrun reporting is labeled `unavailable`, not treated as zero.
- Zero input channels produce no `TAP-IN` WAV and an explicit report field.
- Zero output channels are handled without constructing a fake one-channel buffer.
- Null callback pointers are counted and skipped safely.
- Device changes stop the current capture and begin a newly labeled session after preparation.
- If report writing fails, retain the in-memory summary until the app can display or retry it outside the callback.

## Testing

### Automated diagnostic tests

- Runtime mode defaults to Normal on every construction/launch.
- Bypass is bit-exact for finite stereo input.
- Normal and Bypass report labels match the atomic processing state.
- Capture buffers stop at capacity without allocation or overwrite.
- Output with zero or null channels is handled safely.
- File-format-independent test: decoded sample buffers produce the same downstream diagnostic behavior regardless of source container.

### Permanent-fix tests if ceiling is confirmed

- Process the same signal as one block and as multiple block partitions; outputs match within a defined floating-point tolerance after accounting for fixed filter latency.
- No discontinuity is introduced at callback boundaries.
- Silence remains exact silence.
- Soft clip is finite and bounded for extreme finite input.
- Ceiling mode and value round-trip through project save/load.
- Existing projects with missing ceiling properties use an explicit safe default.

### Build verification

- Rebuild `Debug|x64` with MSBuild after source changes.
- Confirm zero errors and report warnings separately.
- Confirm the resulting executable timestamp and path.
- Do not treat a suspiciously short no-op incremental build as verification; use Rebuild when source compilation is not evident.

## Restoration And Commit Strategy

1. Baseline remains `27ec3fa`.
2. Commit the approved specification separately.
3. Implement diagnostic scaffolding in a dedicated commit after an implementation plan is approved.
4. Run the four tests and preserve generated reports outside tracked source.
5. Remove all bypass and instrumentation scaffolding after evidence is collected.
6. Verify tracked source matches the intended post-baseline state and `git status --short` is clean, excluding only explicitly documented nested-vendor state.
7. Implement the single confirmed permanent fix in a new commit.

No diagnostic behavior is included in a Release build or retained after diagnosis.

## Success Criteria

- The four runs are automatically and unambiguously labeled.
- Evidence identifies the first failing boundary or confirms the master ceiling hypothesis.
- The permanent fix is device-independent and format-independent.
- Imported WAV/MP3/FLAC/AIFF/OGG playback, live monitoring, and recorded playback are clean through Apollo and built-in audio.
- Debug and Release builds complete without errors.
- No new audio-thread allocation, locking, logging, or file I/O is introduced.
- Diagnostic scaffolding is removed before the production fix is considered complete.
