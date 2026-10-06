#pragma once
#include "ShortcutProfileManager.h"
#include "PlatformStyleManager.h"
#include "ContextResolver.h"
#include "ShortcutSearchEngine.h"
#include "ShortcutPersistenceManager.h"

namespace DAW {

/**
 * ShortcutEngineCore — top-level coordinator for the shortcut system.
 *
 * Ties together:
 *   - ShortcutProfileManager  (profile registry + active profile)
 *   - PlatformStyleManager    (modifier translation + GUI style)
 *   - ContextResolver         (context-sensitive dispatch)
 *   - ShortcutPersistenceManager (save/load preferences)
 *
 * Single entry point for the rest of the DAW to interact with shortcuts.
 */
class ShortcutEngineCore
{
public:
    static ShortcutEngineCore& getInstance()
    {
        static ShortcutEngineCore instance;
        return instance;
    }

    // ── Subsystem accessors ─────────────────────────────────────────────────

    ShortcutProfileManager&     profiles()    { return ShortcutProfileManager::getInstance(); }
    PlatformStyleManager&       platform()    { return PlatformStyleManager::getInstance(); }
    ContextResolver&            context()     { return ContextResolver::getInstance(); }
    ShortcutPersistenceManager& persistence() { return ShortcutPersistenceManager::getInstance(); }

    /** Get the active profile. */
    const ShortcutProfile& activeProfile() const
    {
        return ShortcutProfileManager::getInstance().getActiveProfile();
    }

    /** Translate a shortcut string for the current platform style. */
    juce::String translateShortcut(const juce::String& shortcut) const
    {
        return PlatformStyleManager::getInstance().translateShortcut(shortcut);
    }

    // ── Profile switching ────────────────────────────────────────────────────

    void switchProfile(int index)
    {
        ShortcutProfileManager::getInstance().setActiveByIndex(index);
        auto& p = ShortcutProfileManager::getInstance().getActiveProfile();
        ShortcutPersistenceManager::getInstance().set(
            ShortcutPersistenceManager::kActiveProfileId, p.profileId);
    }

    void switchProfile(const juce::String& id)
    {
        ShortcutProfileManager::getInstance().setActiveById(id);
        ShortcutPersistenceManager::getInstance().set(
            ShortcutPersistenceManager::kActiveProfileId, id);
    }

    // ── Search ───────────────────────────────────────────────────────────────

    std::vector<ShortcutSearchEngine::FilteredResult>
    searchActiveProfile(const juce::String& query) const
    {
        return ShortcutSearchEngine::filter(activeProfile(), query);
    }

    // ── Restore persisted state ──────────────────────────────────────────────

    void restorePersistedState()
    {
        auto& pm  = ShortcutPersistenceManager::getInstance();
        auto& spm = ShortcutProfileManager::getInstance();
        auto& psm = PlatformStyleManager::getInstance();

        auto savedId = pm.get(ShortcutPersistenceManager::kActiveProfileId);
        if (savedId.isNotEmpty())
            spm.setActiveById(savedId);

        auto savedPlatform = pm.get(ShortcutPersistenceManager::kShortcutPlatform, "windows");
        psm.setShortcutStyle(savedPlatform == "apple"
            ? PlatformStyleManager::ShortcutStyle::Apple
            : PlatformStyleManager::ShortcutStyle::Windows);

        auto savedGUI = pm.get(ShortcutPersistenceManager::kGUIVisualStyle, "windows");
        psm.setGUIStyle(savedGUI == "apple"
            ? PlatformStyleManager::GUIStyle::Apple
            : PlatformStyleManager::GUIStyle::Windows);
    }

private:
    ShortcutEngineCore() = default;
    JUCE_DECLARE_NON_COPYABLE(ShortcutEngineCore)
};

} // namespace DAW
