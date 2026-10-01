#pragma once

#include "PluginSandboxAudioTransportCore.h"
#include "PluginSandboxAutomationTransportCore.h"   // Phase E2A (additive)
#include "PluginSandboxDiagnosticsCore.h"
#include <juce_audio_processors_headless/format_types/juce_VST3EnumerationDiagnostic.h>

#include <mutex>

namespace DAW {

class PluginSandboxProcessCore
{
public:
    enum class StartResult
    {
        Started,
        ProtocolMismatch,
        Failed
    };

    /** Phase D: cheap control-plane liveness classification. Never called on
        the realtime thread. */
    enum class Liveness
    {
        NotPrepared,
        Alive,
        Dead
    };

    struct Options
    {
        std::uint32_t requestedProtocol = APEX_PLUGIN_SANDBOX_PROTOCOL_V1;
        DWORD startupTimeoutMs = kPluginSandboxStartupTimeoutMs;
        DWORD pluginCreateTimeoutMs = kPluginSandboxPluginCreateTimeoutMs;
        DWORD editorCreateTimeoutMs = kPluginSandboxEditorCreateTimeoutMs;
        bool refuseShutdownForTest = false;
        bool enableAudioTransport = false;
        bool enableAutomationTransport = false;   // Phase E2A (additive; requires audio)
        PluginSandboxAudioTransportCore::Configuration audioConfiguration;
        PluginSandboxAudioTestDspMode audioDspMode = PluginSandboxAudioTestDspMode::Identity;
        bool createVst3Plugin = false;
        juce::PluginDescription pluginDescription;
        std::uint64_t audioDelaySequence = 0;
        DWORD audioDelayMilliseconds = 0;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
          juce::String workerExecutablePathForTest;
          PluginSandboxAudioTestFaultPoint audioFaultPoint =
              PluginSandboxAudioTestFaultPoint::None;
           std::uint32_t pluginCreateResponseDelayMilliseconds = 0;
           std::uint32_t workerStartupDelayMilliseconds = 0;
           std::uint32_t editorCreateResponseDelayMilliseconds = 0;
           std::uint32_t editorResizeResponseDelayMilliseconds = 0;
           std::uint32_t editorCloseResponseDelayMilliseconds = 0;
           PluginSandboxControlTestFaultPoint pluginCreateControlFaultPoint =
              PluginSandboxControlTestFaultPoint::None;
          bool failStateRestoreForTest = false;
       #endif
    };

    struct Diagnostics
    {
        juce::String executablePath;
        juce::String endpoint;
        juce::String sessionToken;
        juce::String error;
        std::uint32_t parentProcessId = 0;
        std::uint32_t workerProcessId = 0;
        int maxJobActiveProcesses = 0;
        int activeProcessesAfter = -1;
        bool handshake = false;
        bool workerMode = false;
        bool normalGuiCreated = false;
        bool sameExecutable = false;
        bool protocolMismatchRejected = false;
        bool gracefulShutdown = false;
        bool forcedTermination = false;
        bool reaped = false;
        bool endpointReleased = false;
        bool handlesClosed = false;
        bool recursionGuardEnabled = false;
        bool audioTransportReady = false;
        bool audioStopped = false;
        bool sharedMemoryReleased = true;
        bool sharedMemoryRetainedAfterWorkerExit = false;
        bool workerUnavailableDetected = false;
        std::size_t sharedMemoryBytes = 0;
        juce::String sharedMemoryName;
        std::uint64_t audioGeneration = 0;
        bool hostedPluginCreated = false;
        int hostedPluginLatencySamples = 0;
        int hostedPluginInputChannels = 0;
        int hostedPluginOutputChannels = 0;
        bool hostedPluginModuleLoadedInWorker = false;
        int hostedPluginUniqueId = 0;
        int hostedPluginDeprecatedUid = 0;
        juce::String pluginCreateLastPhase;
        juce::String pluginCreateFailurePhase;
        std::uint32_t pluginCreateWorkerProcessId = 0;
        std::uint64_t pluginCreateWorkerGeneration = 0;
    };

    PluginSandboxProcessCore() = default;
    ~PluginSandboxProcessCore()
    {
        if (running_)
            shutdown(500);
        closeOwnedResources();
    }

    PluginSandboxProcessCore(const PluginSandboxProcessCore&) = delete;
    PluginSandboxProcessCore& operator=(const PluginSandboxProcessCore&) = delete;

    StartResult start(const Options& options = {})
    {
       #if JUCE_WINDOWS
        using namespace PluginSandboxWin32;
        if (running_ || process_.isValid())
        {
            diagnostics_.error = "worker process owner was already used";
            return StartResult::Failed;
        }

        options_ = options;
        const auto startupDeadline = GetTickCount64() + options.startupTimeoutMs;
        diagnostics_ = {};
        diagnostics_.parentProcessId = GetCurrentProcessId();
        diagnostics_.executablePath = resolveCurrentExecutablePath();
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        if (options.workerExecutablePathForTest.isNotEmpty())
            diagnostics_.executablePath = options.workerExecutablePathForTest;
       #endif
        if (diagnostics_.executablePath.isEmpty())
        {
            diagnostics_.error = "GetModuleFileNameW could not resolve the current executable";
            return StartResult::Failed;
        }

        parentExecutableIdentity_ = executableIdentity(diagnostics_.executablePath);
        if (! parentExecutableIdentity_.isValid())
        {
            diagnostics_.error = "could not identify the current executable file";
            return StartResult::Failed;
        }

        diagnostics_.sessionToken = juce::Uuid().toString().removeCharacters("-");
        diagnostics_.endpoint = "\\\\.\\pipe\\APEX.PluginSandbox."
                               + diagnostics_.sessionToken;

        if (options.enableAudioTransport)
        {
            if (! audioTransport_.create(diagnostics_.sessionToken,
                                         options.audioConfiguration,
                                         diagnostics_.error))
            {
                closeOwnedResources();
                return StartResult::Failed;
            }
            diagnostics_.sharedMemoryReleased = false;
            diagnostics_.sharedMemoryBytes = audioTransport_.mappedBytes();
            diagnostics_.sharedMemoryName = audioTransport_.mappingName();
            diagnostics_.audioGeneration = audioTransport_.generation();

            // Phase E2A: optional additive automation sidecar, created under
            // the same session/generation. Disabled by default → frozen
            // Phase C behavior unchanged.
            if (options.enableAutomationTransport
                && ! automationTransport_.create(diagnostics_.sessionToken,
                                                 audioTransport_.generation(),
                                                 diagnostics_.error))
            {
                closeOwnedResources();
                return StartResult::Failed;
            }
        }

        pipe_.reset(CreateNamedPipeW(diagnostics_.endpoint.toWideCharPointer(),
                                     PIPE_ACCESS_DUPLEX,
                                     PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT
                                       | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                     1, kMaximumControlMessageBytes,
                                     kMaximumControlMessageBytes,
                                     0, nullptr));
        if (! pipe_.isValid())
        {
            diagnostics_.error = windowsError("CreateNamedPipeW");
            closeOwnedResources();
            return StartResult::Failed;
        }

        job_.reset(CreateJobObjectW(nullptr, nullptr));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
                                                 | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit = 1;
        if (! job_.isValid()
            || ! SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation,
                                         &limits, sizeof(limits)))
        {
            diagnostics_.error = windowsError("configure worker Job Object");
            closeOwnedResources();
            return StartResult::Failed;
        }
        diagnostics_.recursionGuardEnabled = true;

