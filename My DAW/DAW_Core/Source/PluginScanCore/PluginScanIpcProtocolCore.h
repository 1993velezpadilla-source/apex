#pragma once
#include <JuceHeader.h>
#include "PluginScanJobBuilderCore.h"
#include "PluginScanResultCore.h"
#include <juce_audio_processors_headless/format_types/juce_VST3EnumerationDiagnostic.h>

namespace DAW {

class PluginScanIpcProtocolCore
{
public:
    static constexpr const char* kWorkerFlag = "--plugin-scan-worker";
    static constexpr const char* kPrefix = "SCANIPC ";

    static juce::String buildWorkerCommand(const juce::File& executable,
                                           const PluginScanJobBuilderCore::ScanJob& job)
    {
        juce::String command = "\"" + executable.getFullPathName() + "\" "
            + kWorkerFlag
            + " --scan-job-id " + juce::String(job.jobId)
            + " --scan-path \"" + job.filePath + "\""
            + " --scan-format \"" + job.format + "\""
            + " --scan-mod-time " + juce::String(job.timestamp)
            + " --scan-file-size " + juce::String(job.fileSize);

        if (juce::JUCEApplicationBase::getCommandLineParameters()
                .contains (juce::VST3EnumerationDiagnostic::kCommandLineFlag))
            command += juce::String (" ") + juce::VST3EnumerationDiagnostic::kCommandLineFlag;

        return command;
    }

    static juce::String makeMessage(const juce::String& messageType, const juce::var& payload)
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty("messageType", messageType);
        obj->setProperty("payload", payload);
        return juce::String(kPrefix) + juce::JSON::toString(juce::var(obj.release()), true);
    }

    static bool parseMessage(const juce::String& line, juce::String& messageType, juce::var& payload)
    {
        if (!line.startsWith(kPrefix))
            return false;

        auto parsed = juce::JSON::parse(line.fromFirstOccurrenceOf(kPrefix, false, false));
        if (parsed.isVoid())
            return false;

        auto* obj = parsed.getDynamicObject();
        if (obj == nullptr)
            return false;

        messageType = obj->getProperty("messageType").toString();
        payload = obj->getProperty("payload");
        return true;
    }

    static juce::String workerEntryMessage(const PluginScanJobBuilderCore::ScanJob& job)
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty("jobId", job.jobId);
        obj->setProperty("path", job.filePath);
        obj->setProperty("format", job.format);
        return makeMessage("WorkerEntry", juce::var(obj.release()));
    }

    static juce::String candidateBeginMessage(const PluginScanJobBuilderCore::ScanJob& job)
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty("jobId", job.jobId);
        obj->setProperty("path", job.filePath);
        obj->setProperty("format", job.format);
        return makeMessage("CandidateBegin", juce::var(obj.release()));
    }

    static juce::String candidateResultMessage(const PluginScanResult& result)
    {
        return makeMessage("CandidateResult", PluginScanResultCore::toVar(result));
    }

    static juce::String workerCompleteMessage(const PluginScanJobBuilderCore::ScanJob& job,
                                              PluginScanState finalState)
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty("jobId", job.jobId);
        obj->setProperty("path", job.filePath);
        obj->setProperty("format", job.format);
        obj->setProperty("finalState", PluginScanStateCore::toString(finalState));
        return makeMessage("WorkerComplete", juce::var(obj.release()));
    }
};

} // namespace DAW
