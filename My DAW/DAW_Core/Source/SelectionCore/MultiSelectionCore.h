#pragma once
#include "SelectionTypesCore.h"
#include <vector>
#include <algorithm>

namespace DAW {

/**
 * MultiSelectionCore — message-thread-only multi-selection state.
 *
 * Tracks, clips, and plugin slots each have their own independent sets.
 * No raw pointers stored — stable string IDs only.
 */
class MultiSelectionCore
{
public:
    // ── Listener ──────────────────────────────────────────────────────────
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void multiSelectionChanged(SelectionKind) = 0;
    };

    void addListener   (Listener* l) { listeners_.push_back(l); }
    void removeListener(Listener* l) { listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), l), listeners_.end()); }

    // ── Core operations ───────────────────────────────────────────────────

    void clear()
    {
        bool anyChanged = !tracks_.empty() || !clips_.empty() || !pluginSlots_.empty();
        tracks_.clear(); primaryTrack_      = {};
        clips_.clear();  primaryClip_       = {};
        pluginSlots_.clear(); primarySlot_  = {};
        if (anyChanged) { notify(SelectionKind::Track); notify(SelectionKind::Clip); notify(SelectionKind::PluginSlot); }
    }

    void clearKind(SelectionKind kind)
    {
        switch (kind)
        {
            case SelectionKind::Track:
                if (!tracks_.empty()) { tracks_.clear(); primaryTrack_ = {}; notify(kind); }
                break;
            case SelectionKind::Clip:
                if (!clips_.empty()) { clips_.clear(); primaryClip_ = {}; notify(kind); }
                break;
            case SelectionKind::PluginSlot:
                if (!pluginSlots_.empty()) { pluginSlots_.clear(); primarySlot_ = {}; notify(kind); }
                break;
        }
    }

    /** Single click — clears same kind, selects one, sets as primary. */
    void selectSingle(const SelectionTarget& t)
    {
        auto& set = getSet(t.kind);
        auto& primary = getPrimary(t.kind);
        set.clear();
        if (t.isValid())
        {
            set.push_back(t);
            primary = t;
        }
        else
        {
            primary = {};
        }
        notify(t.kind);
    }

    /** Ctrl+click — toggle membership without clearing. */
    void toggle(const SelectionTarget& t)
    {
        if (!t.isValid()) return;
        auto& set = getSet(t.kind);
        auto& primary = getPrimary(t.kind);
        auto it = findIn(set, t);
        if (it != set.end())
        {
            set.erase(it);
            if (primary == t)
                primary = set.empty() ? SelectionTarget{} : set.back();
        }
        else
        {
            set.push_back(t);
            primary = t;
        }
        notify(t.kind);
    }

    /** Add to selection (does not clear). */
    void add(const SelectionTarget& t)
    {
        if (!t.isValid()) return;
        auto& set = getSet(t.kind);
        if (findIn(set, t) == set.end())
        {
            set.push_back(t);
            getPrimary(t.kind) = t;
            notify(t.kind);
        }
    }

    /** Remove from selection. */
    void remove(const SelectionTarget& t)
    {
        auto& set = getSet(t.kind);
        auto it = findIn(set, t);
        if (it != set.end())
        {
            set.erase(it);
            auto& primary = getPrimary(t.kind);
            if (primary == t)
                primary = set.empty() ? SelectionTarget{} : set.back();
            notify(t.kind);
        }
    }

    bool contains(const SelectionTarget& t) const
    {
        const auto& set = getSetConst(t.kind);
        return findInConst(set, t) != set.end();
    }

    int count(SelectionKind kind) const { return (int)getSetConst(kind).size(); }
    bool hasMultiple(SelectionKind kind) const { return count(kind) > 1; }
    bool isEmpty(SelectionKind kind) const { return count(kind) == 0; }

    const std::vector<SelectionTarget>& getSelected(SelectionKind kind) const { return getSetConst(kind); }

    SelectionTarget getPrimaryTarget(SelectionKind kind) const
    {
        switch (kind)
        {
            case SelectionKind::Track:      return primaryTrack_;
            case SelectionKind::Clip:       return primaryClip_;
            case SelectionKind::PluginSlot: return primarySlot_;
        }
        return {};
    }

    void setPrimary(const SelectionTarget& t)
    {
        if (!t.isValid()) return;
        getPrimary(t.kind) = t;
        // Make sure it's in the set too
        if (findIn(getSet(t.kind), t) == getSet(t.kind).end())
            getSet(t.kind).push_back(t);
    }

    /** Context-click semantics: preserve an existing selection when the
     * clicked target is already a member; otherwise select that target alone. */
    void selectContext(const SelectionTarget& t)
    {
        if (!t.isValid())
            return;

        if (contains(t))
        {
            setPrimary(t);
            notify(t.kind);
        }
        else
        {
            selectSingle(t);
        }
    }

    /** Shift+click — select range from primary to clicked, using provided ordered list. */
    void selectRange(const SelectionTarget& to,
                     const std::vector<SelectionTarget>& orderedList,
                     bool additive = false)
    {
        if (orderedList.empty()) { selectSingle(to); return; }
        auto& primary = getPrimary(to.kind);
        auto& set = getSet(to.kind);

        SelectionTarget from = primary.isValid() ? primary : orderedList.front();

        auto idxOf = [&](const SelectionTarget& tgt) -> int {
            for (int i = 0; i < (int)orderedList.size(); ++i)
                if (orderedList[(size_t)i] == tgt) return i;
            return -1;
        };

        int a = idxOf(from), b = idxOf(to);
        if (a < 0) a = 0;
        if (b < 0) b = (int)orderedList.size() - 1;
        if (a > b) std::swap(a, b);

        if (!additive) set.clear();
        for (int i = a; i <= b; ++i)
            if (findIn(set, orderedList[(size_t)i]) == set.end())
                set.push_back(orderedList[(size_t)i]);

        primary = to;
        notify(to.kind);
    }

    /** Marquee — select all items inside rectangle from provided visible list.
     *  replaceExisting=true clears first, false adds. */
    template<typename BoundsGetter>
    void selectInsideMarquee(SelectionKind kind,
                             const juce::Rectangle<float>& marqueeBounds,
                             const std::vector<SelectionTarget>& visibleTargets,
                             BoundsGetter boundsOf,
                             bool replaceExisting)
    {
        auto& set = getSet(kind);
        if (replaceExisting) set.clear();

        bool anyAdded = false;
        for (const auto& target : visibleTargets)
        {
            auto b = boundsOf(target);
            if (marqueeBounds.intersects(b))
            {
                if (findIn(set, target) == set.end())
                {
                    set.push_back(target);
                    anyAdded = true;
                }
            }
        }

        if (anyAdded || replaceExisting)
        {
            auto& primary = getPrimary(kind);
            if (!set.empty() && (replaceExisting || !primary.isValid()))
                primary = set.back();
            notify(kind);
        }
    }