         const auto nextWorkerGeneration = workerGeneration_ + 1;
         juce::StringArray arguments;
        arguments.add(diagnostics_.executablePath);
        arguments.add(PluginSandboxCommandLineCore::kWorkerFlag);
        arguments.add("--ipc=" + diagnostics_.endpoint);
         arguments.add("--protocol=" + juce::String(options.requestedProtocol));
         arguments.add("--editor-create-timeout-ms="
                       + juce::String(static_cast<int>(options.editorCreateTimeoutMs)));
        arguments.add("--session=" + diagnostics_.sessionToken);
        arguments.add("--worker-generation="
                      + juce::String(static_cast<juce::int64>(nextWorkerGeneration)));
        if (options.refuseShutdownForTest)
            arguments.add("--test-refuse-shutdown");
        if (options.enableAudioTransport)
        {
            arguments.add("--audio-shm=" + audioTransport_.mappingName());
            arguments.add("--audio-dsp=" + audioDspModeToken(options.audioDspMode));
            if (options.audioDelaySequence > 0)
                arguments.add("--audio-delay-sequence="
                              + juce::String(static_cast<juce::int64>(options.audioDelaySequence)));
            if (options.audioDelayMilliseconds > 0)
                arguments.add("--audio-delay-ms="
                              + juce::String(static_cast<int>(options.audioDelayMilliseconds)));
           #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
            if (options.audioFaultPoint
                != PluginSandboxAudioTestFaultPoint::None)
            {
                arguments.add("--audio-fault-point="
                    + audioFaultPointToken(options.audioFaultPoint));
            }
           #endif
        }
        #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
         if (options.failStateRestoreForTest)
             arguments.add("--test-fail-state-restore");
         if (options.pluginCreateResponseDelayMilliseconds > 0)
             arguments.add("--test-plugin-create-response-delay-ms="
                           + juce::String(static_cast<juce::int64>(
                               options.pluginCreateResponseDelayMilliseconds)));
          if (options.workerStartupDelayMilliseconds > 0)
              arguments.add("--test-worker-startup-delay-ms="
                            + juce::String(static_cast<juce::int64>(
                                options.workerStartupDelayMilliseconds)));
          if (options.editorCreateResponseDelayMilliseconds > 0)
              arguments.add("--test-editor-create-response-delay-ms="
                            + juce::String(static_cast<juce::int64>(
                                options.editorCreateResponseDelayMilliseconds)));
          if (options.editorResizeResponseDelayMilliseconds > 0)
              arguments.add("--test-editor-resize-response-delay-ms="
                            + juce::String(static_cast<juce::int64>(
                                options.editorResizeResponseDelayMilliseconds)));
          if (options.editorCloseResponseDelayMilliseconds > 0)
              arguments.add("--test-editor-close-response-delay-ms="
                            + juce::String(static_cast<juce::int64>(
                                options.editorCloseResponseDelayMilliseconds)));
         if (options.pluginCreateControlFaultPoint
             == PluginSandboxControlTestFaultPoint::PluginCreateFailBeforeInstance)
              arguments.add("--test-plugin-create-fail-before-instance");
        #endif
        if (juce::JUCEApplicationBase::getCommandLineParameters()
                .contains (juce::VST3EnumerationDiagnostic::kCommandLineFlag))
            arguments.add (juce::VST3EnumerationDiagnostic::kCommandLineFlag);
        if (options.enableAutomationTransport && automationTransport_.isPrepared())
            arguments.add("--automation-shm=" + automationTransport_.mappingName());

        auto commandLine = buildWindowsCommandLine(arguments);
        std::wstring mutableCommandLine(commandLine.toWideCharPointer());
        mutableCommandLine.push_back(L'\0');

        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION processInformation {};
        const auto workingDirectory = juce::File(diagnostics_.executablePath)
                                          .getParentDirectory().getFullPathName();

        if (! CreateProcessW(diagnostics_.executablePath.toWideCharPointer(),
                             mutableCommandLine.data(), nullptr, nullptr, FALSE,
                             CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             nullptr, workingDirectory.toWideCharPointer(),
                             &startup, &processInformation))
        {
            diagnostics_.error = windowsError("CreateProcessW");
            closeOwnedResources();
            return StartResult::Failed;
        }

        process_.reset(processInformation.hProcess);
        PluginSandboxWin32::UniqueHandle primaryThread(processInformation.hThread);
        diagnostics_.workerProcessId = processInformation.dwProcessId;
        launchedWorkerProcessId_ = processInformation.dwProcessId;
         workerGeneration_ = nextWorkerGeneration;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
         ++workerStartCount_;
       #endif

        if (! AssignProcessToJobObject(job_.get(), process_.get()))
        {
            diagnostics_.error = windowsError("AssignProcessToJobObject");
            TerminateProcess(process_.get(), kForcedTerminationExitCode);
            WaitForSingleObject(process_.get(), 3000);
            closeOwnedResources();
            return StartResult::Failed;
        }

        diagnostics_.maxJobActiveProcesses = queryActiveProcessCount();
        if (ResumeThread(primaryThread.get()) == static_cast<DWORD>(-1))
        {
            diagnostics_.error = windowsError("ResumeThread");
            forceTerminateAndReap();
            closeOwnedResources();
            return StartResult::Failed;
        }

        if (! connectPipe(remainingMilliseconds(startupDeadline)))
        {
            forceTerminateAndReap();
            closeOwnedResources();
            return StartResult::Failed;
        }

        const auto hello = makeMessage(MessageType::Hello, options.requestedProtocol,
                                       diagnostics_.sessionToken,
                                       diagnostics_.parentProcessId, 0,
                                       parentExecutableIdentity_);
        if (! writeMessage(pipe_.get(), hello, remainingMilliseconds(startupDeadline),
                           diagnostics_.error))
        {
            forceTerminateAndReap();
            closeOwnedResources();
            return StartResult::Failed;
        }

        ControlMessage response;
        if (! readMessage(pipe_.get(), response, remainingMilliseconds(startupDeadline),
                          diagnostics_.error)
            || ! validateMessageEnvelope(response, diagnostics_.sessionToken,
                                          diagnostics_.error))
        {
            forceTerminateAndReap();
            closeOwnedResources();
            return StartResult::Failed;
        }

        const ExecutableIdentity childIdentity { response.executableVolumeSerial,
                                                 response.executableFileIndex };
        diagnostics_.sameExecutable = childIdentity == parentExecutableIdentity_;
        diagnostics_.workerMode = (response.flags & MessageFlags::WorkerMode) != 0;
        diagnostics_.normalGuiCreated = (response.flags & MessageFlags::NormalGuiCreated) != 0;
        diagnostics_.audioTransportReady =
            (response.flags & MessageFlags::AudioTransportReady) != 0;

        ULONG namedPipeClientProcessId = 0;
        if (! GetNamedPipeClientProcessId(pipe_.get(), &namedPipeClientProcessId)
            || namedPipeClientProcessId != launchedWorkerProcessId_
            || response.processId != launchedWorkerProcessId_)
        {
            diagnostics_.error = "worker PID does not match the process attached to the pipe";
            forceTerminateAndReap();
            closeOwnedResources();
            return StartResult::Failed;
        }

        if (response.type == static_cast<std::uint16_t>(MessageType::HelloReject))
        {
            if (response.protocolVersion != APEX_PLUGIN_SANDBOX_PROTOCOL_V1
                || ! diagnostics_.sameExecutable || ! diagnostics_.workerMode
                || diagnostics_.normalGuiCreated)
            {
                diagnostics_.error = "invalid protocol-rejection identity";
                forceTerminateAndReap();
                closeOwnedResources();
                return StartResult::Failed;
            }
            diagnostics_.protocolMismatchRejected = true;
            diagnostics_.error = "worker rejected incompatible protocol "
                               + juce::String(options.requestedProtocol)
                               + "; worker supports " + juce::String(response.protocolVersion);
            waitForExitAndFinalize(remainingMilliseconds(startupDeadline));
            return StartResult::ProtocolMismatch;
        }

        if (response.type != static_cast<std::uint16_t>(MessageType::HelloAck)
            || response.protocolVersion != APEX_PLUGIN_SANDBOX_PROTOCOL_V1
            || ! diagnostics_.sameExecutable
            || ! diagnostics_.workerMode
            || diagnostics_.normalGuiCreated
            || (options.enableAudioTransport && ! diagnostics_.audioTransportReady)
            || (! options.enableAudioTransport && diagnostics_.audioTransportReady))
        {
            diagnostics_.error = "worker HELLO_ACK failed identity or mode validation";
            forceTerminateAndReap();
            closeOwnedResources();
            return StartResult::Failed;
        }

        if (options.createVst3Plugin)
        {
            const auto pluginCreateDeadline = GetTickCount64()
                                             + options.pluginCreateTimeoutMs;
            if (! options.enableAudioTransport
                || ! createHostedPlugin(pluginCreateDeadline))
            {
                forceTerminateAndReap();
                closeOwnedResources();
                return StartResult::Failed;
            }
        }

        diagnostics_.handshake = true;
        running_ = true;
        if (options.enableAudioTransport)
        {
            audioTransport_.markWorkerAvailable(true);
            audioTransport_.openRealtimeGate();
        }
        return StartResult::Started;
       #else
        juce::ignoreUnused(options);
        diagnostics_.error = "Phase A worker bootstrap is Windows-only";
        return StartResult::Failed;
       #endif
    }

