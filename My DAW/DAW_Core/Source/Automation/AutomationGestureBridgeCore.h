#pragma once

#include "AutomationParameterCore.h"
#include "AutomationParameterRegistryCore.h"
#include "AutomationGestureQueueCore.h"
#include "AutomationClockCore.h"
#include <JuceHeader.h>
#include <unordered_map>
#include <memory>

namespace apex::automation
{
    /**
        Bridges every AutomationParameter and every hosted plugin's
        AudioProcessor into the AutomationGestureQueue.

        For native APEX parameters: attaches a per-parameter Listener that
        translates parameterValueChanged / GestureBegan / GestureEnded into
        queue events.

        For hosted plugins: acts as juce::AudioProcessorListener, converting
        audioProcessorParameterChange* callbacks into queue events. These may
        arrive on the audio thread -- the push() call is lock-free.
    */
    class AutomationGestureBridge
        : public juce::AudioProcessorListener
    {
    public:
        AutomationGestureBridge (AutomationParameterRegistry& reg,
                                 AutomationGestureQueue&      q,
                                 AutomationClock&             c)
            : registry (reg)
            , queue    (q)
            , clock    (c)
        {
        }

        ~AutomationGestureBridge() override
        {
            detachFromAllParameters();
            detachFromAllPlugins();
        }

        // ----- Native parameter attach (message thread) ------------------

        void attachToParameter (AutomationParameter& p)
        {
            const auto id = p.getID();
            if (nativeListeners.count (id)) return;

            auto lst = std::make_unique<NativeListener> (queue, clock, id);
            p.addListener (lst.get());
            nativeListeners.emplace (id, std::move (lst));
        }

        void detachFromParameter (ParameterID id)
        {
            auto it = nativeListeners.find (id);
            if (it == nativeListeners.end()) return;
            if (auto* p = registry.find (id))
                p->removeListener (it->second.get());
            nativeListeners.erase (it);
        }

        void detachFromAllParameters()
        {
            for (auto& [id, lst] : nativeListeners)
                if (auto* p = registry.find (id))
                    p->removeListener (lst.get());
            nativeListeners.clear();
        }

        // ----- Hosted plugin attach (message thread) --------------------

        void attachToPlugin (juce::AudioProcessor&                proc,
                             std::unordered_map<int, ParameterID> paramIndexToID)
        {
            proc.addListener (this);
            pluginIndexMaps[&proc] = std::move (paramIndexToID);
        }

        void detachFromPlugin (juce::AudioProcessor& proc)
        {
            proc.removeListener (this);
            pluginIndexMaps.erase (&proc);
        }

        void detachFromAllPlugins()
        {
            for (auto& [proc, _] : pluginIndexMaps)
                if (proc != nullptr)
                    proc->removeListener (this);
            pluginIndexMaps.clear();
        }

        // ----- juce::AudioProcessorListener (may be called on audio thread) ---

        void audioProcessorParameterChanged (juce::AudioProcessor* proc,
                                             int                   parameterIndex,
                                             float                 newValue) override
        {
            const ParameterID id = resolvePluginParam (proc, parameterIndex);
            if (id == kInvalidParameterID) return;

            AutomationGestureQueue::Event e;
            e.paramID         = id;
            e.kind            = AutomationGestureQueue::EventKind::ValueChange;
            e.source          = ChangeSource::Plugin;
            e.normalizedValue = juce::jlimit (0.0f, 1.0f, newValue);
            e.ppqAtCapture    = clock.snapshot().blockStartPPQ;
            queue.push (e);
        }

        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor* proc,
                                                        int parameterIndex) override
        {
            const ParameterID id = resolvePluginParam (proc, parameterIndex);
            if (id == kInvalidParameterID) return;

            AutomationGestureQueue::Event e;
            e.paramID      = id;
            e.kind         = AutomationGestureQueue::EventKind::GestureBegin;
            e.source       = ChangeSource::Plugin;
            // Capture while inside the plugin's gesture-begin callback,
            // before delayed queue processing observes its later new value.
            // Do not allocate or lock in this potentially audio-thread path.
            if (proc != nullptr)
            {
                const auto& parameters = proc->getParameters();
                if (juce::isPositiveAndBelow (parameterIndex, parameters.size()))
                {
                    if (auto* parameter = parameters.getUnchecked (parameterIndex))
                    {
                        e.normalizedValue = juce::jlimit (0.0f, 1.0f, parameter->getValue());
                        e.hasStartValue = true;
                    }
                }
            }
            e.ppqAtCapture = clock.snapshot().blockStartPPQ;
            queue.push (e);
        }

        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor* proc,
                                                      int parameterIndex) override
        {
            const ParameterID id = resolvePluginParam (proc, parameterIndex);
            if (id == kInvalidParameterID) return;

            AutomationGestureQueue::Event e;
            e.paramID      = id;
            e.kind         = AutomationGestureQueue::EventKind::GestureEnd;
            e.source       = ChangeSource::Plugin;
            e.ppqAtCapture = clock.snapshot().blockStartPPQ;
            queue.push (e);
        }

        void audioProcessorChanged (juce::AudioProcessor*,
                                    const ChangeDetails&) override {}

        static AutomationGestureBridge& getInstance();

    private:
        ParameterID resolvePluginParam (juce::AudioProcessor* proc,
                                        int                   index) const noexcept
        {
            auto it = pluginIndexMaps.find (proc);
            if (it == pluginIndexMaps.end()) return kInvalidParameterID;
            auto pit = it->second.find (index);
            return (pit != it->second.end()) ? pit->second : kInvalidParameterID;
        }

        // ----- Inner listener for native params -------------------------

        struct NativeListener : public AutomationParameter::Listener
        {
            NativeListener (AutomationGestureQueue& q,
                            AutomationClock&        c,
                            ParameterID             id)
                : queue (q), clock (c), paramID (id) {}

            void parameterValueChanged (AutomationParameter&,
                                        float        newNormalized,
                                        ChangeSource source) override
            {
                if (source != ChangeSource::User && source != ChangeSource::Plugin)
                    return;

                AutomationGestureQueue::Event e;
                e.paramID         = paramID;
                e.kind            = AutomationGestureQueue::EventKind::ValueChange;
                e.source          = source;
                e.normalizedValue = newNormalized;
                e.ppqAtCapture    = clock.snapshot().blockStartPPQ;
                queue.push (e);
            }

            void parameterGestureBegan (AutomationParameter& p) override
            {
                AutomationGestureQueue::Event e;
                e.paramID          = paramID;
                e.kind             = AutomationGestureQueue::EventKind::GestureBegin;
                e.source           = ChangeSource::User;
                // Deferred UI dispatch may see a much later parameter value.
                // Use the snapshot taken in AutomationParameter::beginGesture.
                e.normalizedValue  = p.getValueAtGestureBegin();
                e.hasStartValue    = true;
                e.ppqAtCapture     = clock.snapshot().blockStartPPQ;
                queue.push (e);
            }

            void parameterGestureEnded (AutomationParameter&) override
            {
                AutomationGestureQueue::Event e;
                e.paramID      = paramID;
                e.kind         = AutomationGestureQueue::EventKind::GestureEnd;
                e.source       = ChangeSource::User;
                e.ppqAtCapture = clock.snapshot().blockStartPPQ;
                queue.push (e);
            }

            AutomationGestureQueue& queue;
            AutomationClock&        clock;
            const ParameterID       paramID;
        };

        AutomationParameterRegistry& registry;
        AutomationGestureQueue&      queue;
        AutomationClock&             clock;

        std::unordered_map<ParameterID, std::unique_ptr<NativeListener>>              nativeListeners;
        std::unordered_map<juce::AudioProcessor*, std::unordered_map<int, ParameterID>> pluginIndexMaps;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationGestureBridge)
    };

    inline AutomationGestureBridge& AutomationGestureBridge::getInstance()
    {
        static AutomationGestureBridge instance (
            AutomationParameterRegistry::getInstance(),
            AutomationGestureQueue::getInstance(),
            AutomationClock::getInstance());
        return instance;
    }
}
