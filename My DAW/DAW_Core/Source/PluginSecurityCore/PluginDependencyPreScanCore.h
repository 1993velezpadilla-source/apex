#pragma once
#include <JuceHeader.h>

#if JUCE_WINDOWS
#include <windows.h>
#include <imagehlp.h>
#pragma comment(lib, "imagehlp.lib")
#endif

namespace DAW {

/**
 * PluginDependencyPreScanCore
 *
 * Nucleus: pre-scans plugin DLL dependencies to detect blocked DLLs
 * BEFORE we try to load the plugin (which triggers Windows Security alerts).
 *
 * Checks:
 *   - Does the plugin import any known blocked DLLs? (Wibu, iLok, etc.)
 *   - Are those DLLs accessible on disk?
 *   - Would loading this plugin trigger Application Control block?
 *
 * If blocked dependencies detected → skip plugin, avoid Windows alerts.
 */
class PluginDependencyPreScanCore
{
public:
    struct DependencyCheckResult
    {
        bool hasBlockedDependencies = false;
        juce::StringArray blockedDlls;
        juce::String reason;
    };

    static DependencyCheckResult checkPlugin(const juce::String& pluginPath)
    {
        DependencyCheckResult result;

#if JUCE_WINDOWS
        // Read PE import table to get dependency list
        auto deps = getImportedDlls(pluginPath);

        for (auto& dep : deps)
        {
            if (isKnownBlockedDll(dep))
            {
                result.hasBlockedDependencies = true;
                result.blockedDlls.add(dep);
                result.reason = "Plugin depends on blocked DLL: " + dep;
                DBG("Pre-scan: " + pluginPath + " blocked due to dependency: " + dep);
            }
        }
#endif

        return result;
    }

private:
#if JUCE_WINDOWS
    static juce::StringArray getImportedDlls(const juce::String& dllPath)
    {
        juce::StringArray result;

        auto fileHandle = CreateFileW(dllPath.toWideCharPointer(),
            GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);

        if (fileHandle == INVALID_HANDLE_VALUE)
            return result;

        auto mapping = CreateFileMappingW(fileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping)
        {
            CloseHandle(fileHandle);
            return result;
        }

        auto baseAddr = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        if (!baseAddr)
        {
            CloseHandle(mapping);
            CloseHandle(fileHandle);
            return result;
        }

        // Parse PE headers
        auto dosHeader = (PIMAGE_DOS_HEADER)baseAddr;
        if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE)
        {
            auto ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)baseAddr + dosHeader->e_lfanew);
            if (ntHeaders->Signature == IMAGE_NT_SIGNATURE)
            {
                auto importDir = &ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
                if (importDir->Size > 0)
                {
                    auto importDesc = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)baseAddr + importDir->VirtualAddress);

                    while (importDesc->Name != 0)
                    {
                        auto dllName = (const char*)((BYTE*)baseAddr + importDesc->Name);
                        result.add(juce::String(dllName));
                        ++importDesc;
                    }
                }
            }
        }

        UnmapViewOfFile(baseAddr);
        CloseHandle(mapping);
        CloseHandle(fileHandle);

        return result;
    }

    static bool isKnownBlockedDll(const juce::String& dllName)
    {
        auto lower = dllName.toLowerCase();

        // ONLY block DLLs we KNOW are blocked by Application Control
        // Be very conservative — only Wibu TLS DLLs with the exact pattern
        return lower.contains("wibu-tls-") ||
               lower.startsWith("wibu-tls") ||
               (lower.contains("wibu") && lower.contains("-tls-"));
    }
#endif
};

} // namespace DAW
