#pragma once

#include "AutomationTypes.h"
#include "AutomationParameterCore.h"
#include "AutomationParameterRegistryCore.h"
#include "AutomationLaneStoreCore.h"
#include "AutomationModeStateCore.h"
#include "AutomationParameterKeyCore.h"
#include "AutomationGestureQueueCore.h"
#include "AutomationClockCore.h"
#include "AutomationTransportStateCore.h"
#include <JuceHeader.h>
#include <unordered_map>
#include <vector>

namespace apex::automation
{
    /**
        Drains the gesture queue and writes breakpoints into AutomationLane.

        Session lifecycle per parameter:
          1. GestureBegin (or first ValueChange in Write mode) opens a session.
             An opening anchor point is seeded at the prior lane value so
             automation before the touch is not retroactively altered.
          2. ValueChange events append raw (PPQ, value) points.
          3. GestureEnd closes the session (Touch/Write/Trim): raw points are
             simplified via Ramer-Douglas-Peucker and merged into the lane.
          4. In Latch mode, GestureEnd does NOT close -- the last value is
             held and kept written until transport stops.
          5. Write mode opens implicitly on first ValueChange and stays open
             until transport stops.

        Threading: message thread only. Reads the lock-free queue, writes
        lanes via the RCU snapshot-swap established in AutomationLaneStoreCore.
    */
    class AutomationRecorder : private juce::Timer
    {
    public:
        AutomationRecorder (AutomationParameterRegistry& reg,
                            AutomationLaneStore&         laneStore,
                            AutomationModeState&         modes,
                            AutomationGestureQueue&      q,
                            AutomationClock&             c,
                            AutomationTransportState&    transport)
            : registry  (reg)
            , lanes     (laneStore)
            , modeState (modes)
            , queue     (q)
            , clock     (c)
            , armState  (transport)
        {
            startTimerHz (kDrainHz);
        }

        ~AutomationRecorder() override { stopTimer(); }

        static constexpr int         kDrainHz                 = 90;
        static constexpr std::size_t kMaxRawPointsPerSession  = 8192;
        static constexpr float       kSimplifyEpsilon         = 0.005f;
        static constexpr double      kLatchSampleStepPPQ      = 30.0;
        static constexpr double      kLiveCommitStepPPQ       = 0.015;

        static AutomationRecorder& getInstance();

    private:
        struct Session
        {
            ParameterID             paramID           = kInvalidParameterID;
            AutomationMode          mode              = AutomationMode::Touch;
            double                  startPPQ          = 0.0;
            double                  lastPPQ           = 0.0;
            float                   lastValue         = 0.0f;
            float                   priorLaneValue    = 0.0f;
            double                  lastLiveCommitPPQ = -1.0;
            bool                    open              = false;
            bool                    latchSustaining   = false;
            std::vector<Breakpoint> rawPoints;
        };

        void timerCallback() override
        {
            JUCE_ASSERT_MESSAGE_THREAD;

            handleTransportEdges();

            AutomationGestureQueue::Event e;
            while (queue.pop (e))
                processEvent (e);

            extendLatchSustainSessions();
        }

        void handleTransportEdges()
        {
            if (clock.consumeTransportStoppedEdge())
            {
                for (auto& [id, s] : sessions)
                    if (s.open) closeSession (s, true);
                sessions.clear();
                modeState.clearAllLatches();
            }

            if (clock.consumeTransportStartedEdge())
                sessions.clear();
        }