    bool shutdown(DWORD gracefulTimeoutMs)
    {
       #if JUCE_WINDOWS
        using namespace PluginSandboxWin32;
        if (! running_)
            return diagnostics_.reaped;

        const auto shutdownDeadline = GetTickCount64() + gracefulTimeoutMs;

        audioTransport_.closeRealtimeGate();
        const bool realtimeDrained =
            audioTransport_.waitForRealtimeDrain(remainingMilliseconds(shutdownDeadline));
        bool audioStopped = ! options_.enableAudioTransport;
        if (realtimeDrained && options_.enableAudioTransport)
            audioStopped = sendControlAndWait(MessageType::AudioStop,
                                              MessageType::AudioStopAck,
                                              shutdownDeadline);
        diagnostics_.audioStopped = audioStopped;
        audioTransport_.markWorkerAvailable(false);

        juce::String operationError;
        const auto request = makeMessage(MessageType::Shutdown,
                                         APEX_PLUGIN_SANDBOX_PROTOCOL_V1,
                                         diagnostics_.sessionToken,
                                         diagnostics_.parentProcessId, 0,
                                         parentExecutableIdentity_);
        ControlMessage response;
        const bool graceful = realtimeDrained && audioStopped
                           && writeMessage(pipe_.get(), request,
                                            remainingMilliseconds(shutdownDeadline), operationError)
                           && readMessage(pipe_.get(), response,
                                          remainingMilliseconds(shutdownDeadline), operationError)
                           && validateMessageEnvelope(response, diagnostics_.sessionToken,
                                                      operationError)
                           && response.type == static_cast<std::uint16_t>(MessageType::ShutdownAck)
                           && response.protocolVersion == APEX_PLUGIN_SANDBOX_PROTOCOL_V1
                           && response.processId == launchedWorkerProcessId_
                           && ExecutableIdentity { response.executableVolumeSerial,
                                                   response.executableFileIndex }
                                  == parentExecutableIdentity_
                           && WaitForSingleObject(process_.get(),
                                                  remainingMilliseconds(shutdownDeadline))
                                  == WAIT_OBJECT_0
                           && processExitedCleanly();

        if (graceful)
        {
            diagnostics_.gracefulShutdown = true;
            diagnostics_.reaped = true;
        }
        else
        {
            if (diagnostics_.error.isEmpty())
                diagnostics_.error = operationError;
            forceTerminateAndReap();
        }

        if (! diagnostics_.reaped)
            return false;
        running_ = false;
        if (! finalizeExitedProcess())
            return false;
        return diagnostics_.reaped;
       #else
        juce::ignoreUnused(gracefulTimeoutMs);
        return false;
       #endif
    }

    bool reprepareAudio(const PluginSandboxAudioTransportCore::Configuration& configuration,
                        DWORD timeoutMs)
    {
       #if JUCE_WINDOWS
        if (! running_ || ! options_.enableAudioTransport)
        {
            diagnostics_.error = "audio transport is not active";
            return false;
        }

        const auto deadline = GetTickCount64() + timeoutMs;
        const bool automationWasPrepared = options_.enableAutomationTransport
            && automationTransport_.isPrepared();
        audioTransport_.closeRealtimeGate();
        automationTransport_.closeRealtimeGate();
        if (! audioTransport_.waitForRealtimeDrain(remainingMilliseconds(deadline))
            || ! sendControlAndWait(PluginSandboxWin32::MessageType::AudioStop,
                                    PluginSandboxWin32::MessageType::AudioStopAck,
                                    deadline))
        {
            diagnostics_.error = "audio transport did not quiesce for reprepare";
            return false;
        }

        audioTransport_.markWorkerAvailable(false);
        if (! audioTransport_.reconfigureAfterWorkerStopped(configuration,
                                                             diagnostics_.error)
            || (automationWasPrepared
                && ! automationTransport_.reconfigureAfterWorkerStopped(
                       audioTransport_.generation())))
        {
            if (diagnostics_.error.isEmpty())
                diagnostics_.error = "automation transport did not reconfigure";
            return false;
        }

        if (! sendControlAndWait(PluginSandboxWin32::MessageType::AudioStart,
                                 PluginSandboxWin32::MessageType::AudioStartAck,
                                 deadline))
            return false;

        options_.audioConfiguration = configuration;
        diagnostics_.audioGeneration = audioTransport_.generation();
        diagnostics_.audioStopped = false;
        audioTransport_.markWorkerAvailable(true);
        audioTransport_.openRealtimeGate();
        if (automationWasPrepared)
            automationTransport_.openRealtimeGate();
        return true;
       #else
        juce::ignoreUnused(configuration, timeoutMs);
        return false;
       #endif
    }

    bool terminateWorkerForTest()
    {
       #if JUCE_WINDOWS
        return terminateWorkerAndRetainTransportForTest()
            && cleanupTerminatedWorkerForTest();
       #else
        return false;
       #endif
    }

    bool terminateWorkerAndRetainTransportForTest()
    {
       #if JUCE_WINDOWS
        if (! running_ || ! process_.isValid())
            return false;

        diagnostics_.forcedTermination = true;
        const bool terminationRequested = job_.isValid()
            ? TerminateJobObject(job_.get(),
                                 PluginSandboxWin32::kForcedTerminationExitCode) != FALSE
            : TerminateProcess(process_.get(),
                               PluginSandboxWin32::kForcedTerminationExitCode) != FALSE;
        diagnostics_.reaped = terminationRequested
            && WaitForSingleObject(process_.get(), 5000) == WAIT_OBJECT_0;
        if (! diagnostics_.reaped)
            return false;

        audioTransport_.closeRealtimeGate();
        if (! audioTransport_.waitForRealtimeDrain(3000))
            return false;
        audioTransport_.markWorkerAvailable(false);
        diagnostics_.workerUnavailableDetected = true;
        diagnostics_.sharedMemoryRetainedAfterWorkerExit = audioTransport_.isPrepared()
            && PluginSandboxAudioTransportCore::mappingExists(
                   diagnostics_.sharedMemoryName);
        running_ = false;
        return diagnostics_.sharedMemoryRetainedAfterWorkerExit;
       #else
        return false;
       #endif
    }

    bool cleanupTerminatedWorkerForTest()
    {
       #if JUCE_WINDOWS
        return diagnostics_.reaped && finalizeExitedProcess();
       #else
        return false;
       #endif
    }

    bool pollWorkerHealth()
    {
       #if JUCE_WINDOWS
        if (! process_.isValid())
            return false;
        if (WaitForSingleObject(process_.get(), 0) != WAIT_OBJECT_0)
            return running_;

        diagnostics_.reaped = true;
        diagnostics_.workerUnavailableDetected = true;
        audioTransport_.closeRealtimeGate();
        if (! audioTransport_.waitForRealtimeDrain(3000))
            return false;
        audioTransport_.markWorkerAvailable(false);
        running_ = false;
        diagnostics_.sharedMemoryRetainedAfterWorkerExit = audioTransport_.isPrepared()
            && PluginSandboxAudioTransportCore::mappingExists(
                   diagnostics_.sharedMemoryName);
        return false;
       #else
        return false;
       #endif
    }

    /** Phase D: control-plane-only liveness snapshot. Uses a zero-timeout
        handle wait so it can never block. NotPrepared means the process core
        was never started or has been fully finalized. */
    Liveness pollLiveness() const noexcept
    {
       #if JUCE_WINDOWS
        if (! running_ || ! process_.isValid())
            return Liveness::NotPrepared;
        return WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT
            ? Liveness::Alive
            : Liveness::Dead;
       #else
        return Liveness::NotPrepared;
       #endif
    }

    /** Phase D: retire a dead or hung worker and release every owned resource
        so a fresh start() may follow. Control plane only.

        - closes the realtime gate and drains in-flight RT exchanges (bounded),
        - force-terminates the worker via the Job Object if it is still alive,
        - reaps the process handle,
        - finalizes pipe/process/job/mapping ownership.

        Returns false when the RT drain or reap does not complete within the
        bound; the caller must retry on a later control-plane tick. */
    bool retireWorkerAfterFailure(DWORD drainTimeoutMs)
    {
       #if JUCE_WINDOWS
        if (! running_ && ! process_.isValid())
            return true;    // already retired

        audioTransport_.closeRealtimeGate();
        if (! audioTransport_.waitForRealtimeDrain(drainTimeoutMs))
            return false;
        audioTransport_.markWorkerAvailable(false);

        if (process_.isValid() && WaitForSingleObject(process_.get(), 0) != WAIT_OBJECT_0)
        {
            diagnostics_.forcedTermination = true;
            if (job_.isValid())
                TerminateJobObject(job_.get(), PluginSandboxWin32::kForcedTerminationExitCode);
            else
                TerminateProcess(process_.get(), PluginSandboxWin32::kForcedTerminationExitCode);
            if (WaitForSingleObject(process_.get(), 3000) != WAIT_OBJECT_0)
                return false;    // still alive; leave ownership for a retry
        }

        diagnostics_.reaped = true;
        diagnostics_.workerUnavailableDetected = true;
        running_ = false;
        return finalizeExitedProcess();
       #else
        juce::ignoreUnused(drainTimeoutMs);
        return false;
       #endif
    }

