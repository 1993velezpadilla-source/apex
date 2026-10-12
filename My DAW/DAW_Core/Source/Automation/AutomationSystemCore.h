#pragma once

#include "AutomationTypes.h"
#include "AutomationParameterCore.h"
#include "AutomationParameterRegistryCore.h"
#include "AutomationModeStateCore.h"
#include "AutomationLaneStoreCore.h"
#include "AutomationEvaluatorCore.h"
#include "AutomationUIDispatcherCore.h"
#include "AutomationAttachmentCore.h"
#include "AutomationClockCore.h"
#include "AutomationGestureQueueCore.h"
#include "AutomationGestureBridgeCore.h"
#include "AutomationTransportStateCore.h"
#include "AutomationRecorderCore.h"
#include <JuceHeader.h>
#include <vector>

namespace apex::automation
{
    /**
        Single entry point for the entire APEX automation subsystem.

        AudioEngine calls:
          onAudioBlockStart(playheadPPQ, ppqPerSample, transportRolling)
          once per processBlock BEFORE reading any parameter values. This
          publishes the clock and runs the evaluator so parameters with
          non-empty lanes in Read/Touch/Latch are updated before any
          downstream code reads them.

        UI / project code calls:
          createNativeParameter, attachHostedPlugin, setMode, armRecording.
    */
    class AutomationSystem
    {
    public:
        static AutomationSystem& getInstance()
        {
            static AutomationSystem instance;
            return instance;
        }

        // ----- Audio thread entry point (call from processBlock) --------

        void onAudioBlockStart (double playheadPPQAtBlockStart,
                                double ppqPerSample,
                                bool   transportRolling) noexcept
        {
            clock.publishFromAudioThread (playheadPPQAtBlockStart,
                                          ppqPerSample,
                                          transportRolling);
            evaluator.evaluateBlock (playheadPPQAtBlockStart,
                                     transportRolling);
        }

        /** Offline bounce has its own PPQ timeline. Evaluate saved lanes at
            the export position without publishing a synthetic Play transition
            into the live clock consumed by the gesture recorder and UI.
            The audio renderer and plugin chains receive export sample/PPQ
            explicitly; this is only the block-rate parameter mirror. */
        void onOfflineBlockStart (double exportPPQAtBlockStart) noexcept
        {
            evaluator.evaluateBlock (exportPPQAtBlockStart, true);
        }

        // ----- Native parameter registration ----------------------------

        AutomationParameter* createNativeParameter (ParameterID    id,
                                                    juce::String   name,
                                                    ParameterRange range)
        {
            JUCE_ASSERT_MESSAGE_THREAD;
            auto* p = registry.createParameter (id, std::move (name), range,
                                                ParameterScope::Native);
            if (p != nullptr)
                bridge.attachToParameter (*p);
            return p;
        }

        void removeNativeParameter (ParameterID id)
        {
            JUCE_ASSERT_MESSAGE_THREAD;

            const bool lastRegistration = registry.isLastRegistration (id);
            if (lastRegistration)
                bridge.detachFromParameter (id);

            // A parameter registration belongs to the MIXER UI/host lifetime,
            // not to the project lane lifetime. Rebuilding a MixerStrip,
            // collapsing a folder or closing a plugin editor must never erase
            // persisted automation or its Read/Write/Touch/Latch/Trim mode.
            // True track deletion and New Project explicitly discard the
            // corresponding project-scoped lanes/modes at their own boundary.
            registry.unregisterParameter (id);
        }

        // ----- Hosted plugin attachment ---------------------------------

        struct PluginAttachInfo
        {
            int                            parameterIndex = 0;
            ParameterID                    paramID        = kInvalidParameterID;
            juce::String                   name;
            ParameterRange                 range;
            juce::AudioProcessorParameter* pluginParam    = nullptr;
            PluginInstanceID               pluginInstance = kNativeInstanceID;
        };

        void attachHostedPlugin (juce::AudioProcessor&                proc,
                                 const std::vector<PluginAttachInfo>& params)
        {
            JUCE_ASSERT_MESSAGE_THREAD;

            std::unordered_map<int, ParameterID> indexMap;
            indexMap.reserve (params.size());

            for (const auto& info : params)
            {
                auto* ap = registry.createParameter (info.paramID,
                                                     info.name,
                                                     info.range,
                                                     ParameterScope::Plugin,
                                                     info.pluginInstance);
                if (ap != nullptr)
                {
                    ap->bindToPluginParameter (info.pluginParam);
                    bridge.attachToParameter (*ap);
                }
                indexMap.emplace (info.parameterIndex, info.paramID);
            }

            bridge.attachToPlugin (proc, std::move (indexMap));
        }

        void detachHostedPlugin (juce::AudioProcessor&           proc,
                                 const std::vector<ParameterID>& paramIDs)
        {
            JUCE_ASSERT_MESSAGE_THREAD;
            bridge.detachFromPlugin (proc);
            for (auto id : paramIDs)
                removeNativeParameter (id);
        }

        // ----- Mode / arm control ---------------------------------------

        void setMode              (ParameterID id, AutomationMode m) { modeState.setMode (id, m); }
        void setGlobalDefaultMode (AutomationMode m)                 { modeState.setGlobalDefaultMode (m); }
        void armRecording         (bool armed)                       { armState.setRecordArmed (armed); }
        void setAutoCreateLaneOnTouch (bool enabled)                 { armState.setAutoCreateLaneOnTouch (enabled); }

        // ----- Accessors ------------------------------------------------

        AutomationParameterRegistry& getRegistry()  noexcept { return registry; }
        AutomationLaneStore&         getLaneStore()  noexcept { return laneStore; }
        AutomationModeState&         getModeState()  noexcept { return modeState; }
        AutomationTransportState&    getArmState()   noexcept { return armState; }
        AutomationGestureQueue&      getQueue()      noexcept { return queue; }
        AutomationClock&             getClock()      noexcept { return clock; }

        /** Final message-thread teardown after realtime callbacks and UI
         *  parameter owners have been destroyed. Retired parameter snapshots
         *  are reclaimed here rather than during CRT static destruction. */
        void shutdownForApplicationExit()
        {
            JUCE_ASSERT_MESSAGE_THREAD;
            if (applicationExitPrepared)
                return;

            recorder.shutdownForApplicationExit();
            dispatcher.pause();
            bridge.detachFromAllPlugins();
            bridge.detachFromAllParameters();
            registry.clear();
            applicationExitPrepared = true;
        }

    private:
        AutomationSystem()
            : registry   (AutomationParameterRegistry::getInstance())
            , laneStore  (AutomationLaneStore::getInstance())
            , modeState  (AutomationModeState::getInstance())
            , queue      (AutomationGestureQueue::getInstance())
            , clock      (AutomationClock::getInstance())
            , armState   (AutomationTransportState::getInstance())
            , bridge     (AutomationGestureBridge::getInstance())
            , evaluator  (registry, laneStore, modeState)
            , dispatcher (AutomationUIDispatcher::getInstance())
            , recorder   (AutomationRecorder::getInstance())
        {
        }

        AutomationParameterRegistry& registry;
        AutomationLaneStore&         laneStore;
        AutomationModeState&         modeState;
        AutomationGestureQueue&      queue;
        AutomationClock&             clock;
        AutomationTransportState&    armState;
        AutomationGestureBridge&     bridge;

        AutomationEvaluator          evaluator;

        AutomationUIDispatcher&      dispatcher;
        AutomationRecorder&          recorder;
        bool                         applicationExitPrepared = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationSystem)
    };
}
