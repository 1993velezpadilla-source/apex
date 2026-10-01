# Task 6 Lifecycle Closure Design

## Scope

Close only the remaining recording lifecycle and export shutdown findings. Preserve callback-local input validity, waveform truth, writer rejection handling, offline render isolation, transactional export publication, monitoring ownership, and stable clip identity.

The pre-existing allocation and lock behavior in StepSequencer and AudioEngine callback paths is outside this patch. It will be documented as a separate architecture violation requiring explicit authorization.

## Recording Lifecycle

`RecordingLifecycleStateCore` is a pure message-thread state machine with a queryable snapshot. Stop state is `ready`, `pendingDrain`, or `terminalFailure`. A pending stop poll produces exactly one of three actions: keep waiting, finalize after callbacks drain, or enter terminal failure after the second deadline.

Terminal failure clears pending restart and disables recording restart. Active writer objects remain retained while callbacks may reference them. UI waveform take/input state is cleared. Once normal shutdown has stopped the device callback, `RecordingEngine::shutdown` safely stops and destroys retained writers before track, clip, transport, and audio-file subsystems are destroyed.

Immediate callback drain and deferred callback drain share the same normal writer-stop, diagnostic capture, clip-finalization publication, and async completion path.

## Pending Restart

If transport enters record while async clip finalization is active, the lifecycle state records one pending restart. Leaving record, shutdown, or terminal failure cancels it. Async finalization consumes it once and starts a new take only if transport remains in record and shutdown or terminal failure is not active.

## Export Shutdown

`MainComponent::~MainComponent` first detaches the self-owned export progress window and suppresses future progress/completion posting. It then cooperatively requests cancellation, signals thread exit, and destroys `exportRenderCore_`. Destruction waits until the export worker actually exits while `ApplicationCore`, the prepared audio engine, plugin state, and offline-render buffers remain alive. Only after that join may `shutdownAudio()` trigger `audioDeviceStopped()`, `releaseResources()`, and later UI/application teardown.

The export worker is never force-killed. Shutdown waits in bounded diagnostic intervals, but after each expired interval it logs from the control thread and continues waiting. A genuinely hung in-process plugin can therefore make application close wait indefinitely. This is preferable to proceeding into use-after-free or using JUCE `stopThread`, whose timeout path force-terminates the native thread and bypasses `OfflineRenderGuard` destruction.

Already queued UI callbacks retain the existing `SafePointer` checks. Callback suppression prevents new posts after shutdown begins; the progress window's finished/detached state makes any callback that crossed the suppression race harmless. Cooperative stack unwinding guarantees `OfflineRenderGuard::endOfflineRender` runs once cancellation reaches a render safe point.

### Device Stop and Deferred Resource Release

Every control-thread `shutdownAudio()` begins by calling one centralized export detach, cancel, and cooperative-join function. The destructor relies on this path rather than duplicating export teardown. The ASIO zombie restart path also routes its direct callback removal/device close through `shutdownAudio()`.

Backend-delivered `audioDeviceStopped()` never joins the export worker. One packed atomic state owns the audio stream generation and one of three states: running, stopped with release deferred, or stopped with resources already released. If export is active, stop atomically transitions the current generation to deferred and returns. Otherwise it claims release exactly once.

`audioDeviceAboutToStart()` advances the packed generation to running before preparing resources. Export completion captures a `MainComponent` `SafePointer` and the export generation. On the message thread, only the matching active export can clear export-active state and attempt to consume deferred release. Consumption uses one compare/exchange from deferred to released for the same audio generation; if a replacement device has advanced the generation, the stale completion cannot release newly prepared resources.

Control shutdown suppresses queued completion, joins export, invalidates the active export generation, and consumes any same-generation deferred release before device close. A later duplicate stop or stale completion observes the released/new generation state and cannot release twice.

## Testing

Pure no-sleep tests prove pending-to-drained finalization, pending-to-terminal failure, restart permission after safe cleanup, restart denial after terminal failure, and one-shot pending/retry/cancel behavior. The existing focused waveform/input/export and identity regressions remain unchanged except for additional lifecycle sections.

Debug and Release focused tests, evidence validation, and unsigned Debug and Release application rebuilds are required. Pure no-sleep tests cover defer, one-shot same-generation release, restart invalidation, and control-shutdown consumption. No pure helper will claim to prove a real worker join or backend callback ordering. No physical-device, blocked-plugin, or forced hung-callback runtime claim will be made.
