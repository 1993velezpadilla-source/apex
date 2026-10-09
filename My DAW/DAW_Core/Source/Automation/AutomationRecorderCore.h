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
#include <limits>

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
    class AutomationRecorder : private juce::Timer,
                               private juce::DeletedAtShutdown
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
            lastObservedArmed = armState.isRecordArmed();
            lastObservedOverflow = queue.getOverflowCount();
            startTimerHz (kDrainHz);
        }

        ~AutomationRecorder() override { stopTimer(); }

        void shutdownForApplicationExit()
        {
            stopTimer();
            sessions.clear();
        }

        static constexpr int         kDrainHz                 = 90;
        static constexpr std::size_t kMaxRawPointsPerSession  = 8192;
        static constexpr float       kSimplifyEpsilon         = 0.005f;
        static constexpr double      kLatchSampleStepPPQ      = 30.0;
        static constexpr double      kLiveCommitStepPPQ       = 0.015;

        static AutomationRecorder& getInstance();

        // Explicit message-thread test probe. The APEXTests target does not
        // define JUCE_UNIT_TESTS even though it compiles JUCE UnitTests; keep
        // this thin wrapper available in both configurations.
        // Production still drains through the 90 Hz timer.
        void drainForTests() { timerCallback(); }

    private:
        struct Session
        {
            ParameterID             paramID           = kInvalidParameterID;
            AutomationMode          mode              = AutomationMode::Touch;
            double                  startPPQ          = 0.0;
            double                  lastPPQ           = 0.0;
            float                   lastValue         = 0.0f;
            float                   priorLaneValue    = 0.0f;
            // Preserve the pre-touch curve while live recording replaces
            // portions of the lane. Release returns to this original curve,
            // evaluated at the release time (NOT the touch-start value).
            AutomationLane::Snapshot originalCurve;
            // Relative Trim offset envelope; signed values are allowed here.
            std::vector<Breakpoint> trimOffsets;
            float trimGestureOrigin = 0.0f;
            float trimLastOffset = 0.0f;
            double                  lastLiveCommitPPQ = -1.0;
            bool                    open              = false;
            bool                    latchSustaining   = false;
            std::vector<Breakpoint> rawPoints;
        };

        void timerCallback() override
        {
            JUCE_ASSERT_MESSAGE_THREAD;

            // Observe data loss FIRST. A Stop or Punch-Out on the same tick
            // must not extend an incomplete gesture across an unknown gap.
            const bool overflowed = handleQueueOverflow();
            const bool rewound = handleTransportEdges();
            const bool punchedOut = handleRecordArmEdges();

            AutomationGestureQueue::Event e;
            if (rewound || punchedOut || overflowed || !armState.isRecordArmed())
            {
                // A queue event carries PPQ but no loop-iteration identity.
                // Values pending at a backward seek cannot safely be assigned
                // to either take. Discard them rather than overwriting a
                // different lap; fresh gestures are recorded on later ticks.
                while (queue.pop (e)) {}
            }
            else
                while (queue.pop (e))
                    processEvent (e);

            extendLatchSustainSessions();
        }

        bool handleTransportEdges()
        {
            const auto position = clock.snapshot();
            // One observation per timer tick: a separate Stop call followed
            // by Start used to consume the Start transition before it could
            // close/reset the recorder's previous session state.
            const auto edges = clock.consumeTransportEdges();
            if (edges.stopped)
            {
                // A released Latch (or Write) gesture has no more ValueChange
                // events. Preserve its last value all the way to the actual
                // Stop playhead, not just to the final knob movement.
                const double stopPPQ = clock.getPlayheadPPQ();
                for (auto& [id, s] : sessions)
                    if (s.open)
                    {
                        // Stopping with a hand still on a Touch/Trim
                        // control is an implicit gesture release. Without
                        // restoring the pre-gesture curve, its final written
                        // value can remain latched into future playback.
                        if (std::isfinite (stopPPQ) && stopPPQ >= s.lastPPQ)
                        {
                            if (s.mode == AutomationMode::Touch
                                || s.mode == AutomationMode::Trim)
                                finishGestureAt (s, stopPPQ);
                            else
                                finishSustainAtStop (s, stopPPQ);
                        }
                        closeSession (s, true);
                    }
                sessions.clear();
                modeState.clearAllLatches();
            }

            if (edges.started)
            {
                // A Stop/Play turnaround between 90 Hz recorder ticks may
                // hide the intermediate Stop. Commit any buffered points
                // before resetting the take, and never carry Latch across
                // a new transport start.
                for (auto& [id, session] : sessions)
                    if (session.open) closeSession(session, true);
                sessions.clear();
                modeState.clearAllLatches();
            }

            const bool backwardsSeek = position.transportRolling && !edges.started
                && std::isfinite (lastObservedPlayheadPPQ)
                && std::isfinite (position.blockStartPPQ)
                && position.blockStartPPQ + 1.0e-6 < lastObservedPlayheadPPQ;

            if (backwardsSeek)
            {
                // A loop wrap or backward seek while still rolling cannot be
                // represented by one monotonic session range. Never merge
                // (e.g.) [7.5 -> 1.0] into a lane. Fence the take at its
                // last captured PPQ; a new gesture starts a separate take.
                for (auto& [id, session] : sessions)
                    if (session.open) closeSession (session, true);
                sessions.clear();
                modeState.clearAllLatches();
            }

            lastObservedPlayheadPPQ = position.transportRolling
                ? position.blockStartPPQ
                : std::numeric_limits<double>::quiet_NaN();
            return backwardsSeek;
        }

        bool handleRecordArmEdges()
        {
            const bool armedNow = armState.isRecordArmed();
            const bool punchedOut = lastObservedArmed && !armedNow;
            lastObservedArmed = armedNow;
            if (!punchedOut)
                return false;

            // Record-arm falling while Play continues is a Punch-Out edge.
            // The recorder must not leave open sessions/latches writing after
            // that boundary. Finalize at the observed playhead without
            // pretending to provide sample-accurate punch timing.
            const auto snapshot = clock.snapshot();
            for (auto& [id, s] : sessions)
            {
                if (!s.open) continue;
                if (snapshot.transportRolling
                    && std::isfinite(snapshot.blockStartPPQ)
                    && snapshot.blockStartPPQ >= s.lastPPQ)
                {
                    if (s.mode == AutomationMode::Write || s.mode == AutomationMode::Latch)
                        finishSustainAtStop(s, snapshot.blockStartPPQ);
                    else
                        finishGestureAt(s, snapshot.blockStartPPQ);
                }
                closeSession(s, true);
            }
            sessions.clear();
            modeState.clearAllLatches();
            return true; // Pending queue events have no arm-epoch identifier.
        }

        bool handleQueueOverflow()
        {
            const auto count = queue.getOverflowCount();
            if (count == lastObservedOverflow)
                return false;

            lastObservedOverflow = count;
            // MPSC overflow may drop GestureBegin, a ValueChange, or
            // GestureEnd. Never continue a session whose gesture structure
            // is now unknown: a missing End could hold Latch indefinitely.
            // Commit only the last valid captured points (not a guessed
            // Stop/Punch boundary), then reject all currently queued events.
            for (auto& [id, s] : sessions)
                if (s.open) closeSession (s, true);
            sessions.clear();
            modeState.clearAllLatches();
            return true;
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

            // Capture timestamps can arrive from different producers between
            // two 90 Hz ticks. A full loop turn can also occur entirely
            // between ticks, invisible to the rolling-PPQ observer. Never
            // feed non-finite or backwards PPQ into a monotonic session:
            // that would reverse its overwrite interval and splice points
            // from different loop passes into a single take.
            if (! std::isfinite (e.ppqAtCapture))
                return;

            auto existing = sessions.find (e.paramID);
            if (existing != sessions.end() && existing->second.open
                && e.ppqAtCapture + 1.0e-9 < existing->second.lastPPQ)
            {
                closeSession (existing->second, true);
                sessions.erase (existing);
                // Event pass identity is unknown. Drop this ambiguous event,
                // including GestureBegin, rather than inventing a new take.
                return;
            }

            switch (e.kind)
            {
                case AutomationGestureQueue::EventKind::GestureBegin:
                    openSessionIfNeeded (*param, mode, e.ppqAtCapture,
                                         e.hasStartValue ? e.normalizedValue
                                                         : param->getNormalizedValue());
                    break;

                case AutomationGestureQueue::EventKind::ValueChange:
                {
                    if (mode == AutomationMode::Write)
                        openSessionIfNeeded (*param, mode, e.ppqAtCapture,
                                             param->getNormalizedValue());

                    auto it = sessions.find (e.paramID);
                    if (it == sessions.end() || ! it->second.open) break;

                    auto& s = it->second;
                    const float delta = e.normalizedValue - s.trimGestureOrigin;
                    const float value = s.mode == AutomationMode::Trim
                        ? juce::jlimit (0.0f, 1.0f,
                            originalValueAt (s, e.ppqAtCapture) + delta)
                        : e.normalizedValue;
                    if (s.mode == AutomationMode::Trim)
                    {
                        s.trimLastOffset = delta;
                        s.trimOffsets.push_back ({ e.ppqAtCapture, delta,
                                                   CurveType::Linear, 0.0f });
                    }
                    s.rawPoints.push_back ({ e.ppqAtCapture, value,
                                             CurveType::Linear, 0.0f });
                    s.lastPPQ   = e.ppqAtCapture;
                    s.lastValue = value;

                    if (s.lastLiveCommitPPQ < 0.0
                     || std::abs (s.lastPPQ - s.lastLiveCommitPPQ) >= kLiveCommitStepPPQ)
                    {
                        commitPointsToLane (s.paramID, s.rawPoints, s.startPPQ, s.lastPPQ, &s);
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

                    finishGestureAt (s, e.ppqAtCapture);

                    closeSession (s, true);
                    sessions.erase (it);
                    break;
                }
            }
        }

        void openSessionIfNeeded (AutomationParameter& param,
                                  AutomationMode       mode,
                                  double               atPPQ,
                                  float                gestureStartValue)
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
            s.originalCurve = lane != nullptr ? lane->getSnapshot() : nullptr;
            const bool laneHasData = (s.originalCurve != nullptr
                                      && ! s.originalCurve->empty());

            if (laneHasData)
            {
                s.priorLaneValue = AutomationLane::evaluateAt (*s.originalCurve, atPPQ);
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
            // Do not read today's parameter value after the producer
            // already wrote several drag values before recorder drain.
            s.trimGestureOrigin = juce::jlimit (0.0f, 1.0f, gestureStartValue);
            s.trimLastOffset = 0.0f;
            s.trimOffsets.clear();
            if (mode == AutomationMode::Trim)
                s.trimOffsets.push_back ({ atPPQ, 0.0f,
                                           CurveType::Linear, 0.0f });
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
            commitPointsToLane (s.paramID, s.rawPoints, s.startPPQ, s.lastPPQ, &s);
            s.rawPoints.clear();
            s.rawPoints.push_back (tail);
            s.startPPQ = tail.timePPQ;
            if (s.mode == AutomationMode::Trim)
            {
                s.trimOffsets.clear();
                s.trimOffsets.push_back ({ tail.timePPQ, s.trimLastOffset,
                                           CurveType::Linear, 0.0f });
            }
        }

        void finishGestureAt (Session& s, double atPPQ)
        {
            if (s.mode == AutomationMode::Touch)
            {
                if (! s.rawPoints.empty()
                    && s.rawPoints.back().timePPQ < atPPQ)
                    s.rawPoints.back().curveType = CurveType::Hold;

                s.rawPoints.push_back ({ atPPQ,
                                         originalValueAt (s, atPPQ),
                                         CurveType::Linear, 0.0f });
                s.lastPPQ = atPPQ;
            }
            else if (s.mode == AutomationMode::Trim)
            {
                // Preserve the shifted original curve until release,
                // then return to the unshifted curve at the boundary.
                if (atPPQ > s.lastPPQ)
                {
                    const double gap = atPPQ - s.lastPPQ;
                    const double beforeRelease = atPPQ
                        - std::min (1.0e-6, gap * 0.5);
                    s.rawPoints.push_back ({
                        beforeRelease,
                        juce::jlimit (0.0f, 1.0f,
                            originalValueAt (s, beforeRelease) + s.trimLastOffset),
                        CurveType::Hold, 0.0f });
                    if (! s.trimOffsets.empty())
                        s.trimOffsets.back().curveType = CurveType::Hold;
                    s.trimOffsets.push_back ({ beforeRelease, s.trimLastOffset,
                                               CurveType::Hold, 0.0f });
                }
                s.rawPoints.push_back ({ atPPQ,
                                         originalValueAt (s, atPPQ),
                                         CurveType::Linear, 0.0f });
                s.trimOffsets.push_back ({ atPPQ, 0.0f,
                                           CurveType::Linear, 0.0f });
                s.lastPPQ = atPPQ;
            }

        }

        void finishSustainAtStop (Session& s, double stopPPQ)
        {
            // No PPQ wrap inference: a backward jump requires its own
            // loop/punch policy and must never overwrite a previous take.
            if ((s.mode != AutomationMode::Latch && s.mode != AutomationMode::Write)
                || ! std::isfinite(stopPPQ) || stopPPQ <= s.lastPPQ)
                return;

            s.rawPoints.push_back ({ stopPPQ, s.lastValue,
                                     CurveType::Linear, 0.0f });
            s.lastPPQ = stopPPQ;
        }

        void closeSession (Session& s, bool)
        {
            if (! s.open) return;
            s.open = false;
            commitPointsToLane (s.paramID, s.rawPoints, s.startPPQ, s.lastPPQ, &s);
            modeState.setLatchHeld (s.paramID, false);
        }

        static float originalValueAt (const Session& s, double ppq) noexcept
        {
            return s.originalCurve != nullptr && ! s.originalCurve->empty()
                ? AutomationLane::evaluateAt (*s.originalCurve, ppq)
                : s.priorLaneValue;
        }

        void commitPointsToLane (ParameterID paramID,
                                 const std::vector<Breakpoint>& newRawPoints,
                                 double startPPQ, double endPPQ,
                                 const Session* session)
        {
            if (newRawPoints.empty()) return;

            auto& lane = lanes.getOrCreateLane (paramID);
            auto prior = lane.getSnapshot();
            std::vector<Breakpoint> merged;
            merged.reserve ((prior ? prior->size() : 0) + newRawPoints.size()
                            + (session && session->originalCurve
                                ? session->originalCurve->size() : 0));

            if (prior != nullptr)
                for (const auto& bp : *prior)
                    if (bp.timePPQ < startPPQ || bp.timePPQ > endPPQ)
                        merged.push_back (bp);

            std::vector<Breakpoint> working (newRawPoints);
            if (session != nullptr && session->mode == AutomationMode::Trim
                && session->originalCurve != nullptr && ! session->trimOffsets.empty())
            {
                // Retain the base curve knots under a time-varying Trim delta.
                // Otherwise the base curve between knob events is flattened.
                for (const auto& source : *session->originalCurve)
                {
                    if (source.timePPQ <= startPPQ || source.timePPQ >= endPPQ)
                        continue;
                    const bool already = std::any_of (working.begin(), working.end(),
                        [&] (const Breakpoint& p) { return p.timePPQ == source.timePPQ; });
                    if (already) continue;
                    const float offset = AutomationLane::evaluateAt (
                        session->trimOffsets, source.timePPQ);
                    working.push_back ({
                        source.timePPQ,
                        juce::jlimit (0.0f, 1.0f, source.normalizedValue + offset),
                        source.curveType, source.curveTension });
                }
                std::stable_sort (working.begin(), working.end());
            }

            // Trim's imported source knots are structural: RDP only
            // measures linear value error and may delete a Hold/Smooth
            // breakpoint whose curve type matters for playback.
            // Keep them all in Trim; ordinary Touch/Write/Latch retain RDP.
            if (session != nullptr && session->mode == AutomationMode::Trim)
                merged.insert (merged.end(), working.begin(), working.end());
            else
            {
                const auto simplified = simplify (working, kSimplifyEpsilon);
                merged.insert (merged.end(), simplified.begin(), simplified.end());
            }
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
        // Message-thread-only last rolling PPQ. Tracking this independently
        // of the Play/Stop flag catches seek/loop wraps that never Stop.
        double lastObservedPlayheadPPQ = std::numeric_limits<double>::quiet_NaN();
        bool lastObservedArmed = false;
        std::uint64_t lastObservedOverflow = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationRecorder)
    };

    inline AutomationRecorder& AutomationRecorder::getInstance()
    {
        // JUCE owns final destruction so the Timer base is released before
        // MessageManager and its platform event system are torn down.
        static auto* instance = new AutomationRecorder (
            AutomationParameterRegistry::getInstance(),
            AutomationLaneStore::getInstance(),
            AutomationModeState::getInstance(),
            AutomationGestureQueue::getInstance(),
            AutomationClock::getInstance(),
            AutomationTransportState::getInstance());
        return *instance;
    }
}
