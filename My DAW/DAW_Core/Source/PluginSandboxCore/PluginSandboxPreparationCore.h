#pragma once

#include <JuceHeader.h>

#include <cstdint>

namespace DAW {

inline constexpr std::uint32_t kPluginSandboxStartupTimeoutMs = 5000;
inline constexpr std::uint32_t kPluginSandboxPluginCreateTimeoutMs = 60000;
// Editor creation is a separate heavy control-plane operation.  It may enter
// vendor GUI initialization, asset loading, graphics setup, or licensing
// helpers, while resize/close remain on their existing short deadlines.
inline constexpr std::uint32_t kPluginSandboxEditorCreateTimeoutMs = 60000;

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
/** Deterministic worker fault points used only by Phase E2C tests. */
enum class PluginSandboxAudioTestFaultPoint : std::uint32_t
{
    None = 0,
    AfterAudioSubmitBeforeAutomationApply = 1,
    AfterAutomationApplyBeforeWorkerCommit = 2
};

/** Deterministic PluginCreate control-plane seams used only by focused tests. */
enum class PluginSandboxControlTestFaultPoint : std::uint32_t
{
    None = 0,
    PluginCreateWriteTimeout = 1,
    PluginCreateFailBeforeInstance = 2
};
#endif

/** Control-plane preparation request for one worker-hosted sandboxed plugin.

    Deliberately a plain, Windows-free struct so host headers
    (PluginChainCore / PluginInstanceCore) can name it without pulling
    windows.h into UI translation units. SandboxedPluginProxyCore aliases
    this type as its Preparation member for compatibility. */
struct PluginSandboxPreparation
{
    double sampleRate = 48000.0;
    std::uint32_t blockSamples = 512;      // exact sandbox quantum Q
    std::uint32_t mainInputChannels = 2;
    std::uint32_t mainOutputChannels = 2;
    std::uint32_t startupTimeoutMs = kPluginSandboxStartupTimeoutMs;
    // Heavy third-party work is bounded separately from worker bootstrap and
    // lightweight control-plane IPC. This covers the complete PluginCreate
    // request/response transaction, including VST3 enumeration and layout/
    // parameter preparation performed by the worker.
    std::uint32_t pluginCreateTimeoutMs = kPluginSandboxPluginCreateTimeoutMs;
    // EditorCreate has its own heavy control-plane budget. EditorResize and
    // EditorClose intentionally do not use this deadline.
    std::uint32_t editorCreateTimeoutMs = kPluginSandboxEditorCreateTimeoutMs;
    // Maximum host callback size the fixed-quantum adapter must accept.
    // Host callbacks may be LARGER than Q; every callback is reblocked into
    // exact Q-sample worker quanta. 0 falls back to Q.
    std::uint32_t maximumHostBlockSamples = 0;
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
      juce::String workerExecutablePathForTest;
      std::uint32_t editorCreateResponseDelayMilliseconds = 0;
      PluginSandboxAudioTestFaultPoint audioFaultPoint =
          PluginSandboxAudioTestFaultPoint::None;
     bool failStateRestoreForTest = false;
   #endif
};

} // namespace DAW
