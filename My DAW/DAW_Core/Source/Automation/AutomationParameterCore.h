#pragma once

#include "AutomationTypes.h"
#include "AutomationClockCore.h"
#include "AutomationGestureQueueCore.h"
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <limits>
#include <cmath>

namespace apex::automation
{
    /**
        Single automatable parameter.

        Threading model:
          - currentValueN / version / gestureActive are atomic and may be read
            on any thread including the audio thread.
          - Listener notification happens only on the message thread, driven by
            AutomationUIDispatcherCore at ~60Hz. Listeners must not be added or
            removed from non-message threads.
          - bindToPluginParameter() and the std::function setters must be
            assigned before any audio processing begins; they are not lock-free
            against rebinding.
    */
    class AutomationParameter
    {
    public:
        struct Listener
        {
            virtual ~Listener() = default;

            // Called on the message thread when the value changed since the
            // last dispatcher tick. The widget should refresh visually here
            // using dontSendNotification semantics to avoid feedback loops.
            virtual void parameterValueChanged (AutomationParameter& p,
                                                float newNormalized,
                                                ChangeSource source) = 0;

            // Called on the message thread when gesture state transitions.
            // Widgets can ignore these -- they exist for the recorder.
            virtual void parameterGestureBegan (AutomationParameter& /*p*/) {}
            virtual void parameterGestureEnded (AutomationParameter& /*p*/) {}
        };

        AutomationParameter (ParameterID       id,
                             juce::String      displayName,
                             ParameterRange    range,
                             ParameterScope    scope          = ParameterScope::Native,
                             PluginInstanceID  pluginInstance = kNativeInstanceID)
            : paramID          (id)
            , name             (std::move (displayName))
            , paramRange       (range)
            , paramScope       (scope)
            , pluginInstanceID (pluginInstance)
        {
            currentValueN.store (paramRange.normalize (paramRange.defaultValue),
                                 std::memory_order_relaxed);
        }

        // ----- Identification ---------------------------------------------

        ParameterID         getID()             const noexcept { return paramID; }
        const juce::String& getName()           const noexcept { return name; }
        const ParameterRange& getRange()        const noexcept { return paramRange; }
        ParameterScope      getScope()          const noexcept { return paramScope; }
        PluginInstanceID    getPluginInstance() const noexcept { return pluginInstanceID; }

        // ----- Value access (any thread) ----------------------------------

        float getNormalizedValue() const noexcept
        {
            return currentValueN.load (std::memory_order_acquire);
        }

        float getDenormalizedValue() const noexcept
        {
            return paramRange.denormalize (getNormalizedValue());
        }

        std::uint64_t getVersion() const noexcept
        {
            return version.load (std::memory_order_acquire);
        }

        bool isGestureActive() const noexcept
        {
            return gestureActive.load (std::memory_order_acquire);
        }

        // The UI dispatcher can notify gesture listeners after multiple
        // parameter value writes. Preserve the actual gesture-start value.
        float getValueAtGestureBegin() const noexcept
        {
            return gestureBeginValueN.load (std::memory_order_acquire);
        }

        // Native UI notifications are deferred. Capture PPQ at the actual
        // parameter operation, not at the next 60 Hz dispatcher callback.
        // When no bridge is attached, the getter returns NaN so its listener
        // can fall back to its own clock without inventing a timestamp.
        void bindGestureCaptureClock (AutomationClock* c) noexcept
        {
            gestureCaptureClock.store (c, std::memory_order_release);
        }

        void unbindGestureCaptureClock (AutomationClock* expected) noexcept
        {
            gestureCaptureClock.compare_exchange_strong (expected, nullptr,
                std::memory_order_acq_rel, std::memory_order_acquire);
        }

        // The bridge attaches the native recording queue before interaction.
        // Capture the event at its *producer*, not via the coalescing 60 Hz
        // widget notifier, which cannot preserve successive fast knob moves.
        // Queue and clock must outlive this binding; detach on message thread
        // after input callbacks quiesce, before destruction.
        void bindDirectGestureQueue (AutomationGestureQueue* q) noexcept
        {
            directGestureQueue.store (q, std::memory_order_release);
        }

        void unbindDirectGestureQueue (AutomationGestureQueue* expected) noexcept
        {
            directGestureQueue.compare_exchange_strong (expected, nullptr,
                std::memory_order_acq_rel, std::memory_order_acquire);
        }

        bool hasDirectGestureQueue() const noexcept
        {
            return directGestureQueue.load (std::memory_order_acquire) != nullptr;
        }

        double getCapturedGestureBeginPPQ() const noexcept
        {
            return gestureBeginPPQ.load (std::memory_order_acquire);
        }

