#pragma once

#include <JuceHeader.h>

#include <cstdint>
#include <atomic>
#include <cstring>
#include <string>
#include "PluginSandboxPreparationCore.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX   // keep windows.h min/max macros from poisoning std::min/std::max
 #endif
 #include <windows.h>
#endif

namespace DAW {

inline constexpr std::uint32_t APEX_PLUGIN_SANDBOX_PROTOCOL_V1 = 1;

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
inline constexpr const char* kPluginSandboxTestStateRestoreFailure =
    "test-injected E1 state restore failure";
#endif

enum class ApexProcessMode
{
    NormalApex,
    PluginWorker,
    PluginSandboxSelfTest
};

enum class PluginSandboxAudioTestDspMode : std::uint32_t
{
    Identity = 1,
    GainHalf = 2,
    GainQuarter = 3,
    ChannelMarker = 4,
    DeadlineIdentity = 5
};

class ApexProcessModeStateCore
{
public:
    static void setProcessMode(ApexProcessMode mode) noexcept
    {
        mode_.store(mode, std::memory_order_release);
        normalGuiCreated_.store(false, std::memory_order_release);
    }

    static ApexProcessMode processMode() noexcept
    {
        return mode_.load(std::memory_order_acquire);
    }

    static void markNormalGuiCreated() noexcept
    {
        normalGuiCreated_.store(true, std::memory_order_release);
    }

    static bool normalGuiCreated() noexcept
    {
        return normalGuiCreated_.load(std::memory_order_acquire);
    }

private:
    inline static std::atomic<ApexProcessMode> mode_ { ApexProcessMode::NormalApex };
    inline static std::atomic<bool> normalGuiCreated_ { false };
};

struct PluginWorkerCommandLine
{
    bool valid = false;
    juce::String error;
    juce::String ipcEndpoint;
    juce::String sessionToken;
    std::uint32_t requestedProtocol = 0;
    std::uint64_t workerGeneration = 0;
    std::uint32_t editorCreateTimeoutMs = kPluginSandboxEditorCreateTimeoutMs;
    bool refuseShutdownForTest = false;
    bool audioTransportRequested = false;
    juce::String audioMappingName;
    bool automationTransportRequested = false;   // Phase E2A (additive)
    juce::String automationMappingName;          // Phase E2A (additive)
    PluginSandboxAudioTestDspMode audioDspMode = PluginSandboxAudioTestDspMode::Identity;
    std::uint64_t audioDelaySequence = 0;
    DWORD audioDelayMilliseconds = 0;
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
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

struct PluginSandboxEnumOnlyCommandLine
{
    bool requested = false;
    bool valid = false;
    juce::String error;
    juce::String pluginPath;
    std::uint32_t ceilingMilliseconds = 120000;
};

class PluginSandboxCommandLineCore
{
public:
    static constexpr const char* kWorkerFlag = "--apex-plugin-worker";
    static constexpr const char* kSelfTestFlag = "--apex-plugin-sandbox-self-test";
    static constexpr const char* kVst3DiagnosticFlag = "--apex-vst3-enum-diagnostic";
    static constexpr const char* kEnumOnlyFlag = "--apex-enum-only";
    static constexpr const char* kEnumCeilingFlag = "--apex-enum-ceiling-ms";
    static constexpr std::uint32_t kDefaultEnumOnlyCeilingMilliseconds = 120000;
    static constexpr std::uint32_t kMaximumEnumOnlyCeilingMilliseconds = 600000;

    static ApexProcessMode resolveProcessMode(const juce::StringArray& arguments) noexcept
    {
        if (arguments.contains(kWorkerFlag, false))
            return ApexProcessMode::PluginWorker;
        if (arguments.contains(kSelfTestFlag, false))
            return ApexProcessMode::PluginSandboxSelfTest;
        return ApexProcessMode::NormalApex;
    }

