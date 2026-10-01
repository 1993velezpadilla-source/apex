#pragma once

#include "../PluginScanCore/PluginScanAuditLogCore.h"

#include <cstdint>

namespace DAW
{

/**
    Bounded, control-plane-only diagnostics for one PluginCreate transaction.

    The worker enters before normal APEX logger initialisation, so the existing
    audit-file facility is the shared parent/worker destination. This helper
    is never called by audio, processBlock, shared-memory, or automation paths.
*/
class PluginSandboxDiagnosticsCore
{
public:
    static void appendPluginCreatePhase(const juce::String& sessionToken,
                                        std::uint32_t workerProcessId,
                                        std::uint64_t workerGeneration,
                                        const char* phase,
                                        const juce::String& detail = {})
    {
        const auto session = sessionToken.substring(0, 64);
        const auto boundedDetail = detail.substring(0, 256);
        auto line = juce::String("[APEX-SANDBOX] op=PluginCreate phase=")
            + phase
            + " pid=" + juce::String(static_cast<juce::int64>(workerProcessId))
            + " generation=" + juce::String(static_cast<juce::int64>(workerGeneration))
            + " session=" + session;

        if (boundedDetail.isNotEmpty())
            line += " detail=" + boundedDetail;

        PluginScanAuditLogCore::appendLine("plugin_ui_flow.log", line);
    }
};

} // namespace DAW