        double getCapturedGestureEndPPQ() const noexcept
        {
            return gestureEndPPQ.load (std::memory_order_acquire);
        }

        double getCapturedValueChangePPQ() const noexcept
        {
            return latestValueChangePPQ.load (std::memory_order_acquire);
        }

        // ----- Value writers ----------------------------------------------

        // Called by APEX UI widgets when the user drags/clicks the control.
        void setValueFromUser (float newNormalized)
        {
            if (!std::isfinite (newNormalized)) return;
            const double ppq = captureInputPPQ();
            latestValueChangePPQ.store (ppq, std::memory_order_release);
            writeValue (newNormalized, ChangeSource::User);
            publishDirectGesture (AutomationGestureQueue::EventKind::ValueChange,
                                  ChangeSource::User, newNormalized, ppq, false);
            forwardToPluginIfBound (newNormalized);
        }

        // Called by the AutomationEvaluator on the audio thread during
        // playback. MUST be lock-free and allocation-free.
        void setValueFromAutomation (float newNormalized) noexcept
        {
            if (!std::isfinite (newNormalized)) return;
            writeValue (newNormalized, ChangeSource::Automation);
            if (auto* p = pluginParam.load (std::memory_order_acquire))
                p->setValue (std::clamp (newNormalized, 0.0f, 1.0f));
        }

        // Called by AudioProcessorListener when a hosted plugin's GUI moved
        // the parameter itself.
        void setValueFromPlugin (float newNormalized)
        {
            if (!std::isfinite (newNormalized)) return;
            const double ppq = captureInputPPQ();
            latestValueChangePPQ.store (ppq, std::memory_order_release);
            writeValue (newNormalized, ChangeSource::Plugin);
            publishDirectGesture (AutomationGestureQueue::EventKind::ValueChange,
                                  ChangeSource::Plugin, newNormalized, ppq, false);
        }

        // For preset loads, undo, scripted assignment, etc.
        void setValueProgrammatic (float newNormalized)
        {
            if (!std::isfinite (newNormalized)) return;
            writeValue (newNormalized, ChangeSource::Programmatic);
            forwardToPluginIfBound (newNormalized);
        }

        // ----- Gesture lifecycle ------------------------------------------

        void beginGesture()
        {
            const bool wasActive = gestureActive.exchange (true, std::memory_order_acq_rel);
            if (! wasActive)
            {
                const float startValue = getNormalizedValue();
                const double ppq = captureInputPPQ();
                gestureBeginValueN.store (startValue, std::memory_order_release);
                gestureBeginPPQ.store (ppq, std::memory_order_release);
                publishDirectGesture (AutomationGestureQueue::EventKind::GestureBegin,
                                      ChangeSource::User, startValue, ppq, true);
                pendingGestureBegin.store (true, std::memory_order_release);
                version.fetch_add (1, std::memory_order_acq_rel);
            }
        }

        void endGesture()
        {
            const bool wasActive = gestureActive.exchange (false, std::memory_order_acq_rel);
            if (wasActive)
            {
                const double ppq = captureInputPPQ();
                gestureEndPPQ.store (ppq, std::memory_order_release);
                publishDirectGesture (AutomationGestureQueue::EventKind::GestureEnd,
                                      ChangeSource::User, getNormalizedValue(), ppq, false);
                pendingGestureEnd.store (true, std::memory_order_release);
                version.fetch_add (1, std::memory_order_acq_rel);
            }
        }

        // ----- Plugin parameter bridge ------------------------------------

        void bindToPluginParameter (juce::AudioProcessorParameter* p) noexcept
        {
            pluginParam.store (p, std::memory_order_release);
        }

        juce::AudioProcessorParameter* getBoundPluginParameter() const noexcept
        {
            return pluginParam.load (std::memory_order_acquire);
        }

        // ----- Listener management (message thread only) ------------------

        void addListener    (Listener* l)
        {
            if (l == nullptr)
                return;

            listeners.remove (l);
            listeners.add (l);
        }
        void removeListener (Listener* l) { listeners.remove (l); }