    static PluginWorkerCommandLine parseWorker(const juce::StringArray& arguments)
    {
        PluginWorkerCommandLine result;
        if (resolveProcessMode(arguments) != ApexProcessMode::PluginWorker)
        {
            result.error = "worker mode token is absent";
            return result;
        }

        bool duplicate = false;
        result.ipcEndpoint = getSingleValue(arguments, "--ipc", duplicate);
        if (duplicate)
        {
            result.error = "duplicate --ipc argument";
            return result;
        }

        result.sessionToken = getSingleValue(arguments, "--session", duplicate);
        if (duplicate)
        {
            result.error = "duplicate --session argument";
            return result;
        }

        const auto protocolText = getSingleValue(arguments, "--protocol", duplicate);
        if (duplicate)
        {
            result.error = "duplicate --protocol argument";
            return result;
        }

        result.refuseShutdownForTest = arguments.contains("--test-refuse-shutdown", false);

        const auto workerGeneration = getSingleValue(arguments,
                                                     "--worker-generation",
                                                     duplicate);
        if (duplicate)
        {
            result.error = "duplicate worker generation argument";
            return result;
        }
        if (workerGeneration.isNotEmpty())
        {
            const auto parsedGeneration = workerGeneration.getLargeIntValue();
            if (workerGeneration.retainCharacters("0123456789") != workerGeneration
                || parsedGeneration <= 0)
            {
                result.error = "invalid worker generation";
                return result;
            }
            result.workerGeneration = static_cast<std::uint64_t>(parsedGeneration);
        }

        const auto editorCreateTimeout = getSingleValue(
            arguments, "--editor-create-timeout-ms", duplicate);
        if (duplicate)
        {
            result.error = "duplicate EditorCreate timeout argument";
            return result;
        }
        if (editorCreateTimeout.isNotEmpty())
        {
            const auto parsedTimeout = editorCreateTimeout.getIntValue();
            if (editorCreateTimeout.retainCharacters("0123456789")
                    != editorCreateTimeout
                || parsedTimeout <= 0
                || parsedTimeout > static_cast<int>(kPluginSandboxEditorCreateTimeoutMs))
            {
                result.error = "invalid EditorCreate timeout";
                return result;
            }
            result.editorCreateTimeoutMs = static_cast<std::uint32_t>(parsedTimeout);
        }

       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
         const auto pluginCreateResponseDelay = getSingleValue(
             arguments, "--test-plugin-create-response-delay-ms", duplicate);
        if (duplicate)
        {
            result.error = "duplicate PluginCreate response-delay argument";
            return result;
        }
        if (pluginCreateResponseDelay.isNotEmpty())
        {
            const auto parsedDelay = pluginCreateResponseDelay.getIntValue();
            if (pluginCreateResponseDelay.retainCharacters("0123456789")
                    != pluginCreateResponseDelay
                || parsedDelay <= 0 || parsedDelay > 60000)
            {
                result.error = "invalid PluginCreate response delay";
                return result;
            }
            result.pluginCreateResponseDelayMilliseconds =
                static_cast<std::uint32_t>(parsedDelay);
        }
        const auto workerStartupDelay = getSingleValue(
            arguments, "--test-worker-startup-delay-ms", duplicate);
        if (duplicate)
        {
            result.error = "duplicate worker startup-delay argument";
            return result;
        }
         if (workerStartupDelay.isNotEmpty())
        {
            const auto parsedDelay = workerStartupDelay.getIntValue();
            if (workerStartupDelay.retainCharacters("0123456789")
                    != workerStartupDelay
                || parsedDelay <= 0 || parsedDelay > 60000)
            {
                result.error = "invalid worker startup delay";
                return result;
            }
             result.workerStartupDelayMilliseconds =
                 static_cast<std::uint32_t>(parsedDelay);
         }

         const auto editorCreateResponseDelay = getSingleValue(
             arguments, "--test-editor-create-response-delay-ms", duplicate);
         if (duplicate)
         {
             result.error = "duplicate EditorCreate response-delay argument";
             return result;
         }
         if (editorCreateResponseDelay.isNotEmpty())
         {
             const auto parsedDelay = editorCreateResponseDelay.getIntValue();
             if (editorCreateResponseDelay.retainCharacters("0123456789")
                     != editorCreateResponseDelay
                 || parsedDelay <= 0 || parsedDelay > 60000)
             {
                 result.error = "invalid EditorCreate response delay";
                 return result;
             }
             result.editorCreateResponseDelayMilliseconds =
                 static_cast<std::uint32_t>(parsedDelay);
         }

         const auto editorResizeResponseDelay = getSingleValue(
             arguments, "--test-editor-resize-response-delay-ms", duplicate);
         if (duplicate)
         {
             result.error = "duplicate EditorResize response-delay argument";
             return result;
         }
         if (editorResizeResponseDelay.isNotEmpty())
         {
             const auto parsedDelay = editorResizeResponseDelay.getIntValue();
             if (editorResizeResponseDelay.retainCharacters("0123456789")
                     != editorResizeResponseDelay
                 || parsedDelay <= 0 || parsedDelay > 60000)
             {
                 result.error = "invalid EditorResize response delay";
                 return result;
             }
             result.editorResizeResponseDelayMilliseconds =
                 static_cast<std::uint32_t>(parsedDelay);
         }

         const auto editorCloseResponseDelay = getSingleValue(
             arguments, "--test-editor-close-response-delay-ms", duplicate);
         if (duplicate)
         {
             result.error = "duplicate EditorClose response-delay argument";
             return result;
         }
         if (editorCloseResponseDelay.isNotEmpty())
         {
             const auto parsedDelay = editorCloseResponseDelay.getIntValue();
             if (editorCloseResponseDelay.retainCharacters("0123456789")
                     != editorCloseResponseDelay
                 || parsedDelay <= 0 || parsedDelay > 60000)
             {
                 result.error = "invalid EditorClose response delay";
                 return result;
             }
             result.editorCloseResponseDelayMilliseconds =
                 static_cast<std::uint32_t>(parsedDelay);
         }
         if (arguments.contains("--test-plugin-create-fail-before-instance", false))
            result.pluginCreateControlFaultPoint =
                PluginSandboxControlTestFaultPoint::PluginCreateFailBeforeInstance;
       #endif

        if (result.sessionToken.length() != 32
            || result.sessionToken.retainCharacters("0123456789abcdefABCDEF") != result.sessionToken)
        {
            result.error = "invalid worker session token";
            return result;
        }
        if (result.ipcEndpoint != "\\\\.\\pipe\\APEX.PluginSandbox."
                                    + result.sessionToken)
        {
            result.error = "IPC endpoint does not match the worker session token";
            return result;
        }
        if (protocolText.isEmpty()
            || protocolText.retainCharacters("0123456789") != protocolText)
        {
            result.error = "invalid protocol version";
            return result;
        }

        const auto parsedProtocol = protocolText.getLargeIntValue();
        if (parsedProtocol <= 0 || parsedProtocol > 0xffffffffLL)
        {
            result.error = "protocol version is out of range";
            return result;
        }

        result.requestedProtocol = static_cast<std::uint32_t>(parsedProtocol);

        result.audioMappingName = getSingleValue(arguments, "--audio-shm", duplicate);
        if (duplicate)
        {
            result.error = "duplicate --audio-shm argument";
            return result;
        }
        result.audioTransportRequested = result.audioMappingName.isNotEmpty();
        if (result.audioTransportRequested)
        {
            const auto expectedMapping = "Local\\APEX.PluginSandbox.Audio."
                                       + result.sessionToken;
            if (result.audioMappingName != expectedMapping)
            {
                result.error = "audio mapping name does not match the worker session token";
                return result;
            }

            const auto dspMode = getSingleValue(arguments, "--audio-dsp", duplicate);
            if (duplicate)
            {
                result.error = "duplicate --audio-dsp argument";
                return result;
            }
            if (dspMode.isEmpty() || dspMode == "identity")
                result.audioDspMode = PluginSandboxAudioTestDspMode::Identity;
            else if (dspMode == "gain-half")
                result.audioDspMode = PluginSandboxAudioTestDspMode::GainHalf;
            else if (dspMode == "gain-quarter")
                result.audioDspMode = PluginSandboxAudioTestDspMode::GainQuarter;
            else if (dspMode == "channel-marker")
                result.audioDspMode = PluginSandboxAudioTestDspMode::ChannelMarker;
            else if (dspMode == "deadline-identity")
                result.audioDspMode = PluginSandboxAudioTestDspMode::DeadlineIdentity;
            else
            {
                result.error = "invalid Phase B deterministic DSP mode";
                return result;
            }

            const auto delaySequence = getSingleValue(arguments, "--audio-delay-sequence", duplicate);
            if (duplicate)
            {
                result.error = "duplicate --audio-delay-sequence argument";
                return result;
            }
            const auto delayMilliseconds = getSingleValue(arguments, "--audio-delay-ms", duplicate);
            if (duplicate)
            {
                result.error = "duplicate --audio-delay-ms argument";
                return result;
            }
            if (delaySequence.isNotEmpty())
            {
                if (delaySequence.retainCharacters("0123456789") != delaySequence
                    || delaySequence.getLargeIntValue() <= 0)
                {
                    result.error = "invalid audio delay sequence";
                    return result;
                }
                result.audioDelaySequence =
                    static_cast<std::uint64_t>(delaySequence.getLargeIntValue());
            }
            if (delayMilliseconds.isNotEmpty())
            {
                const auto parsedDelay = delayMilliseconds.getIntValue();
                if (delayMilliseconds.retainCharacters("0123456789") != delayMilliseconds
                    || parsedDelay <= 0 || parsedDelay > 60000)
                {
                    result.error = "invalid audio delay duration";
                    return result;
                }
                result.audioDelayMilliseconds = static_cast<DWORD>(parsedDelay);
            }

           #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
            const auto faultPoint = getSingleValue(arguments,
                                                   "--audio-fault-point",
                                                   duplicate);
            if (duplicate)
            {
                result.error = "duplicate --audio-fault-point argument";
                return result;
            }
            if (faultPoint.isNotEmpty())
            {
                if (faultPoint == "after-submit-before-automation-apply")
                    result.audioFaultPoint =
                        PluginSandboxAudioTestFaultPoint::AfterAudioSubmitBeforeAutomationApply;
                else if (faultPoint == "after-automation-apply-before-worker-commit")
                    result.audioFaultPoint =
                        PluginSandboxAudioTestFaultPoint::AfterAutomationApplyBeforeWorkerCommit;
                else
                {
                    result.error = "invalid Phase E2C audio fault point";
                    return result;
                }
            }
           #endif
        }

        // Phase E2A automation sidecar (additive; only present when enabled).
        result.automationMappingName = getSingleValue(arguments, "--automation-shm", duplicate);
        if (duplicate)
        {
            result.error = "duplicate --automation-shm argument";
            return result;
        }
        result.automationTransportRequested = result.automationMappingName.isNotEmpty();
        if (result.automationTransportRequested)
        {
            const auto expectedAutomationMapping = "Local\\APEX.PluginSandbox.Automation."
                                                 + result.sessionToken;
            if (result.automationMappingName != expectedAutomationMapping)
            {
                result.error = "automation mapping name does not match the worker session token";
                return result;
            }
            if (! result.audioTransportRequested)
            {
                result.error = "automation transport requires the audio transport";
                return result;
            }
        }

       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        result.failStateRestoreForTest =
            arguments.contains("--test-fail-state-restore", false);
       #endif

        result.valid = true;
        return result;
    }