    PluginSandboxAudioTransportCore& audioTransport() noexcept { return audioTransport_; }
    const PluginSandboxAudioTransportCore& audioTransport() const noexcept { return audioTransport_; }
    PluginSandboxAutomationTransportCore& automationTransport() noexcept { return automationTransport_; }
    const PluginSandboxAutomationTransportCore& automationTransport() const noexcept
    {
        return automationTransport_;
    }
    const Diagnostics& diagnostics() const noexcept { return diagnostics_; }
    bool isRunning() const noexcept { return running_; }
    std::uint64_t currentWorkerGeneration() const noexcept { return workerGeneration_; }
    std::uint32_t currentWorkerProcessId() const noexcept
    {
        return launchedWorkerProcessId_;
    }

    struct EditorInfo
    {
        std::uint64_t workerGeneration = 0;
        std::uint64_t requestSequence = 0;
        std::uint64_t nativeWindowHandle = 0;
        std::uint32_t workerProcessId = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        juce::String pluginInstanceId;
    };

    /** Phase F1 editor lifecycle. These methods are control-plane only and
        must never be called from the realtime audio path. */
    bool createEditor(const juce::String& pluginInstanceId,
                      std::uint64_t requestSequence,
                      EditorInfo& result,
                      DWORD timeoutMs,
                      juce::String& error)
    {
       #if JUCE_WINDOWS
        using namespace PluginSandboxWin32;
        result = {};
        if (! running_ || ! connected_ || ! process_.isValid())
        {
            error = "sandbox worker is not available for editor create";
            return false;
        }
        if (pluginInstanceId.isEmpty() || requestSequence == 0
            || workerGeneration_ == 0)
        {
            error = "invalid Phase F1 editor create identity";
            return false;
        }

        EditorControlMessage response;
        if (! sendEditorRequest(MessageType::EditorCreateRequest,
                                MessageType::EditorCreateResponse,
                                pluginInstanceId,
                                requestSequence,
                                0, 0, 0,
                                response, timeoutMs, error))
            return false;
        if (response.success == 0)
        {
            readEditorError(response, error);
            return false;
        }
        if (! validateEditorWindowIdentity(response.nativeWindowHandle,
                                           response.workerGeneration,
                                           response.processId,
                                           pluginInstanceId, error))
            return false;

        result.workerGeneration = response.workerGeneration;
        result.requestSequence = response.requestSequence;
        result.nativeWindowHandle = response.nativeWindowHandle;
        result.workerProcessId = response.processId;
        result.width = response.width;
        result.height = response.height;
        result.pluginInstanceId = pluginInstanceId;
        return true;
       #else
        juce::ignoreUnused(pluginInstanceId, requestSequence, result, timeoutMs);
        error = "Phase F1 editor hosting is Windows-only";
        return false;
       #endif
    }

    bool resizeEditor(const EditorInfo& current,
                      std::uint32_t width,
                      std::uint32_t height,
                      DWORD timeoutMs,
                      juce::String& error)
    {
       #if JUCE_WINDOWS
        using namespace PluginSandboxWin32;
        if (! validateEditorWindowIdentity(current.nativeWindowHandle,
                                           current.workerGeneration,
                                           current.workerProcessId,
                                           current.pluginInstanceId, error))
            return false;
        EditorControlMessage response;
        if (! sendEditorRequest(MessageType::EditorResizeRequest,
                                MessageType::EditorResizeResponse,
                                current.pluginInstanceId,
                                current.requestSequence,
                                current.nativeWindowHandle,
                                width, height,
                                response, timeoutMs, error))
            return false;
        if (response.success == 0)
        {
            readEditorError(response, error);
            return false;
        }
        return validateEditorWindowIdentity(response.nativeWindowHandle,
                                            response.workerGeneration,
                                            response.processId,
                                            current.pluginInstanceId, error);
       #else
        juce::ignoreUnused(current, width, height, timeoutMs);
        error = "Phase F1 editor hosting is Windows-only";
        return false;
       #endif
    }

    bool closeEditor(const EditorInfo& current,
                     DWORD timeoutMs,
                     juce::String& error)
    {
       #if JUCE_WINDOWS
        using namespace PluginSandboxWin32;
        if (! validateEditorWindowIdentity(current.nativeWindowHandle,
                                           current.workerGeneration,
                                           current.workerProcessId,
                                           current.pluginInstanceId, error))
            return false;
        EditorControlMessage response;
        if (! sendEditorRequest(MessageType::EditorCloseRequest,
                                MessageType::EditorCloseResponse,
                                current.pluginInstanceId,
                                current.requestSequence,
                                current.nativeWindowHandle,
                                0, 0,
                                response, timeoutMs, error))
            return false;
        if (response.success == 0)
        {
            readEditorError(response, error);
            return false;
        }
        return true;
       #else
        juce::ignoreUnused(current, timeoutMs);
        error = "Phase F1 editor hosting is Windows-only";
        return false;
       #endif
    }

    struct EditorAsyncStatus
    {
        PluginSandboxWin32::EditorAsyncStatus status =
            PluginSandboxWin32::EditorAsyncStatus::Closed;
        EditorInfo info;
        bool desiredOpen = false;
        juce::String error;
    };

    /** Phase F3 submit. The response is only a bounded worker acknowledgement;
        vendor editor construction continues on the worker GUI lane. */
    bool submitEditorOpen(const juce::String& pluginInstanceId,
                          std::uint64_t requestSequence,
                          EditorAsyncStatus& result,
                          DWORD timeoutMs,
                          juce::String& error)
    {
       #if JUCE_WINDOWS
        std::lock_guard<std::mutex> pipeLock(editorPipeMutex_);
        PluginSandboxWin32::EditorAsyncControlMessage response;
        if (! sendEditorAsyncRequest(PluginSandboxWin32::MessageType::EditorOpenSubmitRequest,
                                     PluginSandboxWin32::MessageType::EditorOpenSubmitResponse,
                                     pluginInstanceId, requestSequence,
                                     response, timeoutMs, error))
            return false;
        result = editorAsyncStatusFromResponse(response, pluginInstanceId);
        return true;
       #else
        juce::ignoreUnused(pluginInstanceId, requestSequence, result, timeoutMs);
        error = "Phase F3 editor hosting is Windows-only";
        return false;
       #endif
    }

    /** Phase F3 status query. Bounded metadata only; this function does not
        wait on vendor GUI creation. */
    bool queryEditorStatus(const juce::String& pluginInstanceId,
                           std::uint64_t requestSequence,
                           EditorAsyncStatus& result,
                           DWORD timeoutMs,
                           juce::String& error)
    {
       #if JUCE_WINDOWS
        std::lock_guard<std::mutex> pipeLock(editorPipeMutex_);
        PluginSandboxWin32::EditorAsyncControlMessage response;
        if (! sendEditorAsyncRequest(PluginSandboxWin32::MessageType::EditorStatusQueryRequest,
                                     PluginSandboxWin32::MessageType::EditorStatusQueryResponse,
                                     pluginInstanceId, requestSequence,
                                     response, timeoutMs, error))
            return false;
        result = editorAsyncStatusFromResponse(response, pluginInstanceId);
        return true;
       #else
        juce::ignoreUnused(pluginInstanceId, requestSequence, result, timeoutMs);
        error = "Phase F3 editor hosting is Windows-only";
        return false;
       #endif
    }

