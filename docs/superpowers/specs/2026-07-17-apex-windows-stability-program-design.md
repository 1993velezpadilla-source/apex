# APEX Windows Stability Program Design

**Date:** 2026-07-17
**Status:** Conversational design approved; written specification awaiting review
**Initial platform:** Windows 10/11 x64
**Implementation strategy:** Staged migration; preserve working APEX systems

## 1. Purpose

APEX must behave like a dependable commercial DAW during recording, editing, beat production, mixing, mastering, saving, reopening, and export. The normal user workflow is:

```text
open APEX
→ choose a device and project
→ record, edit, mix, or master for as long as needed
→ save/export
→ stop when the user is finished
```

There is no user-facing “long-session mode.” Large-file selection, recording queues, media streaming, cache management, recovery, and diagnostic collection are automatic.

The program targets zero known data-loss defects and zero observed deadline misses in declared release-test environments. It does not make the impossible claim that no click, driver failure, hardware failure, or third-party plug-in defect can ever occur on every computer.

## 2. Authority and evidence

The governing authority order is:

1. Official device, operating-system, plug-in-format, and JUCE contracts.
2. `DAW_BRAIN.md` architecture and evidence boundaries.
3. The current APEX repository.
4. Measured builds, tests, traces, and hardware results.

Relevant Brain contracts include:

- Callback work must be bounded and free of allocation, blocking, file/GUI/logging work, and topology mutation (`DAW_BRAIN.md:1276-1354`).
- Reliability claims require percentiles, maxima, xruns, environment manifests, failure injection, and soak tests (`DAW_BRAIN.md:37367-37519`).
- Recording overflow, disk failure, interruption, and long duration must preserve valid media without false success (`DAW_BRAIN.md:42574-42670`).
- Realtime diagnostics must publish fixed-size facts to a non-realtime drain (`DAW_BRAIN.md:45923-46046`).

Evidence terms in this design:

- **Fact:** directly observed repository or official SDK behavior.
- **Synthesis:** an engineering conclusion derived from multiple documented constraints.
- **Inference:** plausible from repository structure or observed behavior but not explicitly proven.
- **Recommendation:** architecture selected for future APEX implementation.
- **Unknown:** behavior requiring measurement or additional code/runtime evidence.
- **Time-sensitive:** version, licensing, policy, support, or product behavior that must be pinned and rechecked.

Section 4 reports repository or pinned-SDK Facts unless a bullet says otherwise. Sections 5–14 are APEX Recommendations derived from those facts and the Brain contracts. Section 15 records Unknowns. Licensing, SDK support, driver behavior, and plug-in compatibility remain Time-sensitive even where the program sets a target.

## 3. Scope and non-goals

### 3.1 Included

- Reproducible source and build baseline.
- Realtime callback hardening.
- Reliable recording with no hardcoded duration limit.
- Streamed playback for long media.
- Project, autosave, and recovery safety relevant to long sessions.
- ASIO and Windows Audio device compatibility.
- Native 64-bit VST3, VST2, and CLAP hosting.
- Bridged 32-bit VST2 hosting in a separate x86 process.
- Plug-in scan isolation, quarantine, state preservation, and staged runtime isolation.
- Automated regression, performance, hardware, failure-injection, and soak gates.
- Beat-making completion after the stability foundation.

### 3.2 Excluded from the initial Windows program

- A broad engine rewrite.
- macOS/Linux support before the Windows release gates pass.
- Audio Unit hosting before the macOS port.
- AAX hosting as a general Windows DAW format.
- Loading arbitrary DLL files that have not been identified as valid plug-ins.
- Directly loading 32-bit code into the 64-bit APEX process.
- Universal compatibility claims without a pinned device/plug-in test matrix.

## 4. Verified repository baseline

### 4.1 Working foundations

