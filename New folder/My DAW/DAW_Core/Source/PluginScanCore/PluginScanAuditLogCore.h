#pragma once
#include <JuceHeader.h>

#if JUCE_WINDOWS
 #include <process.h>
#endif

namespace DAW {

class PluginScanAuditLogCore
{
public:
    static juce::File getRootDirectory()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW_Core");
        dir.createDirectory();
        return dir;
    }

    static juce::File getLogFile(const juce::String& fileName)
    {
        return getRootDirectory().getChildFile(fileName);
    }

    static juce::File getStartupTraceFile()
    {
        return getLogFile("scan_startup_trace.log");
    }

    static void appendLine(const juce::String& fileName, const juce::String& line)
    {
        auto file = getLogFile(fileName);
        file.appendText(timestampPrefix() + line + "\n");
    }

    static void appendStartupTrace(const juce::String& functionName,
                                   const juce::String& selectedPath,
                                   int cacheCount,
                                   int failureCount,
                                   const juce::String& details = {})
    {
        juce::String line = juce::String("pid=") + juce::String((int)getCurrentProcessId())
            + " function=" + functionName
            + " mode=" + selectedPath
            + " cacheCount=" + juce::String(cacheCount)
            + " failureCount=" + juce::String(failureCount)
            + " cachePath=\"" + getDefaultCacheFile().getFullPathName() + "\""
            + " failurePath=\"" + getDefaultFailureFile().getFullPathName() + "\"";

        if (details.isNotEmpty())
            line += " " + details;

        appendLine("scan_startup_trace.log", line);
    }

    static void replaceWithHeader(const juce::String& fileName, const juce::String& header)
    {
        auto file = getLogFile(fileName);
        file.replaceWithText(timestampPrefix() + header + "\n");
    }

    static void resetLogs()
    {
        static const char* names[] = {
            "scan_worker_entry.log",
            "scan_worker_candidates.log",
            "scan_worker_results.log",
            "scan_host_ipc.log",
            "scan_summary.log"
        };

        for (auto* name : names)
            getLogFile(name).deleteFile();
    }

private:
    static juce::File getDefaultCacheFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW_Core")
            .getChildFile("plugin_cache.xml");
    }

    static juce::File getDefaultFailureFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW_Core")
            .getChildFile("plugin_scan_failures.xml");
    }

    static int getCurrentProcessId()
    {
#if JUCE_WINDOWS
        return _getpid();
#else
        return 0;
#endif
    }

    static juce::String timestampPrefix()
    {
        return "[" + juce::Time::getCurrentTime().toString(true, true, true, true) + "] ";
    }
};

} // namespace DAW
