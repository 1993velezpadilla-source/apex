#pragma once
#include <JuceHeader.h>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <tlhelp32.h>
#endif
#include "../PluginSafetyCore/PluginProtectionRuntimeGuardCore.h"
#include "../PluginSafetyCore/MessageBoxSuppressorCore.h"
#include "../PluginSecurityCore/PluginLoadFailureClassifierCore.h"
#include "../PluginSecurityCore/PluginDependencyPreScanCore.h"

namespace DAW {

/**
 * PluginScanWorkerCore
 *
 * Nucleus: the sacrificial subprocess scanner.
 *
 * Batch mode (--scan-batch):
 *   Scans ALL plugins in the given search paths using JUCE's
 *   PluginDirectoryScanner with a deadman file. Emits one
 *   PLUGIN_BEGIN / PLUGIN_OK / PLUGIN_FAIL block per plugin found.
 *   If the subprocess crashes, the parent reads the deadman file
 *   to know which plugin killed it, blacklists it, and relaunches.
 *
 * Single mode (--scan-plugin, legacy):
 *   Scans one plugin file. Kept as fallback for targeted retries.
 */
class PluginScanWorkerCore
{
public:
    // ── Batch mode — scan all plugins in search paths ────────────────────

    static int runBatch(const juce::String& searchPathsStr,
                        const juce::String& deadmanPath,
                        const juce::String& skipListStr)
    {
        // CRITICAL: Log immediately so we know we got here
        std::cout << "RUNBATCH_ENTERED" << std::endl;
        std::cout.flush();

        suppressErrorDialogs();
        PluginProtectionRuntimeGuardCore runtimeGuard;

        // Write results to a temp file alongside the deadman file.
        // This survives a subprocess crash — the coordinator reads it after exit.
        auto resultsFile = juce::File(deadmanPath).getSiblingFile("scan_results_partial.txt");
        resultsFile.getParentDirectory().createDirectory();
        resultsFile.deleteFile();

        auto emit = [&resultsFile](const juce::String& line)
        {
            std::cout << line.toStdString() << "\n";
            std::cout.flush();
            resultsFile.appendText(line + "\n");
        };

        juce::AudioPluginFormatManager formatManager;
#if JUCE_PLUGINHOST_VST3
        formatManager.addFormat(new juce::VST3PluginFormat());
#endif
#if JUCE_PLUGINHOST_VST
        formatManager.addFormat(new juce::VSTPluginFormat());
#endif

        juce::FileSearchPath searchPaths(searchPathsStr);
        juce::File           deadmanFile(deadmanPath);
        juce::StringArray    skipFiles;
        skipFiles.addTokens(skipListStr, "|", "");

        // Obj 6: Log search paths and format registration
        emit("WORKER_FORMAT_COUNT=" + juce::String(formatManager.getNumFormats()));
        for (int i = 0; i < formatManager.getNumFormats(); ++i)
        {
            auto* fmt = formatManager.getFormat(i);
            emit("WORKER_FORMAT=" + (fmt ? fmt->getName() : juce::String("null")));
        }
        emit("WORKER_SEARCH_PATH_COUNT=" + juce::String(searchPaths.getNumPaths()));
        for (int i = 0; i < searchPaths.getNumPaths(); ++i)
            emit("WORKER_SEARCH_PATH=" + searchPaths[i].getFullPathName());
        emit("WORKER_SKIP_COUNT=" + juce::String(skipFiles.size()));

        juce::KnownPluginList knownList;

        // Obj 5: Stage counters
        int totalCandidatesScanned = 0;
        int okCount = 0;
        int failCount = 0;
        int skippedCount = 0;
        int totalDescriptionsAdded = 0;
        juce::String currentCandidate;

        try
        {
            for (int f = 0; f < formatManager.getNumFormats(); ++f)
            {
                auto* format = formatManager.getFormat(f);
                if (!format) continue;

                // PluginDirectoryScanner with recursive=true finds all .vst3
                // bundles including those in subdirectories
                juce::PluginDirectoryScanner scanner(
                    knownList, *format, searchPaths,
                    /*recursive=*/true, deadmanFile,
                    /*allowAsync=*/false);

                emit("SCANNING_FORMAT=" + format->getName());

                juce::String pluginName;
                int prevCount = knownList.getNumTypes();

                auto emitNewTypes = [&](const juce::String& candidate, int countBefore) -> int
                {
                    int countAfter = knownList.getNumTypes();
                    int added = countAfter - countBefore;
                    if (added > 0)
                    {
                        for (int i = countBefore; i < countAfter; ++i)
                        {
                            auto* desc = knownList.getType(i);
                            if (!desc) continue;
                            if (skipFiles.contains(desc->fileOrIdentifier, true))
                            {
                                skippedCount++;
                                emit("PLUGIN_SKIPPED");
                                emit("path=" + desc->fileOrIdentifier);
                                emit("format=" + desc->pluginFormatName);
                                emit("name=" + desc->name);
                                emit("manufacturer=" + desc->manufacturerName);
                                emit("uniqueId=" + juce::String(desc->uniqueId));
                                emit("reason=InSkipList");
                                emit("timestamp=" + juce::String(juce::Time::currentTimeMillis()));
                                emit("PLUGIN_END");
                                continue;
                            }
                            okCount++;
                            totalDescriptionsAdded++;
                            emit("PLUGIN_OK");
                            emit("path=" + desc->fileOrIdentifier);
                            emit("name=" + desc->name);
                            emit("manufacturer=" + desc->manufacturerName);
                            emit("format=" + desc->pluginFormatName);
                            emit("category=" + desc->category);
                            emit("uniqueId=" + juce::String(desc->uniqueId));
                            emit("isInstrument=" + juce::String(desc->isInstrument ? "1" : "0"));
                            emit("numInputs=" + juce::String(desc->numInputChannels));
                            emit("numOutputs=" + juce::String(desc->numOutputChannels));
                            emit("version=" + desc->version);
                            emit("addedTypes=" + juce::String(added));
                            emit("timestamp=" + juce::String(juce::Time::currentTimeMillis()));
                            emit("PLUGIN_END");
                        }
                    }
                    return countAfter;
                };

                while (scanner.scanNextFile(true, pluginName))
                {
                    currentCandidate = pluginName;
                    totalCandidatesScanned++;
                    prevCount = emitNewTypes(pluginName, prevCount);
                }

                // Handle last file — scanNextFile returns false after scanning it,
                // so the loop body never ran for those types.
                {
                    int finalCount = knownList.getNumTypes();
                    if (finalCount > prevCount)
                    {
                        totalCandidatesScanned++;
                        prevCount = emitNewTypes(pluginName, prevCount);
                    }
                }

                emit("FORMAT_FILES_SCANNED=" + juce::String(totalCandidatesScanned));
                emit("FORMAT_PLUGINS_FOUND=" + juce::String(knownList.getNumTypes()));

                for (auto& fail : scanner.getFailedFiles())
                {
                    if (skipFiles.contains(fail, true)) continue;
                    failCount++;
                    emit("PLUGIN_FAIL");
                    emit("path=" + fail);
                    emit("format=" + format->getName());
                    emit("name=");
                    emit("manufacturer=");
                    emit("uniqueId=");
                    emit("reason=ScannerReportedFailure");
                    emit("error=Scan failed");
                    emit("addedTypes=0");
                    emit("timestamp=" + juce::String(juce::Time::currentTimeMillis()));
                    emit("PLUGIN_END");
                }
            }
        }
        catch (const std::exception& ex)
        {
            emit("PLUGIN_FAIL");
            emit("path=" + currentCandidate);
            emit("format=Unknown");
            emit("name=");
            emit("manufacturer=");
            emit("uniqueId=");
            emit("reason=UnexpectedException");
            emit("error=" + juce::String(ex.what()));
            emit("addedTypes=0");
            emit("timestamp=" + juce::String(juce::Time::currentTimeMillis()));
            emit("PLUGIN_END");
            failCount++;
        }
        catch (...)
        {
            emit("PLUGIN_FAIL");
            emit("path=" + currentCandidate);
            emit("format=Unknown");
            emit("name=");
            emit("manufacturer=");
            emit("uniqueId=");
            emit("reason=UnknownException");
            emit("error=Unknown exception during batch scan");
            emit("addedTypes=0");
            emit("timestamp=" + juce::String(juce::Time::currentTimeMillis()));
            emit("PLUGIN_END");
            failCount++;
        }

        // Obj 5: Emit summary counters
        emit("SCAN_SUMMARY");
        emit("totalCandidatesScanned=" + juce::String(totalCandidatesScanned));
        emit("okCount=" + juce::String(okCount));
        emit("failCount=" + juce::String(failCount));
        emit("skippedCount=" + juce::String(skippedCount));
        emit("totalDescriptionsAdded=" + juce::String(totalDescriptionsAdded));
        emit("knownListSize=" + juce::String(knownList.getNumTypes()));

        emit("SCAN_COMPLETE");
        emit("timestamp=" + juce::String(juce::Time::currentTimeMillis()));

        // Obj 3: Clear deadman on clean success
        deadmanFile.deleteFile();

        return 0;
    }