- Routing snapshots exist and are read by the engine (`AudioEngine.h:534-571`).
- Device buffers and several scratch arrays are prepared ahead of processing (`AudioEngine.h:281-358`).
- Recording uses JUCE `ThreadedWriter` to move file I/O off the callback (`RecordingDiskWriterCore.h:34-110`).
- JUCE 8.0.12 automatically writes RF64 headers once WAV data exceeds the RIFF size boundary (`juce_WavAudioFormat.cpp:1703-1809`).
- Recording queue rejection counters exist (`RecordingDiskWriterCore.h:17-25,88-110`).
- ASIO guarding and recovery code already exists under `DeviceCore` and `MainComponent`.
- VST3 and legacy VST2 formats are currently registered (`PluginScanFormatsCore.h:8-20`).
- Step sequencer, pattern manager, and drum sampler systems exist and are connected to the audio callback.

### 4.2 Realtime conflicts requiring staged correction

The current callback path still contains work that conflicts with the Brain contract:

- Graph-change handling calls buffer/PDC synchronization from processing (`AudioEngine.h:1474-1485`).
- Node lookup and active-edge containers are rebuilt and mutated in processing (`AudioEngine.h:1487-1509`).
- Per-clip and per-track DSP objects are created lazily (`AudioEngine.h:830-918`).
- Route gain state may be inserted or resized during processing (`AudioEngine.h:1014-1028`).
- Clip playback takes a try-lock and skips the track block when contention occurs (`AudioEngine.h:1619-1640`).
- A thread-local pre-FX buffer can grow on first callback use (`AudioEngine.h:1662-1668`).
- Per-slot wet/dry storage is assigned in the callback (`AudioEngine.h:1706-1710`).
- Transport discontinuity cleanup clears dynamic maps from the callback path (`AudioEngine.h:521-526,1088-1126`).

Map reservation alone is not proof of allocation freedom because node-based map insertion can still allocate.

### 4.3 Recording and media conflicts

- `samplesWritten_` and finalizer request counts use 32-bit integers (`RecordingDiskWriterCore.h:119,138`; `RecordingClipFinalizerCore.h:15-25`).
- FIFO acceptance is counted as written media before actual disk success is known (`RecordingDiskWriterCore.h:88-110`).
- JUCE’s background writer currently ignores the return value from the underlying file writes while draining its FIFO (`juce_AudioFormatWriter.cpp:273-302`).
- APEX’s explicit `flushToDisk()` method is empty (`RecordingDiskWriterCore.h:112-114`).
- Finalization loads the entire completed recording into the audio cache (`RecordingClipFinalizerCore.h:101-107`; `AudioFileManager.h:143-153`).
- A five-hour stereo 48 kHz recording would require roughly 6.9 GB as decoded float audio, so whole-file caching is unsuitable.

### 4.4 Validation gaps

- The primary performance test projects contain placeholder tests (`PerformanceTests1.cpp:8-15`; `PerformanceTests2.cpp:8-15`).
- The true-peak microbenchmark does not exercise the full callback, devices, recording, plug-ins, or long-session behavior.
- No APEX-owned five/twelve/twenty-four-hour hardware soak harness was found.
- The application source currently appears untracked from the repository root, so a clean reversible baseline is required before engine migration.

### 4.5 Beat-making baseline

The existing beat system is useful but partial:

- Pattern, channel, velocity, probability, pitch offset, serialization, and snapshot concepts exist (`StepSequencerModel.h:14-73`).
- The claimed realtime snapshot currently acquires `std::mutex` (`StepSequencerModel.h:56-83`).
- Playback scans steps and pattern repeats from the timeline origin, so work can grow with transport position (`StepSequencerPlaybackCore.cpp:30-123`).
- Callback playback constructs MIDI storage and acquires the snapshot (`MainComponent.cpp:5933-5956`).
- The sampler has a prebuilt voice pool, but model publication and sample-state ownership need an explicit realtime contract.

## 5. Program architecture

### 5.1 Separation of planes

```text
mutable project/UI state
→ control-plane validation and preparation
→ immutable prepared runtime generation
→ block-boundary adoption by audio callback
→ non-realtime retirement and reclamation
```

