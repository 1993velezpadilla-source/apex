#pragma once

#include "AutomationTypes.h"
#include "AutomationParameterKeyCore.h"
#include "AutomationModeStateCore.h"
#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <atomic>
#include <algorithm>
#include <cmath>
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
            // Persistence/import and hostile plugin input can bypass the
            // recorder's own validation. Drop invalid knots BEFORE sorting:
            // NaN in a std::sort comparator violates strict weak ordering.
            // Only edits/replacement pay this cost; the audio reader is
            // unchanged and receives clean immutable snapshots.
            newPoints.erase (std::remove_if (newPoints.begin(), newPoints.end(),
                [] (const Breakpoint& p)
                {
                    return !std::isfinite (p.timePPQ)
                        || !std::isfinite (p.normalizedValue);
                }), newPoints.end());
            for (auto& p : newPoints)
            {
                p.normalizedValue = std::clamp (p.normalizedValue, 0.0f, 1.0f);
                if (!std::isfinite (p.curveTension))
                    p.curveTension = 0.0f;
            }

            // stable_sort keeps insertion order for equal timePPQ so the
            // "last added wins" dedupe below is deterministic (std::sort is
            // unstable and left the surviving point unspecified).
            std::stable_sort (newPoints.begin(), newPoints.end());

            if (newPoints.size() > 1)
            {
                size_t write = 0;
                for (size_t read = 1; read < newPoints.size(); ++read)
                {
                    if (newPoints[read].timePPQ == newPoints[write].timePPQ)
                        newPoints[write] = newPoints[read];  // keep-last
                    else
                        newPoints[++write] = newPoints[read];
                }
                newPoints.resize (write + 1);
            }

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
            if (!std::isfinite (timePPQ)) return pts.front().normalizedValue;

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
        using LaneMap = std::unordered_map<ParameterID, std::shared_ptr<AutomationLane>>;

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

        // ----- Audio-thread read (lock-free, allocation-free) -------------

        /** Immutable copy-on-write snapshot of the lane map, published on
         *  every structural mutation. The audio thread reads ONLY through
         *  this — never through the locked map. */
        std::shared_ptr<const LaneMap> getLaneSnapshotRT() const noexcept
        {
            return std::atomic_load_explicit (&publishedLanes_, std::memory_order_acquire);
        }

        /** Lock-free lane lookup for the audio thread. Returns a shared_ptr,
         *  so a lane stays alive for the duration of a block even if the
         *  message thread removes it concurrently. */
        std::shared_ptr<AutomationLane> findLaneRT (ParameterID id) const noexcept
        {
            auto snap = getLaneSnapshotRT();
            if (snap == nullptr)
                return nullptr;
            auto it = snap->find (id);
            return it == snap->end() ? nullptr : it->second;
        }

        /** Bumped on every structural publish; RT consumers re-resolve
         *  cached lookups when this changes. */
        std::uint64_t getChangeGeneration() const noexcept
        {
            return changeGeneration_.load (std::memory_order_acquire);
        }

        AutomationLane& getOrCreateLane (ParameterID id)
        {
            const juce::ScopedLock sl (mapLock);
            auto it = lanes.find (id);
            if (it != lanes.end()) return *it->second;
            auto [inserted, ok] = lanes.emplace (id, std::make_shared<AutomationLane> (id));
            publishLocked();
            return *inserted->second;
        }

        void removeLane (ParameterID id)
        {
            const juce::ScopedLock sl (mapLock);
            lanes.erase (id);
            publishLocked();
        }

        void removeLanesWithKeyPrefix (const juce::String& prefix)
        {
            std::vector<ParameterID> idsToRemove;

            for (const auto& [key, id] : AutomationParameterKeyRegistry::getInstance().findAllKeysWithPrefix (prefix))
                idsToRemove.push_back (id);

            const juce::ScopedLock sl (mapLock);
            for (auto id : idsToRemove)
                lanes.erase (id);
            publishLocked();
        }

        void clear()
        {
            const juce::ScopedLock sl (mapLock);
            lanes.clear();
            publishLocked();
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
            // Batch: suppress per-mutation publishes inside clear() and
            // getOrCreateLane(), then publish exactly once at the end.
            ++publishBatchDepth_;
            clear();

            if (state.isValid())
            {
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

            --publishBatchDepth_;
            const juce::ScopedLock sl (mapLock);
            publishLocked();
        }

        static AutomationLaneStore& getInstance()
        {
            static AutomationLaneStore instance;
            return instance;
        }

    private:
        // Copy-on-write publication (message thread only, called with
        // mapLock held). Copies only shared_ptrs — lane point data is
        // shared, never duplicated. Suppressed inside batched restores.
        void publishLocked()
        {
            if (publishBatchDepth_ > 0)
                return;
            auto snap = std::make_shared<const LaneMap> (lanes);
            auto old = std::atomic_exchange_explicit (&publishedLanes_, std::move (snap), std::memory_order_acq_rel);
            retiredLanesSnapshot_ = std::move (old);   // destroyed at the NEXT publish (message thread)
            changeGeneration_.fetch_add (1, std::memory_order_acq_rel);
        }

        LaneMap lanes;
        mutable juce::CriticalSection mapLock;

        // Published immutable view for the audio thread (never mutated in place).
        std::shared_ptr<const LaneMap> publishedLanes_;
        std::shared_ptr<const LaneMap> retiredLanesSnapshot_;   // message-thread retire slot
        std::atomic<std::uint64_t> changeGeneration_ { 0 };
        int publishBatchDepth_ = 0;   // message thread only

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationLaneStore)
    };
}
