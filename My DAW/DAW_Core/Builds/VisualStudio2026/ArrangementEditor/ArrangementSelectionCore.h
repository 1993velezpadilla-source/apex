// ===========================================================================
// ArrangementSelectionCore.h
// Owns the selection set for clips in the arrangement.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <set>
#include <vector>
#include <functional>
#include <algorithm>

namespace ArrangementEditor
{
    class ArrangementSelectionCore
    {
    public:
        ArrangementSelectionCore() = default;

        // -----------------------------------------------------------------------
        // Selection operations
        // -----------------------------------------------------------------------
        void selectClip(const juce::Uuid& id)
        {
            if (m_selected.insert(id).second)
                fireChanged();
        }

        void deselectClip(const juce::Uuid& id)
        {
            if (m_selected.erase(id) > 0)
                fireChanged();
        }

        void toggleClip(const juce::Uuid& id)
        {
            if (m_selected.count(id))
                deselectClip(id);
            else
                selectClip(id);
        }

        void selectAll(const std::vector<juce::Uuid>& allIds)
        {
            m_selected.clear();
            for (auto& id : allIds)
                m_selected.insert(id);
            fireChanged();
        }

        void deselectAll()
        {
            if (!m_selected.empty())
            {
                m_selected.clear();
                fireChanged();
            }
        }

        void setSelection(const std::set<juce::Uuid>& newSelection)
        {
            if (m_selected == newSelection)
                return;
            m_selected = newSelection;
            fireChanged();
        }

        void purgeInvalidSelection(const std::function<bool(const juce::Uuid&)>& clipExists)
        {
            purgeInvalidSelectionImpl(clipExists);
        }

        template <typename ClipExists>
        void purgeInvalidSelection(ClipExists&& clipExists)
        {
            std::function<bool(const juce::Uuid&)> fn(std::forward<ClipExists>(clipExists));
            purgeInvalidSelectionImpl(fn);
        }

        // -----------------------------------------------------------------------
        // Query
        // -----------------------------------------------------------------------
        bool isSelected(const juce::Uuid& id) const
        {
            return m_selected.count(id) > 0;
        }

        const std::set<juce::Uuid>& getSelectedIds() const
        {
            return m_selected;
        }

        int getSelectionCount() const
        {
            return (int)m_selected.size();
        }

        bool hasSelection() const
        {
            return !m_selected.empty();
        }

        // -----------------------------------------------------------------------
        // Callbacks
        // -----------------------------------------------------------------------
        std::function<void()> onSelectionChanged;

    private:
        std::set<juce::Uuid> m_selected;

        void purgeInvalidSelectionImpl(const std::function<bool(const juce::Uuid&)>& clipExists)
        {
            const auto before = m_selected.size();
            for (auto it = m_selected.begin(); it != m_selected.end();)
            {
                if (!clipExists(*it))
                    it = m_selected.erase(it);
                else
                    ++it;
            }

            const auto removed = before - m_selected.size();
            DBG("[CRASH TRACE] action=selection_purge removed=" << (int)removed);
            if (removed > 0)
                fireChanged();
        }

        void fireChanged()
        {
            if (onSelectionChanged)
                onSelectionChanged();
        }
    };

} // namespace ArrangementEditor