The callback must not traverse mutable project objects as its authority. Routing, clips, plug-in execution, buffers, PDC, and per-node runtime state are prepared before publication.

### 5.2 Permanent execution invariants

- `RoutingGraph`, or an explicitly validated successor, is the routing source of truth; UI and Bubblegum cable visuals are projections, not realtime authority.
- Dry monitoring and wet monitoring each have one contribution owner; no live-input path may be contributed twice.
- Offline export is isolated from live input and transport-only fades.
- Failed project saves and exports preserve the previous valid artifact.
- Stable IDs, not raw pointers, cross persistence and module boundaries.

### 5.3 Prepared runtime generation

Each proposed generation contains:

- Validated processing order and route fan-in.
- Pre-sized node, sidechain, PDC, MIDI, automation, and scratch storage.
- Prepared clip DSP, gain/pan ramps, and transport-discontinuity state.
- Prepared plug-in instances, bus layouts, latency, and bypass policy.
- Stable IDs for all cross-module relationships.
- Fixed capacities and explicit overflow behavior.

Exact class names will follow existing repository naming during implementation planning. The architectural contract, not a new class name, is authoritative.

### 5.4 Publication and reclamation

- Publish only fully prepared, validated generations.
- Adopt at a defined block boundary.
- Retain the previous valid generation if preparation fails.
- Never destroy the last owner of a retired generation on the audio thread.
- Reclaim retired state on the control plane after audio-thread acknowledgment or an equivalent safe epoch.

### 5.5 Serial oracle before multicore

The prepared serial renderer is the correctness oracle. Parallel or anticipative scheduling is enabled only after differential tests prove routing, events, PDC, and output within the declared tolerance and measurements show improved deadline tails at target buffer sizes.

## 6. Automatic recording and long-media design

### 6.1 User contract

The user presses Record and continues until pressing Stop. There is no duration setting or long-session toggle. Internal five-, twelve-, and twenty-four-hour runs are test durations, not product limits.

Practical termination conditions are external realities such as exhausted storage, device loss, process termination, or hardware failure. APEX must preserve and identify the valid prefix in those cases.

### 6.2 Capture pipeline

```text
device callback
→ fixed-capacity SPSC capture queue
→ dedicated writer
→ uniquely identified in-progress RF64 media
→ verified header/metadata finalization
→ transactional clip publication
```

Requirements:

- 64-bit frame and byte counters throughout.
- No callback file I/O or waiting.
- Actual disk-write status propagated to the recording state machine.
- Explicit states: preparing, active, stopping, finalizing, complete, interrupted, failed.
- Queue overflow marks a discontinuity, safely stops capture, and never claims a complete take.
- Disk full preserves the valid prefix and reports its duration.
- Device loss preserves the valid prefix and stops the take before attempting device recovery.
- Stop/finalize runs outside realtime without arbitrary sleeps.
- Recovery sidecar or equivalent journal associates the in-progress media with stable project/take IDs and last durable frame.

### 6.3 Long-media playback

- Short assets may remain fully memory-cached under an explicit memory budget.
- Long assets use bounded asynchronous read-ahead.
- The realtime reader consumes prepared blocks and applies a declared underrun policy.
- Waveform pyramids are generated separately and may drop visual updates.
- Playback and recording never wait for waveform analysis.
- Cache memory remains bounded as session duration grows.

## 7. Device compatibility design

### 7.1 Initial hardware laboratory

- Apollo Solo USB ASIO: primary professional gate at 48 kHz/64 frames.
- Volt 1 ASIO: compatibility gate at 48 kHz/64 and 128 frames.
- Realtek Windows Audio/WASAPI: consumer fallback gate at 48 kHz/128 frames.
- Additional sample rates and buffers are tested where the driver reports support.

### 7.2 Device lifecycle