    // ── Single mode (legacy/retry) ────────────────────────────────────────

    static int run(const juce::String& pluginPath, const juce::String& formatName)
    {
        suppressErrorDialogs();
        PluginProtectionRuntimeGuardCore runtimeGuard;

        juce::AudioPluginFormatManager formatManager;
#if JUCE_PLUGINHOST_VST3
        if (formatName == "VST3")
            formatManager.addFormat(new juce::VST3PluginFormat());
#endif
#if JUCE_PLUGINHOST_VST
        if (formatName == "VST")
            formatManager.addFormat(new juce::VSTPluginFormat());
#endif

        if (formatManager.getNumFormats() == 0)
        {
            emitFailure(pluginPath, "No format handler for: " + formatName, "scan_worker_format_setup");
            return 1;
        }

        try
        {
            return doSingleScan(formatManager, pluginPath, formatName);
        }
        catch (const std::exception& e)
        {
            emitFailure(pluginPath, juce::String("Exception: ") + e.what(), "scan_worker_exception");
            return 1;
        }
        catch (...)
        {
            emitFailure(pluginPath, "Unknown exception during scan", "scan_worker_exception");
            return 1;
        }
    }

private:
    static void suppressErrorDialogs()
    {
#if JUCE_WINDOWS
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        SetProcessDEPPolicy(0);
        {
            typedef BOOL (WINAPI *WerSetFlagsFunc)(DWORD);
            if (auto* mod = GetModuleHandleA("kernel32.dll"))
                if (auto fn = (WerSetFlagsFunc)GetProcAddress(mod, "WerSetFlags"))
                    fn(0x0010);
        }

        // Suppress TLS/Wibu "initialization failed" MessageBox using a plain Win32
        // thread — juce::Timer deadlocks here because there is no MessageManager.
        // These dialogs appear when Wibu DLL is blocked by Application Control policy.
        struct DlgKiller {
            static DWORD WINAPI run(LPVOID) {
                while (true) {
                    EnumWindows([](HWND hwnd, LPARAM) -> BOOL {
                        wchar_t cls[64] = {};
                        GetClassNameW(hwnd, cls, 64);
                        if (wcscmp(cls, L"#32770") == 0) {
                            wchar_t title[256] = {};
                            GetWindowTextW(hwnd, title, 256);
                            auto t = juce::String(title).toLowerCase();
                            if (t.contains("tls") || t.contains("wibu") ||
                                t.contains("failed") || t.contains("blocked"))
                            {
                                HWND btn = FindWindowExW(hwnd, nullptr, L"Button", nullptr);
                                PostMessageW(btn ? btn : hwnd,
                                             btn ? BM_CLICK : WM_CLOSE, 0, 0);
                            }
                        }
                        return TRUE;
                    }, 0);
                    Sleep(30);
                }
                return 0;
            }
        };
        static bool started = false;
        if (!started) {
            started = true;
            CloseHandle(CreateThread(nullptr, 0, DlgKiller::run, nullptr, 0, nullptr));
        }
#endif
    }