    /** Phase F3 cancellation intent. The worker acknowledges the transition;
        final editor destruction is reported later by status queries. */
    bool cancelEditor(const juce::String& pluginInstanceId,
                      std::uint64_t requestSequence,
                      EditorAsyncStatus& result,
                      DWORD timeoutMs,
                      juce::String& error)
    {
       #if JUCE_WINDOWS
        std::lock_guard<std::mutex> pipeLock(editorPipeMutex_);
        PluginSandboxWin32::EditorAsyncControlMessage response;
        if (! sendEditorAsyncRequest(PluginSandboxWin32::MessageType::EditorCancelRequest,
                                     PluginSandboxWin32::MessageType::EditorCancelResponse,
                                     pluginInstanceId, requestSequence,
                                     response, timeoutMs, error))
            return false;
        result = editorAsyncStatusFromResponse(response, pluginInstanceId);
        return true;
       #else
        juce::ignoreUnused(pluginInstanceId, requestSequence, result, timeoutMs);
        error = "Phase F3 editor hosting is Windows-only";
        return false;
       #endif
    }

   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    /** Phase D diagnostics: the OS PID of the process object currently owned
        by the process handle. Distinguishes a stale stored PID from actual
        handle ownership. Test/diagnostic use only; no behavior change. */
    std::uint32_t processHandlePidForTest() const noexcept
    {
       #if JUCE_WINDOWS
        return process_.isValid()
            ? GetProcessId(process_.get())
            : 0;
       #else
        return 0;
       #endif
    }

    /** Phase D diagnostics: total CreateProcessW successes across this
        process core's lifetime. Test/diagnostic use only. */
    std::uint32_t workerStartCountForTest() const noexcept { return workerStartCount_; }

    /** Phase D diagnostics: raw wait result + exit code from THE ACTUAL
        process handle. Test/diagnostic use only. */
    juce::String processHandleRawForTest() const noexcept
    {
       #if JUCE_WINDOWS
        if (! process_.isValid())
            return "no-handle";
        const DWORD waitResult = WaitForSingleObject(process_.get(), 0);
        DWORD exitCode = 0xFFFFFFFF;
        const BOOL exitOk = GetExitCodeProcess(process_.get(), &exitCode);
        const DWORD lastError = GetLastError();
        return "wait=" + juce::String(static_cast<int>(waitResult))
            + " exitOk=" + juce::String(exitOk ? 1 : 0)
            + " code=" + juce::String(static_cast<int>(exitCode))
            + " err=" + juce::String(static_cast<int>(lastError));
       #else
        return "n/a";
       #endif
    }

    DWORD processExitCodeForTest() const noexcept
    {
       #if JUCE_WINDOWS
        if (! process_.isValid())
            return 0xFFFFFFFFu;
        DWORD exitCode = 0xFFFFFFFFu;
        return GetExitCodeProcess(process_.get(), &exitCode) != FALSE
            ? exitCode : 0xFFFFFFFFu;
       #else
        return 0xFFFFFFFFu;
       #endif
    }
   #endif

private:
   #if JUCE_WINDOWS
    static void readEditorError(const PluginSandboxWin32::EditorControlMessage& response,
                                juce::String& error)
    {
        juce::String decoded;
        juce::String decodeError;
        if (! PluginSandboxWin32::readBoundedUtf8(response.error, decoded,
                                                   decodeError, "editorError"))
            error = decodeError;
        else
            error = decoded.isNotEmpty() ? decoded : "worker rejected editor request";
    }

    bool validateEditorWindowIdentity(std::uint64_t nativeWindowHandle,
                                      std::uint64_t generation,
                                      std::uint32_t processId,
                                      const juce::String& pluginInstanceId,
                                      juce::String& error) const
    {
        if (nativeWindowHandle == 0
            || generation == 0 || generation != workerGeneration_
            || processId == 0 || processId != launchedWorkerProcessId_
            || pluginInstanceId.isEmpty())
        {
            error = "Phase F1 editor identity does not match the active worker";
            return false;
        }

        const auto hwnd = reinterpret_cast<HWND>(
            static_cast<std::uintptr_t>(nativeWindowHandle));
        if (hwnd == nullptr || ! IsWindow(hwnd))
        {
            error = "Phase F1 editor HWND is not a live window";
            return false;
        }

        DWORD ownerProcessId = 0;
        if (GetWindowThreadProcessId(hwnd, &ownerProcessId) == 0
            || ownerProcessId != processId)
        {
            error = "Phase F1 editor HWND belongs to a different process";
            return false;
        }
        return true;
    }

    bool sendEditorRequest(PluginSandboxWin32::MessageType requestType,
                           PluginSandboxWin32::MessageType responseType,
                           const juce::String& pluginInstanceId,
                           std::uint64_t requestSequence,
                           std::uint64_t nativeWindowHandle,
                           std::uint32_t width,
                           std::uint32_t height,
                           PluginSandboxWin32::EditorControlMessage& response,
                           DWORD timeoutMs,
                           juce::String& error)
    {
        using namespace PluginSandboxWin32;
        const std::lock_guard<std::mutex> pipeLock(editorPipeMutex_);
        EditorControlMessage request;
        if (! makeEditorMessage(requestType,
                                diagnostics_.sessionToken,
                                pluginInstanceId,
                                diagnostics_.parentProcessId,
                                workerGeneration_,
                                requestSequence,
                                nativeWindowHandle,
                                width, height,
                                request, error))
            return false;

        if (! writeFixedMessage(pipe_.get(), request, timeoutMs, error)
            || ! readFixedMessage(pipe_.get(), response, timeoutMs, error)
            || ! validateEditorMessageEnvelope(response,
                                               diagnostics_.sessionToken,
                                               error))
            return false;

        juce::String responsePluginInstanceId;
        if (! readBoundedUtf8(response.pluginInstanceId,
                              responsePluginInstanceId, error,
                              "pluginInstanceId")
            || static_cast<MessageType>(response.type) != responseType
            || response.processId != launchedWorkerProcessId_
            || response.workerGeneration != workerGeneration_
            || response.requestSequence != requestSequence
            || responsePluginInstanceId != pluginInstanceId)
        {
            error = "Phase F1 editor response identity mismatch";
            return false;
        }
        return true;
    }

    static EditorAsyncStatus editorAsyncStatusFromResponse(
        const PluginSandboxWin32::EditorAsyncControlMessage& response,
        const juce::String& pluginInstanceId)
    {
        EditorAsyncStatus result;
        result.status = static_cast<PluginSandboxWin32::EditorAsyncStatus>(
            response.status);
        result.info.workerGeneration = response.workerGeneration;
        result.info.requestSequence = response.requestSequence;
        result.info.nativeWindowHandle = response.nativeWindowHandle;
        result.info.workerProcessId = response.processId;
        result.info.width = response.width;
        result.info.height = response.height;
        result.info.pluginInstanceId = pluginInstanceId;
        result.desiredOpen = response.success != 0
            && result.status != PluginSandboxWin32::EditorAsyncStatus::Closed
            && result.status != PluginSandboxWin32::EditorAsyncStatus::Cancelled
            && result.status != PluginSandboxWin32::EditorAsyncStatus::Failed;
        juce::String ignored;
        if (! PluginSandboxWin32::readBoundedUtf8(response.error, result.error,
                                                  ignored, "editorAsyncError"))
            result.error = "worker returned an invalid Phase F3 editor error";
        return result;
    }

    bool sendEditorAsyncRequest(PluginSandboxWin32::MessageType requestType,
                                PluginSandboxWin32::MessageType responseType,
                                const juce::String& pluginInstanceId,
                                std::uint64_t requestSequence,
                                PluginSandboxWin32::EditorAsyncControlMessage& response,
                                DWORD timeoutMs,
                                juce::String& error)
    {
        using namespace PluginSandboxWin32;
        if (! running_ || ! connected_ || ! process_.isValid())
        {
            error = "sandbox worker is not available for Phase F3 editor control";
            return false;
        }

        EditorAsyncControlMessage request;
        if (! makeEditorAsyncMessage(requestType,
                                     diagnostics_.sessionToken,
                                     pluginInstanceId,
                                     diagnostics_.parentProcessId,
                                     workerGeneration_,
                                     requestSequence,
                                     request,
                                     error))
            return false;

        if (! writeFixedMessage(pipe_.get(), request, timeoutMs, error)
            || ! readFixedMessage(pipe_.get(), response, timeoutMs, error)
            || ! validateEditorAsyncMessageEnvelope(response,
                                                    diagnostics_.sessionToken,
                                                    error))
            return false;

        juce::String responsePluginInstanceId;
        if (! readBoundedUtf8(response.pluginInstanceId,
                              responsePluginInstanceId, error,
                              "pluginInstanceId")
            || static_cast<MessageType>(response.type) != responseType
            || response.processId != launchedWorkerProcessId_
            || response.workerGeneration != workerGeneration_
            || response.requestSequence != requestSequence
            || responsePluginInstanceId != pluginInstanceId)
        {
            error = "Phase F3 editor response identity mismatch";
            return false;
        }
        return true;
    }