        void processEvent (const AutomationGestureQueue::Event& e)
        {
            // ===== TEMP DIAGNOSTIC =====
            {
                static int eventCount = 0;
                ++eventCount;
                if (eventCount <= 30 || eventCount % 30 == 0)
                {
                    DBG ("[AUTO-REC] event#" << eventCount
                        << " paramID=0x" << juce::String::toHexString ((int) e.paramID)
                        << " kind=" << (int) e.kind
                        << " source=" << (int) e.source
                        << " value=" << e.normalizedValue
                        << " ppqAtCapture=" << e.ppqAtCapture
                        << " | armed=" << (int) armState.isRecordArmed()
                        << " rolling=" << (int) clock.isTransportRolling()
                        << " mode=" << (int) modeState.getMode (e.paramID)
                        << " inRegistry=" << (int) (registry.find (e.paramID) != nullptr));
                }
            }
            // ===== END DIAGNOSTIC =====

            if (e.source == ChangeSource::Automation
             || e.source == ChangeSource::Programmatic)
                return;

            if (! armState.isRecordArmed())    return;
            if (! clock.isTransportRolling())  return;

            auto* param = registry.find (e.paramID);
            if (param == nullptr) return;

            const auto mode = modeState.getMode (e.paramID);
            if (mode == AutomationMode::Off || mode == AutomationMode::Read)
                return;

            switch (e.kind)
            {
                case AutomationGestureQueue::EventKind::GestureBegin:
                    openSessionIfNeeded (*param, mode, e.ppqAtCapture);
                    break;

                case AutomationGestureQueue::EventKind::ValueChange:
                {
                    if (mode == AutomationMode::Write)
                        openSessionIfNeeded (*param, mode, e.ppqAtCapture);

                    auto it = sessions.find (e.paramID);
                    if (it == sessions.end() || ! it->second.open) break;

                    auto& s = it->second;
                    s.rawPoints.push_back ({ e.ppqAtCapture, e.normalizedValue,
                                             CurveType::Linear, 0.0f });
                    s.lastPPQ   = e.ppqAtCapture;
                    s.lastValue = e.normalizedValue;

                    if (s.lastLiveCommitPPQ < 0.0
                     || std::abs (s.lastPPQ - s.lastLiveCommitPPQ) >= kLiveCommitStepPPQ)
                    {
                        commitPointsToLane (s.paramID, s.rawPoints, s.startPPQ, s.lastPPQ);
                        s.lastLiveCommitPPQ = s.lastPPQ;
                    }

                    if (s.rawPoints.size() >= kMaxRawPointsPerSession)
                        flushSessionMidStream (s);
                    break;
                }

                case AutomationGestureQueue::EventKind::GestureEnd:
                {
                    auto it = sessions.find (e.paramID);
                    if (it == sessions.end()) break;
                    auto& s = it->second;

                    if (s.mode == AutomationMode::Latch
                     || s.mode == AutomationMode::Write)
                    {
                        s.latchSustaining = true;
                        modeState.setLatchHeld (e.paramID, true);
                        break;
                    }

                    if (s.mode == AutomationMode::Touch)
                    {
                        s.rawPoints.push_back ({ e.ppqAtCapture, s.priorLaneValue,
                                                 CurveType::Linear, 0.0f });
                    }

                    closeSession (s, true);
                    sessions.erase (it);
                    break;
                }
            }
        }

        void openSessionIfNeeded (AutomationParameter& param,
                                  AutomationMode       mode,
                                  double               atPPQ)
        {
            auto& s = sessions[param.getID()];
            if (s.open) return;

            s.paramID         = param.getID();
            s.mode            = mode;
            s.startPPQ        = atPPQ;
            s.lastPPQ         = atPPQ;
            s.open            = true;
            s.latchSustaining = false;
            s.lastLiveCommitPPQ = -1.0;

            auto lane = lanes.findLane (param.getID());
            const bool laneHasData = (lane != nullptr && ! lane->isEmpty());

            if (laneHasData)
            {
                auto snap = lane->getSnapshot();
                s.priorLaneValue = AutomationLane::evaluateAt (*snap, atPPQ);
            }
            else
            {
                s.priorLaneValue = param.getNormalizedValue();
                if (armState.isAutoCreateLaneOnTouchEnabled())
                    (void) lanes.getOrCreateLane (param.getID());
            }

            s.rawPoints.clear();
            s.rawPoints.reserve (256);
            s.rawPoints.push_back ({ atPPQ, s.priorLaneValue,
                                     CurveType::Linear, 0.0f });
            s.lastValue = s.priorLaneValue;
        }

        void extendLatchSustainSessions()
        {
            const auto snap = clock.snapshot();
            if (! snap.transportRolling) return;

            for (auto& [id, s] : sessions)
            {
                if (! s.open || ! s.latchSustaining) continue;

                if (snap.blockStartPPQ > s.lastPPQ + kLatchSampleStepPPQ)
                {
                    s.rawPoints.push_back ({ snap.blockStartPPQ, s.lastValue,
                                             CurveType::Linear, 0.0f });
                    s.lastPPQ = snap.blockStartPPQ;

                    if (s.rawPoints.size() >= kMaxRawPointsPerSession)
                        flushSessionMidStream (s);
                }
            }
        }