    static int doSingleScan(juce::AudioPluginFormatManager& formatManager,
                            const juce::String& pluginPath,
                            const juce::String& formatName)
    {
        juce::KnownPluginList knownList;
        auto* format = formatManager.getFormat(0);
        juce::FileSearchPath singlePath;
        juce::File pluginFile(pluginPath);

        if (pluginFile.existsAsFile() || pluginFile.isDirectory())
            singlePath.add(pluginFile.getParentDirectory());
        else
        {
            emitFailure(pluginPath, "Plugin file not found: " + pluginPath, "scan_worker_validation");
            return 1;
        }

        juce::PluginDirectoryScanner scanner(
            knownList, *format, singlePath, false, juce::File());

        juce::String name;
        while (scanner.scanNextFile(true, name)) {}

        auto types = knownList.getTypes();
        for (auto& desc : types)
        {
            if (juce::File(desc.fileOrIdentifier).getFullPathName()
                    .equalsIgnoreCase(pluginFile.getFullPathName()))
            {
                writeResult("status", "OK");
                writeResult("name",          desc.name);
                writeResult("manufacturer",  desc.manufacturerName);
                writeResult("format",        desc.pluginFormatName);
                writeResult("category",      desc.category);
                writeResult("uniqueId",      juce::String(desc.uniqueId));
                writeResult("deprecatedUid", juce::String(desc.deprecatedUid));
                writeResult("isInstrument",  desc.isInstrument ? "1" : "0");
                writeResult("numInputs",     juce::String(desc.numInputChannels));
                writeResult("numOutputs",    juce::String(desc.numOutputChannels));
                writeResult("hasMidi",       "0");
                writeResult("version",       desc.version);
                return 0;
            }
        }

        auto failures = scanner.getFailedFiles();
        juce::String failInfo;
        for (auto& f : failures) failInfo += f + "; ";
        emitFailure(pluginPath,
            failInfo.isEmpty() ? "Plugin not recognised by " + formatName + " format"
                               : "Scan failures: " + failInfo,
            "scan_worker_result");
        return 1;
    }

    static void writeResult(const juce::String& key, const juce::String& value)
    {
        std::cout << key.toStdString() << "=" << value.toStdString() << std::endl;
    }

    static void emitFailure(const juce::String& pluginPath,
                            const juce::String& rawError,
                            const juce::String& loadStage)
    {
        auto failure = PluginLoadFailureClassifierCore::classify(pluginPath, rawError, loadStage);
        writeResult("status", "FAIL");
        writeResult("error", rawError);
        writeResult("blockedDll", failure.blockedDllPath);
        writeResult("protectionVendor", failure.protectionVendor);
        writeResult("loadStage", loadStage);
        writeResult("errorCategory", juce::String((int) failure.category));
    }
};

} // namespace DAW