    static PluginSandboxEnumOnlyCommandLine parseEnumOnly(
        const juce::StringArray& arguments)
    {
        PluginSandboxEnumOnlyCommandLine result;
        result.ceilingMilliseconds = kDefaultEnumOnlyCeilingMilliseconds;

        result.requested = arguments.contains(kWorkerFlag, false)
                        && hasOption(arguments, kEnumOnlyFlag);
        if (! result.requested)
            return result;

        if (! arguments.contains(kVst3DiagnosticFlag, false))
        {
            result.error = "enum-only mode requires --apex-vst3-enum-diagnostic";
            return result;
        }

        bool duplicate = false;
        result.pluginPath = getSingleValue(arguments, kEnumOnlyFlag, duplicate);
        if (duplicate)
        {
            result.error = "duplicate --apex-enum-only argument";
            return result;
        }

        if (result.pluginPath.isEmpty() || result.pluginPath.startsWith("--"))
        {
            result.error = "enum-only mode requires a VST3 path after --apex-enum-only";
            return result;
        }

        const auto ceilingText = getSingleValue(arguments, kEnumCeilingFlag, duplicate);
        if (duplicate)
        {
            result.error = "duplicate --apex-enum-ceiling-ms argument";
            return result;
        }

        if (ceilingText.isNotEmpty())
        {
            const auto parsedCeiling = ceilingText.getLargeIntValue();
            if (ceilingText.retainCharacters("0123456789") != ceilingText
                || parsedCeiling <= 0
                || parsedCeiling > kMaximumEnumOnlyCeilingMilliseconds)
            {
                result.error = "invalid enum-only diagnostic ceiling";
                return result;
            }
            result.ceilingMilliseconds = static_cast<std::uint32_t>(parsedCeiling);
        }

        result.valid = true;
        return result;
    }

    static juce::String getSingleValue(const juce::StringArray& arguments,
                                       const juce::String& key,
                                       bool& duplicate)
    {
        duplicate = false;
        juce::String result;
        int matches = 0;

        for (int i = 0; i < arguments.size(); ++i)
        {
            const auto& argument = arguments.getReference(i);
            if (argument == key)
            {
                ++matches;
                if (i + 1 < arguments.size())
                    result = arguments.getReference(i + 1);
            }
            else if (argument.startsWith(key + "="))
            {
                ++matches;
                result = argument.substring(key.length() + 1);
            }
        }

        duplicate = matches > 1;
        return matches == 1 ? result : juce::String();
    }

private:
    static bool hasOption(const juce::StringArray& arguments,
                          const juce::String& key)
    {
        for (const auto& argument : arguments)
            if (argument == key || argument.startsWith(key + "="))
                return true;

        return false;
    }
};

#if JUCE_WINDOWS
namespace PluginSandboxWin32
{
inline constexpr std::uint32_t kMessageMagic = 0x41505342; // "APSB"
inline constexpr std::uint16_t kMessageLayoutVersion = 1;
inline constexpr DWORD kForcedTerminationExitCode = 0xE17F0001u;
#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
inline constexpr DWORD kE2CTestAfterAudioSubmitExitCode = 0xE2C00002u;
inline constexpr DWORD kE2CTestAfterAutomationApplyExitCode = 0xE2C00003u;
#endif

enum class MessageType : std::uint16_t
{
    Hello = 1,
    HelloAck = 2,
    HelloReject = 3,
    Shutdown = 4,
    ShutdownAck = 5,
    AudioStop = 6,
    AudioStopAck = 7,
    AudioStart = 8,
    AudioStartAck = 9,
    PluginCreate = 10,
    PluginCreateAck = 11,
    PluginCreateReject = 12,

    // Phase E1 (control plane only) — additive on protocol V1.
    ParameterMetadataRequest = 13,
    ParameterSetRequest = 14,
    ParameterSetResponse = 15,
    StateGetRequest = 16,
    StateSetBegin = 17,
    StateSetResult = 18,
    E1PayloadBegin = 19,
    E1PayloadChunk = 20,

    // Phase E2B (control plane only) - additive on protocol V1.
    LiveValuesRequest = 21,
    LiveValuesResponse = 22,

    // Phase F1 editor lifecycle (control plane only).  The editor never
    // crosses the audio/shared-memory path; only bounded metadata crosses
    // this pipe.
    EditorCreateRequest = 23,
    EditorCreateResponse = 24,
    EditorResizeRequest = 25,
    EditorResizeResponse = 26,
    EditorCloseRequest = 27,
    EditorCloseResponse = 28,

