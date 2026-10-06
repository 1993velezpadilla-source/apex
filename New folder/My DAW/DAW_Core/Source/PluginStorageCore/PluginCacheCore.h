#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * PluginCacheCore
 *
 * Nucleus: persistent cache of successfully scanned plugin metadata.
 * On startup, loads cached results so we skip full rescans.
 * Only rescans when paths change, user requests, or cache is stale.
 */
class PluginCacheCore
{
public:
    struct CachedPlugin
    {
        juce::String path;
        juce::String format;         // "VST3", "VST", etc.
        juce::String name;
        juce::String manufacturer;
        juce::String category;
        juce::String uniqueId;
        juce::String version;
        bool         isInstrument  = false;
        int          numInputs     = 0;
        int          numOutputs    = 0;
        bool         hasMidi       = false;
        juce::int64  fileModTime   = 0;  // last-modified time of plugin binary
        juce::int64  scanTimestamp = 0;  // when we scanned it
    };

    PluginCacheCore() = default;

    // ── Query ────────────────────────────────────────────────────────────

    bool hasCachedEntry(const juce::String& pluginPath) const
    {
        for (auto& c : cache_)
            if (c.path.equalsIgnoreCase(pluginPath)) return true;
        return false;
    }

    bool isCacheStale(const juce::String& pluginPath) const
    {
        // Check mod-time against any entry for this file path
        for (auto& c : cache_)
        {
            if (c.path.equalsIgnoreCase(pluginPath))
            {
                juce::File f(pluginPath);
                if (!f.exists()) return true;
                return f.getLastModificationTime().toMilliseconds() != c.fileModTime;
            }
        }
        return true; // not cached = stale
    }

    const std::vector<CachedPlugin>& getAll() const { return cache_; }
    int getCount() const { return (int)cache_.size(); }

    /** Returns unique file paths that are cached (for skip-list building). */
    juce::StringArray getCachedPaths() const
    {
        juce::StringArray paths;
        for (auto& c : cache_)
            paths.addIfNotAlreadyThere(c.path);
        return paths;
    }

    // ── Mutate ───────────────────────────────────────────────────────────

    void addOrUpdate(const CachedPlugin& entry)
    {
        // Key on path+uniqueId so shell VST3s (Waves, FabFilter, etc.) that
        // expose 100+ plugins from one file each get their own cache entry.
        for (auto& c : cache_)
        {
            if (c.path.equalsIgnoreCase(entry.path) && c.uniqueId == entry.uniqueId)
            {
                c = entry;
                DBG("[PluginCacheCore] Updated: " + entry.name + " | key=" + entry.path + "+" + entry.uniqueId
                    + " | manufacturer=" + entry.manufacturer + " | format=" + entry.format
                    + " | modTime=" + juce::String(entry.fileModTime) + " | total=" + juce::String((int)cache_.size()));
                return;
            }
        }
        cache_.push_back(entry);
        DBG("[PluginCacheCore] Inserted: " + entry.name + " | key=" + entry.path + "+" + entry.uniqueId
            + " | manufacturer=" + entry.manufacturer + " | format=" + entry.format
            + " | modTime=" + juce::String(entry.fileModTime) + " | total=" + juce::String((int)cache_.size()));
    }

    void remove(const juce::String& pluginPath)
    {
        // Remove ALL entries for this path (all sub-plugins of a shell VST3)
        cache_.erase(
            std::remove_if(cache_.begin(), cache_.end(),
                [&](const CachedPlugin& c) { return c.path.equalsIgnoreCase(pluginPath); }),
            cache_.end());
    }

    void clear() { cache_.clear(); }

    // ── Build KnownPluginList from cache ─────────────────────────────────

