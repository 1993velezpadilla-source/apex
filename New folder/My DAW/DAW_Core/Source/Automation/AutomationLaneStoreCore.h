#pragma once

#include "AutomationTypes.h"
#include "AutomationParameterKeyCore.h"
#include "AutomationModeStateCore.h"
#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <atomic>
#include <algorithm>
#include <unordered_map>

namespace apex::automation
{
    /**
        Per-parameter automation lane (sorted breakpoints).

        Audio thread takes a snapshot via getSnapshot() -- a shared_ptr that
        remains valid until the consumer releases it, even if the message
        thread swaps in a new snapshot mid-block (RCU pattern).
    */
    class AutomationLane
    {
    public:
        using PointVector = std::vector<Breakpoint>;
        using Snapshot    = std::shared_ptr<const PointVector>;

        AutomationLane (ParameterID id) : paramID (id)
        {
            std::atomic_store_explicit (
                &snapshot,
                std::make_shared<const PointVector>(),
                std::memory_order_release);
        }

        ParameterID getParameterID() const noexcept { return paramID; }

        // ----- Audio-thread read ----------------------------------------

        Snapshot getSnapshot() const noexcept
        {
            return std::atomic_load_explicit (&snapshot, std::memory_order_acquire);
        }

        bool isEmpty() const noexcept
        {
            auto s = getSnapshot();
            return s == nullptr || s->empty();
        }

        // ----- Message-thread edits -------------------------------------

        void replacePoints (PointVector newPoints)
        {
            std::sort (newPoints.begin(), newPoints.end());
            auto snap = std::make_shared<const PointVector> (std::move (newPoints));
            std::atomic_store_explicit (&snapshot, snap, std::memory_order_release);
        }

        void addPoint (Breakpoint bp)
        {
            auto current = getSnapshot();
            PointVector next = current ? *current : PointVector{};
            next.push_back (bp);
            replacePoints (std::move (next));
        }

        bool setPoint (int index, Breakpoint bp)
        {
            if (index < 0)
                return false;

            auto current = getSnapshot();
            if (current == nullptr || index >= (int) current->size())
                return false;

            PointVector next = *current;
            next[(size_t) index] = bp;
            replacePoints (std::move (next));
            return true;
        }

        bool removePoint (int index)
        {
            if (index < 0)
                return false;

            auto current = getSnapshot();
            if (current == nullptr || index >= (int) current->size())
                return false;

            PointVector next = *current;
            next.erase (next.begin() + index);
            replacePoints (std::move (next));
            return true;
        }

        void clear()
        {
            replacePoints ({});
        }

        std::size_t size() const noexcept
        {
            auto s = getSnapshot();
            return s ? s->size() : 0;
        }

        // ----- Curve evaluation (audio thread, lock-free) ---------------

        static float evaluateAt (const PointVector& pts, double timePPQ) noexcept
        {
            if (pts.empty()) return 0.0f;

            if (timePPQ <= pts.front().timePPQ)
                return pts.front().normalizedValue;

            if (timePPQ >= pts.back().timePPQ)
                return pts.back().normalizedValue;

            auto upper = std::upper_bound (pts.begin(), pts.end(), timePPQ,
                [] (double t, const Breakpoint& bp) { return t < bp.timePPQ; });

            const auto& b = *upper;
            const auto& a = *(upper - 1);

            const double span = b.timePPQ - a.timePPQ;
            if (span <= 0.0) return a.normalizedValue;

            const double t = (timePPQ - a.timePPQ) / span;
            return interpolateSegment (a, b, static_cast<float> (t));
        }

    private:
        static float interpolateSegment (const Breakpoint& a,
                                         const Breakpoint& b,
                                         float t) noexcept
        {
            switch (a.curveType)
            {
                case CurveType::Hold:
                    return a.normalizedValue;

                case CurveType::Linear:
                    return a.normalizedValue + t * (b.normalizedValue - a.normalizedValue);

                case CurveType::Smooth:
                {
                    const float tension = std::clamp (a.curveTension, -1.0f, 1.0f);
                    float shaped = t * t * (3.0f - 2.0f * t);
                    if (tension > 0.0f)
                        shaped = std::pow (shaped, 1.0f + tension * 2.0f);
                    else if (tension < 0.0f)
                        shaped = 1.0f - std::pow (1.0f - shaped, 1.0f - tension * 2.0f);
                    return a.normalizedValue + shaped * (b.normalizedValue - a.normalizedValue);
                }

                case CurveType::Bezier:
                    // Reserved for Phase 2 -- fall back to linear.
                    return a.normalizedValue + t * (b.normalizedValue - a.normalizedValue);

                default:
                    return a.normalizedValue;
            }
        }