    // Phase F3: two-phase, non-blocking editor construction.  The submit,
    // status, and cancel transactions carry only bounded metadata; vendor GUI
    // work never runs as part of the request/response transaction.
    EditorOpenSubmitRequest = 29,
    EditorOpenSubmitResponse = 30,
    EditorStatusQueryRequest = 31,
    EditorStatusQueryResponse = 32,
    EditorCancelRequest = 33,
    EditorCancelResponse = 34
};

enum class EditorAsyncStatus : std::uint32_t
{
    Rejected = 0,
    Accepted = 1,
    AlreadyCreating = 2,
    AlreadyReady = 3,
    Creating = 4,
    Slow = 5,
    Ready = 6,
    Failed = 7,
    CancelPending = 8,
    Cancelled = 9,
    Stale = 10,
    WorkerGone = 11,
    Closed = 12
};

enum MessageFlags : std::uint32_t
{
    WorkerMode = 1u << 0,
    NormalGuiCreated = 1u << 1,
    RefusesShutdownForTest = 1u << 2,
    AudioTransportReady = 1u << 3
};

#pragma pack(push, 1)
struct ControlMessage
{
    std::uint32_t magic = kMessageMagic;
    std::uint16_t layoutVersion = kMessageLayoutVersion;
    std::uint16_t type = 0;
    std::uint32_t byteSize = 0;
    std::uint32_t protocolVersion = 0;
    std::uint32_t processId = 0;
    std::uint32_t flags = 0;
    std::uint32_t executableVolumeSerial = 0;
    std::uint64_t executableFileIndex = 0;
    char sessionToken[33] = {};
};
#pragma pack(pop)

static_assert(sizeof(ControlMessage) <= 128, "Phase A control messages must remain bounded");

// Phase C extends the frozen Phase A envelope with bounded, versioned
// lifecycle records. Audio never travels through this pipe.
inline constexpr std::uint16_t kPluginMessageLayoutVersion = 1;
inline constexpr std::size_t kPluginPathBytes = 2048;
inline constexpr std::size_t kPluginNameBytes = 256;
inline constexpr std::size_t kPluginManufacturerBytes = 256;
inline constexpr std::size_t kPluginVersionBytes = 64;
inline constexpr std::size_t kPluginFormatBytes = 32;
inline constexpr std::size_t kPluginInstanceIdBytes = 96;
inline constexpr std::size_t kPluginErrorBytes = 512;
inline constexpr std::size_t kEditorErrorBytes = 256;

#pragma pack(push, 1)
struct PluginCreateMessage
{
    std::uint32_t magic = kMessageMagic;
    std::uint16_t layoutVersion = kPluginMessageLayoutVersion;
    std::uint16_t type = static_cast<std::uint16_t>(MessageType::PluginCreate);
    std::uint32_t byteSize = sizeof(PluginCreateMessage);
    std::uint32_t protocolVersion = APEX_PLUGIN_SANDBOX_PROTOCOL_V1;
    std::uint32_t processId = 0;
    std::uint64_t audioGeneration = 0;
    double sampleRate = 0.0;
    std::uint32_t maximumBlockSamples = 0;
    std::uint32_t inputChannels = 0;
    std::uint32_t outputChannels = 0;
    std::int32_t uniqueId = 0;
    std::int32_t deprecatedUid = 0;
    char sessionToken[33] = {};
    char fileOrIdentifier[kPluginPathBytes] = {};
    char name[kPluginNameBytes] = {};
    char manufacturer[kPluginManufacturerBytes] = {};
    char version[kPluginVersionBytes] = {};
    char pluginFormat[kPluginFormatBytes] = {};
};

struct PluginCreateResponse
{
    std::uint32_t magic = kMessageMagic;
    std::uint16_t layoutVersion = kPluginMessageLayoutVersion;
    std::uint16_t type = static_cast<std::uint16_t>(MessageType::PluginCreateReject);
    std::uint32_t byteSize = sizeof(PluginCreateResponse);
    std::uint32_t protocolVersion = APEX_PLUGIN_SANDBOX_PROTOCOL_V1;
    std::uint32_t processId = 0;
    std::uint64_t audioGeneration = 0;
    std::uint32_t pluginLatencySamples = 0;
    std::uint32_t inputChannels = 0;
    std::uint32_t outputChannels = 0;
    std::uint32_t moduleLoadedInWorker = 0;
    std::int32_t uniqueId = 0;
    std::int32_t deprecatedUid = 0;
    char sessionToken[33] = {};
    char error[kPluginErrorBytes] = {};
};
#pragma pack(pop)

static_assert(sizeof(PluginCreateMessage) <= 4096,
              "Phase C plugin-create metadata must remain bounded");
static_assert(sizeof(PluginCreateResponse) <= 1024,
              "Phase C plugin-create response must remain bounded");
inline constexpr DWORD kMaximumControlMessageBytes = 4096;

/** Fixed-width Phase F1 editor lifecycle message.  nativeWindowHandle is a
    serialized HWND value only; it is never dereferenced or treated as a
    pointer across the process boundary.  Requests and responses share this
    bounded record so the existing named-pipe framing remains unchanged. */
struct EditorControlMessage
{
    std::uint32_t magic = kMessageMagic;
    std::uint16_t layoutVersion = kPluginMessageLayoutVersion;
    std::uint16_t type = 0;
    std::uint32_t byteSize = sizeof(EditorControlMessage);
    std::uint32_t protocolVersion = APEX_PLUGIN_SANDBOX_PROTOCOL_V1;
    std::uint32_t processId = 0;
    std::uint64_t workerGeneration = 0;
    std::uint64_t requestSequence = 0;
    std::uint64_t nativeWindowHandle = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t success = 0;
    char sessionToken[33] = {};
    char pluginInstanceId[kPluginInstanceIdBytes] = {};
    char error[kEditorErrorBytes] = {};
};

static_assert(sizeof(EditorControlMessage) <= 1024,
              "Phase F1 editor messages must remain bounded");

/** Phase F3 bounded asynchronous editor control message.  A submit or
    status/cancel transaction returns only an acknowledgement/snapshot.  The
    worker GUI lane performs vendor construction after the submit response. */
struct EditorAsyncControlMessage
{
    std::uint32_t magic = kMessageMagic;
    std::uint16_t layoutVersion = kPluginMessageLayoutVersion;
    std::uint16_t type = 0;
    std::uint32_t byteSize = sizeof(EditorAsyncControlMessage);
    std::uint32_t protocolVersion = APEX_PLUGIN_SANDBOX_PROTOCOL_V1;
    std::uint32_t processId = 0;
    std::uint64_t workerGeneration = 0;
    std::uint64_t requestSequence = 0;
    std::uint64_t nativeWindowHandle = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t success = 0;
    std::uint32_t status = static_cast<std::uint32_t>(EditorAsyncStatus::Rejected);
    char sessionToken[33] = {};
    char pluginInstanceId[kPluginInstanceIdBytes] = {};
    char error[kEditorErrorBytes] = {};
};

static_assert(sizeof(EditorAsyncControlMessage) <= 1024,
              "Phase F3 async editor messages must remain bounded");

// ═══════════════════════════════════════════════════════════════════════════
// PHASE E1 — static parameter control + state-chunk transport (CONTROL PLANE
// ONLY). Extends the frozen Phase A/C envelope additively: new message types
// on protocol V1, a SMALL fixed-size E1 control envelope for every operation,
// and a separate SMALL chunk message for bulk payloads (plugin state and
// parameter metadata).
//
// Size discipline: the frozen Phase A pipe is a nonblocking MESSAGE pipe, and
// direct evidence (WriteFile GetLastError == ERROR_INVALID_FUNCTION) proved
// that large fixed envelopes fail worker→parent while the proven ~600-byte
// messages succeed. Both E1 structs are therefore kept in the proven-working
// size class (~300 bytes), with bulk data carried only in 256-byte chunks.
//
// - No pointer values or host addresses cross the pipe.
// - Every payload is length-validated before reading; total payloads are
//   bounded by kE1MaximumPayloadBytes (1 MiB) — never allocated from an
//   untrusted plugin-provided length.
// - Audio never travels through this pipe (Phase B shared memory is frozen).
// ═══════════════════════════════════════════════════════════════════════════

inline constexpr std::uint16_t kE1MessageLayoutVersion = 1;
inline constexpr std::size_t kE1ChunkPayloadBytes = 256;
inline constexpr std::uint32_t kE1MaximumPayloadBytes = 1024 * 1024;
inline constexpr std::uint32_t kE1MaximumChunks =
    kE1MaximumPayloadBytes / kE1ChunkPayloadBytes;   // 4096
inline constexpr std::uint32_t kE1MaximumParameters = 256;
inline constexpr std::size_t kE1ParameterIdBytes = 64;
inline constexpr std::size_t kE1ErrorBytes = 128;

enum class E1PayloadPurpose : std::uint32_t
{
    None = 0,
    ParameterMetadata = 1,
    StateGet = 2,
    StateSet = 3,
    LiveValues = 4   // Phase E2B — additive live worker parameter values
};

// Phase E2B live-value payload (carried in E1 chunks): uint32 count followed
// by count × { uint32 index; float normalizedValue } — 1028 bytes maximum for
// the frozen 128-ordinal worker automation target space.
inline constexpr std::uint32_t kE2BMaximumLiveValueEntries = 128;
inline constexpr std::uint32_t kE2BMaximumLiveValuesPayloadBytes =
    4 + kE2BMaximumLiveValueEntries * 8;
static_assert(kE2BMaximumLiveValuesPayloadBytes <= 4096,
              "E2B live-value payload must remain bounded");

/** Small fixed E1 control envelope: requests, begin markers, parameter
    set/response, and state-set results. No bulk payload. */
struct E1ControlMessage
{
    std::uint32_t magic = kMessageMagic;
    std::uint16_t layoutVersion = kE1MessageLayoutVersion;
    std::uint16_t type = 0;                      // E1 MessageType values
    std::uint32_t byteSize = sizeof(E1ControlMessage);
    std::uint32_t protocolVersion = APEX_PLUGIN_SANDBOX_PROTOCOL_V1;
    std::uint32_t processId = 0;
    std::uint64_t audioGeneration = 0;
    std::uint32_t purpose = 0;                   // E1PayloadPurpose
    std::uint32_t totalBytes = 0;
    std::uint32_t totalChunks = 0;
    char parameterId[kE1ParameterIdBytes] = {};
    float normalizedValue = 0.0f;
    float appliedValue = 0.0f;
    std::uint32_t ok = 0;
    char sessionToken[33] = {};
    char error[kE1ErrorBytes] = {};
};

/** Small E1 chunk message: one 256-byte slice of a bounded payload. */
struct E1ChunkMessage
{
    std::uint32_t magic = kMessageMagic;
    std::uint16_t layoutVersion = kE1MessageLayoutVersion;
    std::uint16_t type = 0;                      // E1PayloadChunk
    std::uint32_t byteSize = sizeof(E1ChunkMessage);
    std::uint32_t protocolVersion = APEX_PLUGIN_SANDBOX_PROTOCOL_V1;
    std::uint32_t processId = 0;
    std::uint64_t audioGeneration = 0;
    std::uint32_t purpose = 0;                   // E1PayloadPurpose
    std::uint32_t totalBytes = 0;
    std::uint32_t totalChunks = 0;
    std::uint32_t chunkIndex = 0;
    std::uint32_t payloadBytes = 0;
    char sessionToken[33] = {};
    std::uint8_t payload[kE1ChunkPayloadBytes] = {};
};

static_assert(sizeof(E1ControlMessage) <= 1024,
              "E1 control envelope must stay in the proven-working size class");
static_assert(sizeof(E1ChunkMessage) <= 1024,
              "E1 chunk message must stay in the proven-working size class");

inline bool makeE1Message(std::uint16_t type,
                          const juce::String& session,
                          std::uint32_t processId,
                          std::uint64_t audioGeneration,
                          E1ControlMessage& message)
{
    std::memset(&message, 0, sizeof(message));
    message.magic = kMessageMagic;
    message.layoutVersion = kE1MessageLayoutVersion;
    message.type = type;
    message.byteSize = sizeof(E1ControlMessage);
    message.protocolVersion = APEX_PLUGIN_SANDBOX_PROTOCOL_V1;
    message.processId = processId;
    message.audioGeneration = audioGeneration;
    session.copyToUTF8(message.sessionToken, sizeof(message.sessionToken));
    return true;
}

inline bool makeE1ChunkMessage(const juce::String& session,
                               std::uint32_t processId,
                               std::uint64_t audioGeneration,
                               E1ChunkMessage& message)
{
    std::memset(&message, 0, sizeof(message));
    message.magic = kMessageMagic;
    message.layoutVersion = kE1MessageLayoutVersion;
    message.type = static_cast<std::uint16_t>(MessageType::E1PayloadChunk);
    message.byteSize = sizeof(E1ChunkMessage);
    message.protocolVersion = APEX_PLUGIN_SANDBOX_PROTOCOL_V1;
    message.processId = processId;
    message.audioGeneration = audioGeneration;
    session.copyToUTF8(message.sessionToken, sizeof(message.sessionToken));
    return true;
}

/** Validate the frozen-common fields of every E1 control message. */
inline bool validateE1Envelope(const E1ControlMessage& message,
                               const juce::String& session,
                               std::uint64_t expectedGeneration,
                               std::uint32_t expectedProcessId,
                               juce::String& error)
{
    if (message.magic != kMessageMagic
        || message.layoutVersion != kE1MessageLayoutVersion
        || message.byteSize != sizeof(E1ControlMessage)
        || message.protocolVersion != APEX_PLUGIN_SANDBOX_PROTOCOL_V1)
    {
        error = "invalid E1 control-message envelope";
        return false;
    }
    if (message.sessionToken[32] != '\0'
        || juce::String::fromUTF8(message.sessionToken, 32) != session)
    {
        error = "E1 session token mismatch";
        return false;
    }
    if (message.processId != expectedProcessId
        || message.audioGeneration != expectedGeneration)
    {
        error = "E1 worker identity/generation mismatch";
        return false;
    }
    return true;
}

/** Validate the common fields of an E1 chunk message. */
inline bool validateE1ChunkEnvelope(const E1ChunkMessage& message,
                                    const juce::String& session,
                                    std::uint64_t expectedGeneration,
                                    std::uint32_t expectedProcessId,
                                    juce::String& error)
{
    if (message.magic != kMessageMagic
        || message.layoutVersion != kE1MessageLayoutVersion
        || message.type != static_cast<std::uint16_t>(MessageType::E1PayloadChunk)
        || message.byteSize != sizeof(E1ChunkMessage)
        || message.protocolVersion != APEX_PLUGIN_SANDBOX_PROTOCOL_V1)
    {
        error = "invalid E1 chunk-message envelope";
        return false;
    }
    if (message.sessionToken[32] != '\0'
        || juce::String::fromUTF8(message.sessionToken, 32) != session)
    {
        error = "E1 chunk session token mismatch";
        return false;
    }
    if (message.processId != expectedProcessId
        || message.audioGeneration != expectedGeneration)
    {
        error = "E1 chunk identity/generation mismatch";
        return false;
    }
    return true;
}

inline bool validE1PayloadBounds(std::uint32_t totalBytes,
                                 std::uint32_t totalChunks,
                                 juce::String& error) noexcept
{
    if (totalBytes == 0
        || totalBytes > kE1MaximumPayloadBytes
        || totalChunks == 0
        || totalChunks > kE1MaximumChunks
        || (totalBytes + kE1ChunkPayloadBytes - 1) / kE1ChunkPayloadBytes
               != totalChunks)
    {
        error = "E1 payload bounds are invalid";
        return false;
    }
    return true;
}

inline bool validE1ChunkFields(const E1ChunkMessage& message, juce::String& error) noexcept
{
    if (message.totalChunks == 0
        || message.chunkIndex >= message.totalChunks
        || message.payloadBytes == 0
        || message.payloadBytes > kE1ChunkPayloadBytes
        || (message.chunkIndex + 1 < message.totalChunks
            && message.payloadBytes != kE1ChunkPayloadBytes))
    {
        error = "E1 chunk fields are invalid";
        return false;
    }
    return true;
}

inline bool copyE1Error(const juce::String& source, E1ControlMessage& message) noexcept
{
    const int bytes = juce::jmin<int>(source.getNumBytesAsUTF8(),
                                      static_cast<int>(kE1ErrorBytes) - 1);
    std::memset(message.error, 0, sizeof(message.error));
    source.copyToUTF8(message.error, bytes + 1);
    message.error[kE1ErrorBytes - 1] = '\0';
    return true;
}

template <std::size_t Capacity>
inline bool copyBoundedUtf8(const juce::String& source,
                            char (&destination)[Capacity],
                            juce::String& error,
                            const char* fieldName)
{
    const auto bytes = source.getNumBytesAsUTF8();
    if (bytes >= Capacity)
    {
        error = juce::String(fieldName) + " exceeds the Phase C protocol bound";
        return false;
    }
    std::memset(destination, 0, Capacity);
    source.copyToUTF8(destination, Capacity);
    return true;
}

template <std::size_t Capacity>
inline bool readBoundedUtf8(const char (&source)[Capacity],
                            juce::String& result,
                            juce::String& error,
                            const char* fieldName)
{
    if (source[Capacity - 1] != '\0')
    {
        error = juce::String(fieldName) + " is not null terminated";
        return false;
    }
    result = juce::String::fromUTF8(source, static_cast<int>(std::strlen(source)));
    return true;
}

inline bool makePluginCreateMessage(const juce::PluginDescription& description,
                                    const juce::String& session,
                                    std::uint32_t processId,
                                    std::uint64_t audioGeneration,
                                    double sampleRate,
                                    std::uint32_t maximumBlockSamples,
                                    std::uint32_t inputChannels,
                                    std::uint32_t outputChannels,
                                    PluginCreateMessage& message,
                                    juce::String& error)
{
    if (session.length() != 32 || audioGeneration == 0
        || sampleRate < 8000.0 || sampleRate > 384000.0
        || maximumBlockSamples == 0
        || inputChannels == 0 || inputChannels > 8
        || outputChannels == 0 || outputChannels > 8)
    {
        error = "invalid Phase C plugin-create configuration";
        return false;
    }

    message.processId = processId;
    message.audioGeneration = audioGeneration;
    message.sampleRate = sampleRate;
    message.maximumBlockSamples = maximumBlockSamples;
    message.inputChannels = inputChannels;
    message.outputChannels = outputChannels;
    message.uniqueId = description.uniqueId;
    message.deprecatedUid = description.deprecatedUid;
    return copyBoundedUtf8(session, message.sessionToken, error, "sessionToken")
        && copyBoundedUtf8(description.fileOrIdentifier, message.fileOrIdentifier,
                           error, "fileOrIdentifier")
        && copyBoundedUtf8(description.name, message.name, error, "name")
        && copyBoundedUtf8(description.manufacturerName, message.manufacturer,
                           error, "manufacturer")
        && copyBoundedUtf8(description.version, message.version, error, "version")
        && copyBoundedUtf8(description.pluginFormatName, message.pluginFormat,
                           error, "pluginFormat");
}

inline bool validatePluginCreateEnvelope(const PluginCreateMessage& message,
                                         const juce::String& session,
                                         juce::String& error)
{
    if (message.magic != kMessageMagic
        || message.layoutVersion != kPluginMessageLayoutVersion
        || message.type != static_cast<std::uint16_t>(MessageType::PluginCreate)
        || message.byteSize != sizeof(PluginCreateMessage)
        || message.protocolVersion != APEX_PLUGIN_SANDBOX_PROTOCOL_V1)
    {
        error = "invalid Phase C plugin-create envelope";
        return false;
    }
    if (message.sessionToken[32] != '\0'
        || juce::String::fromUTF8(message.sessionToken, 32) != session)
    {
        error = "Phase C plugin-create session mismatch";
        return false;
    }
    return true;
}

inline bool validatePluginCreateResponse(const PluginCreateResponse& response,
                                         const juce::String& session,
                                         juce::String& error)
{
    if (response.magic != kMessageMagic
        || response.layoutVersion != kPluginMessageLayoutVersion
        || response.byteSize != sizeof(PluginCreateResponse)
        || response.protocolVersion != APEX_PLUGIN_SANDBOX_PROTOCOL_V1)
    {
        error = "invalid Phase C plugin-create response envelope";
        return false;
    }
    if (response.sessionToken[32] != '\0'
        || juce::String::fromUTF8(response.sessionToken, 32) != session)
    {
        error = "Phase C plugin-create response session mismatch";
        return false;
    }
    return true;
}

inline bool isEditorMessageType(MessageType type) noexcept
{
    return type == MessageType::EditorCreateRequest
        || type == MessageType::EditorResizeRequest
        || type == MessageType::EditorCloseRequest;
}

inline bool isEditorResponseType(MessageType type) noexcept
{
    return type == MessageType::EditorCreateResponse
        || type == MessageType::EditorResizeResponse
        || type == MessageType::EditorCloseResponse;
}

inline bool makeEditorMessage(MessageType type,
                              const juce::String& session,
                              const juce::String& pluginInstanceId,
                              std::uint32_t processId,
                              std::uint64_t workerGeneration,
                              std::uint64_t requestSequence,
                              std::uint64_t nativeWindowHandle,
                              std::uint32_t width,
                              std::uint32_t height,
                              EditorControlMessage& message,
                              juce::String& error)
{
    if (! isEditorMessageType(type) && ! isEditorResponseType(type))
    {
        error = "invalid Phase F1 editor message type";
        return false;
    }
    if (session.length() != 32 || pluginInstanceId.isEmpty()
        || workerGeneration == 0 || requestSequence == 0)
    {
        error = "invalid Phase F1 editor message identity";
        return false;
    }

    std::memset(&message, 0, sizeof(message));
    message.magic = kMessageMagic;
    message.layoutVersion = kPluginMessageLayoutVersion;
    message.type = static_cast<std::uint16_t>(type);
    message.byteSize = sizeof(EditorControlMessage);
    message.protocolVersion = APEX_PLUGIN_SANDBOX_PROTOCOL_V1;
    message.processId = processId;
    message.workerGeneration = workerGeneration;
    message.requestSequence = requestSequence;
    message.nativeWindowHandle = nativeWindowHandle;
    message.width = width;
    message.height = height;
    return copyBoundedUtf8(session, message.sessionToken, error, "sessionToken")
        && copyBoundedUtf8(pluginInstanceId, message.pluginInstanceId,
                           error, "pluginInstanceId");
}

inline bool validateEditorMessageEnvelope(const EditorControlMessage& message,
                                          const juce::String& session,
                                          juce::String& error)
{
    const auto type = static_cast<MessageType>(message.type);
    if (message.magic != kMessageMagic
        || message.layoutVersion != kPluginMessageLayoutVersion
        || message.byteSize != sizeof(EditorControlMessage)
        || message.protocolVersion != APEX_PLUGIN_SANDBOX_PROTOCOL_V1
        || (! isEditorMessageType(type) && ! isEditorResponseType(type)))
    {
        error = "invalid Phase F1 editor message envelope";
        return false;
    }
    if (message.sessionToken[32] != '\0'
        || juce::String::fromUTF8(message.sessionToken, 32) != session)
    {
        error = "Phase F1 editor session mismatch";
        return false;
    }
    if (message.workerGeneration == 0 || message.requestSequence == 0)
    {
        error = "Phase F1 editor identity is incomplete";
        return false;
    }
    if (message.pluginInstanceId[kPluginInstanceIdBytes - 1] != '\0')
    {
        error = "Phase F1 editor plugin identity is not terminated";
        return false;
    }
    return true;
}

inline bool isEditorAsyncRequestType(MessageType type) noexcept
{
    return type == MessageType::EditorOpenSubmitRequest
        || type == MessageType::EditorStatusQueryRequest
        || type == MessageType::EditorCancelRequest;
}

inline bool isEditorAsyncResponseType(MessageType type) noexcept
{
    return type == MessageType::EditorOpenSubmitResponse
        || type == MessageType::EditorStatusQueryResponse
        || type == MessageType::EditorCancelResponse;
}

inline bool makeEditorAsyncMessage(MessageType type,
                                   const juce::String& session,
                                   const juce::String& pluginInstanceId,
                                   std::uint32_t processId,
                                   std::uint64_t workerGeneration,
                                   std::uint64_t requestSequence,
                                   EditorAsyncControlMessage& message,
                                   juce::String& error)
{
    if (! isEditorAsyncRequestType(type) && ! isEditorAsyncResponseType(type))
    {
        error = "invalid Phase F3 editor message type";
        return false;
    }
    if (session.length() != 32 || pluginInstanceId.isEmpty()
        || workerGeneration == 0 || requestSequence == 0)
    {
        error = "invalid Phase F3 editor message identity";
        return false;
    }

    std::memset(&message, 0, sizeof(message));
    message.magic = kMessageMagic;
    message.layoutVersion = kPluginMessageLayoutVersion;
    message.type = static_cast<std::uint16_t>(type);
    message.byteSize = sizeof(EditorAsyncControlMessage);
    message.protocolVersion = APEX_PLUGIN_SANDBOX_PROTOCOL_V1;
    message.processId = processId;
    message.workerGeneration = workerGeneration;
    message.requestSequence = requestSequence;
    return copyBoundedUtf8(session, message.sessionToken, error, "sessionToken")
        && copyBoundedUtf8(pluginInstanceId, message.pluginInstanceId,
                           error, "pluginInstanceId");
}

inline bool validateEditorAsyncMessageEnvelope(
    const EditorAsyncControlMessage& message,
    const juce::String& session,
    juce::String& error)
{
    const auto type = static_cast<MessageType>(message.type);
    if (message.magic != kMessageMagic
        || message.layoutVersion != kPluginMessageLayoutVersion
        || message.byteSize != sizeof(EditorAsyncControlMessage)
        || message.protocolVersion != APEX_PLUGIN_SANDBOX_PROTOCOL_V1
        || (! isEditorAsyncRequestType(type) && ! isEditorAsyncResponseType(type)))
    {
        error = "invalid Phase F3 editor message envelope";
        return false;
    }
    if (message.sessionToken[32] != '\0'
        || juce::String::fromUTF8(message.sessionToken, 32) != session)
    {
        error = "Phase F3 editor session mismatch";
        return false;
    }
    if (message.workerGeneration == 0 || message.requestSequence == 0)
    {
        error = "Phase F3 editor identity is incomplete";
        return false;
    }
    if (message.pluginInstanceId[kPluginInstanceIdBytes - 1] != '\0'
        || message.error[kEditorErrorBytes - 1] != '\0'
        || message.status > static_cast<std::uint32_t>(EditorAsyncStatus::Closed))
    {
        error = "Phase F3 editor response contains invalid bounded fields";
        return false;
    }
    return true;
}

inline bool copyEditorAsyncError(const juce::String& source,
                                 EditorAsyncControlMessage& message) noexcept
{
    const int bytes = juce::jmin<int>(source.getNumBytesAsUTF8(),
                                      static_cast<int>(kEditorErrorBytes) - 1);
    std::memset(message.error, 0, sizeof(message.error));
    source.copyToUTF8(message.error, bytes + 1);
    message.error[kEditorErrorBytes - 1] = '\0';
    return true;
}

inline bool copyEditorError(const juce::String& source,
                            EditorControlMessage& message) noexcept
{
    const int bytes = juce::jmin<int>(source.getNumBytesAsUTF8(),
                                      static_cast<int>(kEditorErrorBytes) - 1);
    std::memset(message.error, 0, sizeof(message.error));
    source.copyToUTF8(message.error, bytes + 1);
    message.error[kEditorErrorBytes - 1] = '\0';
    return true;
}

struct ExecutableIdentity
{
    std::uint32_t volumeSerial = 0;
    std::uint64_t fileIndex = 0;

