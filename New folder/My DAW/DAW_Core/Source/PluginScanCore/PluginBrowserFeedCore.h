#pragma once
#include <JuceHeader.h>
#include "PluginScanAuditLogCore.h"
#include "../PluginStorageCore/PluginCacheCore.h"

namespace DAW {

class PluginBrowserFeedCore
{
public:
    void rebuildFromCache(const PluginCacheCore& cache, juce::KnownPluginList& knownPlugins) const
    {
        auto knownBefore = knownPlugins.getNumTypes();
        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginBrowserFeedCore::rebuildFromCache start"
                " cacheCount=" + juce::String(cache.getCount())
                + " knownBefore=" + juce::String(knownBefore));

        knownPlugins.clear();
        cache.populateKnownList(knownPlugins);

        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginBrowserFeedCore::rebuildFromCache end"
                " cacheCount=" + juce::String(cache.getCount())
                + " knownAfter=" + juce::String(knownPlugins.getNumTypes()));
    }
};

} // namespace DAW
