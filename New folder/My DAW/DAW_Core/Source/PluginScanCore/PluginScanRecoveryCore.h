#pragma once
#include <JuceHeader.h>
#include "PluginScanAuditLogCore.h"
#include "PluginScanDeadmanCore.h"
#include "PluginScanFailureStoreCore.h"
#include "PluginScanResultCore.h"

namespace DAW {

class PluginScanRecoveryCore
{
public:
    void recoverInterruptedScan(const PluginScanDeadmanCore& deadman,
                                PluginScanFailureStoreCore& failureStore) const
    {
        auto candidatePath = deadman.readCurrentCandidatePath();
        if (candidatePath.isEmpty())
            return;

        auto result = PluginScanResultCore::makeBasicResult(0, candidatePath, {}, PluginScanState::Crashed);
        result.failureReason = "RecoveredFromDeadman";
        result.errorText = "Previous scan ended without a terminal result.";
        failureStore.storeResult(result);
        PluginScanAuditLogCore::appendLine("scan_host_ipc.log",
            "RECOVERY candidate=" + candidatePath + " reason=" + result.failureReason);
        deadman.clear();
    }
};

} // namespace DAW
