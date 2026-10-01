#pragma once
#include <JuceHeader.h>
#include "KeyBindingProfile.h"
#include "../ActionCore/ActionManager.h"

namespace DAW {

/**
 * KeyBindingManager — resolves key presses to ActionIDs via the active profile.
 *
 * All keyboard input flows through here. The manager looks up the pressed key
 * in the active profile's binding list and dispatches through ActionManager.
 * This replaces hard-coded key→action wiring throughout the codebase.
 */
class KeyBindingManager
{
public:
    static KeyBindingManager& getInstance()
    {
        static KeyBindingManager instance;
        return instance;
    }

    /** Load a profile by name. Returns false if not found. */
    bool loadProfile(const juce::String& name)
    {
        for (auto& p : builtinProfiles_)
        {
            if (p.name.equalsIgnoreCase(name))
            {
                activeProfile_ = p;
                rebuildLookup();
                return true;
            }
        }
        return false;
    }

    /** Set a custom profile directly. */
    void setProfile(const KeyBindingProfile& profile)
    {
        activeProfile_ = profile;
        rebuildLookup();
    }

    const KeyBindingProfile& getActiveProfile() const { return activeProfile_; }

    /** Get names of all built-in profiles. */
    juce::StringArray getProfileNames() const
    {
        juce::StringArray names;
        for (auto& p : builtinProfiles_)
            names.add(p.name);
        return names;
    }

    /**
     * Handle a key press. Returns true if a binding was found and dispatched.
     *
     * @param key      The pressed key.
     * @param context  The current UI context ("global", "arrangement", "mixer").
     */
    bool handleKeyPress(const juce::KeyPress& key, const juce::String& context = "global")
    {
        // Professional DAW baseline: undo/redo must always be available from
        // the global key path regardless of the active profile's key-case or
        // whether a child timeline component currently owns keyboard focus.
        const auto keyCode = key.getKeyCode();
        const bool isZ = keyCode == 'z' || keyCode == 'Z';
        const bool isY = keyCode == 'y' || keyCode == 'Y';
        if (key.getModifiers().isCtrlDown() && key.getModifiers().isShiftDown() && isZ)
        {
            ActionManager::getInstance().dispatch(ActionID::EditRedo);
            return true;
        }

        if (key.getModifiers().isCtrlDown() && isY)
        {
            ActionManager::getInstance().dispatch(ActionID::EditRedo);
            return true;
        }

        if (key.getModifiers().isCtrlDown() && isZ)
        {
            ActionManager::getInstance().dispatch(ActionID::EditUndo);
            return true;
        }

        // Context-specific bindings first, then global fallback
        for (auto& b : activeProfile_.bindings)
        {
            if (matchesKeyPress(b.key, key) && b.context == context)
            {
                ActionManager::getInstance().dispatch(b.action);
                return true;
            }
        }
        if (context != "global")
        {
            for (auto& b : activeProfile_.bindings)
            {
                if (matchesKeyPress(b.key, key) && b.context == "global")
                {
                    ActionManager::getInstance().dispatch(b.action);
                    return true;
                }
            }
        }
        return false;
    }

    /** Detect conflicting bindings in the active profile. */
    std::vector<std::pair<KeyBinding, KeyBinding>> detectConflicts() const
    {
        std::vector<std::pair<KeyBinding, KeyBinding>> conflicts;
        for (size_t i = 0; i < activeProfile_.bindings.size(); ++i)
        {
            for (size_t j = i + 1; j < activeProfile_.bindings.size(); ++j)
            {
                auto& a = activeProfile_.bindings[i];
                auto& b = activeProfile_.bindings[j];
                if (a.key == b.key && a.context == b.context)
                    conflicts.push_back({ a, b });
            }
        }
        return conflicts;
    }

    /** Returns a formatted shortcut string for the given action in the active profile.
     *  Returns empty string if no binding is found.
     *  Example outputs: "Ctrl+Z", "Shift+Delete", "Space", "F11" */
    juce::String getKeyDescription(ActionID action) const
    {
        for (auto& b : activeProfile_.bindings)
        {
            if (b.action == action)
                return formatKeyPress(b.key);
        }
        return {};
    }

private:
    static bool matchesKeyPress(const juce::KeyPress& a, const juce::KeyPress& b)
    {
        if (a == b)
            return true;

        if (a.getModifiers() != b.getModifiers())
            return false;

        const int ak = a.getKeyCode();
        const int bk = b.getKeyCode();
        if (((ak >= 'a' && ak <= 'z') || (ak >= 'A' && ak <= 'Z'))
            && ((bk >= 'a' && bk <= 'z') || (bk >= 'A' && bk <= 'Z')))
            return juce::CharacterFunctions::toUpperCase((juce::juce_wchar) ak)
                == juce::CharacterFunctions::toUpperCase((juce::juce_wchar) bk);

        return false;
    }

    static juce::String formatKeyPress(const juce::KeyPress& k)
    {
        juce::String s;
        auto mods = k.getModifiers();
        if (mods.isCtrlDown())  s += "Ctrl+";
        if (mods.isAltDown())   s += "Alt+";
        if (mods.isShiftDown()) s += "Shift+";

        int kc = k.getKeyCode();
        if (kc >= 'A' && kc <= 'Z')        s += juce::String::charToString((juce::juce_wchar)kc);
        else if (kc >= '0' && kc <= '9')   s += juce::String::charToString((juce::juce_wchar)kc);
        else if (kc == juce::KeyPress::spaceKey)       s += "Space";
        else if (kc == juce::KeyPress::returnKey)      s += "Enter";
        else if (kc == juce::KeyPress::deleteKey)      s += "Delete";
        else if (kc == juce::KeyPress::backspaceKey)   s += "Backspace";
        else if (kc == juce::KeyPress::homeKey)        s += "Home";
        else if (kc == juce::KeyPress::endKey)         s += "End";
        else if (kc == juce::KeyPress::upKey)          s += "Up";
        else if (kc == juce::KeyPress::downKey)        s += "Down";
        else if (kc == juce::KeyPress::leftKey)        s += "Left";
        else if (kc == juce::KeyPress::rightKey)       s += "Right";
        else if (kc == juce::KeyPress::F1Key)          s += "F1";
        else if (kc == juce::KeyPress::F9Key)          s += "F9";
        else if (kc == juce::KeyPress::F10Key)         s += "F10";
        else if (kc == juce::KeyPress::F11Key)         s += "F11";
        else if (kc == juce::KeyPress::F12Key)         s += "F12";
        else                                           s += juce::String::charToString((juce::juce_wchar)kc);
        return s;
    }
    KeyBindingManager()
    {
        builtinProfiles_ = {
            KeyBindingProfile::createDefault(),
            KeyBindingProfile::createProTools(),
            KeyBindingProfile::createLogicPro(),
            KeyBindingProfile::createFLStudio(),
            KeyBindingProfile::createAbletonLive(),
            KeyBindingProfile::createReaper(),
            KeyBindingProfile::createStudioOne(),
            KeyBindingProfile::createCubase(),
            KeyBindingProfile::createCakewalk(),
        };
        activeProfile_ = builtinProfiles_[0]; // DAW Core default
    }

    KeyBindingProfile              activeProfile_;
    std::vector<KeyBindingProfile> builtinProfiles_;

    void rebuildLookup()
    {
        // Future: build a hash map for O(1) key lookup if profile size grows
    }

    JUCE_DECLARE_NON_COPYABLE(KeyBindingManager)
};

} // namespace DAW
