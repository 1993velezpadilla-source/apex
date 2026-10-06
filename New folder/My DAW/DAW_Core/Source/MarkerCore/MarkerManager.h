#pragma once
#include <JuceHeader.h>
#include "SectionMarker.h"

namespace DAW {

/**
 * MarkerManager — owns and manages all section markers in a project.
 *
 * Provides CRUD, lookup-by-name, lookup-by-type+ordinal, fuzzy search,
 * serialization, and a Listener interface for UI sync.
 */
class MarkerManager
{
public:
    MarkerManager() = default;
    ~MarkerManager() = default;

    // ── CRUD ──────────────────────────────────────────────────────────────────

    SectionMarker* addMarker(const juce::String& name,
                             const juce::String& sectionType,
                             SamplePosition position,
                             SamplePosition length = 0)
    {
        SectionMarker m;
        m.id          = "MRK_" + juce::String(++nextId_);
        m.name        = name;
        m.sectionType = sectionType;
        m.position    = position;
        m.length      = length;
        m.ordinal     = countByType(sectionType) + 1;
        m.color       = defaultColorForType(sectionType);

        markers_.add(new SectionMarker(std::move(m)));
        sortByPosition();
        listeners_.call([&](Listener& l) { l.markerAdded(markers_.getLast()); });
        return markers_.getLast();
    }

    bool removeMarker(const MarkerID& id)
    {
        for (int i = 0; i < markers_.size(); ++i)
        {
            if (markers_[i]->id == id)
            {
                listeners_.call([&](Listener& l) { l.markerRemoved(id); });
                markers_.remove(i);
                return true;
            }
        }
        return false;
    }

    void removeAllMarkers()
    {
        while (markers_.size() > 0)
        {
            auto id = markers_[0]->id;
            listeners_.call([&](Listener& l) { l.markerRemoved(id); });
            markers_.remove(0);
        }
    }

    // ── Access ────────────────────────────────────────────────────────────────

    int getNumMarkers() const { return markers_.size(); }

    SectionMarker* getMarker(int index) const
    {
        return (index >= 0 && index < markers_.size()) ? markers_[index] : nullptr;
    }

    SectionMarker* getMarkerByID(const MarkerID& id) const
    {
        for (auto* m : markers_)
            if (m->id == id) return m;
        return nullptr;
    }

    const juce::OwnedArray<SectionMarker>& getAllMarkers() const { return markers_; }

    // ── Lookup ────────────────────────────────────────────────────────────────

    SectionMarker* findByName(const juce::String& name) const
    {
        auto lower = name.toLowerCase();
        for (auto* m : markers_)
            if (m->name.toLowerCase() == lower) return m;
        return nullptr;
    }

    SectionMarker* findByTypeAndOrdinal(const juce::String& type, int ordinal) const
    {
        for (auto* m : markers_)
            if (m->sectionType == type && m->ordinal == ordinal) return m;
        return nullptr;
    }

    SectionMarker* findByFuzzyName(const juce::String& query) const
    {
        auto lower = query.toLowerCase().trim();
        // Exact match first
        for (auto* m : markers_)
            if (m->name.toLowerCase() == lower) return m;
        // Alias match
        for (auto* m : markers_)
            for (auto& alias : m->aliases)
                if (alias.toLowerCase() == lower) return m;
        // Partial match
        for (auto* m : markers_)
            if (m->name.toLowerCase().contains(lower)) return m;
        return nullptr;
    }

    /** Find the marker at or just before the given position. */
    SectionMarker* findAtPosition(SamplePosition pos) const
    {
        SectionMarker* best = nullptr;
        for (auto* m : markers_)
        {
            if (m->position <= pos)
                best = m;
            else
                break; // sorted by position
        }
        return best;
    }

    /** Find the next marker after the given position. */
    SectionMarker* findNextAfter(SamplePosition pos) const
    {
        for (auto* m : markers_)
            if (m->position > pos) return m;
        return nullptr;
    }

    /** Find the previous marker before the given position. */
    SectionMarker* findPreviousBefore(SamplePosition pos) const
    {
        SectionMarker* best = nullptr;
        for (auto* m : markers_)
        {
            if (m->position < pos)
                best = m;
            else
                break;
        }
        return best;
    }

    // ── Mutation ──────────────────────────────────────────────────────────────

    void moveMarker(const MarkerID& id, SamplePosition newPosition)
    {
        if (auto* m = getMarkerByID(id))
        {
            m->position = newPosition;
            sortByPosition();
            listeners_.call([&](Listener& l) { l.markerChanged(m); });
        }
    }

    void renameMarker(const MarkerID& id, const juce::String& newName)
    {
        if (auto* m = getMarkerByID(id))
        {
            m->name = newName;
            listeners_.call([&](Listener& l) { l.markerChanged(m); });
        }
    }

    // ── Serialization ─────────────────────────────────────────────────────────

    juce::ValueTree getState() const
    {
        juce::ValueTree state("Markers");
        for (auto* m : markers_)
            state.addChild(m->getState(), -1, nullptr);
        return state;
    }

    void restoreState(const juce::ValueTree& state)
    {
        markers_.clear();
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            auto* m = new SectionMarker();
            m->restoreState(state.getChild(i));
            markers_.add(m);
        }
        sortByPosition();
        listeners_.call([](Listener& l) { l.markersReloaded(); });
    }

    // ── Listener ──────────────────────────────────────────────────────────────

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void markerAdded(SectionMarker*) {}
        virtual void markerRemoved(const MarkerID&) {}
        virtual void markerChanged(SectionMarker*) {}
        virtual void markersReloaded() {}
    };

    void addListener(Listener* l)    { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

private:
    juce::OwnedArray<SectionMarker> markers_;
    juce::ListenerList<Listener>    listeners_;
    int nextId_ = 0;

    int countByType(const juce::String& type) const
    {
        int n = 0;
        for (auto* m : markers_)
            if (m->sectionType == type) ++n;
        return n;
    }

    void sortByPosition()
    {
        struct Cmp {
            static int compareElements(SectionMarker* a, SectionMarker* b)
            { return a->position < b->position ? -1 : (a->position > b->position ? 1 : 0); }
        };
        Cmp cmp;
        markers_.sort(cmp);
    }

    static juce::Colour defaultColorForType(const juce::String& type)
    {
        if (type == SectionTypes::Intro)      return juce::Colour(0xff38b2f8);
        if (type == SectionTypes::Verse)      return juce::Colour(0xff14b87a);
        if (type == SectionTypes::PreChorus)  return juce::Colour(0xfffbbf24);
        if (type == SectionTypes::Chorus)     return juce::Colour(0xff7c3aed);
        if (type == SectionTypes::Hook)       return juce::Colour(0xffff5577);
        if (type == SectionTypes::Bridge)     return juce::Colour(0xff06b6d4);
        if (type == SectionTypes::Breakdown)  return juce::Colour(0xff64646e);
        if (type == SectionTypes::Drop)       return juce::Colour(0xffe83535);
        if (type == SectionTypes::Solo)       return juce::Colour(0xffffaa44);
        if (type == SectionTypes::Adlib)      return juce::Colour(0xffcc55ff);
        if (type == SectionTypes::Outro)      return juce::Colour(0xff38b2f8);
        return juce::Colour(0xff7c3aed); // custom default
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkerManager)
};

} // namespace DAW
