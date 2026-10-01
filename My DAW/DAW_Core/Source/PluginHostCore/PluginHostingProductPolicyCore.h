#pragma once

namespace DAW
{

/**
 * UI-only product switch for the optional sandbox menu entries.
 *
 * This does not participate in execution-mode semantics, project persistence,
 * restore, or the sandbox engine.  The underlying handlers and APIs remain
 * available to internal callers.
 */
class PluginHostingProductPolicyCore final
{
public:
    inline static constexpr bool kSandboxUserFeatureEnabled = false;
    inline static constexpr const char* kSandboxDisabledMessage =
        "Sandbox hosting is temporarily disabled in this build.";
};

} // namespace DAW
