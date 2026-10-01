#pragma once
#include "ShortcutModels.h"
#include "ShortcutProfileData.h"

namespace DAW {

/**
 * ShortcutProfileManager — registry of all shortcut profiles.
 *
 * Manages the active profile and notifies listeners on profile changes.
 * Single source of truth for profile selection across the engine and UI.
 */
class ShortcutProfileManager
{
public:
    static ShortcutProfileManager& getInstance()
    {
        static ShortcutProfileManager instance;
        return instance;
    }

    /** Listener interface for profile change notifications. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void shortcutProfileChanged(const ShortcutProfile& newProfile) = 0;
    };

    void addListener(Listener* l)       { listeners_.push_back(l); }
    void removeListener(Listener* l)
    {
        listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), l),
                         listeners_.end());
    }

    /** Get all registered profiles. */
    const std::vector<ShortcutProfile>& getProfiles() const { return profiles_; }

    /** Get the currently active profile. */
    const ShortcutProfile& getActiveProfile() const { return profiles_[activeIndex_]; }

    /** Get active profile index. */
    int getActiveIndex() const { return activeIndex_; }

    /** Get list of profile display names. */
    juce::StringArray getProfileNames() const
    {
        juce::StringArray names;
        for (auto& p : profiles_)
            names.add(p.displayName);
        return names;
    }

    /** Set active profile by index. Returns false if out of range. */
    bool setActiveByIndex(int index)
    {
        if (index < 0 || index >= (int)profiles_.size()) return false;
        if (index == activeIndex_) return true;
        activeIndex_ = index;
        notifyListeners();
        return true;
    }

    /** Set active profile by profileId. Returns false if not found. */
    bool setActiveById(const juce::String& id)
    {
        for (int i = 0; i < (int)profiles_.size(); ++i)
        {
            if (profiles_[i].profileId == id)
                return setActiveByIndex(i);
        }
        return false;
    }

    /** Set active profile by display name. Returns false if not found. */
    bool setActiveByName(const juce::String& name)
    {
        for (int i = 0; i < (int)profiles_.size(); ++i)
        {
            if (profiles_[i].displayName.equalsIgnoreCase(name))
                return setActiveByIndex(i);
        }
        return false;
    }

    /** Find a profile by id. Returns nullptr if not found. */
    const ShortcutProfile* findProfile(const juce::String& id) const
    {
        for (auto& p : profiles_)
            if (p.profileId == id) return &p;
        return nullptr;
    }

private:
    ShortcutProfileManager()
    {
        profiles_ = ShortcutProfileData::createAllProfiles();
        activeIndex_ = 0;
    }

    void notifyListeners()
    {
        auto& active = profiles_[activeIndex_];
        for (auto* l : listeners_)
            l->shortcutProfileChanged(active);
    }

    std::vector<ShortcutProfile> profiles_;
    int                          activeIndex_ = 0;
    std::vector<Listener*>       listeners_;

    JUCE_DECLARE_NON_COPYABLE(ShortcutProfileManager)
};

} // namespace DAW
