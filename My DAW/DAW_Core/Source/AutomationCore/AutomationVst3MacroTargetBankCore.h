#pragma once
#include "LastTouchedPluginParameterCore.h"
#include "AutomationMacroMappingCore.h"
#include <vector>

namespace DAW {

/**
 * Explicit, session-scoped destinations for a multi-VST3 macro.
 *
 * A touch is only an identity CANDIDATE. Every destination must be verified
 * again against its *currently hosted* VST3 instance and parameter metadata
 * before compiling, applying, undoing or redoing a macro transaction.
 *
 * A project/automation-manager replacement MUST clear this bank. It is not
 * serialized and contains no live processor pointers or stale approval bits.
 * All methods run only on the UI/control thread.
 */
class AutomationVst3MacroTargetBankCore
{
public:
    using Identity = LastTouchedPluginParameter;
    static constexpr size_t maxTargets = AutomationMacroMappingCore::maxBindings;

    static bool sameIdentity(const Identity& a, const Identity& b) noexcept
    {
        return a.trackId == b.trackId
            && a.pluginSlotIndex == b.pluginSlotIndex
            && a.pluginInstanceId == b.pluginInstanceId
            && a.parameterId == b.parameterId;
    }

    bool add(const Identity& touched)
    {
        if (!touched.isValid() || touched.pluginInstanceId.isEmpty()
            || targets_.size() >= maxTargets)
            return false;
        for (const auto& existing : targets_)
            if (sameIdentity(existing, touched))
                return false;
        targets_.push_back(touched);
        return true;
    }

    void clear() noexcept { targets_.clear(); }
    size_t size() const noexcept { return targets_.size(); }
    bool empty() const noexcept { return targets_.empty(); }
    const std::vector<Identity>& targets() const noexcept { return targets_; }

private:
    std::vector<Identity> targets_;
};

} // namespace DAW
