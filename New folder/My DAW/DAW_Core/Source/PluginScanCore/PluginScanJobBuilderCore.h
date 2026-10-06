#pragma once
#include <JuceHeader.h>
#include "PluginScanAuditLogCore.h"
#include "PluginScanFormatsCore.h"
#include "PluginScanPathsCore.h"
#include "PluginScanStateCore.h"

namespace DAW {

class PluginScanJobBuilderCore
{
public:
    struct ScanJob
    {
        int jobId = 0;
        juce::String filePath;
        juce::String format;
        juce::int64 timestamp = 0;
        juce::int64 fileSize = 0;
        PluginScanState state = PluginScanState::Pending;
    };

    std::vector<ScanJob> buildJobs(const PluginScanPathsCore& paths,
                                   const PluginScanFormatsCore& formats) const
    {
        std::vector<ScanJob> jobs;
        juce::StringArray seenKeys;
        int nextJobId = 1;
        int invalidFormatCount = 0;
        int duplicateCount = 0;
        std::map<juce::String, int> rootCandidateCounts;

        for (const auto& rootPath : paths.getPathStrings())
        {
            auto root = juce::File(rootPath);
            PluginScanAuditLogCore::appendLine("scan_summary.log",
                "ROOT path=\"" + root.getFullPathName() + "\" exists=" + juce::String(root.isDirectory() ? 1 : 0));
            if (!root.isDirectory())
                continue;

            collectFromDirectory(root, root.getFullPathName(), formats, seenKeys, nextJobId, jobs,
                invalidFormatCount, duplicateCount, rootCandidateCounts);
        }

        PluginScanAuditLogCore::appendLine("scan_summary.log",
            "DISCOVERY_COUNTS total=" + juce::String((int)jobs.size())
                + " duplicate=" + juce::String(duplicateCount)
                + " invalidFormat=" + juce::String(invalidFormatCount));

        for (const auto& [rootPath, count] : rootCandidateCounts)
            PluginScanAuditLogCore::appendLine("scan_summary.log",
                "ROOT_CANDIDATES path=\"" + rootPath + "\" count=" + juce::String(count));

        return jobs;
    }

private:
    void collectFromDirectory(const juce::File& directory,
                              const juce::String& rootPath,
                              const PluginScanFormatsCore& formats,
                              juce::StringArray& seenKeys,
                              int& nextJobId,
                              std::vector<ScanJob>& jobs,
                              int& invalidFormatCount,
                              int& duplicateCount,
                              std::map<juce::String, int>& rootCandidateCounts) const
    {
        for (const auto& entry : juce::RangedDirectoryIterator(directory, false, "*", juce::File::findFilesAndDirectories))
        {
            auto file = entry.getFile();
            auto detectedFormat = formats.detectFormatForPath(file);

            if (detectedFormat.isNotEmpty())
            {
                auto normalisedPath = file.getFullPathName();
                auto key = normalisedPath.toLowerCase() + "|" + detectedFormat.toLowerCase();
                if (!seenKeys.contains(key))
                {
                    seenKeys.add(key);
                    ++rootCandidateCounts[rootPath];
                    ScanJob job;
                    job.jobId = nextJobId++;
                    job.filePath = normalisedPath;
                    job.format = detectedFormat;
                    job.timestamp = file.exists() ? file.getLastModificationTime().toMilliseconds() : 0;
                    job.fileSize = file.existsAsFile() ? file.getSize() : 0;
                    job.state = PluginScanState::Pending;
                    jobs.push_back(job);
                    PluginScanAuditLogCore::appendLine("scan_summary.log",
                        "DISCOVERED jobId=" + juce::String(job.jobId)
                        + " format=" + job.format
                        + " path=" + job.filePath);
                }
                else
                {
                    ++duplicateCount;
                    PluginScanAuditLogCore::appendLine("scan_summary.log",
                        "DUPLICATE format=" + detectedFormat + " path=" + normalisedPath);
                }

                if (file.isDirectory())
                    continue;
            }
            else if (file.existsAsFile())
            {
                ++invalidFormatCount;
            }

            if (file.isDirectory())
                collectFromDirectory(file, rootPath, formats, seenKeys, nextJobId, jobs,
                    invalidFormatCount, duplicateCount, rootCandidateCounts);
        }
    }
};

} // namespace DAW
