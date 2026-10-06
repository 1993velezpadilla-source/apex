#pragma once

#include "AutomationTypes.h"
#include "AutomationLaneStoreCore.h"
#include "AutomationParameterRegistryCore.h"
#include "AutomationParameterKeyCore.h"
#include "../MixerScaleCore/FaderRangeCore.h"
#include <JuceHeader.h>
#include <unordered_map>
#include <vector>
#include <memory>

namespace apex::automation
{
    class AutomationArrangementBridge
    {
    public:
        static AutomationArrangementBridge& getInstance()
        {
            static AutomationArrangementBridge instance;
            return instance;
        }

        static ParameterID computeIDForKey (const juce::String& key)
        {
            return AutomationParameterKeyRegistry::getInstance().findID (key);
        }

        static ParameterID volumeID (const juce::String& trackID)
        {
            return AutomationParameterKeyRegistry::getInstance().getOrCreateID (AutomationParameterKeyRegistry::trackVolumeKey (trackID));
        }

        static ParameterID panID (const juce::String& trackID)
        {
            return AutomationParameterKeyRegistry::getInstance().getOrCreateID (AutomationParameterKeyRegistry::trackPanKey (trackID));
        }

        static ParameterID muteID (const juce::String& trackID)
        {
            return AutomationParameterKeyRegistry::getInstance().getOrCreateID (AutomationParameterKeyRegistry::trackMuteKey (trackID));
        }

        static ParameterID soloID (const juce::String& trackID)
        {
            return AutomationParameterKeyRegistry::getInstance().getOrCreateID (AutomationParameterKeyRegistry::trackSoloKey (trackID));
        }

        static ParameterID sendLevelID (const juce::String& trackID, const juce::String& routeID)
        {
            return AutomationParameterKeyRegistry::getInstance().getOrCreateID (AutomationParameterKeyRegistry::trackSendLevelKey (trackID, routeID));
        }

        static ParameterID sendBypassID (const juce::String& trackID, const juce::String& routeID)
        {
            return AutomationParameterKeyRegistry::getInstance().getOrCreateID (AutomationParameterKeyRegistry::trackSendBypassKey (trackID, routeID));
        }

        struct GainBreakpoint
        {
            double timeSamples = 0.0;
            float  gainValue   = 1.0f;
        };

        struct PanBreakpoint
        {
            double timeSamples = 0.0;
            float  panValue    = 0.0f;
        };

        struct BoolBreakpoint
        {
            double timeSamples = 0.0;
            bool   active      = false;
        };

        struct SendLevelBreakpoint
        {
            double timeSamples = 0.0;
            float  gainValue   = 1.0f;
        };

        // Plugin parameter breakpoints — normalized 0..1 (no gain conversion needed).
        struct PluginParamBreakpoint
        {
            double timeSamples     = 0.0;
            float  normalizedValue = 0.0f;
        };

        std::vector<PluginParamBreakpoint>
        getPluginParamBreakpoints (const juce::String& trackID,
                                   int                 slotIndex,
                                   const juce::String& pluginName,
                                   const juce::String& paramID,
                                   double              sampleRate,
                                   double              bpm) const
        {
            std::vector<PluginParamBreakpoint> out;
            if (sampleRate <= 0.0 || bpm <= 0.0) return out;

            const auto key = AutomationParameterKeyRegistry::pluginParamKey (
                trackID, slotIndex, pluginName, paramID);
            const auto pid = computeIDForKey (key);
            if (pid == kInvalidParameterID) return out;

            auto lane = AutomationLaneStore::getInstance().findLane (pid);
            if (lane == nullptr) return out;
            auto snap = lane->getSnapshot();
            if (snap == nullptr || snap->empty()) return out;

            const double ppqToSamples = (60.0 / bpm) * sampleRate;
            out.reserve (snap->size());
            for (const auto& bp : *snap)
                out.push_back ({ bp.timePPQ * ppqToSamples,
                                 juce::jlimit (0.0f, 1.0f, bp.normalizedValue) });
            return out;
        }

        bool hasPluginParamAutomation (const juce::String& trackID,
                                       int                 slotIndex,
                                       const juce::String& pluginName,
                                       const juce::String& paramID) const
        {
            const auto key = AutomationParameterKeyRegistry::pluginParamKey (
                trackID, slotIndex, pluginName, paramID);
            return paramHasAutomation (computeIDForKey (key));
        }