    bool connectPipe(DWORD timeoutMs)
    {
        using namespace PluginSandboxWin32;
        const auto deadline = GetTickCount64() + timeoutMs;
        for (;;)
        {
            if (ConnectNamedPipe(pipe_.get(), nullptr))
            {
                connected_ = true;
                return true;
            }

            const auto connectError = GetLastError();
            if (connectError == ERROR_PIPE_CONNECTED)
            {
                connected_ = true;
                return true;
            }
            if (connectError != ERROR_PIPE_LISTENING && connectError != ERROR_NO_DATA)
            {
                diagnostics_.error = windowsError("ConnectNamedPipe", connectError);
                return false;
            }
            if (WaitForSingleObject(process_.get(), 0) == WAIT_OBJECT_0)
            {
                diagnostics_.error = "worker exited before connecting to IPC";
                return false;
            }
            if (GetTickCount64() >= deadline)
            {
                diagnostics_.error = "worker IPC connection timed out";
                return false;
            }
            Sleep(1);
        }
    }

    bool sendControlAndWait(PluginSandboxWin32::MessageType requestType,
                            PluginSandboxWin32::MessageType expectedResponseType,
                            ULONGLONG deadline)
    {
        using namespace PluginSandboxWin32;
        juce::String error;
        const auto request = makeMessage(requestType,
                                         APEX_PLUGIN_SANDBOX_PROTOCOL_V1,
                                         diagnostics_.sessionToken,
                                         diagnostics_.parentProcessId, 0,
                                         parentExecutableIdentity_);
        ControlMessage response;
        const bool ok = writeMessage(pipe_.get(), request,
                                     remainingMilliseconds(deadline), error)
                     && readMessage(pipe_.get(), response,
                                    remainingMilliseconds(deadline), error)
                     && validateMessageEnvelope(response,
                                                diagnostics_.sessionToken, error)
                     && response.type == static_cast<std::uint16_t>(expectedResponseType)
                     && response.protocolVersion == APEX_PLUGIN_SANDBOX_PROTOCOL_V1
                     && response.processId == launchedWorkerProcessId_
                     && ExecutableIdentity { response.executableVolumeSerial,
                                             response.executableFileIndex }
                            == parentExecutableIdentity_;
        if (! ok && diagnostics_.error.isEmpty())
            diagnostics_.error = error.isNotEmpty() ? error : "invalid worker control response";
        return ok;
    }

    bool createHostedPlugin(ULONGLONG deadline)
    {
        using namespace PluginSandboxWin32;
        recordPluginCreatePhase("PLUGINCREATE_CONTEXT",
                                "audioGeneration="
                                    + juce::String(static_cast<juce::int64>(
                                        audioTransport_.generation())));

        PluginCreateMessage request;
        if (! makePluginCreateMessage(options_.pluginDescription,
                                      diagnostics_.sessionToken,
                                      diagnostics_.parentProcessId,
                                      audioTransport_.generation(),
                                      options_.audioConfiguration.sampleRate,
                                      options_.audioConfiguration.maxSamples,
                                      options_.audioConfiguration.maxInputChannels,
                                      options_.audioConfiguration.maxOutputChannels,
                                      request, diagnostics_.error))
        {
            recordPluginCreateFailure("PLUGINCREATE_REQUEST_BUILD_FAILURE",
                                      diagnostics_.error);
            return false;
        }

        PluginCreateResponse response;
        recordPluginCreatePhase("PLUGINCREATE_WRITE_BEGIN");
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        if (options_.pluginCreateControlFaultPoint
            == PluginSandboxControlTestFaultPoint::PluginCreateWriteTimeout)
        {
            diagnostics_.error = "PluginCreate request write timeout";
            recordPluginCreateFailure("PLUGINCREATE_WRITE_TIMEOUT",
                                      "test-injected write timeout");
            return false;
        }
       #endif

        juce::String transportError;
        if (! writeFixedMessage(pipe_.get(), request,
                                remainingMilliseconds(deadline), transportError))
        {
            const bool timedOut = transportError == "IPC operation timed out";
            diagnostics_.error = timedOut
                ? "PluginCreate request write timeout"
                : "PluginCreate request write failed: " + transportError;
            recordPluginCreateFailure(timedOut
                                          ? "PLUGINCREATE_WRITE_TIMEOUT"
                                          : "PLUGINCREATE_WRITE_FAILURE",
                                      transportError);
            return false;
        }

        recordPluginCreatePhase("PLUGINCREATE_WRITE_COMPLETE");
        recordPluginCreatePhase("PLUGINCREATE_RESPONSE_READ_BEGIN");
        transportError.clear();
        if (! readFixedMessage(pipe_.get(), response,
                               remainingMilliseconds(deadline), transportError))
        {
            const bool timedOut = transportError == "IPC operation timed out";
            diagnostics_.error = timedOut
                ? "PluginCreate response read timeout"
                : "PluginCreate response read failed: " + transportError;
            recordPluginCreateFailure(timedOut
                                          ? "PLUGINCREATE_RESPONSE_READ_TIMEOUT"
                                          : "PLUGINCREATE_RESPONSE_READ_FAILURE",
                                      transportError);
            return false;
        }

        recordPluginCreatePhase("PLUGINCREATE_RESPONSE_READ_COMPLETE");
        transportError.clear();
        if (! validatePluginCreateResponse(response,
                                           diagnostics_.sessionToken,
                                           transportError)
            || response.processId != launchedWorkerProcessId_
            || response.audioGeneration != audioTransport_.generation())
        {
            diagnostics_.error = transportError.isNotEmpty()
                ? transportError
                : "PluginCreate response identity or generation mismatch";
            recordPluginCreateFailure("PLUGINCREATE_RESPONSE_VALIDATION_FAILURE",
                                      diagnostics_.error);
            return false;
        }

        if (response.type != static_cast<std::uint16_t>(MessageType::PluginCreateAck))
        {
            juce::String workerError;
            juce::String decodeError;
            if (! readBoundedUtf8(response.error, workerError,
                                  decodeError, "pluginCreateError"))
                workerError = decodeError;
            diagnostics_.error = workerError.isNotEmpty()
                ? workerError : "worker rejected VST3 creation";
            recordPluginCreateFailure("PLUGINCREATE_WORKER_REJECT",
                                      diagnostics_.error);
            return false;
        }

        if (response.inputChannels != options_.audioConfiguration.maxInputChannels
            || response.outputChannels != options_.audioConfiguration.maxOutputChannels)
        {
            diagnostics_.error = "worker VST3 layout acknowledgement mismatch";
            recordPluginCreateFailure("PLUGINCREATE_RESPONSE_LAYOUT_FAILURE",
                                      diagnostics_.error);
            return false;
        }

        diagnostics_.hostedPluginCreated = true;
        diagnostics_.hostedPluginLatencySamples = static_cast<int>(
            response.pluginLatencySamples);
        diagnostics_.hostedPluginInputChannels = static_cast<int>(
            response.inputChannels);
        diagnostics_.hostedPluginOutputChannels = static_cast<int>(
            response.outputChannels);
        diagnostics_.hostedPluginModuleLoadedInWorker =
            response.moduleLoadedInWorker == 1u;
        diagnostics_.hostedPluginUniqueId = response.uniqueId;
        diagnostics_.hostedPluginDeprecatedUid = response.deprecatedUid;
        recordPluginCreatePhase("PLUGINCREATE_COMPLETE");
        return true;
    }

    void recordPluginCreatePhase(const char* phase,
                                 const juce::String& detail = {})
    {
        diagnostics_.pluginCreateLastPhase = phase;
        diagnostics_.pluginCreateWorkerProcessId = launchedWorkerProcessId_;
        diagnostics_.pluginCreateWorkerGeneration = workerGeneration_;
        PluginSandboxDiagnosticsCore::appendPluginCreatePhase(
            diagnostics_.sessionToken,
            diagnostics_.pluginCreateWorkerProcessId,
            diagnostics_.pluginCreateWorkerGeneration,
            phase,
            detail);
    }

    void recordPluginCreateFailure(const char* phase,
                                   const juce::String& detail)
    {
        diagnostics_.pluginCreateFailurePhase = phase;
        recordPluginCreatePhase(phase, detail);
    }

   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    static juce::String audioFaultPointToken(
        PluginSandboxAudioTestFaultPoint point)
    {
        switch (point)
        {
            case PluginSandboxAudioTestFaultPoint::AfterAudioSubmitBeforeAutomationApply:
                return "after-submit-before-automation-apply";
            case PluginSandboxAudioTestFaultPoint::AfterAutomationApplyBeforeWorkerCommit:
                return "after-automation-apply-before-worker-commit";
            case PluginSandboxAudioTestFaultPoint::None:
            default:
                return {};
        }
    }
   #endif