        const ParameterID paramID;
        std::shared_ptr<const PointVector> snapshot;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationLane)
    };

    /**
        Owns every AutomationLane in the project, keyed by ParameterID.
    */
    class AutomationLaneStore
    {
    public:
        AutomationLaneStore() = default;

        // Returns a shared_ptr so the audio thread can safely hold the lane
        // alive even if the message thread calls removeLane() concurrently.
        std::shared_ptr<AutomationLane> findLane (ParameterID id) const noexcept
        {
            const juce::ScopedLock sl (mapLock);
            auto it = lanes.find (id);
            return it == lanes.end() ? nullptr : it->second;
        }

        AutomationLane& getOrCreateLane (ParameterID id)
        {
            const juce::ScopedLock sl (mapLock);
            auto it = lanes.find (id);
            if (it != lanes.end()) return *it->second;
            auto [inserted, ok] = lanes.emplace (id, std::make_shared<AutomationLane> (id));
            return *inserted->second;
        }

        void removeLane (ParameterID id)
        {
            const juce::ScopedLock sl (mapLock);
            lanes.erase (id);
        }

        void removeLanesWithKeyPrefix (const juce::String& prefix)
        {
            std::vector<ParameterID> idsToRemove;

            for (const auto& [key, id] : AutomationParameterKeyRegistry::getInstance().findAllKeysWithPrefix (prefix))
                idsToRemove.push_back (id);

            const juce::ScopedLock sl (mapLock);
            for (auto id : idsToRemove)
                lanes.erase (id);
        }

        void clear()
        {
            const juce::ScopedLock sl (mapLock);
            lanes.clear();
        }

        std::size_t size() const noexcept
        {
            const juce::ScopedLock sl (mapLock);
            return lanes.size();
        }

        template <typename Fn>
        void forEachLane (Fn&& fn) const
        {
            const juce::ScopedLock sl (mapLock);
            for (auto& [id, lane] : lanes)
                fn (*lane);
        }

        juce::ValueTree getState() const
        {
            juce::ValueTree state ("APEXAutomation");
            state.setProperty ("version", 1, nullptr);

            const juce::ScopedLock sl (mapLock);
            for (auto& [id, lane] : lanes)
            {
                auto snap = lane->getSnapshot();
                if (snap == nullptr || snap->empty())
                    continue;

                juce::ValueTree laneTree ("Lane");
                laneTree.setProperty ("parameterId", (juce::int64) id, nullptr);
                laneTree.setProperty ("parameterKey", AutomationParameterKeyRegistry::getInstance().findKey (id), nullptr);

                for (const auto& bp : *snap)
                {
                    juce::ValueTree pointTree ("Point");
                    pointTree.setProperty ("timePPQ", bp.timePPQ, nullptr);
                    pointTree.setProperty ("normalizedValue", bp.normalizedValue, nullptr);
                    pointTree.setProperty ("curveType", (int) bp.curveType, nullptr);
                    pointTree.setProperty ("curveTension", bp.curveTension, nullptr);
                    laneTree.addChild (pointTree, -1, nullptr);
                }

                state.addChild (laneTree, -1, nullptr);
            }

            // Persist key registry and mode state as children so they round-trip
            // together with the lanes in a single APEXAutomation subtree.
            state.addChild (AutomationParameterKeyRegistry::getInstance().getState(), -1, nullptr);
            state.addChild (AutomationModeState::getInstance().getState(), -1, nullptr);

            return state;
        }

        void restoreState (const juce::ValueTree& state)
        {
            clear();

            if (! state.isValid())
                return;

            // 1. Key registry first — all downstream lookups depend on stable IDs.
            AutomationParameterKeyRegistry::getInstance().restoreState (
                state.getChildWithName ("KeyRegistry"));

            // 2. Mode state — uses the now-stable IDs via key lookup.
            AutomationModeState::getInstance().restoreState (
                state.getChildWithName ("ModeState"));

            // 3. Lanes — same key→ID path as before.
            for (int i = 0; i < state.getNumChildren(); ++i)
            {
                auto laneTree = state.getChild (i);
                if (! laneTree.hasType ("Lane"))
                    continue;

                const auto key = laneTree.getProperty ("parameterKey", {}).toString();
                const ParameterID id = key.isNotEmpty()
                    ? AutomationParameterKeyRegistry::getInstance().getOrCreateID (key)
                    : (ParameterID) (juce::int64) laneTree.getProperty ("parameterId", (juce::int64) kInvalidParameterID);

                if (id == kInvalidParameterID)
                    continue;

                AutomationLane::PointVector points;
                points.reserve ((size_t) laneTree.getNumChildren());

                for (int p = 0; p < laneTree.getNumChildren(); ++p)
                {
                    auto pointTree = laneTree.getChild (p);
                    if (! pointTree.hasType ("Point"))
                        continue;

                    Breakpoint bp;
                    bp.timePPQ = (double) pointTree.getProperty ("timePPQ", 0.0);
                    bp.normalizedValue = juce::jlimit (0.0f, 1.0f, (float) pointTree.getProperty ("normalizedValue", 0.0f));
                    bp.curveType = (CurveType) juce::jlimit (0, 3, (int) pointTree.getProperty ("curveType", (int) CurveType::Linear));
                    bp.curveTension = juce::jlimit (-1.0f, 1.0f, (float) pointTree.getProperty ("curveTension", 0.0f));
                    points.push_back (bp);
                }

                if (! points.empty())
                    getOrCreateLane (id).replacePoints (std::move (points));
            }
        }

        static AutomationLaneStore& getInstance()
        {
            static AutomationLaneStore instance;
            return instance;
        }

    private:
        std::unordered_map<ParameterID, std::shared_ptr<AutomationLane>> lanes;
        mutable juce::CriticalSection mapLock;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationLaneStore)
    };
}
