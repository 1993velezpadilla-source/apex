#pragma once

#include "AutomationParameterCore.h"
#include "AutomationParameterRegistryCore.h"
#include "AutomationGestureQueueCore.h"
#include "AutomationClockCore.h"
#include <JuceHeader.h>
#include <unordered_map>
#include <memory>
#include <cmath>
#include <vector>
#include <atomic>

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
            p.bindGestureCaptureClock (&clock);
            p.addListener (lst.get());
            nativeListeners.emplace (id, std::move (lst));
        }

        void detachFromParameter (ParameterID id)
        {
            auto it = nativeListeners.find (id);
            if (it == nativeListeners.end()) return;
            if (auto* p = registry.find (id))
            {
                p->removeListener (it->second.get());
                p->unbindGestureCaptureClock (&clock);
            }
            nativeListeners.erase (it);
        }

        void detachFromAllParameters()
        {
            for (auto& [id, lst] : nativeListeners)
                if (auto* p = registry.find (id))
                {
                    p->removeListener (lst.get());
                    p->unbindGestureCaptureClock (&clock);
                }
            nativeListeners.clear();
        }

        // ----- Hosted plugin attach (message thread) --------------------

        void attachToPlugin (juce::AudioProcessor&                proc,
                             std::unordered_map<int, ParameterID> paramIndexToID)
        {
            JUCE_ASSERT_MESSAGE_THREAD;
            const bool firstAttach = pluginIndexMaps.find (&proc) == pluginIndexMaps.end();
            pluginIndexMaps[&proc] = std::move (paramIndexToID);
            // Publish before registering the listener: a callback fired as
            // soon as JUCE adds it must already resolve the new mapping.
            publishPluginIndexMaps();
            if (firstAttach)
                proc.addListener (this);
        }

        void detachFromPlugin (juce::AudioProcessor& proc)
        {
            JUCE_ASSERT_MESSAGE_THREAD;
            if (pluginIndexMaps.erase (&proc) == 0)
                return;
            // Remove the mapping before detaching, so any in-flight
            // callback observing the new snapshot is discarded safely.
            publishPluginIndexMaps();
            proc.removeListener (this);
        }

        void detachFromAllPlugins()
        {
            JUCE_ASSERT_MESSAGE_THREAD;
            // Snapshot is published before destroying mutable registration
            // data; concurrent audio callbacks never inspect that data.
            auto previous = std::move (pluginIndexMaps);
            pluginIndexMaps.clear();
            publishPluginIndexMaps();
            for (auto& [proc, _] : previous)
                if (proc != nullptr)
                    proc->removeListener (this);
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
        using PluginMap = std::unordered_map<juce::AudioProcessor*,
                                              std::unordered_map<int, ParameterID>>;

        struct PluginMapSnapshot
        {
            PluginMap bindings;
        };

        // Message thread ONLY. The published object is immutable; the audio
        // thread atomically loads its address and never touches mutable maps.
        void publishPluginIndexMaps()
        {
            auto next = std::make_unique<PluginMapSnapshot>();
            next->bindings = pluginIndexMaps;
            auto* immutable = next.get();
            // Old snapshots stay alive until the bridge is destroyed.
            // Readers that loaded one before a plugin detaches must never
            // encounter a freed unordered_map while a callback is running.
            retainedPluginSnapshots.push_back (std::move (next));
            publishedPluginMaps.store (immutable, std::memory_order_release);
        }

        ParameterID resolvePluginParam (juce::AudioProcessor* proc,
                                        int                   index) const noexcept
        {
            const auto* snapshot = publishedPluginMaps.load (std::memory_order_acquire);
            if (snapshot == nullptr) return kInvalidParameterID;
            const auto it = snapshot->bindings.find (proc);
            if (it == snapshot->bindings.end()) return kInvalidParameterID;
            const auto pit = it->second.find (index);
            return (pit != it->second.end()) ? pit->second : kInvalidParameterID;
        }

        // ----- Inner listener for native params -------------------------

        struct NativeListener : public AutomationParameter::Listener
        {
            NativeListener (AutomationGestureQueue& q,
                            AutomationClock&        c,
                            ParameterID             id)
                : queue (q), clock (c), paramID (id) {}

            void parameterValueChanged (AutomationParameter& p,
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
                const double capturedPPQ = p.getCapturedValueChangePPQ();
                e.ppqAtCapture = std::isfinite (capturedPPQ)
                    ? capturedPPQ : clock.snapshot().blockStartPPQ;
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
                const double capturedPPQ = p.getCapturedGestureBeginPPQ();
                e.ppqAtCapture = std::isfinite (capturedPPQ)
                    ? capturedPPQ : clock.snapshot().blockStartPPQ;
                queue.push (e);
            }

            void parameterGestureEnded (AutomationParameter& p) override
            {
                AutomationGestureQueue::Event e;
                e.paramID      = paramID;
                e.kind         = AutomationGestureQueue::EventKind::GestureEnd;
                e.source       = ChangeSource::User;
                const double capturedPPQ = p.getCapturedGestureEndPPQ();
                e.ppqAtCapture = std::isfinite (capturedPPQ)
                    ? capturedPPQ : clock.snapshot().blockStartPPQ;
                queue.push (e);
            }

            AutomationGestureQueue& queue;
            AutomationClock&        clock;
            const ParameterID       paramID;
        };

        AutomationParameterRegistry& registry;
        AutomationGestureQueue&      queue;
        AutomationClock&             clock;

        std::unordered_map<ParameterID, std::unique_ptr<NativeListener>> nativeListeners;

        // Mutable registrations live only on the message thread. Historical
        // immutable snapshots cost memory per attach/detach, deliberately
        // trading that off against locks, allocations and use-after-free on
        // hosted-plugin audio callbacks. A bounded reclamation scheme would
        // require explicit audio-reader quiescence/epochs.
        PluginMap pluginIndexMaps;
        std::vector<std::unique_ptr<const PluginMapSnapshot>> retainedPluginSnapshots;
        std::atomic<const PluginMapSnapshot*> publishedPluginMaps { nullptr };

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
