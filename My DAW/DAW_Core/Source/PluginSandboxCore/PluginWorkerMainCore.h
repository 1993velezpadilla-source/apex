#pragma once

#include "PluginSandboxProtocolCore.h"
#include "PluginSandboxPreparationCore.h"
#include "PluginSandboxDiagnosticsCore.h"
#include "PluginSandboxEnumOnlyDiagnosticCore.h"
#include "PluginWorkerAudioTransportCore.h"

#include <chrono>
#include <future>
#include <thread>

namespace DAW {

class PluginWorkerMainCore
{
public:
    static int run(const juce::StringArray& arguments)
    {
       #if JUCE_WINDOWS
        using namespace PluginSandboxWin32;

        const auto enumOnly = PluginSandboxCommandLineCore::parseEnumOnly(arguments);
        if (enumOnly.requested)
        {
            if (! enumOnly.valid)
            {
                std::cerr << "APEX enum-only diagnostic argument failure: "
                          << enumOnly.error << std::endl;
                return PluginSandboxEnumOnlyDiagnosticCore::kInvalidArgumentsExitCode;
            }

            return PluginSandboxEnumOnlyDiagnosticCore::run(enumOnly);
        }

         const auto parsed = PluginSandboxCommandLineCore::parseWorker(arguments);
         if (! parsed.valid)
             return 64;

         // The production JUCEApplication creates the manager before entering
         // worker mode. The console test executable enters this function
         // directly, so establish the same worker-main-thread ownership for
         // both launch paths before any editor marshal can be requested.
         auto* workerMessageManager = juce::MessageManager::getInstance();
         if (workerMessageManager == nullptr)
             return 63;
         workerMessageManager->setCurrentThreadAsMessageThread();

       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        if (parsed.workerStartupDelayMilliseconds > 0)
            Sleep(parsed.workerStartupDelayMilliseconds);
       #endif

        if (! WaitNamedPipeW(parsed.ipcEndpoint.toWideCharPointer(), 5000))
            return 65;

        UniqueHandle pipe(CreateFileW(parsed.ipcEndpoint.toWideCharPointer(),
                                      GENERIC_READ | GENERIC_WRITE,
                                      0, nullptr, OPEN_EXISTING,
                                      0, nullptr));
        if (! pipe.isValid())
            return 66;

        DWORD readMode = PIPE_READMODE_MESSAGE | PIPE_NOWAIT;
        if (! SetNamedPipeHandleState(pipe.get(), &readMode, nullptr, nullptr))
            return 67;

        ControlMessage hello;
        juce::String error;
        if (! readMessage(pipe.get(), hello, 5000, error)
            || ! validateMessageEnvelope(hello, parsed.sessionToken, error)
            || hello.type != static_cast<std::uint16_t>(MessageType::Hello))
            return 68;

        const auto executablePath = resolveCurrentExecutablePath();
        const auto identity = executableIdentity(executablePath);
        ULONG serverProcessId = 0;
        if (! GetNamedPipeServerProcessId(pipe.get(), &serverProcessId)
            || hello.processId != serverProcessId
            || ExecutableIdentity { hello.executableVolumeSerial,
                                    hello.executableFileIndex } != identity)
            return 75;

        PluginWorkerHostedPluginCore hostedPlugin;
        PluginWorkerAudioTransportCore audioTransport;
        PluginWorkerAutomationTransportCore automationTransport;   // Phase E2A
        if (parsed.audioTransportRequested && ! audioTransport.open(parsed, error))
            return 76;
        if (parsed.automationTransportRequested
            && ! automationTransport.open(parsed.automationMappingName,
                                          parsed.sessionToken, error))
            return 84;

        const auto flags = static_cast<std::uint32_t>(MessageFlags::WorkerMode)
                         | (ApexProcessModeStateCore::normalGuiCreated()
                              ? static_cast<std::uint32_t>(MessageFlags::NormalGuiCreated)
                              : 0u)
                          | (parsed.refuseShutdownForTest
                               ? static_cast<std::uint32_t>(MessageFlags::RefusesShutdownForTest)
                               : 0u)
                          | (audioTransport.isOpen()
                               ? static_cast<std::uint32_t>(MessageFlags::AudioTransportReady)
                               : 0u);

        if (parsed.requestedProtocol != APEX_PLUGIN_SANDBOX_PROTOCOL_V1
            || hello.protocolVersion != APEX_PLUGIN_SANDBOX_PROTOCOL_V1)
        {
            const auto rejection = makeMessage(MessageType::HelloReject,
                                               APEX_PLUGIN_SANDBOX_PROTOCOL_V1,
                                               parsed.sessionToken,
                                               GetCurrentProcessId(), flags, identity);
            writeMessage(pipe.get(), rejection, 2000, error);
            return 69;
        }

        const auto acknowledgement = makeMessage(MessageType::HelloAck,
                                                  APEX_PLUGIN_SANDBOX_PROTOCOL_V1,
                                                  parsed.sessionToken,
                                                  GetCurrentProcessId(), flags, identity);
        if (! writeMessage(pipe.get(), acknowledgement, 2000, error))
            return 70;

        // The worker's JUCE MessageManager belongs to this thread. The
        // existing named-pipe/audio service loop is deliberately moved to a
        // separate service thread so native editor peers can receive Win32
        // and JUCE messages while control/audio IPC remains active.
        std::packaged_task<int()> serviceTask([&]() -> int
        {
        std::uint32_t idleLoops = 0;

        // ── Phase E1 helpers (control plane; run between audio quanta) ─────
        const auto e1Generation = [&]() noexcept -> std::uint64_t
        {
            return audioTransport.isOpen() ? audioTransport.generation() : 0;
        };

        const auto sendE1Payload = [&](E1PayloadPurpose purpose,
                                       const juce::MemoryBlock& data,
                                       juce::String& transportError) -> bool
        {
            const auto totalBytes = static_cast<std::uint32_t>(data.getSize());
            const auto totalChunks =
                (totalBytes + kE1ChunkPayloadBytes - 1) / kE1ChunkPayloadBytes;

            E1ControlMessage begin;
            makeE1Message(static_cast<std::uint16_t>(MessageType::E1PayloadBegin),
                          parsed.sessionToken, GetCurrentProcessId(),
                          e1Generation(), begin);
            begin.purpose = static_cast<std::uint32_t>(purpose);
            begin.totalBytes = totalBytes;
            begin.totalChunks = totalChunks;

            DWORD beginWriteError = 0;
            if (! writeFixedMessage(pipe.get(), begin, 2000, transportError,
                                    &beginWriteError))
            {
                audioTransport.recordDiagnosticCode(1000 + beginWriteError);
                return false;
            }

            for (std::uint32_t chunkIndex = 0; chunkIndex < totalChunks; ++chunkIndex)
            {
                E1ChunkMessage chunk;
                makeE1ChunkMessage(parsed.sessionToken, GetCurrentProcessId(),
                                   e1Generation(), chunk);
                chunk.purpose = static_cast<std::uint32_t>(purpose);
                chunk.totalBytes = totalBytes;
                chunk.totalChunks = totalChunks;
                chunk.chunkIndex = chunkIndex;
                chunk.payloadBytes = static_cast<std::uint32_t>(
                    juce::jmin<std::size_t>(kE1ChunkPayloadBytes,
                                            data.getSize()
                                                - static_cast<std::size_t>(chunkIndex)
                                                      * kE1ChunkPayloadBytes));
                std::memcpy(chunk.payload,
                            static_cast<const std::uint8_t*>(data.getData())
                                + static_cast<std::size_t>(chunkIndex)
                                      * kE1ChunkPayloadBytes,
                            chunk.payloadBytes);
                DWORD chunkWriteError = 0;
                if (! writeFixedMessage(pipe.get(), chunk, 2000, transportError,
                                        &chunkWriteError))
                {
                    audioTransport.recordDiagnosticCode(1000 + chunkWriteError);
                    return false;
                }

            }
            return true;
        };

        const auto receiveE1Payload = [&](const E1ControlMessage& begin,
                                          E1PayloadPurpose purpose,
                                          juce::MemoryBlock& data,
                                          juce::String& transportError) -> bool
        {
            if (begin.type != static_cast<std::uint16_t>(MessageType::StateSetBegin)
                || begin.purpose != static_cast<std::uint32_t>(purpose)
                || ! validE1PayloadBounds(begin.totalBytes, begin.totalChunks,
                                          transportError))
                return false;

            data.reset();
            juce::MemoryOutputStream stream(data, true);
            for (std::uint32_t i = 0; i < begin.totalChunks; ++i)
            {
                E1ChunkMessage chunk;
                if (! readFixedMessage(pipe.get(), chunk, 2000, transportError)
                    || ! validateE1ChunkEnvelope(chunk, parsed.sessionToken,
                                                 e1Generation(), serverProcessId,
                                                 transportError)
                    || chunk.purpose != static_cast<std::uint32_t>(purpose)
                    || ! validE1ChunkFields(chunk, transportError)
                    || chunk.chunkIndex != i
                    || chunk.totalBytes != begin.totalBytes
                    || chunk.totalChunks != begin.totalChunks)
                {
                    if (transportError.isEmpty())
                        transportError = "invalid E1 chunk stream";
                    return false;
                }
                stream.write(chunk.payload, static_cast<size_t>(chunk.payloadBytes));
            }
            return true;
        };

        for (;;)
        {
            ControlMessage request;
            const auto readResult = tryReadMessageNonBlocking(pipe.get(), request);
            if (readResult == TryReadMessageResult::Closed
                || readResult == TryReadMessageResult::Failed)
                return 71;

            if (readResult == TryReadMessageResult::ExtendedMessage)
            {
                // Peek the shared header (magic/layoutVersion/type) without
                // consuming, then dispatch E1 messages additively before the
                // frozen PluginCreate path.
                ControlMessage peeked {};
                DWORD peekedBytes = 0;
                if (! PeekNamedPipe(pipe.get(), &peeked, sizeof(peeked),
                                    &peekedBytes, nullptr, nullptr)
                    || peekedBytes < sizeof(peeked))
                    return 80;

                 const auto peekedType = static_cast<MessageType>(peeked.type);
                 if (peekedType == MessageType::EditorOpenSubmitRequest
                     || peekedType == MessageType::EditorStatusQueryRequest
                     || peekedType == MessageType::EditorCancelRequest)
                 {
                     EditorAsyncControlMessage editorRequest;
                     if (! readFixedMessage(pipe.get(), editorRequest, 2000, error)
                         || ! validateEditorAsyncMessageEnvelope(editorRequest,
                                                                  parsed.sessionToken,
                                                                  error)
                         || editorRequest.processId != serverProcessId
                         || parsed.workerGeneration == 0
                         || editorRequest.workerGeneration != parsed.workerGeneration)
                     {
                         return 88;
                     }

                     juce::String editorPluginInstanceId;
                     if (! readBoundedUtf8(editorRequest.pluginInstanceId,
                                           editorPluginInstanceId, error,
                                           "pluginInstanceId"))
                         return 88;

                     const auto requestType = static_cast<MessageType>(
                         editorRequest.type);
                     const auto responseType = requestType
                         == MessageType::EditorOpenSubmitRequest
                         ? MessageType::EditorOpenSubmitResponse
                         : requestType == MessageType::EditorStatusQueryRequest
                             ? MessageType::EditorStatusQueryResponse
                             : MessageType::EditorCancelResponse;

                     EditorAsyncControlMessage editorResponse;
                     if (! makeEditorAsyncMessage(responseType,
                                                  parsed.sessionToken,
                                                  editorPluginInstanceId,
                                                  GetCurrentProcessId(),
                                                  parsed.workerGeneration,
                                                  editorRequest.requestSequence,
                                                  editorResponse, error))
                         return 88;

                     PluginWorkerHostedPluginCore::AsyncEditorStatus status;
                     juce::String operationError;
                     bool operationSucceeded = false;
                     if (requestType == MessageType::EditorOpenSubmitRequest)
                     {
                         operationSucceeded = hostedPlugin.submitEditorOpenAsync(
                             editorPluginInstanceId,
                             editorRequest.requestSequence,
                            #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
                             parsed.editorCreateResponseDelayMilliseconds,
                            #else
                             0,
                            #endif
                             status, operationError);
                     }
                     else if (requestType == MessageType::EditorStatusQueryRequest)
                     {
                         operationSucceeded = hostedPlugin.queryEditorStatusAsync(
                             editorPluginInstanceId,
                             editorRequest.requestSequence,
                             status, operationError);
                     }
                     else
                     {
                         operationSucceeded = hostedPlugin.cancelEditorAsync(
                             editorPluginInstanceId,
                             editorRequest.requestSequence,
                             status, operationError);
                     }

                     editorResponse.success = operationSucceeded ? 1u : 0u;
                     editorResponse.status = static_cast<std::uint32_t>(
                         operationSucceeded
                             ? status.status
                             : EditorAsyncStatus::Rejected);
                     editorResponse.nativeWindowHandle =
                         status.info.nativeWindowHandle;
                     editorResponse.width = status.info.width;
                     editorResponse.height = status.info.height;
                     if (! operationSucceeded)
                     {
                         if (operationError.isEmpty())
                             operationError = status.error;
                         copyEditorAsyncError(operationError, editorResponse);
                     }
                     else if (status.error.isNotEmpty())
                     {
                         copyEditorAsyncError(status.error, editorResponse);
                     }

                     if (! writeFixedMessage(pipe.get(), editorResponse, 2000, error))
                         return 89;
                     continue;
                 }

                 if (peekedType == MessageType::EditorCreateRequest
                     || peekedType == MessageType::EditorResizeRequest
                     || peekedType == MessageType::EditorCloseRequest)
                {
                    EditorControlMessage editorRequest;
                    if (! readFixedMessage(pipe.get(), editorRequest, 2000, error)
                        || ! validateEditorMessageEnvelope(editorRequest,
                                                           parsed.sessionToken, error)
                        || editorRequest.processId != serverProcessId
                        || parsed.workerGeneration == 0
                        || editorRequest.workerGeneration != parsed.workerGeneration)
                    {
                        return 85;
                    }

                    juce::String editorPluginInstanceId;
                    if (! readBoundedUtf8(editorRequest.pluginInstanceId,
                                          editorPluginInstanceId, error,
                                          "pluginInstanceId"))
                        return 85;

                    const auto responseType =
                        editorRequest.type == static_cast<std::uint16_t>(
                            MessageType::EditorCreateRequest)
                            ? MessageType::EditorCreateResponse
                            : editorRequest.type == static_cast<std::uint16_t>(
                                  MessageType::EditorResizeRequest)
                                ? MessageType::EditorResizeResponse
                                : MessageType::EditorCloseResponse;
                    EditorControlMessage editorResponse;
                    if (! makeEditorMessage(responseType,
                                            parsed.sessionToken,
                                            editorPluginInstanceId,
                                            GetCurrentProcessId(),
                                            parsed.workerGeneration,
                                            editorRequest.requestSequence,
                                            editorRequest.nativeWindowHandle,
                                            editorRequest.width,
                                            editorRequest.height,
                                            editorResponse, error))
                        return 85;

                    juce::String operationError;
                    PluginWorkerHostedPluginCore::EditorInfo editorInfo;
                    bool operationSucceeded = false;
                    if (responseType == MessageType::EditorCreateResponse)
                    {
                        operationSucceeded = hostedPlugin.createEditorOnMessageThread(
                             editorPluginInstanceId, editorInfo,
                             static_cast<DWORD>(parsed.editorCreateTimeoutMs),
                             operationError
                            #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
                             , parsed.editorCreateResponseDelayMilliseconds
                            #endif
                         );
                    }
                    else if (responseType == MessageType::EditorResizeResponse)
                    {
                        operationSucceeded = hostedPlugin.resizeEditorOnMessageThread(
                            editorPluginInstanceId,
                            editorRequest.nativeWindowHandle,
                            editorRequest.width,
                             editorRequest.height,
                             editorInfo, 2000, operationError
                            #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
                             , parsed.editorResizeResponseDelayMilliseconds
                            #endif
                         );
                    }
                    else
                    {
                        operationSucceeded = hostedPlugin.closeEditorOnMessageThread(
                             editorPluginInstanceId,
                             editorRequest.nativeWindowHandle,
                             2000, operationError
                            #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
                             , parsed.editorCloseResponseDelayMilliseconds
                            #endif
                         );
                    }

                    editorResponse.success = operationSucceeded ? 1u : 0u;
                    if (operationSucceeded)
                    {
                        if (responseType != MessageType::EditorCloseResponse)
                        {
                            editorResponse.nativeWindowHandle =
                                editorInfo.nativeWindowHandle;
                            editorResponse.width = editorInfo.width;
                            editorResponse.height = editorInfo.height;
                        }
                    }
                    else
                    {
                        copyEditorError(operationError, editorResponse);
                    }

                    if (! writeFixedMessage(pipe.get(), editorResponse, 2000, error))
                        return 86;
                    continue;
                }

                if (peekedType >= MessageType::ParameterMetadataRequest
                    && peekedType <= MessageType::LiveValuesRequest)
                {
                    E1ControlMessage e1 {};
                    if (! readFixedMessage(pipe.get(), e1, 2000, error))
                    {
                        audioTransport.recordDiagnosticCode(101);
                        juce::Logger::writeToLog("[APEX-E1] worker read failure: " + error);
                        return 83;
                    }
                    if (! validateE1Envelope(e1, parsed.sessionToken,
                                             e1Generation(), serverProcessId, error))
                    {
                        audioTransport.recordDiagnosticCode(102);
                        juce::Logger::writeToLog("[APEX-E1] worker envelope failure: " + error
                            + " reqPid=" + juce::String(static_cast<int>(e1.processId))
                            + " serverPid=" + juce::String(static_cast<int>(serverProcessId))
                            + " reqGen=" + juce::String(static_cast<juce::int64>(e1.audioGeneration))
                            + " localGen=" + juce::String(static_cast<juce::int64>(e1Generation())));
                        return 83;
                    }

                    if (e1.type == static_cast<std::uint16_t>(
                            MessageType::ParameterMetadataRequest))
                    {
                        juce::MemoryBlock metadata;
                        juce::String operationError;
                        const bool ok = hostedPlugin.buildParameterMetadata(
                            metadata, operationError);
                        if (! ok)
                        {
                            audioTransport.recordDiagnosticCode(103);
                            juce::Logger::writeToLog("[APEX-E1] worker metadata failure: "
                                + operationError);
                            return 83;
                        }
                        if (! sendE1Payload(E1PayloadPurpose::ParameterMetadata,
                                            metadata, error))
                        {
                            audioTransport.recordDiagnosticCode(104);
                            juce::Logger::writeToLog("[APEX-E1] worker payload-send failure: "
                                + error);
                            return 83;
                        }
                        continue;
                    }

                    if (e1.type == static_cast<std::uint16_t>(
                            MessageType::StateGetRequest))
                    {
                        juce::MemoryBlock state;
                        juce::String operationError;
                        const bool ok = hostedPlugin.captureState(state, operationError);
                        if (! ok
                            || ! sendE1Payload(E1PayloadPurpose::StateGet,
                                               state, error))
                            return 83;
                        continue;
                    }

                    if (e1.type == static_cast<std::uint16_t>(
                            MessageType::ParameterSetRequest))
                    {
                        E1ControlMessage response;
                        makeE1Message(static_cast<std::uint16_t>(
                                          MessageType::ParameterSetResponse),
                                      parsed.sessionToken, GetCurrentProcessId(),
                                      e1Generation(), response);
                        juce::String operationError;
                        const juce::String parameterId =
                            juce::String::fromUTF8(e1.parameterId,
                                                   static_cast<int>(
                                                       std::strlen(e1.parameterId)));
                        float applied = 0.0f;
                        const bool ok = hostedPlugin.setParameter(
                            parameterId, e1.normalizedValue, applied, operationError);
                        response.ok = ok ? 1u : 0u;
                        response.appliedValue = applied;
                        if (! ok)
                            copyE1Error(operationError, response);
                        if (! writeFixedMessage(pipe.get(), response, 2000, error))
                            return 83;
                        continue;
                    }

                    if (e1.type == static_cast<std::uint16_t>(
                            MessageType::StateSetBegin))
                    {                        juce::MemoryBlock state;
                        juce::String operationError;
                        E1ControlMessage result;
                        makeE1Message(static_cast<std::uint16_t>(
                                          MessageType::StateSetResult),
                                      parsed.sessionToken, GetCurrentProcessId(),
                                      e1Generation(), result);
                        const bool received = receiveE1Payload(
                            e1, E1PayloadPurpose::StateSet, state, operationError);
                       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
                        const bool injectedRestoreFailure =
                            received && parsed.failStateRestoreForTest;
                        if (injectedRestoreFailure)
                            operationError = kPluginSandboxTestStateRestoreFailure;
                       #else
                        const bool injectedRestoreFailure = false;
                       #endif
                        const bool restored = received
                            && ! injectedRestoreFailure
                            && hostedPlugin.restoreState(
                                state.getData(), static_cast<int>(state.getSize()),
                                operationError);
                        result.ok = (received && restored) ? 1u : 0u;
                        if (result.ok == 0)
                            copyE1Error(operationError, result);
                        if (! writeFixedMessage(pipe.get(), result, 2000, error))
                            return 83;
                        continue;
                    }

                    // Phase E2B (control plane): live worker parameter values.
                    if (e1.type == static_cast<std::uint16_t>(
                            MessageType::LiveValuesRequest))
                    {
                        juce::MemoryBlock values;
                        juce::String operationError;
                        if (! hostedPlugin.readLiveParameterValues(values, operationError)
                            || ! sendE1Payload(E1PayloadPurpose::LiveValues,
                                               values, error))
                        {
                            audioTransport.recordDiagnosticCode(105);
                            juce::Logger::writeToLog("[APEX-E2B] worker live-values failure: "
                                + (operationError.isEmpty() ? error : operationError));
                            return 83;
                        }
                        continue;
                    }

                    return 83;   // unknown E1 type
                }

                PluginSandboxDiagnosticsCore::appendPluginCreatePhase(
                    parsed.sessionToken,
                    static_cast<std::uint32_t>(GetCurrentProcessId()),
                    0,
                    "PLUGINCREATE_REQUEST_READ_BEGIN");

                PluginCreateMessage pluginRequest;
                if (! readFixedMessage(pipe.get(), pluginRequest, 2000, error)
                    || ! validatePluginCreateEnvelope(pluginRequest,
                                                      parsed.sessionToken, error)
                    || pluginRequest.processId != serverProcessId
                                                      || ! audioTransport.isOpen()
                                                      || pluginRequest.audioGeneration != audioTransport.generation())
                    return 80;

                PluginSandboxDiagnosticsCore::appendPluginCreatePhase(
                    parsed.sessionToken,
                    static_cast<std::uint32_t>(GetCurrentProcessId()),
                    pluginRequest.audioGeneration,
                    "PLUGINCREATE_RECEIVED");

                PluginCreateResponse response;
                response.processId = GetCurrentProcessId();
                response.audioGeneration = audioTransport.generation();
                copyBoundedUtf8(parsed.sessionToken, response.sessionToken,
                                error, "sessionToken");

                juce::String creationError;
                const bool created = hostedPlugin.create(
                    pluginRequest,
                    creationError,
                   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
                    parsed.pluginCreateControlFaultPoint
                        == PluginSandboxControlTestFaultPoint::PluginCreateFailBeforeInstance
                   #else
                    false
                   #endif
                );
                response.type = static_cast<std::uint16_t>(created
                    ? MessageType::PluginCreateAck
                    : MessageType::PluginCreateReject);
                if (created)
                {
                    response.pluginLatencySamples = static_cast<std::uint32_t>(
                        hostedPlugin.latencySamples());
                    response.inputChannels = static_cast<std::uint32_t>(
                        hostedPlugin.inputChannels());
                    response.outputChannels = static_cast<std::uint32_t>(
                        hostedPlugin.outputChannels());
                    response.moduleLoadedInWorker = hostedPlugin.moduleLoadedInWorker() ? 1u : 0u;
                    response.uniqueId = hostedPlugin.uniqueId();
                    response.deprecatedUid = hostedPlugin.deprecatedUid();
                    audioTransport.setHostedPlugin(&hostedPlugin);
                    if (automationTransport.isOpen())
                        audioTransport.setAutomationSources(
                            &automationTransport,
                            hostedPlugin.parameterTargets(),
                            hostedPlugin.parameterTargetCount());   // Phase E2A
                }
                else
                {
                    copyBoundedUtf8(creationError, response.error,
                                    error, "pluginCreateError");
                }

                PluginSandboxDiagnosticsCore::appendPluginCreatePhase(
                    parsed.sessionToken,
                    static_cast<std::uint32_t>(GetCurrentProcessId()),
                    pluginRequest.audioGeneration,
                    "PLUGINCREATE_RESPONSE_PREPARED",
                    created ? "ack" : "reject");
               #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
                if (parsed.pluginCreateResponseDelayMilliseconds > 0)
                    Sleep(parsed.pluginCreateResponseDelayMilliseconds);
               #endif
                PluginSandboxDiagnosticsCore::appendPluginCreatePhase(
                    parsed.sessionToken,
                    static_cast<std::uint32_t>(GetCurrentProcessId()),
                    pluginRequest.audioGeneration,
                    "PLUGINCREATE_RESPONSE_SEND_BEGIN");
                if (! writeFixedMessage(pipe.get(), response, 2000, error))
                {
                    PluginSandboxDiagnosticsCore::appendPluginCreatePhase(
                        parsed.sessionToken,
                        static_cast<std::uint32_t>(GetCurrentProcessId()),
                        pluginRequest.audioGeneration,
                        "PLUGINCREATE_RESPONSE_SEND_FAILURE", error);
                    return 81;
                }
                PluginSandboxDiagnosticsCore::appendPluginCreatePhase(
                    parsed.sessionToken,
                    static_cast<std::uint32_t>(GetCurrentProcessId()),
                    pluginRequest.audioGeneration,
                    "PLUGINCREATE_RESPONSE_SEND_COMPLETE");
                if (! created)
                    return 82;
                continue;
            }

            if (readResult == TryReadMessageResult::Message)
            {
                if (! validateMessageEnvelope(request, parsed.sessionToken, error)
                    || request.processId != serverProcessId
                    || ExecutableIdentity { request.executableVolumeSerial,
                                            request.executableFileIndex } != identity)
                    return 72;

                const auto type = static_cast<MessageType>(request.type);
                if (type == MessageType::AudioStop && audioTransport.isOpen())
                {
                    audioTransport.stop();
                    hostedPlugin.releaseResources();
                    const auto ack = makeMessage(MessageType::AudioStopAck,
                                                 APEX_PLUGIN_SANDBOX_PROTOCOL_V1,
                                                 parsed.sessionToken,
                                                 GetCurrentProcessId(), flags, identity);
                    if (! writeMessage(pipe.get(), ack, 2000, error))
                        return 77;
                    continue;
                }

                if (type == MessageType::AudioStart && audioTransport.isOpen())
                {
                    if (! audioTransport.start()
                        || (hostedPlugin.inputChannels() > 0
                            && ! hostedPlugin.reprepare(audioTransport.sampleRate(),
                                                       audioTransport.maximumBlockSamples(),
                                                       audioTransport.inputChannels(),
                                                       audioTransport.outputChannels(),
                                                       audioTransport.generation(), error)))
                        return 78;
                    const auto ack = makeMessage(MessageType::AudioStartAck,
                                                 APEX_PLUGIN_SANDBOX_PROTOCOL_V1,
                                                 parsed.sessionToken,
                                                 GetCurrentProcessId(), flags, identity);
                    if (! writeMessage(pipe.get(), ack, 2000, error))
                        return 79;
                    continue;
                }

                if (type != MessageType::Shutdown)
                    return 73;

                if (parsed.refuseShutdownForTest)
                {
                    for (;;)
                        Sleep(1000);
                }

                juce::String editorShutdownError;
                if (! hostedPlugin.closeEditorForShutdown(2000, editorShutdownError))
                    return 87;
                audioTransport.stop();
                audioTransport.close();
                hostedPlugin.close();
                const auto shutdownAck = makeMessage(MessageType::ShutdownAck,
                                                      APEX_PLUGIN_SANDBOX_PROTOCOL_V1,
                                                      parsed.sessionToken,
                                                      GetCurrentProcessId(), flags, identity);
                return writeMessage(pipe.get(), shutdownAck, 2000, error) ? 0 : 74;
            }

            const bool processed = audioTransport.processOneAvailable();
            if ((++idleLoops & 0x3ffu) == 0)
                audioTransport.heartbeat();
            if (! processed)
                Sleep(1);
        }
        });

        auto serviceFuture = serviceTask.get_future();
        std::thread serviceThread(std::move(serviceTask));
        while (serviceFuture.wait_for(std::chrono::milliseconds(0))
                   != std::future_status::ready)
        {
            // JUCE 8 builds used by APEX disable modal-loop support, so
            // MessageManager::runDispatchLoopUntil() is not available here.
            // The worker main thread is already JUCE's message thread; pump
            // the native queue directly so JUCE's hidden callback window and
            // the worker editor peer continue receiving messages.
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                    PostQuitMessage(static_cast<int>(message.wParam));
                else
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            Sleep(1);
        }
        const auto serviceResult = serviceFuture.get();
        serviceThread.join();
        return serviceResult;
       #else
        juce::ignoreUnused(arguments);
        return 63;
       #endif
    }
};

} // namespace DAW
