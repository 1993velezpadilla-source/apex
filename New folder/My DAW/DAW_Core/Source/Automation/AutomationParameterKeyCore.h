#pragma once

#include "AutomationTypes.h"
#include <JuceHeader.h>
#include <unordered_map>
#include <atomic>

namespace apex::automation
{
    class AutomationParameterKeyRegistry
    {
    public:
        AutomationParameterKeyRegistry() = default;

        ParameterID getOrCreateID (const juce::String& key)
        {
            const juce::ScopedLock sl (mapsLock);
            auto it = keyToID.find (key);
            if (it != keyToID.end())
                return it->second;

            const ParameterID id = nextID.fetch_add (1, std::memory_order_acq_rel);
            keyToID.emplace (key, id);
            idToKey.emplace (id, key);
            return id;
        }

        ParameterID findID (const juce::String& key) const
        {
            const juce::ScopedLock sl (mapsLock);
            auto it = keyToID.find (key);
            return it == keyToID.end() ? kInvalidParameterID : it->second;
        }

        juce::String findKey (ParameterID id) const
        {
            const juce::ScopedLock sl (mapsLock);
            auto it = idToKey.find (id);
            return it == idToKey.end() ? juce::String() : it->second;
        }

        bool contains (const juce::String& key) const
        {
            const juce::ScopedLock sl (mapsLock);
            return keyToID.find (key) != keyToID.end();
        }

        // Returns every (key, ID) pair where the key starts with the given prefix.
        // Used by the per-track mode broadcast: "track.TRK_1." returns volume/pan/mute/solo/send keys;
        // "plugin.TRK_1." returns every plugin parameter on that track.
        // Threading: safe from any thread under the existing CriticalSection.
        // Performance: O(N) over registered keys — negligible at mode-button-click cadence.
        std::vector<std::pair<juce::String, ParameterID>>
        findAllKeysWithPrefix (const juce::String& prefix) const
        {
            std::vector<std::pair<juce::String, ParameterID>> out;
            const juce::ScopedLock sl (mapsLock);
            out.reserve (keyToID.size());
            for (const auto& [key, id] : keyToID)
                if (key.startsWith (prefix))
                    out.emplace_back (key, id);
            return out;
        }

        static juce::String trackVolumeKey (const juce::String& trackID)
        {
            return "track." + trackID + ".volume";
        }

        static juce::String trackPanKey (const juce::String& trackID)
        {
            return "track." + trackID + ".pan";
        }

        static juce::String trackMuteKey (const juce::String& trackID)
        {
            return "track." + trackID + ".mute";
        }

        static juce::String trackSoloKey (const juce::String& trackID)
        {
            return "track." + trackID + ".solo";
        }

        static juce::String trackSendLevelKey (const juce::String& trackID,
                                               const juce::String& routeID)
        {
            return "track." + trackID + ".send." + routeID + ".level";
        }

        static juce::String trackSendBypassKey (const juce::String& trackID,
                                                const juce::String& routeID)
        {
            return "track." + trackID + ".send." + routeID + ".bypass";
        }

        static juce::String pluginParamKey (const juce::String& trackID,
                                            int                 slotIndex,
                                            const juce::String& pluginName,
                                            const juce::String& paramID)
        {
            return "plugin." + trackID
                 + ".slot" + juce::String (slotIndex)
                 + "." + sanitizeForKey (pluginName)
                 + "." + sanitizeForKey (paramID);
        }

        juce::ValueTree getState() const
        {
            juce::ValueTree state ("KeyRegistry");
            const juce::ScopedLock sl (mapsLock);
            state.setProperty ("nextID", (juce::int64) nextID.load (std::memory_order_acquire), nullptr);
            for (auto& [key, id] : keyToID)
            {
                juce::ValueTree entry ("Entry");
                entry.setProperty ("key", key, nullptr);
                entry.setProperty ("id",  (juce::int64) id, nullptr);
                state.addChild (entry, -1, nullptr);
            }
            return state;
        }

        void restoreState (const juce::ValueTree& state)
        {
            if (! state.isValid()) return;
            const juce::ScopedLock sl (mapsLock);
            keyToID.clear();
            idToKey.clear();
            ParameterID maxID = 0;
            for (int i = 0; i < state.getNumChildren(); ++i)
            {
                auto entry = state.getChild (i);
                if (! entry.hasType ("Entry")) continue;
                const juce::String key = entry.getProperty ("key", {}).toString();
                const ParameterID id   = (ParameterID) (juce::int64) entry.getProperty ("id", (juce::int64) kInvalidParameterID);
                if (key.isEmpty() || id == kInvalidParameterID) continue;
                keyToID.emplace (key, id);
                idToKey.emplace (id, key);
                if (id > maxID) maxID = id;
            }
            // nextID must be at least one past the highest restored ID
            const ParameterID savedNext = (ParameterID) (juce::int64) state.getProperty ("nextID", (juce::int64) 0);
            nextID.store (savedNext > maxID ? savedNext : maxID + 1, std::memory_order_release);
        }

        static AutomationParameterKeyRegistry& getInstance()
        {
            static AutomationParameterKeyRegistry instance;
            return instance;
        }

    private:
        struct StringHash
        {
            std::size_t operator() (const juce::String& s) const noexcept { return (std::size_t) s.hashCode64(); }
        };

        // Replaces any character that would break key parsing with '_'.
        // Only called from pluginParamKey.
        static juce::String sanitizeForKey (const juce::String& s)
        {
            juce::String out;
            out.preallocateBytes (s.getNumBytesAsUTF8());
            for (auto c : s)
            {
                if (juce::CharacterFunctions::isLetterOrDigit (c) || c == '-' || c == '_')
                    out += c;
                else
                    out += '_';
            }
            return out.isEmpty() ? juce::String ("unnamed") : out;
        }

        mutable juce::CriticalSection mapsLock;
        std::unordered_map<juce::String, ParameterID, StringHash> keyToID;
        std::unordered_map<ParameterID, juce::String> idToKey;
        std::atomic<ParameterID> nextID { 1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationParameterKeyRegistry)
    };
}