        std::vector<GainBreakpoint> getTrackVolumeBreakpointsAsGain (const juce::String& trackID,
                                                                      double sampleRate,
                                                                      double bpm) const
        {
            std::vector<GainBreakpoint> out;
            if (sampleRate <= 0.0 || bpm <= 0.0)
                return out;

            const auto paramID = volumeID (trackID);
            if (paramID == kInvalidParameterID)
                return out;

            auto lane = AutomationLaneStore::getInstance().findLane (paramID);
            if (lane == nullptr)
                return out;

            auto snap = lane->getSnapshot();
            if (snap == nullptr || snap->empty())
                return out;

            auto* faderRange = DAW::FaderRangeCore::getGlobalInstance();
            if (faderRange == nullptr)
                return out;

            const double ppqToSamples = (60.0 / bpm) * sampleRate;
            out.reserve (snap->size());
            for (const auto& bp : *snap)
            {
                const float clampedNorm = juce::jlimit (0.0f, 1.0f, bp.normalizedValue);
                const float db = faderRange->normToDb (clampedNorm);
                out.push_back ({ bp.timePPQ * ppqToSamples, faderRange->dbToGain (db) });
            }
            return out;
        }

        bool hasTrackVolumeAutomation (const juce::String& trackID) const
        {
            return paramHasAutomation (volumeID (trackID));
        }

        std::vector<PanBreakpoint> getTrackPanBreakpoints (const juce::String& trackID,
                                                           double sampleRate,
                                                           double bpm) const
        {
            std::vector<PanBreakpoint> out;
            if (sampleRate <= 0.0 || bpm <= 0.0)
                return out;

            const auto paramID = panID (trackID);
            if (paramID == kInvalidParameterID)
                return out;

            auto lane = AutomationLaneStore::getInstance().findLane (paramID);
            if (lane == nullptr)
                return out;

            auto snap = lane->getSnapshot();
            if (snap == nullptr || snap->empty())
                return out;

            const double ppqToSamples = (60.0 / bpm) * sampleRate;
            out.reserve (snap->size());
            for (const auto& bp : *snap)
                out.push_back ({ bp.timePPQ * ppqToSamples, juce::jlimit (-1.0f, 1.0f, bp.normalizedValue * 2.0f - 1.0f) });
            return out;
        }

        bool hasTrackPanAutomation (const juce::String& trackID) const
        {
            return paramHasAutomation (panID (trackID));
        }

        std::vector<BoolBreakpoint> getTrackMuteBreakpoints (const juce::String& trackID,
                                                             double sampleRate,
                                                             double bpm) const
        {
            return getBoolBreakpointsForParam (muteID (trackID), sampleRate, bpm);
        }

        std::vector<BoolBreakpoint> getTrackSoloBreakpoints (const juce::String& trackID,
                                                             double sampleRate,
                                                             double bpm) const
        {
            return getBoolBreakpointsForParam (soloID (trackID), sampleRate, bpm);
        }

        bool hasTrackMuteAutomation (const juce::String& trackID) const
        {
            return paramHasAutomation (muteID (trackID));
        }

        bool hasTrackSoloAutomation (const juce::String& trackID) const
        {
            return paramHasAutomation (soloID (trackID));
        }

        std::vector<SendLevelBreakpoint> getTrackSendLevelBreakpoints (const juce::String& trackID,
                                                                        const juce::String& routeID,
                                                                        double sampleRate,
                                                                        double bpm) const
        {
            std::vector<SendLevelBreakpoint> out;
            if (sampleRate <= 0.0 || bpm <= 0.0)
                return out;

            const auto paramID = sendLevelID (trackID, routeID);
            if (paramID == kInvalidParameterID)
                return out;

            auto lane = AutomationLaneStore::getInstance().findLane (paramID);
            if (lane == nullptr)
                return out;

            auto snap = lane->getSnapshot();
            if (snap == nullptr || snap->empty())
                return out;

            const double ppqToSamples = (60.0 / bpm) * sampleRate;
            out.reserve (snap->size());
            for (const auto& bp : *snap)
                out.push_back ({ bp.timePPQ * ppqToSamples, juce::jlimit (0.0f, 2.0f, bp.normalizedValue * 2.0f) });
            return out;
        }

        bool hasTrackSendLevelAutomation (const juce::String& trackID,
                                          const juce::String& routeID) const
        {
            return paramHasAutomation (sendLevelID (trackID, routeID));
        }

        std::vector<BoolBreakpoint> getTrackSendBypassBreakpoints (const juce::String& trackID,
                                                                   const juce::String& routeID,
                                                                   double sampleRate,
                                                                   double bpm) const
        {
            return getBoolBreakpointsForParam (sendBypassID (trackID, routeID), sampleRate, bpm);
        }

        bool hasTrackSendBypassAutomation (const juce::String& trackID,
                                           const juce::String& routeID) const
        {
            return paramHasAutomation (sendBypassID (trackID, routeID));
        }