    // ── Phase E1 parent-side control-plane operations ────────────────────────
    // Control plane only: these block on the control pipe with bounded
    // timeouts and never run on the realtime audio thread.

public:
   #if JUCE_WINDOWS
    bool e1Ready(juce::String& error) const
    {
        if (! running_ || ! pipe_.isValid() || ! connected_)
        {
            error = "sandbox worker is not available for E1 control";
            return false;
        }
        return true;
    }

    bool sendE1Request(std::uint16_t type,
                       PluginSandboxWin32::E1ControlMessage& request,
                       juce::String& error)
    {
        using namespace PluginSandboxWin32;
        makeE1Message(type, diagnostics_.sessionToken,
                      GetCurrentProcessId(), audioTransport_.generation(),
                      request);
        return writeFixedMessage(pipe_.get(), request, 5000, error);
    }

    bool receiveE1Chunks(const PluginSandboxWin32::E1ControlMessage& begin,
                         PluginSandboxWin32::E1PayloadPurpose purpose,
                         juce::MemoryBlock& data,
                         juce::String& error)
    {
        using namespace PluginSandboxWin32;
        if (begin.type != static_cast<std::uint16_t>(MessageType::E1PayloadBegin)
            || begin.purpose != static_cast<std::uint32_t>(purpose)
            || ! validE1PayloadBounds(begin.totalBytes, begin.totalChunks, error))
        {
            if (error.isEmpty())
                error = "E1 payload begin mismatch: type="
                    + juce::String(static_cast<int>(begin.type))
                    + " purpose=" + juce::String(static_cast<int>(begin.purpose))
                    + " totalBytes=" + juce::String(static_cast<int>(begin.totalBytes))
                    + " totalChunks=" + juce::String(static_cast<int>(begin.totalChunks));
            return false;
        }

        data.reset();
        juce::MemoryOutputStream stream(data, true);
        for (std::uint32_t i = 0; i < begin.totalChunks; ++i)
        {
            E1ChunkMessage chunk;
            if (! readFixedMessage(pipe_.get(), chunk, 5000, error))
            {
                error = "E1 payload chunk read failed at index "
                    + juce::String(static_cast<int>(i)) + ": " + error;
                return false;
            }
            if (! validateE1ChunkEnvelope(chunk, diagnostics_.sessionToken,
                                          audioTransport_.generation(),
                                          launchedWorkerProcessId_, error)
                || chunk.purpose != static_cast<std::uint32_t>(purpose)
                || ! validE1ChunkFields(chunk, error)
                || chunk.chunkIndex != i
                || chunk.totalBytes != begin.totalBytes
                || chunk.totalChunks != begin.totalChunks)
            {
                if (error.isEmpty())
                    error = "invalid E1 chunk stream from worker";
                return false;
            }
            stream.write(chunk.payload, static_cast<size_t>(chunk.payloadBytes));
        }
        return true;
    }

    bool requestParameterMetadata(juce::MemoryBlock& metadata, juce::String& error)
    {
        using namespace PluginSandboxWin32;
        if (! e1Ready(error))
            return false;
        E1ControlMessage request;
        if (! sendE1Request(static_cast<std::uint16_t>(
                MessageType::ParameterMetadataRequest), request, error))
            return false;

        E1ControlMessage begin;
        if (! readFixedMessage(pipe_.get(), begin, 5000, error)
            || ! validateE1Envelope(begin, diagnostics_.sessionToken,
                                    audioTransport_.generation(),
                                    launchedWorkerProcessId_, error))
            return false;
        return receiveE1Chunks(begin, E1PayloadPurpose::ParameterMetadata,
                               metadata, error);
    }

    /** Phase E2B (control plane): read ACTUAL live worker parameter values.
        Observational only — never mutates the E1 shadows. */
    bool requestLiveParameterValues(juce::MemoryBlock& values,
                                    juce::String& error)
    {
        using namespace PluginSandboxWin32;
        if (! e1Ready(error))
            return false;
        E1ControlMessage request;
        if (! sendE1Request(static_cast<std::uint16_t>(
                MessageType::LiveValuesRequest), request, error))
            return false;

        E1ControlMessage begin;
        if (! readFixedMessage(pipe_.get(), begin, 5000, error)
            || ! validateE1Envelope(begin, diagnostics_.sessionToken,
                                    audioTransport_.generation(),
                                    launchedWorkerProcessId_, error))
            return false;
        return receiveE1Chunks(begin, E1PayloadPurpose::LiveValues,
                               values, error);
    }

    bool setPluginParameter(const juce::String& parameterId,
                            float normalizedValue,
                            float& appliedValue,
                            juce::String& error)
    {
        using namespace PluginSandboxWin32;
        appliedValue = 0.0f;
        if (! e1Ready(error))
            return false;
        if (parameterId.isEmpty()
            || parameterId.getNumBytesAsUTF8() >= static_cast<int>(kE1ParameterIdBytes))
        {
            error = "invalid E1 parameter ID";
            return false;
        }

        E1ControlMessage request;
        makeE1Message(static_cast<std::uint16_t>(MessageType::ParameterSetRequest),
                      diagnostics_.sessionToken, GetCurrentProcessId(),
                      audioTransport_.generation(), request);
        parameterId.copyToUTF8(request.parameterId, sizeof(request.parameterId));
        request.normalizedValue = normalizedValue;
        if (! writeFixedMessage(pipe_.get(), request, 5000, error))
            return false;

        E1ControlMessage response;
        if (! readFixedMessage(pipe_.get(), response, 5000, error)
            || ! validateE1Envelope(response, diagnostics_.sessionToken,
                                    audioTransport_.generation(),
                                    launchedWorkerProcessId_, error)
            || response.type != static_cast<std::uint16_t>(
                MessageType::ParameterSetResponse))
        {
            if (error.isEmpty())
                error = "invalid E1 parameter-set response";
            return false;
        }
        appliedValue = response.appliedValue;
        if (response.ok == 0)
        {
            juce::String operationError;
            if (readBoundedUtf8(response.error, operationError,
                                error, "e1Error"))
                error = operationError;
            return false;
        }
        return true;
    }

    bool capturePluginState(juce::MemoryBlock& state, juce::String& error)
    {
        using namespace PluginSandboxWin32;
        if (! e1Ready(error))
            return false;
        E1ControlMessage request;
        if (! sendE1Request(static_cast<std::uint16_t>(
                MessageType::StateGetRequest), request, error))
            return false;

        E1ControlMessage begin;
        if (! readFixedMessage(pipe_.get(), begin, 5000, error)
            || ! validateE1Envelope(begin, diagnostics_.sessionToken,
                                    audioTransport_.generation(),
                                    launchedWorkerProcessId_, error))
            return false;
        return receiveE1Chunks(begin, E1PayloadPurpose::StateGet, state, error);
    }

