#pragma once

#include "AutomationParameterCore.h"
#include <JuceHeader.h>
#include <unordered_map>
#include <memory>

namespace apex::automation
{
    /**
        Central registry of every automatable parameter in APEX.

        Threading:
          - Registration / deregistration is message-thread only.
          - Read-only access (find, forEach) is safe from any thread provided
            no concurrent registration is happening, via CriticalSection.
    */
    class AutomationParameterRegistry
    {
        using ParameterMap = std::unordered_map<ParameterID, std::shared_ptr<AutomationParameter>>;

    public:
        AutomationParameterRegistry() = default;
        ~AutomationParameterRegistry() { clear(); }

        // ----- Registration (message thread) -----------------------------

        AutomationParameter* registerParameter (std::shared_ptr<AutomationParameter> p)
        {
            if (p == nullptr) return nullptr;

            const ParameterID id = p->getID();
            const juce::ScopedLock sl (registryLock);

            auto it = parameters.find (id);
            if (it != parameters.end())
            {
                ++registrationCounts[id];
                return it->second.get();
            }

            auto* raw = p.get();
            parameters.emplace (id, std::move (p));
            registrationCounts[id] = 1;
            publishLocked();
            return raw;
        }

        AutomationParameter* createParameter (ParameterID       id,
                                              juce::String      name,
                                              ParameterRange    range,
                                              ParameterScope    scope          = ParameterScope::Native,
                                              PluginInstanceID  pluginInstance = kNativeInstanceID)
        {
            return registerParameter (std::make_shared<AutomationParameter> (
                id, std::move (name), range, scope, pluginInstance));
        }

        bool unregisterParameter (ParameterID id)
        {
            const juce::ScopedLock sl (registryLock);

            auto countIt = registrationCounts.find (id);
            if (countIt == registrationCounts.end())
                return false;

            jassert (countIt->second > 0);

            if (--countIt->second > 0)
                return false;

            registrationCounts.erase (countIt);
            auto paramIt = parameters.find (id);
            if (paramIt != parameters.end())
            {
                // Retire instead of destroying: any audio-thread snapshot or
                // in-flight findRT() keeps the parameter alive. Retired
                // parameters are reclaimed only in clear()/destruction, both
                // message-thread contexts — never on the audio thread.
                retired_.push_back (std::move (paramIt->second));
                parameters.erase (paramIt);
            }
            publishLocked();
            return true;
        }

        bool isLastRegistration (ParameterID id) const noexcept
        {
            const juce::ScopedLock sl (registryLock);
            auto it = registrationCounts.find (id);
            return it != registrationCounts.end() && it->second == 1;
        }

        void clear()
        {
            const juce::ScopedLock sl (registryLock);
            parameters.clear();
            registrationCounts.clear();
            retired_.clear();
            publishLocked();
        }

        // ----- Lookup -----------------------------------------------------

        AutomationParameter* find (ParameterID id) const noexcept
        {
            const juce::ScopedLock sl (registryLock);
            auto it = parameters.find (id);
            return it == parameters.end() ? nullptr : it->second.get();
        }

        // ----- Audio-thread reads (lock-free, allocation-free) ------------

        /** Immutable copy-on-write snapshot of the parameter map. */
        std::shared_ptr<const ParameterMap> getSnapshotRT() const noexcept
        {
            return std::atomic_load_explicit (&published_, std::memory_order_acquire);
        }

        /** Lock-free lookup for the audio thread. The returned shared_ptr
         *  keeps the parameter alive for the duration of the block even if
         *  it is unregistered concurrently. */
        std::shared_ptr<AutomationParameter> findRT (ParameterID id) const noexcept
        {
            auto snap = getSnapshotRT();
            if (snap == nullptr)
                return nullptr;
            auto it = snap->find (id);
            return it == snap->end() ? nullptr : it->second;
        }

        /** Lock-free iteration for the audio thread over the published
         *  snapshot — NO registry lock is held at any point. The previous
         *  forEach() held registryLock across the entire walk every rolling
         *  block, the single largest priority-inversion window in the
         *  callback. */
        template <typename Fn>
        void forEachRT (Fn&& fn) const
        {
            auto snap = getSnapshotRT();
            if (snap == nullptr)
                return;
            for (auto& [id, ptr] : *snap)
                fn (*ptr);
        }

        bool contains (ParameterID id) const noexcept
        {
            const juce::ScopedLock sl (registryLock);
            return parameters.find (id) != parameters.end();
        }

        std::size_t size() const noexcept
        {
            const juce::ScopedLock sl (registryLock);
            return parameters.size();
        }

        // ----- Iteration --------------------------------------------------

        template <typename Fn>
        void forEach (Fn&& fn) const
        {
            const juce::ScopedLock sl (registryLock);
            for (auto& [id, ptr] : parameters)
                fn (*ptr);
        }

        std::vector<AutomationParameter*> getAllInScope (ParameterScope scope) const
        {
            std::vector<AutomationParameter*> out;
            const juce::ScopedLock sl (registryLock);
            out.reserve (parameters.size());
            for (auto& [id, ptr] : parameters)
                if (ptr->getScope() == scope)
                    out.push_back (ptr.get());
            return out;
        }

        std::vector<AutomationParameter*> getAllForPluginInstance (PluginInstanceID inst) const
        {
            std::vector<AutomationParameter*> out;
            const juce::ScopedLock sl (registryLock);
            for (auto& [id, ptr] : parameters)
                if (ptr->getPluginInstance() == inst)
                    out.push_back (ptr.get());
            return out;
        }

        // ----- Singleton --------------------------------------------------

        static AutomationParameterRegistry& getInstance()
        {
            static AutomationParameterRegistry instance;
            return instance;
        }

    private:
        // Copy-on-write publication (message thread only, registryLock held).
        // Copies only shared_ptrs — parameter objects are shared, and the
        // published map is never mutated in place.
        void publishLocked()
        {
            auto snap = std::make_shared<const ParameterMap> (parameters);
            auto old = std::atomic_exchange_explicit (&published_, std::move (snap), std::memory_order_acq_rel);
            retiredParamSnapshot_ = std::move (old);   // destroyed at the NEXT publish (message thread)
        }

        ParameterMap parameters;
        std::unordered_map<ParameterID, std::size_t> registrationCounts;
        std::vector<std::shared_ptr<AutomationParameter>> retired_;
        mutable juce::CriticalSection registryLock;

        // Published immutable view for the audio thread.
        std::shared_ptr<const ParameterMap> published_;
        std::shared_ptr<const ParameterMap> retiredParamSnapshot_;   // message-thread retire slot

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationParameterRegistry)
    };
}
