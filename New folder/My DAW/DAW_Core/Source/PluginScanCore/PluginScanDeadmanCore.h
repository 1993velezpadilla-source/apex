#pragma once
#include <JuceHeader.h>
#include "PluginScanJobBuilderCore.h"

namespace DAW {

class PluginScanDeadmanCore
{
public:
    juce::File getFile() const
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW_Core")
            .getChildFile("scan_deadman.txt");
    }

    void writeCurrentCandidate(const PluginScanJobBuilderCore::ScanJob& job) const
    {
        auto file = getFile();
        file.getParentDirectory().createDirectory();
        file.replaceWithText(job.filePath + "\n"
            + "jobId=" + juce::String(job.jobId) + "\n"
            + "format=" + job.format + "\n"
            + "timestamp=" + juce::String(job.timestamp) + "\n"
            + "fileSize=" + juce::String(job.fileSize) + "\n");
    }

    juce::String readCurrentCandidatePath() const
    {
        auto text = getFile().loadFileAsString().trim();
        return text.upToFirstOccurrenceOf("\n", false, false).trim();
    }

    void clear() const
    {
        getFile().deleteFile();
    }
};

} // namespace DAW
