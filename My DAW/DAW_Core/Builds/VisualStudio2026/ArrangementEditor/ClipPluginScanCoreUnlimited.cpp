// ===========================================================================
// ClipPluginScanCoreUnlimited.cpp
// SCANS ALL PLUGINS - NO HARDCODED LIMITS
// ===========================================================================
#include "ClipPluginScanCoreUnlimited.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <algorithm>

namespace ClipPlugins
{
    const char* ClipPluginScanCore::k_cacheFiles[] = {
        "/reaper-vstplugins64.ini",
        "/reaper-vstshells64.ini",
        "/reaper-clap-win64.ini",
        "/reaper-vstplugins.ini",
        "/reaper-ara2plugins.ini"
    };

    ClipPluginScanCore::ClipPluginScanCore()
    {
    }

    void ClipPluginScanCore::setResourcePath(const std::string& path)
    {
        m_resourcePath = path;
    }

    void ClipPluginScanCore::scan()
    {
        m_done = true;
        m_plugins.clear();

        for (int i = 0; i < k_cacheFileCount; ++i)
        {
            scanFile(m_resourcePath + k_cacheFiles[i]);

            if (onProgressUpdate)
                onProgressUpdate((int)m_plugins.size());
        }

        if (onScanComplete)
            onScanComplete();
    }

    void ClipPluginScanCore::rescan()
    {
        m_done = false;
        scan();
    }

    std::vector<const ScannedPlugin*> ClipPluginScanCore::getARA2Plugins() const
    {
        std::vector<const ScannedPlugin*> out;
        for (auto& p : m_plugins)
            if (p.supportsARA2)
                out.push_back(&p);
        return out;
    }

    std::vector<const ScannedPlugin*> ClipPluginScanCore::getPluginsByFormat(PluginFormat fmt) const
    {
        std::vector<const ScannedPlugin*> out;
        for (auto& p : m_plugins)
            if (p.format == fmt)
                out.push_back(&p);
        return out;
    }

    std::vector<const ScannedPlugin*> ClipPluginScanCore::searchByName(const std::string& query) const
    {
        std::vector<const ScannedPlugin*> out;
        std::string lowerQuery = query;
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

        for (auto& p : m_plugins)
        {
            std::string lowerName = p.displayName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

            if (lowerName.find(lowerQuery) != std::string::npos)
                out.push_back(&p);
        }
        return out;
    }

    // -----------------------------------------------------------------------
    void ClipPluginScanCore::scanFile(const std::string& fullPath)
    {
        size_t len = 0;
        char* buf = readFileAlloc(fullPath.c_str(), &len);
        if (!buf) return;
        scanBuffer(buf, len);
        free(buf);
    }

    void ClipPluginScanCore::scanBuffer(const char* buf, size_t len)
    {
        const char* p   = buf;
        const char* end = buf + len;

        while (p < end)
        {
            const char* lineEnd = p;
            while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r') ++lineEnd;
            size_t lineLen = (size_t)(lineEnd - p);

            char name[512] = {}, ident[512] = {};
            if (extractFields(p, lineLen, ident, sizeof(ident), name, sizeof(name)) && name[0])
            {
                ScannedPlugin plugin;
                plugin.displayName = name;
                plugin.cacheIdent = ident;
                plugin.format = inferFormat(ident, name);
                plugin.supportsARA2 = isARA2Plugin(ident, name);
                plugin.vendor = extractVendor(name);

                m_plugins.push_back(plugin);
            }

            p = lineEnd;
            while (p < end && (*p == '\n' || *p == '\r')) ++p;
        }
    }

    // -----------------------------------------------------------------------
    PluginFormat ClipPluginScanCore::inferFormat(const char* ident, const char* name)
    {
        if (!ident) return PluginFormat::Unknown;

        std::string s(ident);
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);

        if (s.find(".vst3") != std::string::npos) return PluginFormat::VST3;
        if (s.find(".clap") != std::string::npos) return PluginFormat::CLAP;
        if (s.find("ara") != std::string::npos) return PluginFormat::ARA2;
        if (s.find(".dll") != std::string::npos) return PluginFormat::VST2;

