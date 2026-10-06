// ===========================================================================
// ClipPluginScanCore.h
// UNLIMITED DYNAMIC PLUGIN SCANNER
// Scans ALL plugins from DAW cache, not just a hardcoded list.
// Detects ARA2 support dynamically from plugin metadata.
// ===========================================================================
#pragma once
#include <string>
#include <vector>
#include <functional>

namespace ClipPlugins
{
    enum class PluginFormat
    {
        VST2,
        VST3,
        CLAP,
        ARA2,
        Unknown
    };

    struct ScannedPlugin
    {
        std::string   displayName;      // from cache
        std::string   cacheIdent;       // full path/identifier
        std::string   vendor;           // extracted from name or path
        PluginFormat  format;
        bool          supportsARA2;     // detected from format or cache file
        bool          enabled = true;

        std::string formatString() const
        {
            switch (format)
            {
            case PluginFormat::VST2: return "VST2";
            case PluginFormat::VST3: return "VST3";
            case PluginFormat::CLAP: return "CLAP";
            case PluginFormat::ARA2: return "ARA2";
            default: return "Unknown";
            }
        }
    };

    class ClipPluginScanCore
    {
    public:
        ClipPluginScanCore();

        void setResourcePath(const std::string& path);

        // Scan ALL plugins (no limit)
        void scan();

        bool scanDone() const { return m_done; }

        // Get all scanned plugins
        const std::vector<ScannedPlugin>& allPlugins() const { return m_plugins; }

        // Filter by ARA2 support
        std::vector<const ScannedPlugin*> getARA2Plugins() const;

        // Filter by format
        std::vector<const ScannedPlugin*> getPluginsByFormat(PluginFormat fmt) const;

        // Search by name
        std::vector<const ScannedPlugin*> searchByName(const std::string& query) const;

        // Force rescan
        void rescan();

        // Callbacks
        std::function<void()> onScanComplete;
        std::function<void(int)> onProgressUpdate; // called with plugin count

    private:
        std::string                    m_resourcePath;
        bool                           m_done = false;
        std::vector<ScannedPlugin>     m_plugins;

        static const char* k_cacheFiles[];
        static constexpr int k_cacheFileCount = 5;

        void scanFile(const std::string& path);
        void scanBuffer(const char* buf, size_t len);

        static PluginFormat inferFormat(const char* ident, const char* name);
        static bool isARA2Plugin(const char* ident, const char* name);
        static std::string extractVendor(const char* name);
        static bool extractFields(const char* line, size_t len,
                                  char* outIdent, int identLen,
                                  char* outName,  int nameLen);
        static char* readFileAlloc(const char* path, size_t* outLen);
    };

} // namespace ClipPlugins
