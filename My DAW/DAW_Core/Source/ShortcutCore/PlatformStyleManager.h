#pragma once
#include <JuceHeader.h>
#include "ModifierTranslator.h"

namespace DAW {

/**
 * PlatformStyleManager — manages shortcut platform style and GUI visual style.
 *
 * These are two INDEPENDENT settings:
 *   - ShortcutPlatformStyle: controls modifier labels (Ctrl vs ⌘)
 *   - GUIVisualStyle:        controls rendering appearance (Apple vs Windows)
 *
 * All four combinations are valid:
 *   Apple shortcuts + Apple GUI
 *   Apple shortcuts + Windows GUI
 *   Windows shortcuts + Apple GUI
 *   Windows shortcuts + Windows GUI
 */
class PlatformStyleManager
{
public:
    enum class ShortcutStyle { Windows, Apple };
    enum class GUIStyle      { Windows, Apple };

    static PlatformStyleManager& getInstance()
    {
        static PlatformStyleManager instance;
        return instance;
    }

    // ── Shortcut platform style ──────────────────────────────────────────────

    ShortcutStyle getShortcutStyle() const { return shortcutStyle_; }

    void setShortcutStyle(ShortcutStyle s)
    {
        if (s == shortcutStyle_) return;
        shortcutStyle_ = s;
        for (auto* l : listeners_) l->platformStyleChanged();
    }

    /** Translate a shortcut string for display in the current platform style. */
    juce::String translateShortcut(const juce::String& shortcut) const
    {
        auto target = (shortcutStyle_ == ShortcutStyle::Apple)
                    ? ModifierTranslator::PlatformStyle::Apple
                    : ModifierTranslator::PlatformStyle::Windows;
        return ModifierTranslator::translate(shortcut, target);
    }

    // ── GUI visual style ─────────────────────────────────────────────────────

    GUIStyle getGUIStyle() const { return guiStyle_; }

    void setGUIStyle(GUIStyle s)
    {
        if (s == guiStyle_) return;
        guiStyle_ = s;
        for (auto* l : listeners_) l->platformStyleChanged();
    }

    bool isAppleGUI()    const { return guiStyle_ == GUIStyle::Apple; }
    bool isWindowsGUI()  const { return guiStyle_ == GUIStyle::Windows; }
    bool isAppleKeys()   const { return shortcutStyle_ == ShortcutStyle::Apple; }
    bool isWindowsKeys() const { return shortcutStyle_ == ShortcutStyle::Windows; }

    // ── Listener ─────────────────────────────────────────────────────────────

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void platformStyleChanged() = 0;
    };

    void addListener(Listener* l)    { listeners_.push_back(l); }
    void removeListener(Listener* l)
    {
        listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), l),
                         listeners_.end());
    }

private:
    PlatformStyleManager() = default;

    ShortcutStyle         shortcutStyle_ = ShortcutStyle::Windows;
    GUIStyle              guiStyle_      = GUIStyle::Windows;
    std::vector<Listener*> listeners_;

    JUCE_DECLARE_NON_COPYABLE(PlatformStyleManager)
};

} // namespace DAW
