#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * ShortcutPersistenceManager — saves and loads shortcut engine preferences.
 *
 * Persists:
 *   - Active profile id
 *   - Shortcut platform style (Apple/Windows)
 *   - GUI visual style (Apple/Windows)
 *   - Help window position and size
 */
class ShortcutPersistenceManager
{
public:
    static ShortcutPersistenceManager& getInstance()
    {
        static ShortcutPersistenceManager instance;
        return instance;
    }

    /** Save a string setting. */
    void set(const juce::String& key, const juce::String& value)
    {
        props_.setValue(key, value);
    }

    /** Read a string setting. */
    juce::String get(const juce::String& key, const juce::String& fallback = {}) const
    {
        return props_.getValue(key, fallback);
    }

    /** Save an int setting. */
    void setInt(const juce::String& key, int value)
    {
        props_.setValue(key, value);
    }

    /** Read an int setting. */
    int getInt(const juce::String& key, int fallback = 0) const
    {
        return props_.getIntValue(key, fallback);
    }

    // Convenience keys
    static constexpr const char* kActiveProfileId     = "shortcut_active_profile";
    static constexpr const char* kShortcutPlatform    = "shortcut_platform_style";
    static constexpr const char* kGUIVisualStyle      = "gui_visual_style";
    static constexpr const char* kHelpWindowX         = "help_window_x";
    static constexpr const char* kHelpWindowY         = "help_window_y";
    static constexpr const char* kHelpWindowW         = "help_window_w";
    static constexpr const char* kHelpWindowH         = "help_window_h";

    /** Save help window bounds. */
    void saveHelpWindowBounds(juce::Rectangle<int> bounds)
    {
        setInt(kHelpWindowX, bounds.getX());
        setInt(kHelpWindowY, bounds.getY());
        setInt(kHelpWindowW, bounds.getWidth());
        setInt(kHelpWindowH, bounds.getHeight());
    }

    /** Load help window bounds (returns empty rect if never saved). */
    juce::Rectangle<int> loadHelpWindowBounds() const
    {
        int w = getInt(kHelpWindowW, 0);
        int h = getInt(kHelpWindowH, 0);
        if (w <= 0 || h <= 0) return {};
        return { getInt(kHelpWindowX), getInt(kHelpWindowY), w, h };
    }

private:
    ShortcutPersistenceManager() = default;

    juce::PropertySet props_;

    JUCE_DECLARE_NON_COPYABLE(ShortcutPersistenceManager)
};

} // namespace DAW
