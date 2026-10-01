// ===========================================================================
// StretchModeRegistryCore.h
// Singleton registry of available time-stretch modes.
// Maps mode index → display name + engine identifier.
// ===========================================================================
#pragma once
#include <string>
#include <vector>

namespace ArrangementEditor
{
    struct StretchModeEntry
    {
        int         index;
        std::string displayName;
        std::string engineId;
    };

    class StretchModeRegistryCore
    {
    public:
        static StretchModeRegistryCore& getInstance()
        {
            static StretchModeRegistryCore instance;
            return instance;
        }

        const std::vector<StretchModeEntry>& getModes() const
        {
            return m_modes;
        }

        const StretchModeEntry* getMode(int index) const
        {
            if (index < 0 || index >= (int)m_modes.size())
                return nullptr;
            return &m_modes[index];
        }

        std::string getDisplayName(int index) const
        {
            auto* entry = getMode(index);
            return entry ? entry->displayName : "Unknown";
        }

        std::string getEngineId(int index) const
        {
            auto* entry = getMode(index);
            return entry ? entry->engineId : "";
        }

        int getModeCount() const
        {
            return (int)m_modes.size();
        }

    private:
        StretchModeRegistryCore()
        {
            m_modes = {
                { 0, "Elastique 3 Pro",        "elastique_pro" },
                { 1, "Elastique 3 Efficient",  "elastique_efficient" },
                { 2, "Elastique 3 Soloist",    "elastique_soloist" },
                { 3, "Rrreeeaaa",              "rrreeeaaa" },
                { 4, "SoundTouch",             "soundtouch" },
                { 5, "Rubber Band",            "rubberband" },
                { 6, "Re-sample (Sinc)",       "resample_sinc" }
            };
        }

        std::vector<StretchModeEntry> m_modes;
    };

} // namespace ArrangementEditor