private:
    std::vector<SelectionTarget> tracks_;
    std::vector<SelectionTarget> clips_;
    std::vector<SelectionTarget> pluginSlots_;

    SelectionTarget primaryTrack_;
    SelectionTarget primaryClip_;
    SelectionTarget primarySlot_;

    std::vector<Listener*> listeners_;

    std::vector<SelectionTarget>& getSet(SelectionKind k)
    {
        switch (k)
        {
            case SelectionKind::Track:      return tracks_;
            case SelectionKind::Clip:       return clips_;
            case SelectionKind::PluginSlot: return pluginSlots_;
        }
        return tracks_;
    }

    const std::vector<SelectionTarget>& getSetConst(SelectionKind k) const
    {
        switch (k)
        {
            case SelectionKind::Track:      return tracks_;
            case SelectionKind::Clip:       return clips_;
            case SelectionKind::PluginSlot: return pluginSlots_;
        }
        return tracks_;
    }

    SelectionTarget& getPrimary(SelectionKind k)
    {
        switch (k)
        {
            case SelectionKind::Track:      return primaryTrack_;
            case SelectionKind::Clip:       return primaryClip_;
            case SelectionKind::PluginSlot: return primarySlot_;
        }
        return primaryTrack_;
    }

    static std::vector<SelectionTarget>::iterator
    findIn(std::vector<SelectionTarget>& v, const SelectionTarget& t)
    {
        return std::find(v.begin(), v.end(), t);
    }

    static std::vector<SelectionTarget>::const_iterator
    findInConst(const std::vector<SelectionTarget>& v, const SelectionTarget& t)
    {
        return std::find(v.begin(), v.end(), t);
    }

    void notify(SelectionKind kind)
    {
        for (auto* l : listeners_)
            if (l) l->multiSelectionChanged(kind);
    }
};

} // namespace DAW
