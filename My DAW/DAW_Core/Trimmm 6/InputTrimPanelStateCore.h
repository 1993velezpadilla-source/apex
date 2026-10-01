#pragma once
#include <JuceHeader.h>
#include <map>
#include "../UtilityCore/Types.h"
#include "InputTrimPanelEntryModel.h"

namespace DAW {

/**
 * InputTrimPanelStateCore
 *
 * Singleton that holds per-track Input Trim panel state across sessions.
 * Pure data + serialization. Does NOT own panels and does NOT orchestrate
 * show/close — that lives in InputTrimPanelManager.
 *
 * Wire into the project save/load:
 *   On save:   panelStates.getState() appended to the project ValueTree
 *   On load:   panelStates.restoreState(savedSubtree)
 *              then InputTrimPanelManager::restoreFromSession() reopens
 *              panels per the restored state.
 *
 * Listener fires for any entry change so the manager (or a status indicator)
 * can react. The TrackID parameter is empty when restoreState() bulk-replaces.
 */
class InputTrimPanelStateCore
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void panelStateEntryChanged(const TrackID&) = 0;
    };

    InputTrimPanelStateCore() = default;

    static InputTrimPanelStateCore& getGlobalInstance()
    {
        static InputTrimPanelStateCore instance;
        return instance;
    }

    InputTrimPanelEntryModel getEntry(const TrackID& id) const
    {
        auto it = entries_.find(id);
        return it != entries_.end() ? it->second : InputTrimPanelEntryModel{};
    }

    void setEntry(const TrackID& id, const InputTrimPanelEntryModel& m)
    {
        entries_[id] = m;
        listeners_.call([&id](Listener& l) { l.panelStateEntryChanged(id); });
    }

    void clearEntry(const TrackID& id)
    {
        if (entries_.erase(id) > 0)
            listeners_.call([&id](Listener& l) { l.panelStateEntryChanged(id); });
    }

    /** Read-only access to the full map (manager iterates this on session restore). */
    const std::map<TrackID, InputTrimPanelEntryModel>& getAllEntries() const noexcept
    {
        return entries_;
    }

    /** Serialize all entries into a ValueTree under the "InputTrimPanelStates" tag.
     *  Append the returned tree to your project's session root. */
    juce::ValueTree getState() const
    {
        juce::ValueTree state("InputTrimPanelStates");
        for (const auto& kv : entries_)
        {
            juce::ValueTree e("Entry");
            e.setProperty("trackId",   kv.first,            nullptr);
            e.setProperty("open",      kv.second.open,      nullptr);
            e.setProperty("minimized", kv.second.minimized, nullptr);
            e.setProperty("x",         kv.second.x,         nullptr);
            e.setProperty("y",         kv.second.y,         nullptr);
            state.appendChild(e, nullptr);
        }
        return state;
    }

    /** Replace all entries from a previously saved tree.
     *  Pass the subtree retrieved via getChildWithName("InputTrimPanelStates")
     *  from your session root. */
    void restoreState(const juce::ValueTree& state)
    {
        entries_.clear();
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            const auto e = state.getChild(i);
            const TrackID id = e.getProperty("trackId").toString();
            if (id.isEmpty()) continue;

            InputTrimPanelEntryModel m;
            m.open      = (bool) e.getProperty("open",      false);
            m.minimized = (bool) e.getProperty("minimized", false);
            m.x         = (int)  e.getProperty("x",         120);
            m.y         = (int)  e.getProperty("y",         120);
            entries_[id] = m;
        }
        // Bulk change — fire with empty id to mean "everything reloaded"
        listeners_.call([](Listener& l) { l.panelStateEntryChanged({}); });
    }

    void addListener(Listener* l)    { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

private:
    std::map<TrackID, InputTrimPanelEntryModel> entries_;
    juce::ListenerList<Listener>                listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InputTrimPanelStateCore)
};

} // namespace DAW
