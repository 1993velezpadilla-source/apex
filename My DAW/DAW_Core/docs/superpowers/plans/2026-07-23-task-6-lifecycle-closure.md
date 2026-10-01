# Task 6 Lifecycle Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give recording finalization, export ownership, and device reconfiguration bounded, testable lifecycle behavior without releasing or re-preparing shared audio resources while export owns them.

**Architecture:** Use pure atomic state helpers for recording finalization, durable export completion, and the combined device-generation/export-ownership transition. Route message-thread device mutation through one MainComponent preflight, cooperatively cancel/join before mutation, and serialize unsolicited backend prepare publication with export completion so the latest current-generation prepare is applied exactly once.

**Tech Stack:** C++20, JUCE 8.0.12, JUCE UnitTest, PowerShell build and evidence scripts.

## Global Constraints

- Do not allocate, lock, wait, log, perform GUI/file I/O, rebuild graphs, or perform lifecycle work in the realtime callback.
- Do not modify StepSequencer or AudioEngine callback behavior in this patch.
- Preserve unrelated dirty hunks and perform no commit, staging, or history operation.
- Use seed `0xA9E12026` for focused tests.

---

### Task 1: Pure Recording Lifecycle State

**Files:**
- Create: `Source/RecordingCore/RecordingLifecycleStateCore.h`
- Modify: `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`

**Interfaces:**
- Produces: stop states/actions, lifecycle snapshot, pending restart request/cancel/consume operations.

- [ ] Add no-sleep tests for drain, terminal deadline, restart permission, one-shot consume, and cancellation.
- [ ] Run the Debug focused waveform test and verify the missing helper/API RED.
- [ ] Implement the minimal pure state machine.
- [ ] Re-run the Debug focused test and verify GREEN.

### Task 2: RecordingEngine Integration

**Files:**
- Modify: `Source/RecordingCore/RecordingEngine.h`

**Interfaces:**
- Consumes: `RecordingLifecycleStateCore`.
- Produces: `getLifecycleSnapshot()` and bounded message-thread lifecycle behavior.

- [ ] Guard begin/transport transitions with lifecycle restart policy.
- [ ] Extract one normal post-drain finalization path used by immediate and deferred stop.
- [ ] Add the second deferred-stop deadline and terminal failure transition.
- [ ] Retain writers in terminal failure and clean them after device shutdown.
- [ ] Implement one-shot restart after async finalization and all cancellation cases.
- [ ] Compile through the focused Debug test and application build.

### Task 3: Export Worker Shutdown Ordering

**Files:**
- Modify: `Source/RenderCore/ExportRenderCore.h`
- Modify: `Source/RenderCore/ExportRenderCore.cpp`
- Modify: `Source/MainComponent.cpp`

**Interfaces:**
- Produces: `ExportRenderCore::prepareForShutdown()` for callback suppression plus cooperative cancellation.
- Consumes: `juce::Thread::signalThreadShouldExit` and `waitForThreadToExit`; never `stopThread`.

- [ ] Establish source-level RED evidence that the destructor still calls `stopThread(5000)` and export reset follows `shutdownAudio()`.
- [ ] Add an atomic callback-posting gate initialized by `startExport` and disabled by `prepareForShutdown`.
- [ ] Make `prepareForShutdown` set cancellation and signal thread exit without mutating callback function objects concurrently.
- [ ] Replace `stopThread(5000)` with diagnostic-interval `waitForThreadToExit` calls that continue until actual exit.
- [ ] Detach progress UI, call `prepareForShutdown`, and reset/join `exportRenderCore_` before `shutdownAudio()`.
- [ ] Audit queued `SafePointer` callbacks and `OfflineRenderGuard` cancellation unwinding.
- [ ] Rebuild the Debug application.

### Task 4: Generation-Safe Device Resource Release

**Files:**
- Create: `Source/RenderCore/AudioResourceReleaseStateCore.h`
- Modify: `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`
- Modify: `Source/MainComponent.h`
- Modify: `Source/MainComponent.cpp`

