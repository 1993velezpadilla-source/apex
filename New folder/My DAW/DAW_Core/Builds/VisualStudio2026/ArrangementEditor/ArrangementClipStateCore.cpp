// ===========================================================================
// ArrangementClipStateCore.cpp
// ===========================================================================
#include "ArrangementClipStateCore.h"
#include <algorithm>

namespace ArrangementEditor
{
    void ArrangementClipStateCore::addClip(const ArrangementClipModel& clip)
    {
        m_clips.push_back(clip);
        if (onClipAdded) onClipAdded(clip.id);
        if (onStateChanged) onStateChanged();
    }

    void ArrangementClipStateCore::removeClip(const juce::Uuid& id)
    {
        auto it = std::find_if(m_clips.begin(), m_clips.end(),
                               [&](const ArrangementClipModel& c) { return c.id == id; });
        if (it != m_clips.end())
        {
            m_clips.erase(it);
            if (onClipRemoved) onClipRemoved(id);
            if (onStateChanged) onStateChanged();
        }
    }

    void ArrangementClipStateCore::updateClip(const ArrangementClipModel& clip)
    {
        auto* existing = findClip(clip.id);
        if (existing)
        {
            DBG("[PITCH WRITE] source=ClipRefresh value=" << clip.timePitch.pitchSemitones);
            *existing = clip;
            if (onClipChanged) onClipChanged(clip.id);
            if (onStateChanged) onStateChanged();
        }
    }

    ArrangementClipModel* ArrangementClipStateCore::findClip(const juce::Uuid& id)
    {
        auto it = std::find_if(m_clips.begin(), m_clips.end(),
                               [&](const ArrangementClipModel& c) { return c.id == id; });
        return it != m_clips.end() ? &(*it) : nullptr;
    }

    const ArrangementClipModel* ArrangementClipStateCore::findClip(const juce::Uuid& id) const
    {
        auto it = std::find_if(m_clips.begin(), m_clips.end(),
                               [&](const ArrangementClipModel& c) { return c.id == id; });
        return it != m_clips.end() ? &(*it) : nullptr;
    }

    std::vector<ArrangementClipModel*>
    ArrangementClipStateCore::clipsOnTrack(int trackIndex)
    {
        std::vector<ArrangementClipModel*> out;
        for (auto& c : m_clips)
            if (c.trackIndex == trackIndex)
                out.push_back(&c);
        return out;
    }

    std::vector<const ArrangementClipModel*>
    ArrangementClipStateCore::clipsOnTrack(int trackIndex) const
    {
        std::vector<const ArrangementClipModel*> out;
        for (auto& c : m_clips)
            if (c.trackIndex == trackIndex)
                out.push_back(&c);
        return out;
    }

    std::vector<ArrangementClipModel*>
    ArrangementClipStateCore::clipsInRange(double startTime, double endTime)
    {
        std::vector<ArrangementClipModel*> out;
        for (auto& c : m_clips)
            if (c.overlaps(startTime, endTime))
                out.push_back(&c);
        return out;
    }

    std::vector<const ArrangementClipModel*>
    ArrangementClipStateCore::clipsInRange(double startTime, double endTime) const
    {
        std::vector<const ArrangementClipModel*> out;
        for (auto& c : m_clips)
            if (c.overlaps(startTime, endTime))
                out.push_back(&c);
        return out;
    }

    ArrangementClipModel* ArrangementClipStateCore::clipAtPoint(int trackIndex, double time)
    {
        for (auto& c : m_clips)
            if (c.trackIndex == trackIndex && c.contains(time))
                return &c;
        return nullptr;
    }

    void ArrangementClipStateCore::clear()
    {
        m_clips.clear();
        if (onStateChanged) onStateChanged();
    }

} // namespace ArrangementEditor