        void flushSessionMidStream (Session& s)
        {
            if (s.rawPoints.size() < 2) return;
            Breakpoint tail = s.rawPoints.back();
            commitPointsToLane (s.paramID, s.rawPoints, s.startPPQ, s.lastPPQ);
            s.rawPoints.clear();
            s.rawPoints.push_back (tail);
            s.startPPQ = tail.timePPQ;
        }

        void closeSession (Session& s, bool)
        {
            if (! s.open) return;
            s.open = false;
            commitPointsToLane (s.paramID, s.rawPoints, s.startPPQ, s.lastPPQ);
            modeState.setLatchHeld (s.paramID, false);
        }

        void commitPointsToLane (ParameterID                    paramID,
                                 const std::vector<Breakpoint>& newRawPoints,
                                 double                         startPPQ,
                                 double                         endPPQ)
        {
            if (newRawPoints.empty()) return;

            auto& lane  = lanes.getOrCreateLane (paramID);
            auto  prior = lane.getSnapshot();

            std::vector<Breakpoint> merged;
            merged.reserve ((prior ? prior->size() : 0) + newRawPoints.size());

            if (prior != nullptr)
                for (const auto& bp : *prior)
                    if (bp.timePPQ < startPPQ || bp.timePPQ > endPPQ)
                        merged.push_back (bp);

            const auto simplified = simplify (newRawPoints, kSimplifyEpsilon);
            for (const auto& bp : simplified)
                merged.push_back (bp);

            lane.replacePoints (std::move (merged));
        }

        // ----- Ramer-Douglas-Peucker simplification ----------------------

        static std::vector<Breakpoint>
        simplify (const std::vector<Breakpoint>& pts, float epsilon)
        {
            if (pts.size() < 3) return pts;

            std::vector<bool> keep (pts.size(), false);
            keep.front() = true;
            keep.back()  = true;
            rdpRecurse (pts, 0, int (pts.size()) - 1, epsilon, keep);

            std::vector<Breakpoint> out;
            out.reserve (pts.size());
            for (std::size_t i = 0; i < pts.size(); ++i)
                if (keep[i]) out.push_back (pts[i]);
            return out;
        }

        static void rdpRecurse (const std::vector<Breakpoint>& pts,
                                int first, int last,
                                float epsilon,
                                std::vector<bool>& keep)
        {
            if (last <= first + 1) return;

            const auto& a = pts[first];
            const auto& b = pts[last];
            const double dx = b.timePPQ - a.timePPQ;
            const float  dy = b.normalizedValue - a.normalizedValue;

            float maxDist = 0.0f;
            int   maxIdx  = -1;

            for (int i = first + 1; i < last; ++i)
            {
                const double t = (dx == 0.0)
                                 ? 0.0
                                 : (pts[i].timePPQ - a.timePPQ) / dx;
                const float expected = a.normalizedValue + float (t) * dy;
                const float d = std::abs (pts[i].normalizedValue - expected);
                if (d > maxDist) { maxDist = d; maxIdx = i; }
            }

            if (maxDist > epsilon && maxIdx > 0)
            {
                keep[maxIdx] = true;
                rdpRecurse (pts, first,  maxIdx, epsilon, keep);
                rdpRecurse (pts, maxIdx, last,   epsilon, keep);
            }
        }

        AutomationParameterRegistry& registry;
        AutomationLaneStore&         lanes;
        AutomationModeState&         modeState;
        AutomationGestureQueue&      queue;
        AutomationClock&             clock;
        AutomationTransportState&    armState;

        std::unordered_map<ParameterID, Session> sessions;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationRecorder)
    };

    inline AutomationRecorder& AutomationRecorder::getInstance()
    {
        static AutomationRecorder instance (
            AutomationParameterRegistry::getInstance(),
            AutomationLaneStore::getInstance(),
            AutomationModeState::getInstance(),
            AutomationGestureQueue::getInstance(),
            AutomationClock::getInstance(),
            AutomationTransportState::getInstance());
        return instance;
    }
}