**Interfaces:**
- Produces: `AudioResourceReleaseStateCore::deviceStarted`, `deviceStopped`, and `consumeDeferredRelease` over one packed atomic generation/state word.
- Produces: `MainComponent::cancelAndJoinExportBeforeAudioRelease`, `finishExportOnMessageThread`, and one centralized stopped-device cleanup path.

- [ ] Add no-sleep tests proving active export plus stop defers, same-generation completion releases once, restart invalidates stale completion, and control shutdown safely consumes once.
- [ ] Run the Debug waveform test and verify missing-helper/API RED.
- [ ] Implement the packed generation/state helper with compare/exchange transitions.
- [ ] Track active export and export generation in `MainComponent`; completion must capture `SafePointer<MainComponent>` and the matching generation.
- [ ] Make `audioDeviceStopped` defer without joining while export is active and otherwise claim normal release once.
- [ ] Advance stream generation before `audioDeviceAboutToStart` prepares replacement resources.
- [ ] Centralize progress detach, callback suppression, cooperative cancel/join, queued-completion invalidation, and deferred-release consumption.
- [ ] Call the central helper first in `shutdownAudio`; remove destructor duplication and route the zombie restart direct close through `shutdownAudio`.
- [ ] Run the Debug waveform test and Debug application rebuild for GREEN.

### Task 5: Verification and Evidence

**Files:**
- Modify: `.superpowers/sdd/task-6-report.md`

- [ ] Run waveform/input/lifecycle Debug and Release.
- [ ] Run identity Debug and Release.
- [ ] Run evidence validation.
- [ ] Rebuild unsigned Debug and Release applications.
- [ ] Run scoped `git diff --check` and confirm an empty index.
- [ ] Record exact manifests, hashes, line evidence, runtime limits, and the out-of-scope pre-existing callback violations.

### Task 6: Atomic Export Claim and Deferred Device Prepare

**Files:**
- Modify: `Source/RenderCore/AudioResourceReleaseStateCore.h`
- Modify: `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`
- Modify: `Source/MainComponent.h`
- Modify: `Source/MainComponent.cpp`

**Interfaces:**
- Produces: `AudioResourceReleaseStateCore::claimExport`, parameterless `deviceStopped`, `finishExport`, `isExportOwned`, and `getGeneration`.
- Produces: `MainComponent::applyAudioDevicePreparation` plus a lock-protected latest pending `{ generation, activeInputs, blockSize, sampleRate }` snapshot.

- [x] Add no-sleep tests proving stop-before-claim rejects export, claim-before-stop defers release, restart preserves export ownership, and completion leaves the newest generation running.
- [x] Run `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Name recording.live-waveform-low-buffer.v1 -Seed 0xA9E12026` and verify RED on the missing ownership API.
- [x] Extend the packed state enum to `running`, `runningExportOwned`, `stoppedDeferred`, and `stoppedReleased`; use compare/exchange for every transition.
- [x] Claim ownership before constructing `ExportRenderCore`; reject export when no running generation can be claimed and relinquish ownership if worker startup fails.
- [x] Under one non-realtime prepare lock, let `audioDeviceAboutToStart` either publish the latest generation's prepare parameters or apply them immediately; let export completion release ownership and consume matching pending parameters under the same lock.
- [x] Re-run the focused Debug test and verify GREEN.

### Task 7: Central Device Mutation Preflight

**Files:**
- Modify: `Source/MainComponent.h`
- Modify: `Source/MainComponent.cpp`
- Modify: `Source/DeviceCore/DeviceSessionCore.h`
- Modify: `Source/UICore/AudioDevicePanelUI.h`

**Interfaces:**
- Produces: `MainComponent::prepareForAudioDeviceMutation`, a message-thread-only call to `cancelAndJoinExportBeforeAudioRelease`.
- Changes: `DeviceSessionCore` and delayed advanced-panel setup accept a pre-mutation callback supplied by `MainComponent`.

- [x] Route `setAudioChannels`, startup type selection, ASIO repair/restart, input repair, staged session commit/rollback, and delayed control-panel reapply through the pre-mutation callback before the first manager mutation.
- [x] Keep legacy selector changes safe by requiring the panel-open path to stop the device first; atomic export claim must reject while the selector owns a stopped device.
- [x] Confirm no control/message-thread `setCurrentAudioDeviceType`, `setAudioDeviceSetup`, `initialise`, or `closeAudioDevice` path can overlap an owned export.

