#pragma once
#include <JuceHeader.h>
#include "PluginScanAuditLogCore.h"
#include "PluginScanResultCore.h"
#include "../PluginStorageCore/PluginCacheCore.h"

namespace DAW {

class PluginScanCacheWriterCore
{
public:
    int persistSuccessfulResult(const PluginScanResult& result, PluginCacheCore& cache) const
    {
        if (result.resultType != PluginScanState::Success)
            return 0;

        cache.remove(result.candidatePath);

        int insertCount = 0;
        for (const auto& description : result.producedDescriptions)
        {
            PluginCacheCore::CachedPlugin entry;
            entry.path = description.path;
            entry.format = description.format;
            entry.name = description.name;
            entry.manufacturer = description.manufacturer;
            entry.category = description.category;
            entry.uniqueId = description.uniqueId;
            entry.deprecatedUid = description.deprecatedUid;
            entry.version = description.version;
            entry.isInstrument = description.isInstrument;
            entry.numInputs = description.numInputs;
            entry.numOutputs = description.numOutputs;
            entry.hasMidi = description.hasMidi;
            entry.scanTimestamp = result.timestamp;
            entry.fileModTime = juce::File(description.path).exists()
                ? juce::File(description.path).getLastModificationTime().toMilliseconds()
                : 0;
            cache.addOrUpdate(entry);
            ++insertCount;
        }

        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginScanCacheWriterCore::persistSuccessfulResult"
                " path=\"" + result.candidatePath + "\""
                " inserted=" + juce::String(insertCount)
                + " cacheCount=" + juce::String(cache.getCount())
                + " resultType=" + juce::String((int) result.resultType));

        return insertCount;
    }
};

} // namespace DAW