        float getCurrentNormalizedValueForKey (const juce::String& fullKey) const
        {
            const auto paramID = computeIDForKey (fullKey);
            if (paramID == kInvalidParameterID)
                return -1.0f;
            auto* param = AutomationParameterRegistry::getInstance().find (paramID);
            return param == nullptr ? -1.0f : param->getNormalizedValue();
        }

        void addTrackVolumeBreakpointFromGain (const juce::String& trackID,
                                               double timeSamples,
                                               float gainValue,
                                               double sampleRate,
                                               double bpm)
        {
            auto* faderRange = DAW::FaderRangeCore::getGlobalInstance();
            if (faderRange == nullptr || sampleRate <= 0.0 || bpm <= 0.0)
                return;

            const auto paramID = volumeID (trackID);
            if (paramID == kInvalidParameterID)
                return;

            auto& lane = AutomationLaneStore::getInstance().getOrCreateLane (paramID);
            auto snap = lane.getSnapshot();
            AutomationLane::PointVector next = snap ? *snap : AutomationLane::PointVector{};

            const double ppq = samplesToPPQ (timeSamples, sampleRate, bpm);
            const float normalized = faderRange->gainToNorm (gainValue);
            next.push_back ({ ppq, normalized, CurveType::Linear, 0.0f });
            lane.replacePoints (std::move (next));
        }

        void moveTrackVolumeBreakpointFromGain (const juce::String& trackID,
                                                int index,
                                                double timeSamples,
                                                float gainValue,
                                                double sampleRate,
                                                double bpm)
        {
            auto* faderRange = DAW::FaderRangeCore::getGlobalInstance();
            if (faderRange == nullptr || sampleRate <= 0.0 || bpm <= 0.0 || index < 0)
                return;

            const auto paramID = volumeID (trackID);
            if (paramID == kInvalidParameterID)
                return;

            auto lane = AutomationLaneStore::getInstance().findLane (paramID);
            if (lane == nullptr)
                return;

            auto snap = lane->getSnapshot();
            if (snap == nullptr || index >= (int) snap->size())
                return;

            AutomationLane::PointVector next = *snap;
            next[(size_t) index].timePPQ = samplesToPPQ (timeSamples, sampleRate, bpm);
            next[(size_t) index].normalizedValue = faderRange->gainToNorm (gainValue);
            lane->replacePoints (std::move (next));
        }

        void removeTrackVolumeBreakpoint (const juce::String& trackID, int index)
        {
            if (index < 0)
                return;

            const auto paramID = volumeID (trackID);
            if (paramID == kInvalidParameterID)
                return;

            auto lane = AutomationLaneStore::getInstance().findLane (paramID);
            if (lane == nullptr)
                return;

            auto snap = lane->getSnapshot();
            if (snap == nullptr || index >= (int) snap->size())
                return;

            AutomationLane::PointVector next = *snap;
            next.erase (next.begin() + index);
            lane->replacePoints (std::move (next));
        }

        void addSendLevelBreakpoint (const juce::String& trackID,
                                     const juce::String& routeID,
                                     double timeSamples,
                                     float gainValue,
                                     double sampleRate,
                                     double bpm)
        {
            addNormalizedBreakpointForParam (sendLevelID (trackID, routeID), timeSamples,
                                             juce::jlimit (0.0f, 1.0f, gainValue * 0.5f),
                                             sampleRate, bpm, CurveType::Linear);
        }

        void moveSendLevelBreakpoint (const juce::String& trackID,
                                      const juce::String& routeID,
                                      int index,
                                      double timeSamples,
                                      float gainValue,
                                      double sampleRate,
                                      double bpm)
        {
            setNormalizedBreakpointForParam (sendLevelID (trackID, routeID), index, timeSamples,
                                             juce::jlimit (0.0f, 1.0f, gainValue * 0.5f),
                                             sampleRate, bpm, CurveType::Linear);
        }

        void removeSendLevelBreakpoint (const juce::String& trackID,
                                        const juce::String& routeID,
                                        int index)
        {
            removeBreakpointForParam (sendLevelID (trackID, routeID), index);
        }

        void addSendBypassBreakpoint (const juce::String& trackID,
                                      const juce::String& routeID,
                                      double timeSamples,
                                      bool bypassed,
                                      double sampleRate,
                                      double bpm)
        {
            addNormalizedBreakpointForParam (sendBypassID (trackID, routeID), timeSamples,
                                             bypassed ? 1.0f : 0.0f,
                                             sampleRate, bpm, CurveType::Hold);
        }

        void moveSendBypassBreakpoint (const juce::String& trackID,
                                       const juce::String& routeID,
                                       int index,
                                       double timeSamples,
                                       bool bypassed,
                                       double sampleRate,
                                       double bpm)
        {
            setNormalizedBreakpointForParam (sendBypassID (trackID, routeID), index, timeSamples,
                                             bypassed ? 1.0f : 0.0f,
                                             sampleRate, bpm, CurveType::Hold);
        }