- Treat opened/active configuration as authoritative rather than assuming requested settings were granted.
- Validate sample rate, block size, channel masks, and stream generation.
- Prepare replacement engine/device state outside the callback.
- Never reopen a device in the callback.
- Preserve the last known valid configuration.
- Do not silently switch devices during an active recording.
- Surface a clear recoverable state when a driver becomes unavailable or stops delivering callbacks.

The implementation remains capability-based; it must not contain Apollo- or Volt-specific audio-engine behavior.

## 8. Plug-in compatibility design

### 8.1 Format scope

Windows native hosting order:

1. 64-bit VST3.
2. 64-bit legacy VST2 DLL where technically and legally valid.
3. 64-bit CLAP after the stability foundation.
4. LV2 only in a later demand-driven phase.

Audio Units belong to the future macOS port. AAX is not promised as a normal third-party APEX host format.

### 8.2 Safe discovery

- Load only files identified by a registered plug-in format; never arbitrary DLLs.
- Scan one candidate per helper-process job or an equivalently isolated bounded unit.
- Apply timeouts, result validation, crash capture, and quarantine.
- Pin binary identity using path plus stable file/build metadata.
- Never instantiate unknown plug-ins in the realtime callback.

### 8.3 Native runtime behavior

- Preserve opaque plug-in state transactionally.
- Validate bus layouts, sample rate, block size, precision, latency, tail, bypass, and event capabilities.
- Handle dynamic latency through prepared PDC updates.
- Missing/quarantined plug-ins reopen as placeholders with stable slot identity and preserved state.
- Invalid output samples are contained according to a declared policy.
- A compatibility corpus covers instruments, samplers, EQ, dynamics, reverb, pitch correction, modulation, and mastering processors.

### 8.4 Bridged 32-bit VST2

Old x86 VST2 DLLs run in a separate 32-bit helper process, never inside 64-bit APEX.

The bridge provides:

- Shared-memory or equivalent bounded audio transport.
- MIDI/event and automation transport.
- State checkpoint and restore.
- Editor-window mediation.
- Reported bridge latency included in PDC.
- Heartbeat, timeout, crash detection, and restart/bypass policy.
- Resource limits and restart-storm protection.

If the bridge fails, APEX preserves the project and plug-in state, bypasses the affected slot or replaces it with a state-preserving placeholder, and reports the failure. The bridge is implemented only after native 64-bit hosting passes its release gates.

## 9. Testing and diagnostics architecture

### 9.1 Test tiers

**Per change:**

- Debug and Release builds.
- Pure unit and contract tests.
- Focused subsystem regressions.
- Project/save/reopen/export tests.
- Callback allocation and bounded-work checks.

**Scheduled:**

- Golden sessions and differential renders.
- Random graph tests with saved seeds.
- Plug-in validators and compatibility corpus.
- Device matrix.
- Failure injection.
- Thermal and endurance soaks.

### 9.2 Realtime metrics

Collect through fixed-size callback-safe facts:

- Callback p50, p95, p99, p99.9, p99.99, and maximum.
- Deadline slack and misses.
- Backend-reported xruns.
- Recording queue occupancy/rejections.
- Worker timeout/fallback counts.
- Graph/device/plug-in generation IDs.
- Memory, handle, and thread growth outside realtime.

Formatting, files, GUI work, and support bundles are produced by a non-realtime drain.

### 9.3 Permanent bug regressions

Every verified defect receives a permanent regression. The Blade regression covers:

- Audio continuity before and after split.
- Left/right source bounds and sample-rate conversion.
- Repeated splits.
- Undo/redo.
- Save/reload.
- Release and Debug behavior.
- Different source/device sample rates.

### 9.4 Failure matrix

Inject and verify:

- Disk full and permission loss.
- Device disconnect, callback stall, and sample-rate change.
- Plug-in scan crash/hang.
- Runtime plug-in crash/hang where isolation exists.
- Missing plug-in/media.
- Malformed project/media.
- Save/export publication failure.
- Recording queue overflow.
- Forced interruption and recovery.

## 10. Release gates