    bool restorePluginState(const juce::MemoryBlock& state, juce::String& error)
    {
        using namespace PluginSandboxWin32;
        if (! e1Ready(error))
            return false;
        if (state.getSize() == 0
            || state.getSize() > kE1MaximumPayloadBytes)
        {
            error = "E1 state chunk is empty or exceeds the payload bound";
            return false;
        }

        E1ControlMessage begin;
        makeE1Message(static_cast<std::uint16_t>(MessageType::StateSetBegin),
                      diagnostics_.sessionToken, GetCurrentProcessId(),
                      audioTransport_.generation(), begin);
        begin.purpose = static_cast<std::uint32_t>(E1PayloadPurpose::StateSet);
        begin.totalBytes = static_cast<std::uint32_t>(state.getSize());
        begin.totalChunks = (begin.totalBytes + kE1ChunkPayloadBytes - 1)
                          / kE1ChunkPayloadBytes;
        if (! writeFixedMessage(pipe_.get(), begin, 5000, error))
            return false;

        for (std::uint32_t chunkIndex = 0; chunkIndex < begin.totalChunks; ++chunkIndex)
        {
            E1ChunkMessage chunk;
            makeE1ChunkMessage(diagnostics_.sessionToken, GetCurrentProcessId(),
                               audioTransport_.generation(), chunk);
            chunk.purpose = static_cast<std::uint32_t>(E1PayloadPurpose::StateSet);
            chunk.totalBytes = begin.totalBytes;
            chunk.totalChunks = begin.totalChunks;
            chunk.chunkIndex = chunkIndex;
            chunk.payloadBytes = static_cast<std::uint32_t>(
                juce::jmin<std::size_t>(kE1ChunkPayloadBytes,
                                        state.getSize()
                                            - static_cast<std::size_t>(chunkIndex)
                                                  * kE1ChunkPayloadBytes));
            std::memcpy(chunk.payload,
                        static_cast<const std::uint8_t*>(state.getData())
                            + static_cast<std::size_t>(chunkIndex)
                                  * kE1ChunkPayloadBytes,
                        chunk.payloadBytes);
            if (! writeFixedMessage(pipe_.get(), chunk, 5000, error))
                return false;
        }

        E1ControlMessage result;
        if (! readFixedMessage(pipe_.get(), result, 5000, error)
            || ! validateE1Envelope(result, diagnostics_.sessionToken,
                                    audioTransport_.generation(),
                                    launchedWorkerProcessId_, error)
            || result.type != static_cast<std::uint16_t>(MessageType::StateSetResult))
        {
            if (error.isEmpty())
                error = "invalid E1 state-set result";
            return false;
        }
        if (result.ok == 0)
        {
            juce::String operationError;
            if (readBoundedUtf8(result.error, operationError,
                                error, "e1Error"))
                error = operationError;
            return false;
        }
        return true;
    }
   #else
    bool e1Ready(juce::String& error) const
    {
        error = "E1 control is Windows-only";
        return false;
    }
    bool requestParameterMetadata(juce::MemoryBlock&, juce::String& error)
    { error = "E1 control is Windows-only"; return false; }
    bool setPluginParameter(const juce::String&, float, float&, juce::String& error)
    { error = "E1 control is Windows-only"; return false; }
    bool capturePluginState(juce::MemoryBlock&, juce::String& error)
    { error = "E1 control is Windows-only"; return false; }
    bool restorePluginState(const juce::MemoryBlock&, juce::String& error)
    { error = "E1 control is Windows-only"; return false; }
   #endif

    int queryActiveProcessCount() const
    {
        if (! job_.isValid())
            return 0;
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting {};
        if (! QueryInformationJobObject(job_.get(), JobObjectBasicAccountingInformation,
                                        &accounting, sizeof(accounting), nullptr))
            return -1;
        return static_cast<int>(accounting.ActiveProcesses);
    }

    static DWORD remainingMilliseconds(ULONGLONG deadline) noexcept
    {
        const auto now = GetTickCount64();
        if (now >= deadline)
            return 0;
        return static_cast<DWORD>(juce::jmin<ULONGLONG>(deadline - now, MAXDWORD));
    }

    bool processExitedCleanly() const noexcept
    {
        DWORD exitCode = STILL_ACTIVE;
        return process_.isValid() && GetExitCodeProcess(process_.get(), &exitCode)
            && exitCode == 0;
    }

    static bool endpointIsReleased(const juce::String& endpoint) noexcept
    {
        SetLastError(ERROR_SUCCESS);
        if (WaitNamedPipeW(endpoint.toWideCharPointer(), 0))
            return false;
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }

    void forceTerminateAndReap()
    {
        using namespace PluginSandboxWin32;
        audioTransport_.closeRealtimeGate();
        audioTransport_.waitForRealtimeDrain(3000);
        audioTransport_.markWorkerAvailable(false);
        if (! process_.isValid())
            return;

        if (WaitForSingleObject(process_.get(), 0) != WAIT_OBJECT_0)
        {
            diagnostics_.forcedTermination = true;
            if (job_.isValid())
                TerminateJobObject(job_.get(), kForcedTerminationExitCode);
            else
                TerminateProcess(process_.get(), kForcedTerminationExitCode);
        }
        diagnostics_.reaped = WaitForSingleObject(process_.get(), 3000) == WAIT_OBJECT_0;
    }

    void waitForExitAndFinalize(DWORD timeoutMs)
    {
        if (process_.isValid())
            diagnostics_.reaped = WaitForSingleObject(process_.get(), timeoutMs) == WAIT_OBJECT_0;
        if (! diagnostics_.reaped)
            forceTerminateAndReap();
        finalizeExitedProcess();
    }

    bool finalizeExitedProcess()
    {
        if (process_.isValid()
            && WaitForSingleObject(process_.get(), 0) != WAIT_OBJECT_0)
        {
            diagnostics_.error = "refused to release shared memory before worker reap";
            return false;
        }
        diagnostics_.reaped = true;
        // A signalled process handle and the Job Object accounting view are
        // separate kernel observations.  After a worker exits, the latter
        // can briefly retain the old ActiveProcesses value.  Bound this
        // control-plane settling window before releasing the job handle so a
        // legitimate clean shutdown is not reported as an orphan.
        const auto activeProcessDeadline = GetTickCount64() + 100;
        do
        {
            diagnostics_.activeProcessesAfter = queryActiveProcessCount();
            if (diagnostics_.activeProcessesAfter == 0
                || GetTickCount64() >= activeProcessDeadline)
                break;
            Sleep(1);
        }
        while (true);
        if (connected_ && pipe_.isValid())
            DisconnectNamedPipe(pipe_.get());
        connected_ = false;
        pipe_.reset();

        diagnostics_.endpointReleased = endpointIsReleased(diagnostics_.endpoint);

        process_.reset();
        job_.reset();
        const auto mappingName = diagnostics_.sharedMemoryName;
        audioTransport_.releaseAfterWorkerStopped();
        automationTransport_.releaseAfterWorkerStopped();   // Phase E2A
        diagnostics_.sharedMemoryReleased = mappingName.isEmpty()
            || ! PluginSandboxAudioTransportCore::mappingExists(mappingName);
        diagnostics_.handlesClosed = ! pipe_.isValid() && ! process_.isValid()
                                   && ! job_.isValid() && ! audioTransport_.isPrepared();
        return diagnostics_.handlesClosed && diagnostics_.sharedMemoryReleased;
    }

    void closeOwnedResources()
    {
        if (process_.isValid() && WaitForSingleObject(process_.get(), 0) != WAIT_OBJECT_0)
            forceTerminateAndReap();
        if (process_.isValid() && ! diagnostics_.reaped)
            return;
        if (connected_ && pipe_.isValid())
            DisconnectNamedPipe(pipe_.get());
        connected_ = false;
        pipe_.reset();
        process_.reset();
        job_.reset();
        const auto mappingName = diagnostics_.sharedMemoryName;
        audioTransport_.closeRealtimeGate();
        audioTransport_.waitForRealtimeDrain(3000);
        audioTransport_.markWorkerAvailable(false);
        audioTransport_.releaseAfterWorkerStopped();
        automationTransport_.closeRealtimeGate();            // Phase E2A
        automationTransport_.releaseAfterWorkerStopped();    // Phase E2A
        diagnostics_.sharedMemoryReleased = mappingName.isEmpty()
            || ! PluginSandboxAudioTransportCore::mappingExists(mappingName);
        diagnostics_.handlesClosed = ! audioTransport_.isPrepared();
        if (diagnostics_.endpoint.isNotEmpty())
            diagnostics_.endpointReleased = endpointIsReleased(diagnostics_.endpoint);
    }

    Options options_;
    PluginSandboxAudioTransportCore audioTransport_;
    PluginSandboxAutomationTransportCore automationTransport_;   // Phase E2A (additive)
    PluginSandboxWin32::UniqueHandle pipe_;
    PluginSandboxWin32::UniqueHandle process_;
    PluginSandboxWin32::UniqueHandle job_;
    std::mutex editorPipeMutex_;
    PluginSandboxWin32::ExecutableIdentity parentExecutableIdentity_;
    bool connected_ = false;
    bool running_ = false;
    DWORD launchedWorkerProcessId_ = 0;
    std::uint64_t workerGeneration_ = 0;
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
     std::uint32_t workerStartCount_ = 0;   // Phase D diagnostics: lifetime CreateProcessW successes
   #endif
   #else
    void closeOwnedResources() {}
   #endif

    Diagnostics diagnostics_;

    static juce::String audioDspModeToken(PluginSandboxAudioTestDspMode mode)
    {
        switch (mode)
        {
            case PluginSandboxAudioTestDspMode::GainHalf: return "gain-half";
            case PluginSandboxAudioTestDspMode::GainQuarter: return "gain-quarter";
            case PluginSandboxAudioTestDspMode::ChannelMarker: return "channel-marker";
            case PluginSandboxAudioTestDspMode::DeadlineIdentity: return "deadline-identity";
            case PluginSandboxAudioTestDspMode::Identity:
            default: return "identity";
        }
    }
};

} // namespace DAW
