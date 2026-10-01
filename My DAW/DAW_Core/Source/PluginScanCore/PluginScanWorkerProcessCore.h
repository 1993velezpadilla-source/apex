#pragma once
#include <JuceHeader.h>
#include "PluginScanAuditLogCore.h"
#include "PluginScanDeadmanCore.h"
#include "PluginScanFormatsCore.h"
#include "PluginScanIpcProtocolCore.h"
#include "PluginScanResultCore.h"
#include "../PluginSafetyCore/PluginProtectionRuntimeGuardCore.h"
#include "../PluginSecurityCore/PluginLoadFailureClassifierCore.h"

namespace DAW {

class PluginScanWorkerProcessCore
{
public:
    static int run(int jobId,
                   const juce::String& candidatePath,
                   const juce::String& formatName,
                   juce::int64 fileTimestamp,
                   juce::int64 fileSize)
    {
        PluginScanJobBuilderCore::ScanJob job;
        job.jobId = jobId;
        job.filePath = candidatePath;
        job.format = formatName;
        job.timestamp = fileTimestamp;
        job.fileSize = fileSize;
        job.state = PluginScanState::Pending;

        PluginScanAuditLogCore::appendStartupTrace(
            "PluginScanWorkerProcessCore::run",
            "worker",
            -1,
            -1,
            "jobId=" + juce::String(job.jobId)
                + " path=\"" + job.filePath + "\" format=" + job.format);

        PluginScanAuditLogCore::appendLine("scan_worker_entry.log",
            "ENTRY jobId=" + juce::String(job.jobId)
            + " path=" + job.filePath
            + " format=" + job.format);
        PluginScanAuditLogCore::appendLine("scan_worker_candidates.log",
            "BEGIN jobId=" + juce::String(job.jobId)
            + " path=" + job.filePath
            + " format=" + job.format);

        std::cout << PluginScanIpcProtocolCore::workerEntryMessage(job) << std::endl;
        std::cout << PluginScanIpcProtocolCore::candidateBeginMessage(job) << std::endl;
        std::cout.flush();

        PluginScanDeadmanCore deadman;
        deadman.writeCurrentCandidate(job);

        PluginProtectionRuntimeGuardCore runtimeGuard;
        PluginScanResult result = PluginScanResultCore::makeBasicResult(job.jobId, job.filePath, job.format, PluginScanState::Failed);

        try
        {
            PluginScanFormatsCore formats;
            auto* format = formats.findFormatByName(job.format);
            if (format == nullptr)
            {
                result.failureReason = "UnsupportedFormat";
                result.errorText = "No registered format matched the worker job.";
            }
            else
            {
                juce::OwnedArray<juce::PluginDescription> descriptions;
                format->findAllTypesForFile(descriptions, job.filePath);

                if (descriptions.isEmpty())
                {
                    result.failureReason = "NoPluginTypesAdded";
                    result.errorText = "The plugin file produced no plugin descriptions.";
                }
                else
                {
                    result.resultType = PluginScanState::Success;
                    result.producedMultiplePluginDescriptions = descriptions.size() > 1;

                    for (auto* description : descriptions)
                    {
                        if (description == nullptr)
                            continue;

                        PluginScannedDescriptionRecord record;
                        record.path = description->fileOrIdentifier;
                        record.format = description->pluginFormatName;
                        record.name = description->name;
                        record.manufacturer = description->manufacturerName;
                        record.uniqueId = juce::String(description->uniqueId);
                        record.deprecatedUid = juce::String(description->deprecatedUid);
                        record.category = description->category;
                        record.version = description->version;
                        record.isInstrument = description->isInstrument;
                        record.numInputs = description->numInputChannels;
                        record.numOutputs = description->numOutputChannels;
                        record.hasMidi = false;
                        result.producedDescriptions.push_back(record);
                    }

                    const auto& primary = result.producedDescriptions.front();
                    result.pluginName = primary.name;
                    result.manufacturer = primary.manufacturer;
                    result.uniqueId = primary.uniqueId;
                    result.deprecatedUid = primary.deprecatedUid;
                    result.category = primary.category;
                    result.version = primary.version;
                }
            }
        }
        catch (const std::exception& ex)
        {
            auto failure = PluginLoadFailureClassifierCore::classify(job.filePath, ex.what(), "plugin_scan_worker");
            result.resultType = PluginScanState::Failed;
            result.failureReason = failureReasonFromClassification(failure);
            result.errorText = ex.what();
        }
        catch (...)
        {
            result.resultType = PluginScanState::Failed;
            result.failureReason = "UnknownFailure";
            result.errorText = "Unknown exception in worker.";
        }

        PluginScanAuditLogCore::appendLine("scan_worker_results.log",
            "RESULT jobId=" + juce::String(result.jobId)
            + " state=" + PluginScanStateCore::toString(result.resultType)
            + " path=" + result.candidatePath
            + " format=" + result.format
            + " name=" + result.pluginName
            + " reason=" + result.failureReason
            + " descriptions=" + juce::String(static_cast<int>(result.producedDescriptions.size())));

        std::cout << PluginScanIpcProtocolCore::candidateResultMessage(result) << std::endl;
        std::cout << PluginScanIpcProtocolCore::workerCompleteMessage(job, result.resultType) << std::endl;
        std::cout.flush();
        return 0;
    }

private:
    static juce::String failureReasonFromClassification(const PluginLoadFailureInfo& failure)
    {
        switch (failure.category)
        {
            case PluginLoadFailureCategory::SecurityPolicyBlocked:
            case PluginLoadFailureCategory::CopyProtectionRuntimeBlocked:
            case PluginLoadFailureCategory::RuntimeDependencyFailure:
                return "BlockedDependency";
            case PluginLoadFailureCategory::TimedOut:
                return "TimedOut";
            case PluginLoadFailureCategory::InvalidBinary:
                return "ParseError";
            case PluginLoadFailureCategory::Unsupported:
                return "Unsupported";
            case PluginLoadFailureCategory::CrashDuringInitialisation:
                return "Crashed";
            case PluginLoadFailureCategory::UnknownHostLoadFailure:
            case PluginLoadFailureCategory::None:
                break;
        }

        return "UnknownFailure";
    }
};

} // namespace DAW