        return PluginFormat::VST3; // default assumption
    }

    bool ClipPluginScanCore::isARA2Plugin(const char* ident, const char* name)
    {
        if (!ident || !name) return false;

        std::string identStr(ident);
        std::string nameStr(name);

        std::transform(identStr.begin(), identStr.end(), identStr.begin(), ::tolower);
        std::transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);

        // Check if in ARA2 cache file
        if (identStr.find("ara") != std::string::npos)
            return true;

        // Check known ARA2 plugin names
        const char* ara2Keywords[] = {
            "melodyne", "spectralayers", "revoice", "vocalign", "repitch",
            "ampire", "chord track"
        };

        for (auto keyword : ara2Keywords)
        {
            if (nameStr.find(keyword) != std::string::npos)
                return true;
        }

        return false;
    }

    std::string ClipPluginScanCore::extractVendor(const char* name)
    {
        if (!name) return "Unknown";

        std::string nameStr(name);

        // Common vendor patterns
        if (nameStr.find("Melodyne") != std::string::npos) return "Celemony";
        if (nameStr.find("SpectraLayers") != std::string::npos) return "Steinberg";
        if (nameStr.find("Auto-Tune") != std::string::npos) return "Antares";
        if (nameStr.find("Waves") != std::string::npos) return "Waves";
        if (nameStr.find("iZotope") != std::string::npos) return "iZotope";
        if (nameStr.find("Nectar") != std::string::npos) return "iZotope";
        if (nameStr.find("Revoice") != std::string::npos) return "Synchro Arts";
        if (nameStr.find("VocALign") != std::string::npos) return "Synchro Arts";
        if (nameStr.find("FabFilter") != std::string::npos) return "FabFilter";
        if (nameStr.find("Valhalla") != std::string::npos) return "Valhalla DSP";
        if (nameStr.find("Native Instruments") != std::string::npos) return "Native Instruments";

        // Extract from parentheses if present: "PluginName (Vendor)"
        size_t openParen = nameStr.find('(');
        size_t closeParen = nameStr.find(')');
        if (openParen != std::string::npos && closeParen != std::string::npos)
        {
            return nameStr.substr(openParen + 1, closeParen - openParen - 1);
        }

        return "Unknown";
    }

    bool ClipPluginScanCore::extractFields(const char* line, size_t lineLen,
                                            char* outIdent, int identLen,
                                            char* outName,  int nameLen)
    {
        // ident = text before '='
        const char* eq = (const char*)memchr(line, '=', lineLen);
        if (!eq) return false;
        int idl = (int)(eq - line);
        while (idl > 0 && (line[idl-1]==' '||line[idl-1]=='\t')) --idl;
        if (idl <= 0 || idl >= identLen) return false;
        memcpy(outIdent, line, idl); outIdent[idl] = '\0';

        // name = last comma field
        const char* lastComma = nullptr;
        for (size_t i = 0; i < lineLen; ++i)
            if (line[i] == ',') lastComma = line + i;
        if (!lastComma) return false;
        const char* name = lastComma + 1;
        int nl = (int)(line + lineLen - name);
        while (nl > 0 && (name[nl-1]=='\r'||name[nl-1]=='\n'||
                           name[nl-1]==' '||name[nl-1]=='\t')) --nl;
        if (nl <= 0 || nl >= nameLen) return false;
        memcpy(outName, name, nl); outName[nl] = '\0';

        return true;
    }

    char* ClipPluginScanCore::readFileAlloc(const char* path, size_t* outLen)
    {
        FILE* f = nullptr;
        fopen_s(&f, path, "rb");
        if (!f) return nullptr;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) { fclose(f); return nullptr; }
        char* buf = (char*)malloc((size_t)sz + 1);
        if (!buf)  { fclose(f); return nullptr; }
        size_t rd = fread(buf, 1, (size_t)sz, f);
        buf[rd] = '\0';
        fclose(f);
        if (outLen) *outLen = rd;
        return buf;
    }

} // namespace ClipPlugins