    bool isValid() const noexcept { return volumeSerial != 0 || fileIndex != 0; }
    bool operator==(const ExecutableIdentity& other) const noexcept
    {
        return volumeSerial == other.volumeSerial && fileIndex == other.fileIndex;
    }
    bool operator!=(const ExecutableIdentity& other) const noexcept
    {
        return ! (*this == other);
    }
};

class UniqueHandle
{
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
            reset(other.release());
        return *this;
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    bool isValid() const noexcept
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    HANDLE get() const noexcept { return handle_; }
    HANDLE release() noexcept
    {
        auto result = handle_;
        handle_ = nullptr;
        return result;
    }
    void reset(HANDLE replacement = nullptr) noexcept
    {
        if (isValid())
            CloseHandle(handle_);
        handle_ = replacement;
    }

private:
    HANDLE handle_ = nullptr;
};

inline juce::String windowsError(const juce::String& operation, DWORD error = GetLastError())
{
    return operation + " failed with Win32 error " + juce::String(static_cast<int>(error));
}

inline juce::String resolveCurrentExecutablePath()
{
    std::wstring buffer(512, L'\0');
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size() - 1)
            return juce::String(buffer.data(), static_cast<int>(length));
        buffer.resize(buffer.size() * 2, L'\0');
        if (buffer.size() > 32768)
            return {};
    }
}

inline ExecutableIdentity executableIdentity(const juce::String& path)
{
    UniqueHandle file(CreateFileW(path.toWideCharPointer(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    BY_HANDLE_FILE_INFORMATION information {};
    if (! file.isValid() || ! GetFileInformationByHandle(file.get(), &information))
        return {};

    return { information.dwVolumeSerialNumber,
             (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32u)
                | information.nFileIndexLow };
}

inline ControlMessage makeMessage(MessageType type,
                                  std::uint32_t protocol,
                                  const juce::String& session,
                                  std::uint32_t processId,
                                  std::uint32_t flags,
                                  const ExecutableIdentity& identity)
{
    ControlMessage message;
    message.byteSize = sizeof(ControlMessage);
    message.type = static_cast<std::uint16_t>(type);
    message.protocolVersion = protocol;
    message.processId = processId;
    message.flags = flags;
    message.executableVolumeSerial = identity.volumeSerial;
    message.executableFileIndex = identity.fileIndex;
    session.copyToUTF8(message.sessionToken, sizeof(message.sessionToken));
    return message;
}

inline bool validateMessageEnvelope(const ControlMessage& message,
                                    const juce::String& session,
                                    juce::String& error)
{
    if (message.magic != kMessageMagic
        || message.layoutVersion != kMessageLayoutVersion
        || message.byteSize != sizeof(ControlMessage))
    {
        error = "invalid Phase A control-message envelope";
        return false;
    }
    if (message.sessionToken[32] != '\0'
        || juce::String::fromUTF8(message.sessionToken, 32) != session)
    {
        error = "worker session token mismatch";
        return false;
    }
    return true;
}

template <typename Message>
inline bool writeFixedMessage(HANDLE pipe,
                              const Message& message,
                              DWORD timeoutMs,
                              juce::String& error,
                              DWORD* writeErrorOut = nullptr)
{
    const auto deadline = GetTickCount64() + timeoutMs;
    for (;;)
    {
        DWORD transferred = 0;
        if (WriteFile(pipe, &message, static_cast<DWORD>(sizeof(message)),
                      &transferred, nullptr))
        {
            if (transferred == sizeof(message))
            {
                if (writeErrorOut != nullptr)
                    *writeErrorOut = 0;                  // success
                return true;
            }

            // PIPE_NOWAIT message-mode writes may report success with zero
            // transferred bytes when the peer has not yet made room. No part
            // of a message is accepted in that case, so retry the complete
            // fixed record within this existing control-plane deadline.
            if (transferred != 0)
            {
                error = "short Phase A control-message write";
                if (writeErrorOut != nullptr)
                    *writeErrorOut = 0xFFFFFFFFu;   // marker: BOOL true, partial transfer
                return false;
            }
        }
        else
        {
            const auto writeError = GetLastError();
            // ERROR_SEM_IS_SET (103) is the observed nonblocking named-pipe
            // backpressure result for the dense E1 burst. A failed synchronous
            // WriteFile transferred no bytes, so retrying the same record is
            // safe and preserves bounded control-plane behavior.
            if (writeError != ERROR_NO_DATA
                && writeError != ERROR_PIPE_BUSY
                && writeError != ERROR_SEM_IS_SET)
            {
                error = windowsError("WriteFile", writeError);
                if (writeErrorOut != nullptr)
                    *writeErrorOut = writeError;
                return false;
            }
        }
        if (GetTickCount64() >= deadline)
        {
            error = "IPC operation timed out";
            if (writeErrorOut != nullptr)
                *writeErrorOut = ERROR_TIMEOUT;      // marker: retryable errors exhausted
            return false;
        }
        Sleep(1);
    }
}

template <typename Message>
inline bool readFixedMessage(HANDLE pipe,
                             Message& message,
                             DWORD timeoutMs,
                             juce::String& error)
{
    const auto deadline = GetTickCount64() + timeoutMs;
    for (;;)
    {
        DWORD transferred = 0;
        if (ReadFile(pipe, &message, static_cast<DWORD>(sizeof(message)),
                     &transferred, nullptr))
        {
            if (transferred != sizeof(message))
            {
                error = "short Phase A control-message read";
                return false;
            }
            return true;
        }

        const auto readError = GetLastError();
        if (readError != ERROR_NO_DATA)
        {
            error = windowsError("ReadFile", readError);
            return false;
        }
        if (GetTickCount64() >= deadline)
        {
            error = "IPC operation timed out";
            return false;
        }
        Sleep(1);
    }
}

inline bool writeMessage(HANDLE pipe, const ControlMessage& message,
                         DWORD timeoutMs, juce::String& error)
{
    return writeFixedMessage(pipe, message, timeoutMs, error);
}

inline bool readMessage(HANDLE pipe, ControlMessage& message,
                        DWORD timeoutMs, juce::String& error)
{
    return readFixedMessage(pipe, message, timeoutMs, error);
}

enum class TryReadMessageResult
{
    NoData,
    Message,
    ExtendedMessage,
    Closed,
    Failed
};

inline TryReadMessageResult tryReadMessageNonBlocking(HANDLE pipe,
                                                       ControlMessage& message) noexcept
{
    DWORD available = 0;
    if (! PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
    {
        const auto error = GetLastError();
        return error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED
            ? TryReadMessageResult::Closed : TryReadMessageResult::Failed;
    }
    if (available < sizeof(ControlMessage))
        return TryReadMessageResult::NoData;
    if (available > sizeof(ControlMessage))
        return TryReadMessageResult::ExtendedMessage;

    DWORD transferred = 0;
    if (! ReadFile(pipe, &message, sizeof(message), &transferred, nullptr))
    {
        const auto error = GetLastError();
        if (error == ERROR_NO_DATA)
            return TryReadMessageResult::NoData;
        return error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED
            ? TryReadMessageResult::Closed : TryReadMessageResult::Failed;
    }
    return transferred == sizeof(message)
        ? TryReadMessageResult::Message : TryReadMessageResult::Failed;
}

inline juce::String quoteWindowsArgument(const juce::String& argument)
{
    const std::wstring input(argument.toWideCharPointer());
    if (! input.empty() && input.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return argument;

    std::wstring output = L"\"";
    std::size_t backslashes = 0;
    for (const auto character : input)
    {
        if (character == L'\\')
        {
            ++backslashes;
            continue;
        }
        if (character == L'\"')
        {
            output.append(backslashes * 2 + 1, L'\\');
            output.push_back(character);
            backslashes = 0;
            continue;
        }
        output.append(backslashes, L'\\');
        backslashes = 0;
        output.push_back(character);
    }
    output.append(backslashes * 2, L'\\');
    output.push_back(L'\"');
    return juce::String(output.c_str());
}

inline juce::String buildWindowsCommandLine(const juce::StringArray& arguments)
{
    juce::StringArray quoted;
    for (const auto& argument : arguments)
        quoted.add(quoteWindowsArgument(argument));
    return quoted.joinIntoString(" ");
}
} // namespace PluginSandboxWin32
#endif

} // namespace DAW