        // Called by the UI dispatcher on the message thread.
        void dispatchPendingNotifications (float        latestNormalized,
                                           ChangeSource latestSource)
        {
            if (pendingGestureBegin.exchange (false, std::memory_order_acq_rel))
                listeners.call ([this] (Listener& l) { l.parameterGestureBegan (*this); });

            // GestureBegin / GestureEnd also advance the broad version
            // counter to wake the UI dispatcher. They are NOT value writes.
            // Reporting parameterValueChanged on a gesture-only tick creates
            // a phantom ValueChange in the recorder with a stale timestamp,
            // even though the producer never moved the control.
            const auto currentValueVersion = valueVersion.load (std::memory_order_acquire);
            if (currentValueVersion != lastDispatchedValueVersion)
            {
                lastDispatchedValueVersion = currentValueVersion;
                listeners.call ([this, latestNormalized, latestSource] (Listener& l)
                {
                    l.parameterValueChanged (*this, latestNormalized, latestSource);
                });
            }

            if (pendingGestureEnd.exchange (false, std::memory_order_acq_rel))
                listeners.call ([this] (Listener& l) { l.parameterGestureEnded (*this); });
        }

        ChangeSource getLastSource() const noexcept
        {
            return static_cast<ChangeSource> (
                lastSource.load (std::memory_order_acquire));
        }

    public:
        // Exposed for AutomationEvaluator — writes the atomic state without
        // forwarding to the bound plugin parameter. Plugin parameter writes
        // with proper per-sample smoothing happen in Phase 2 via
        // PluginInstanceCore::applyAutomationAtSample.
        void writeValue (float newNormalized, ChangeSource src) noexcept
        {
            // std::clamp and jlimit do not sanitize NaN; malformed hosted
            // plugin values must never poison realtime parameter atomics.
            if (!std::isfinite (newNormalized)) return;
            newNormalized = std::clamp (newNormalized, 0.0f, 1.0f);
            currentValueN.store (newNormalized, std::memory_order_release);
            lastSource.store (static_cast<std::uint8_t> (src),
                              std::memory_order_release);
            valueVersion.fetch_add (1, std::memory_order_acq_rel);
            version.fetch_add (1, std::memory_order_acq_rel);
        }

        void forwardToPluginIfBound (float newNormalized) noexcept
        {
            if (!std::isfinite (newNormalized)) return;
            if (auto* p = pluginParam.load (std::memory_order_acquire))
                p->setValueNotifyingHost (std::clamp (newNormalized, 0.0f, 1.0f));
        }

        double captureInputPPQ() const noexcept
        {
            if (auto* c = gestureCaptureClock.load (std::memory_order_acquire))
                return c->snapshot().blockStartPPQ;
            return std::numeric_limits<double>::quiet_NaN();
        }

        void publishDirectGesture (AutomationGestureQueue::EventKind kind,
                                   ChangeSource source, float value,
                                   double ppq, bool hasStartValue) noexcept
        {
            if (auto* q = directGestureQueue.load (std::memory_order_acquire))
            {
                AutomationGestureQueue::Event e;
                e.paramID = paramID;
                e.kind = kind;
                e.source = source;
                e.normalizedValue = std::clamp (value, 0.0f, 1.0f);
                e.ppqAtCapture = ppq;
                e.hasStartValue = hasStartValue;
                // A bounded MPSC queue is the only producer-side handoff.
                // Overflow is recorded by push(), and the recorder fences
                // old sessions rather than silently accept lost events.
                (void) q->push (e);
            }
        }

        // Identity
        const ParameterID      paramID;
        const juce::String     name;
        const ParameterRange   paramRange;
        const ParameterScope   paramScope;
        const PluginInstanceID pluginInstanceID;

        // State (all atomic; readable from any thread)
        std::atomic<float>         currentValueN       { 0.0f };
        std::atomic<std::uint64_t> version             { 0 };
        // Changes only on actual value writes, unlike version which also
        // signals gesture edges. Read by the message-thread UI dispatcher.
        std::atomic<std::uint64_t> valueVersion        { 0 };
        std::uint64_t lastDispatchedValueVersion = 0; // message thread only
        std::atomic<bool>          gestureActive       { false };
        std::atomic<float>         gestureBeginValueN  { 0.0f };
        std::atomic<AutomationClock*> gestureCaptureClock { nullptr };
        std::atomic<AutomationGestureQueue*> directGestureQueue { nullptr };
        std::atomic<double> gestureBeginPPQ { std::numeric_limits<double>::quiet_NaN() };
        std::atomic<double> gestureEndPPQ { std::numeric_limits<double>::quiet_NaN() };
        std::atomic<double> latestValueChangePPQ { std::numeric_limits<double>::quiet_NaN() };
        std::atomic<bool>          pendingGestureBegin { false };
        std::atomic<bool>          pendingGestureEnd   { false };
        std::atomic<std::uint8_t>  lastSource          {
            static_cast<std::uint8_t> (ChangeSource::Programmatic) };
        std::atomic<juce::AudioProcessorParameter*> pluginParam { nullptr };

        // Listeners (message-thread only)
        juce::ListenerList<Listener> listeners;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationParameter)
    };
}