    /** Populate a KnownPluginList from cached metadata (avoids loading plugins). */
    void populateKnownList(juce::KnownPluginList& list) const
    {
        for (auto& c : cache_)
        {
            juce::PluginDescription desc;
            desc.fileOrIdentifier     = c.path;
            desc.pluginFormatName     = c.format;
            desc.name                 = c.name;
            desc.manufacturerName     = c.manufacturer;
            desc.category             = c.category;
            desc.uniqueId             = c.uniqueId.getIntValue();
            if (desc.uniqueId == 0 && c.uniqueId.isNotEmpty() && c.uniqueId != "0")
                desc.uniqueId = c.uniqueId.hashCode();
            desc.version              = c.version;
            desc.isInstrument         = c.isInstrument;
            desc.numInputChannels     = c.numInputs;
            desc.numOutputChannels    = c.numOutputs;
            list.addType(desc);
        }
    }

    /** Add a scanned PluginDescription to the cache. */
    void cacheFromDescription(const juce::PluginDescription& desc)
    {
        CachedPlugin entry;
        entry.path          = desc.fileOrIdentifier;
        entry.format        = desc.pluginFormatName;
        entry.name          = desc.name;
        entry.manufacturer  = desc.manufacturerName;
        entry.category      = desc.category;
        entry.uniqueId      = juce::String(desc.uniqueId);
        entry.version       = desc.version;
        entry.isInstrument  = desc.isInstrument;
        entry.numInputs     = desc.numInputChannels;
        entry.numOutputs    = desc.numOutputChannels;
        entry.scanTimestamp  = juce::Time::currentTimeMillis();

        juce::File f(desc.fileOrIdentifier);
        entry.fileModTime = f.exists()
            ? f.getLastModificationTime().toMilliseconds() : 0;

        addOrUpdate(entry);
    }

    // ── Persistence ──────────────────────────────────────────────────────

    void saveToFile(const juce::File& f) const
    {
        auto root = std::make_unique<juce::XmlElement>("PluginCache");
        for (auto& c : cache_)
        {
            auto* node = root->createNewChildElement("Plugin");
            node->setAttribute("path",          c.path);
            node->setAttribute("format",        c.format);
            node->setAttribute("name",          c.name);
            node->setAttribute("manufacturer",  c.manufacturer);
            node->setAttribute("category",      c.category);
            node->setAttribute("uniqueId",      c.uniqueId);
            node->setAttribute("version",       c.version);
            node->setAttribute("isInstrument",  c.isInstrument);
            node->setAttribute("numInputs",     c.numInputs);
            node->setAttribute("numOutputs",    c.numOutputs);
            node->setAttribute("hasMidi",       c.hasMidi);
            node->setAttribute("fileModTime",   juce::String(c.fileModTime));
            node->setAttribute("scanTimestamp", juce::String(c.scanTimestamp));
        }
        root->writeTo(f);
    }

    void loadFromFile(const juce::File& f)
    {
        cache_.clear();
        if (auto xml = juce::XmlDocument::parse(f))
        {
            for (auto* node : xml->getChildIterator())
            {
                if (node->getTagName() != "Plugin") continue;
                CachedPlugin c;
                c.path          = node->getStringAttribute("path");
                c.format        = node->getStringAttribute("format");
                c.name          = node->getStringAttribute("name");
                c.manufacturer  = node->getStringAttribute("manufacturer");
                c.category      = node->getStringAttribute("category");
                c.uniqueId      = node->getStringAttribute("uniqueId");
                c.version       = node->getStringAttribute("version");
                c.isInstrument  = node->getBoolAttribute("isInstrument", false);
                c.numInputs     = node->getIntAttribute("numInputs", 0);
                c.numOutputs    = node->getIntAttribute("numOutputs", 0);
                c.hasMidi       = node->getBoolAttribute("hasMidi", false);
                c.fileModTime   = node->getStringAttribute("fileModTime", "0").getLargeIntValue();
                c.scanTimestamp = node->getStringAttribute("scanTimestamp", "0").getLargeIntValue();
                cache_.push_back(std::move(c));
            }
        }
    }

    static juce::File getDefaultFile()
    {
        return juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory)
                .getChildFile("DAW_Core")
                .getChildFile("plugin_cache.xml");
    }

private:
    std::vector<CachedPlugin> cache_;
};

} // namespace DAW