APEX cannot ship a paid Windows release with:

- A known recording-data-loss regression.
- A known project-corruption regression.
- Failed save replacing the previous valid project.
- Failed export replacing the previous valid export artifact.
- Callback allocation/blocking in a path declared validated.
- Reproducible unexplained xruns/clicks in the release matrix.
- Plug-in scanning that can terminate the main application.
- Unbounded memory growth with session duration.
- A test that passes only in Debug or only in Release without a resolved explanation.

Performance results record build, OS, CPU, power mode, driver, device, sample rate, buffer size, plug-ins, session identity, and exact binary revision.

Internal endurance ladder:

- Accelerated boundary/failure tests on normal development runs.
- Five-hour hardware recording campaign.
- Twelve-hour overnight recording/playback campaign.
- Twenty-four-hour pre-release endurance campaign.

These are evidence gates, not user-visible limits.

## 11. Beat-making feature phase

Beat-making is the first major feature phase after stability because APEX already has a partial vertical slice.

The completion sequence is:

1. Lock-free/bounded pattern snapshot publication.
2. Block-size-independent and seek/loop-correct event scheduling.
3. One shared event model for step grid and piano roll.
4. Stable pattern IDs and arrangement pattern clips.
5. Linked duplicates, independent duplicates, and Make Unique.
6. Velocity, probability, microtiming, swing, groove, ties, ratchets, flam, and repeat.
7. Sampler choke groups, layers, ADSR, tuning, pan, reverse, one-shot/gated modes, slicing, and bounded voice stealing.
8. Timeline placement, undo/redo, save/load, copy/paste, bounce, and export.
9. Scenes/performance launching only after the timeline pattern workflow is stable.

The step grid and piano roll must never become competing truths for the same events.

## 12. Project decomposition and order

The program is implemented as separate reviewed projects:

1. Source-control and reproducible-build baseline.
2. Test, benchmark, telemetry, and regression foundation.
3. Prepared realtime-generation migration.
4. Recording without a hardcoded duration limit and a streamed long-media system.
5. Device and native plug-in compatibility hardening.
6. Bridged 32-bit VST2 helper.
7. Completed beat-making vertical slice.

Only project 1 and the minimum foundation of project 2 enter the first implementation plan.

## 13. Change-control workflow

For every correction:

```text
reproduce or measure
→ identify owning subsystem and violated contract
→ create failing regression
→ apply smallest architecture-correct change
→ build Debug and Release
→ run focused tests
→ run required hardware/session validation
→ record evidence and remaining uncertainty
```

Stop conditions:

- Do not delete working systems for architectural cleanliness.
- Do not enable multicore without the serial oracle.
- Do not change recording publication in a way that risks valid takes.
- Do not make compatibility claims beyond measured evidence.
- Roll back or isolate any phase that regresses playback, recording, saving, routing, PDC, export, or project recovery.

## 14. First implementation project definition

The first implementation project contains only:

- A clean Git baseline for APEX-owned source and project files.
- Exclusions for SDK copies, generated builds, recordings, caches, logs, crash output, and secrets.
- Reproducible Debug and Release build commands.
- A discoverable APEX-owned test runner.
- Blade split regression.
- Callback timing/allocation audit harness.
- Recording writer integrity tests, including actual disk failure propagation expectations.
- Environment and evidence manifest format.

It does not yet rewrite the engine, recording system, plug-in host, or beat-making system.

## 15. Remaining unknowns

- Measured callback tails on Apollo Solo USB, Volt 1, and Realtek.
- Exact plug-in corpus binaries and licensing permissions for automated testing.
- Current device-driver behavior at every advertised sample rate and block size.
- The safest prepared-generation reclamation mechanism for current APEX ownership patterns.
- Memory/read-ahead budget needed for representative long-media sessions.
- Scope and licensing of legacy VST2 distribution/support.
- Runtime sandbox latency policy for native and bridged plug-ins.

These unknowns are resolved by the first evidence and planning phases rather than guessed.
