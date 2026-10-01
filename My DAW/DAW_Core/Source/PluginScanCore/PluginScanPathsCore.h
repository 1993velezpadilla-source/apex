#pragma once
#include <JuceHeader.h>
#include "PluginScanAuditLogCore.h"
#include "PluginScanFormatsCore.h"

namespace DAW {

class PluginScanPathsCore
{
public:
    void buildDefaultPaths(const PluginScanFormatsCore& formats)
    {
        juce::FileSearchPath combined;
        auto addUnique = [&combined](const juce::File& dir)
        {
            auto normalised = juce::File(dir.getFullPathName());
            bool exists = normalised.isDirectory();
            bool alreadyAdded = false;
            for (int i = 0; i < combined.getNumPaths(); ++i)
                if (combined[i].getFullPathName().equalsIgnoreCase(normalised.getFullPathName()))
                    alreadyAdded = true;

            PluginScanAuditLogCore::appendLine("scan_summary.log",
                "DEFAULT_PATH path=\"" + normalised.getFullPathName() + "\" exists=" + juce::String(exists ? 1 : 0)
                    + " alreadyAdded=" + juce::String(alreadyAdded ? 1 : 0));

            if (!dir.isDirectory())
                return;

            for (int i = 0; i < combined.getNumPaths(); ++i)
                if (combined[i].getFullPathName().equalsIgnoreCase(normalised.getFullPathName()))
                    return;

            combined.add(normalised);
        };

        auto& manager = formats.getManager();
        for (int i = 0; i < manager.getNumFormats(); ++i)
        {
            if (auto* format = manager.getFormat(i))
            {
                auto defaults = format->getDefaultLocationsToSearch();
                for (int j = 0; j < defaults.getNumPaths(); ++j)
                    addUnique(defaults[j]);
            }
        }

        addUnique(juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory)
                      .getChildFile("Common Files\\VST3"));
        addUnique(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("Steinberg\\VST3"));
        addUnique(juce::File("C:\\Program Files\\Common Files\\VST3"));
        addUnique(juce::File("C:\\Program Files (x86)\\Common Files\\VST3"));
#if JUCE_PLUGINHOST_VST
        addUnique(juce::File("C:\\Program Files\\Steinberg\\VstPlugins"));
        addUnique(juce::File("C:\\Program Files (x86)\\Steinberg\\VstPlugins"));
        addUnique(juce::File("C:\\VstPlugins"));
        addUnique(juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory)
                      .getChildFile("VstPlugins"));
#endif

        searchPaths_ = combined;
    }

    void addSearchPath(const juce::File& dir)
    {
        if (!dir.isDirectory())
            return;

        for (int i = 0; i < searchPaths_.getNumPaths(); ++i)
            if (searchPaths_[i].getFullPathName().equalsIgnoreCase(dir.getFullPathName()))
                return;

        searchPaths_.add(dir);
    }

    const juce::FileSearchPath& getSearchPaths() const { return searchPaths_; }

    juce::StringArray getPathStrings() const
    {
        juce::StringArray paths;
        for (int i = 0; i < searchPaths_.getNumPaths(); ++i)
            paths.add(searchPaths_[i].getFullPathName());
        return paths;
    }

    juce::String toDisplayString() const
    {
        return searchPaths_.toString();
    }

private:
    juce::FileSearchPath searchPaths_;
};

} // namespace DAW