### Task 8: Durable Export and Recording Completion Fallbacks

**Files:**
- Create: `Source/RenderCore/AsyncCompletionStateCore.h`
- Modify: `Source/RenderCore/ExportRenderCore.h`
- Modify: `Source/RenderCore/ExportRenderCore.cpp`
- Modify: `Source/RenderCore/ExportProgressWindow.h`
- Modify: `Source/RecordingCore/RecordingLifecycleStateCore.h`
- Modify: `Source/RecordingCore/RecordingEngine.h`
- Modify: `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`

**Interfaces:**
- Produces: `AsyncCompletionStateCore<T>::publish`, `consume`, and `suppress` for single-producer, one-shot durable completion.
- Produces: `ExportRenderCore::getProgress` and `dispatchPendingCompletion`; the progress timer polls both.
- Produces: `RecordingLifecycleStateCore::markFinalizationDispatchFailed`, which cancels restart and restores non-terminal ready state.

- [x] Add no-sleep RED tests for one-shot completion consumption, suppression, failed recording-finalizer dispatch cleanup, and restart cancellation.
- [x] Publish export result durably before `running_` becomes false; post a callback that consumes the shared state and leave it pending when `MessageManager::callAsync` returns false.
- [x] Replace asynchronous progress delivery with atomic progress polling, and have `ExportProgressWindow::timerCallback` dispatch pending completion before handling a stopped worker.
- [x] Check self-deletion `callAsync` results and delete synchronously only when posting fails and the method performs no further member access.
- [x] Check recording-finalizer `callAsync`; on false, preserve finalized WAV paths, log them, clear `finalizing_`, cancel pending restart, and leave lifecycle available for another recording.
- [x] Run the focused Debug test and verify GREEN.

### Task 9: Final Integration Verification

**Files:**
- Modify: `.superpowers/sdd/task-6-report.md`

- [x] Run waveform/lifecycle/export/device tests in Debug and Release with seed `0xA9E12026`.
- [x] Run recording identity tests in Debug and Release with seed `0xA9E12026`.
- [x] Run `Scripts\test_validate_test_evidence.ps1` and require zero failures.
- [x] Rebuild unsigned Debug and Release applications and record both executable SHA-256 hashes.
- [x] Run `git diff --check` over scoped files and `git diff --cached --quiet`; do not stage any file.
- [x] Update the report with exact authority owner, thread roles, data flow, lifecycle transitions, manifests, hashes, and remaining runtime limitations.

### Task 10: Native Export Thread Start Failure

**Files:**
- Create: `Source/RenderCore/ExportThreadStartStateCore.h`
- Modify: `Source/RenderCore/ExportRenderCore.cpp`
- Modify: `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`

**Interfaces:**
- Produces: `ExportThreadStartStateCore::resolve(bool nativeStarted)` returning `{ running, releaseOwnership }`.
- Consumes: JUCE `Thread::startThread()` boolean and MainComponent's existing `startExport() == false` cleanup branch.

- [x] Add RED assertions that native start success keeps running/ownership while failure clears running and requests ownership release.
- [x] Run the focused Debug test and verify missing-helper RED.
- [x] Check `startThread()` in `ExportRenderCore::startExport`; on false clear `running_`, suppress callbacks/completion, clear `completionCallback_`, and return false.
- [x] Re-run the focused Debug test and rebuild the Debug application for GREEN.

### Task 11: Deferred Preparation Callback Gate

**Files:**
- Create: `Source/DeviceCore/PendingAudioPreparationGateCore.h`
- Modify: `Source/MainComponent.h`
- Modify: `Source/MainComponent.cpp`
- Modify: `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`

**Interfaces:**
- Produces: `PendingAudioPreparationGateCore::defer`, `shouldGateCallback`, `isCurrent`, `complete`, and `cancel` over one atomic generation.
- Consumes: replacement stream generation from `AudioResourceReleaseStateCore::deviceStarted`.

