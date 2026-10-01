#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * PluginWindowScaleStore
 *
 * Nucleus: persists floating plugin window scale preferences.
 * Supports:
 *   - Universal default scale (applied to all plugin windows)
 *   - Per-plugin scale overrides (keyed by plugin identity)
 *
 * Persistence: JSON file in user app data directory.
 * Survives DAW restart, new project creation, project switches.
 */
class PluginWindowScaleStore
{
public:
    static PluginWindowScaleStore& getInstance()
    {
        static PluginWindowScaleStore instance;
        return instance;
    }

    /** Build a stable identity key for a plugin. */
    static juce::String makePluginKey(const juce::String& format,
                                       const juce::String& manufacturer,
                                       const juce::String& name,
                                       const juce::String& uniqueId = {})
    {
        auto key = format + "|" + manufacturer + "|" + name;
        if (uniqueId.isNotEmpty())
            key += "|" + uniqueId;
        return key;
    }

    // ── Universal scale ──────────────────────────────────────────────────

    float getUniversalScale() const { return universalScale_; }

    void setUniversalScale(float s)
    {
        universalScale_ = s;
        DBG("[PluginWindowScaleStore] Universal scale set: " + juce::String((int)(s * 100)) + "%");
        save();
    }

    // ── Per-plugin scale ─────────────────────────────────────────────────

    bool hasPluginScale(const juce::String& pluginKey) const
    {
        return pluginScales_.count(pluginKey) > 0;
    }

    float getPluginScale(const juce::String& pluginKey) const
    {
        auto it = pluginScales_.find(pluginKey);
        return it != pluginScales_.end() ? it->second : universalScale_;
    }

    void setPluginScale(const juce::String& pluginKey, float s)
    {
        pluginScales_[pluginKey] = s;
        DBG("[PluginWindowScaleStore] Plugin scale set: key=\"" + pluginKey
            + "\" scale=" + juce::String((int)(s * 100)) + "%");
        save();
    }

    void clearPluginScale(const juce::String& pluginKey)
    {
        pluginScales_.erase(pluginKey);
        DBG("[PluginWindowScaleStore] Plugin scale cleared: key=\"" + pluginKey + "\"");
        save();
    }

    /** Get effective scale for a plugin: per-plugin override if set, else universal. */
    float getEffectiveScale(const juce::String& pluginKey) const
    {
        auto it = pluginScales_.find(pluginKey);
        return it != pluginScales_.end() ? it->second : universalScale_;
    }

private:
    float universalScale_ = 1.0f;
    std::map<juce::String, float> pluginScales_;

    PluginWindowScaleStore()
    {
        load();
    }

    juce::File getStoreFile() const
    {
        auto dir = juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory).getChildFile("DAW_Core");
        dir.createDirectory();
        return dir.getChildFile("plugin_window_scales.json");
    }

    void save()
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty("universalScale", (double)universalScale_);

        auto pluginObj = std::make_unique<juce::DynamicObject>();
        for (auto& [key, scale] : pluginScales_)
            pluginObj->setProperty(key, (double)scale);
        obj->setProperty("pluginScales", juce::var(pluginObj.release()));

        auto json = juce::JSON::toString(juce::var(obj.release()));
        getStoreFile().replaceWithText(json);
        DBG("[PluginWindowScaleStore] Saved to: " + getStoreFile().getFullPathName());
    }

    void load()
    {
        auto file = getStoreFile();
        if (!file.existsAsFile()) return;

        auto parsed = juce::JSON::parse(file.loadFileAsString());
        if (auto* obj = parsed.getDynamicObject())
        {
            if (obj->hasProperty("universalScale"))
                universalScale_ = (float)(double)obj->getProperty("universalScale");

            if (auto* pObj = obj->getProperty("pluginScales").getDynamicObject())
            {
                for (auto& prop : pObj->getProperties())
                    pluginScales_[prop.name.toString()] = (float)(double)prop.value;
            }
        }
        DBG("[PluginWindowScaleStore] Loaded: universalScale=" + juce::String((int)(universalScale_ * 100))
            + "% pluginOverrides=" + juce::String((int)pluginScales_.size()));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindowScaleStore)
};

} // namespace DAW