        void removeSendBypassBreakpoint (const juce::String& trackID,
                                         const juce::String& routeID,
                                         int index)
        {
            removeBreakpointForParam (sendBypassID (trackID, routeID), index);
        }

        bool hasSnapshotChangedSinceLastPaint (const juce::String& trackID)
        {
            return hasSnapshotChangedSinceLastPaintForKey (AutomationParameterKeyRegistry::trackVolumeKey (trackID));
        }

        bool hasSnapshotChangedSinceLastPaintForKey (const juce::String& fullKey)
        {
            const auto paramID = computeIDForKey (fullKey);
            if (paramID == kInvalidParameterID)
                return false;
            auto lane = AutomationLaneStore::getInstance().findLane (paramID);
            if (lane == nullptr)
                return false;

            auto current = lane->getSnapshot();
            const void* currentPtr = static_cast<const void*> (current.get());

            const juce::ScopedLock sl (lastSnapLock);
            auto& last = lastSnapshotPtr[fullKey];
            if (last != currentPtr)
            {
                last = currentPtr;
                return true;
            }
            return false;
        }

        void invalidateSnapshotCache (const juce::String& key)
        {
            const juce::ScopedLock sl (lastSnapLock);
            lastSnapshotPtr[key] = nullptr;
        }

    private:
        struct StringHash
        {
            std::size_t operator() (const juce::String& s) const noexcept { return (std::size_t) s.hashCode64(); }
        };

        AutomationArrangementBridge() = default;

        static double samplesToPPQ (double timeSamples, double sampleRate, double bpm) noexcept
        {
            return (timeSamples / sampleRate) * (bpm / 60.0);
        }

        void addNormalizedBreakpointForParam (ParameterID paramID,
                                              double timeSamples,
                                              float normalizedValue,
                                              double sampleRate,
                                              double bpm,
                                              CurveType curveType)
        {
            if (paramID == kInvalidParameterID || sampleRate <= 0.0 || bpm <= 0.0)
                return;

            auto& lane = AutomationLaneStore::getInstance().getOrCreateLane (paramID);
            auto snap = lane.getSnapshot();
            AutomationLane::PointVector next = snap ? *snap : AutomationLane::PointVector{};
            next.push_back ({ samplesToPPQ (timeSamples, sampleRate, bpm), juce::jlimit (0.0f, 1.0f, normalizedValue), curveType, 0.0f });
            lane.replacePoints (std::move (next));
        }

        void setNormalizedBreakpointForParam (ParameterID paramID,
                                              int index,
                                              double timeSamples,
                                              float normalizedValue,
                                              double sampleRate,
                                              double bpm,
                                              CurveType curveType)
        {
            if (paramID == kInvalidParameterID || index < 0 || sampleRate <= 0.0 || bpm <= 0.0)
                return;

            if (auto lane = AutomationLaneStore::getInstance().findLane (paramID))
                lane->setPoint (index, { samplesToPPQ (timeSamples, sampleRate, bpm), juce::jlimit (0.0f, 1.0f, normalizedValue), curveType, 0.0f });
        }

        void removeBreakpointForParam (ParameterID paramID, int index)
        {
            if (paramID == kInvalidParameterID || index < 0)
                return;

            if (auto lane = AutomationLaneStore::getInstance().findLane (paramID))
                lane->removePoint (index);
        }

        std::vector<BoolBreakpoint> getBoolBreakpointsForParam (ParameterID paramID,
                                                                double sampleRate,
                                                                double bpm) const
        {
            std::vector<BoolBreakpoint> out;
            if (paramID == kInvalidParameterID || sampleRate <= 0.0 || bpm <= 0.0)
                return out;

            auto lane = AutomationLaneStore::getInstance().findLane (paramID);
            if (lane == nullptr)
                return out;

            auto snap = lane->getSnapshot();
            if (snap == nullptr || snap->empty())
                return out;

            const double ppqToSamples = (60.0 / bpm) * sampleRate;
            out.reserve (snap->size());
            for (const auto& bp : *snap)
                out.push_back ({ bp.timePPQ * ppqToSamples, bp.normalizedValue >= 0.5f });
            return out;
        }

        bool paramHasAutomation (ParameterID paramID) const
        {
            if (paramID == kInvalidParameterID)
                return false;
            auto lane = AutomationLaneStore::getInstance().findLane (paramID);
            if (lane == nullptr)
                return false;
            auto snap = lane->getSnapshot();
            return snap != nullptr && ! snap->empty();
        }

        mutable juce::CriticalSection lastSnapLock;
        std::unordered_map<juce::String, const void*, StringHash> lastSnapshotPtr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationArrangementBridge)
    };
}
