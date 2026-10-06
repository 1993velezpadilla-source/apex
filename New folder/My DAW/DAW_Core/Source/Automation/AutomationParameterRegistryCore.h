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
    public:
        AutomationParameterRegistry() = default;
        ~AutomationParameterRegistry() { clear(); }

        // ----- Registration (message thread) -----------------------------

        AutomationParameter* registerParameter (std::unique_ptr<AutomationParameter> p)
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
            return raw;
        }

        AutomationParameter* createParameter (ParameterID       id,
                                              juce::String      name,
                                              ParameterRange    range,
                                              ParameterScope    scope          = ParameterScope::Native,
                                              PluginInstanceID  pluginInstance = kNativeInstanceID)
        {
            return registerParameter (std::make_unique<AutomationParameter> (
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
            parameters.erase (id);
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
        }

        // ----- Lookup -----------------------------------------------------

        AutomationParameter* find (ParameterID id) const noexcept
        {
            const juce::ScopedLock sl (registryLock);
            auto it = parameters.find (id);
            return it == parameters.end() ? nullptr : it->second.get();
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
        std::unordered_map<ParameterID, std::unique_ptr<AutomationParameter>> parameters;
        std::unordered_map<ParameterID, std::size_t> registrationCounts;
        mutable juce::CriticalSection registryLock;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationParameterRegistry)
    };
}