- [x] Add RED no-sleep assertions for set/current, latest-generation replacement, stale completion, current completion, and explicit cancellation.
- [x] Run the focused Debug test and verify missing-helper RED.
- [x] Publish the gate when `audioDeviceAboutToStart` defers preparation during export.
- [x] After `DeviceCallbackGuard` enters, clear output and return while the gate is pending, preserving guard destruction and skipping `ApplicationCore` plus drum processing.
- [x] Keep the gate set through current-generation preparation; clear only after preparation returns, or cancel it on stale/released/shutdown paths.
- [x] Re-run the focused Debug test and rebuild the Debug application for GREEN.

### Task 12: Active ASIO Control Panel Preflight

**Files:**
- Modify: `Source/UICore/AudioDevicePanelUI.h`

**Interfaces:**
- Consumes: existing `beforeDeviceMutation_` callback supplied by `MainComponent`.

- [x] Invoke `beforeDeviceMutation_` immediately before active `currentDevice->showControlPanel()`.
- [x] Preserve the delayed setup-reapplication preflight and leave temporary inactive-device control-panel opening ungated.
- [x] Audit exact active and temporary calls in the final report.

### Task 13: Three-Finding Verification

**Files:**
- Modify: `.superpowers/sdd/task-6-report.md`

- [x] Run waveform/lifecycle/export/device Debug and Release with seed `0xA9E12026`.
- [x] Run recording identity Debug and Release with seed `0xA9E12026`.
- [x] Run evidence validation and require zero failed assertions.
- [x] Rebuild unsigned Debug and Release applications and record executable hashes.
- [x] Run scoped `git diff --check`, confirm the index is empty, and preserve unrelated dirty files.
- [x] Record exact ownership, callback-gate, active-panel, RED/GREEN, manifest, hash, and remaining-runtime evidence.

### Task 14: Callback Admission Before Prepared Storage

**Files:**
- Modify: `Source/DeviceCore/PendingAudioPreparationGateCore.h`
- Modify: `Source/AppCore/ApplicationCore.h`
- Modify: `Source/AppCore/ApplicationCore.cpp`
- Modify: `Source/MainComponent.h`
- Modify: `Source/MainComponent.cpp`
- Modify: `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`

**Interfaces:**
- Produces: `PendingAudioPreparationGateCore::callbackAction(bool guardAdmitted)` and `prepareAction(generation, generationCurrent, activeCallbacks)`.
- Produces: `ApplicationCore::waitForRealtimeDeviceCallbacksToDrain(uint32_t timeoutMs)` using the existing `activeDeviceCallbacks_` barrier.
- Changes: `MainComponent::processNextAudioBlock` consumes an already-admitted device callback rather than entering a second guard.

- [x] Add no-sleep RED assertions proving failed admission and a pending gate return before prepared-buffer access, and preparation remains waiting until callback count is zero.
- [x] Run the focused Debug test and verify missing-helper RED.
- [x] Move the single device callback RAII guard to the top of `audioDeviceIOCallbackWithContext`, before every `liveCallbackInputBuffer_` query or access.
- [x] Clear only backend output pointers and return when admission fails or the gate is pending; pass already-admitted work into `processNextAudioBlock` without a second guard.
- [x] Keep the gate set, boundedly drain earlier admitted callbacks, and prepare only for the current generation after drain success; retain pending/gate failure state and log on timeout.
- [x] Re-run the focused Debug test and rebuild the Debug application for GREEN.

### Task 15: Final Race Verification

**Files:**
- Modify: `.superpowers/sdd/task-6-report.md`

- [x] Run lifecycle/export/device Debug and Release with seed `0xA9E12026`.
- [x] Run recording identity Debug and Release with seed `0xA9E12026`.
- [x] Run evidence validation and require zero failures.
- [x] Rebuild unsigned Debug and Release applications and record hashes.
- [x] Run `git diff --check`, confirm the index is empty, and preserve unrelated dirty files.
- [x] Record exact callback ownership, drain timeout, RED/GREEN, manifests, hashes, and runtime limits.
