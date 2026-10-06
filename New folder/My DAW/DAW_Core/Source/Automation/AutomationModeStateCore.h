#pragma once

#include "AutomationTypes.h"
#include "AutomationParameterKeyCore.h"
#include <JuceHeader.h>
#include <unordered_map>
#include <atomic>

namespace apex::automation
{
    /**
        Holds the current AutomationMode per parameter, plus a global default.

        Modes are read on the audio thread by the evaluator and on the message
        thread by the recorder, so all storage is atomic.
    */
    class AutomationModeState
    {
    public:
        AutomationModeState() = default;

        // ----- Global default -------------------------------------------

        void setGlobalDefaultMode (AutomationMode m) noexcept
        {
            globalDefault.store (static_cast<std::uint8_t> (m),
                                 std::memory_order_release);
        }

        AutomationMode getGlobalDefaultMode() const noexcept
        {
            return static_cast<AutomationMode> (
                globalDefault.load (std::memory_order_acquire));
        }

        // ----- Per-parameter override -----------------------------------

        void setMode (ParameterID id, AutomationMode m)
        {
            const juce::ScopedLock sl (mapLock);
            modes[id].store (static_cast<std::uint8_t> (m),
                             std::memory_order_release);
        }

        AutomationMode getMode (ParameterID id) const noexcept
        {
            {
                const juce::ScopedLock sl (mapLock);
                auto it = modes.find (id);
                if (it != modes.end())
                    return static_cast<AutomationMode> (
                        it->second.load (std::memory_order_acquire));
            }
            return getGlobalDefaultMode();
        }

        void clearOverride (ParameterID id)
        {
            const juce::ScopedLock sl (mapLock);
            modes.erase (id);
        }

        void clearAll()
        {
            const juce::ScopedLock sl (mapLock);
            modes.clear();
        }

        // ----- Convenience queries --------------------------------------

        bool isReading (ParameterID id) const noexcept
        {
            const auto m = getMode (id);
            return m == AutomationMode::Read
                || m == AutomationMode::Write
                || m == AutomationMode::Touch
                || m == AutomationMode::Latch
                || m == AutomationMode::Trim;
        }

        bool isWriting (ParameterID id, bool gestureActive) const noexcept
        {
            const auto m = getMode (id);
            switch (m)
            {
                case AutomationMode::Write: return true;
                case AutomationMode::Touch: return gestureActive;
                case AutomationMode::Latch: return gestureActive || latchHeld (id);
                case AutomationMode::Trim:  return gestureActive;
                default:                    return false;
            }
        }

        void setLatchHeld (ParameterID id, bool held)
        {
            const juce::ScopedLock sl (mapLock);
            latched[id].store (held, std::memory_order_release);
        }

        bool latchHeld (ParameterID id) const noexcept
        {
            const juce::ScopedLock sl (mapLock);
            auto it = latched.find (id);
            return it != latched.end()
                && it->second.load (std::memory_order_acquire);
        }

        void clearAllLatches()
        {
            const juce::ScopedLock sl (mapLock);
            for (auto& [id, flag] : latched)
                flag.store (false, std::memory_order_release);
        }

        juce::ValueTree getState() const
        {
            juce::ValueTree state ("ModeState");
            state.setProperty ("globalDefault", (int) getGlobalDefaultMode(), nullptr);
            const juce::ScopedLock sl (mapLock);
            for (auto& [id, modeAtomic] : modes)
            {
                const juce::String key = AutomationParameterKeyRegistry::getInstance().findKey (id);
                if (key.isEmpty()) continue;
                juce::ValueTree entry ("Mode");
                entry.setProperty ("key",  key, nullptr);
                entry.setProperty ("mode", (int) modeAtomic.load (std::memory_order_acquire), nullptr);
                state.addChild (entry, -1, nullptr);
            }
            return state;
        }

        void restoreState (const juce::ValueTree& state)
        {
            if (! state.isValid()) return;
            clearAll();
            const auto defaultMode = static_cast<AutomationMode> (
                juce::jlimit (0, 5, (int) state.getProperty ("globalDefault", (int) AutomationMode::Read)));
            setGlobalDefaultMode (defaultMode);
            for (int i = 0; i < state.getNumChildren(); ++i)
            {
                auto entry = state.getChild (i);
                if (! entry.hasType ("Mode")) continue;
                const juce::String key = entry.getProperty ("key", {}).toString();
                if (key.isEmpty()) continue;
                const ParameterID id = AutomationParameterKeyRegistry::getInstance().findID (key);
                if (id == kInvalidParameterID) continue;
                const auto mode = static_cast<AutomationMode> (
                    juce::jlimit (0, 5, (int) entry.getProperty ("mode", (int) AutomationMode::Read)));
                setMode (id, mode);
            }
        }

        static AutomationModeState& getInstance()
        {
            static AutomationModeState instance;
            return instance;
        }

    private:
        std::atomic<std::uint8_t> globalDefault {
            static_cast<std::uint8_t> (AutomationMode::Read) };

        std::unordered_map<ParameterID, std::atomic<std::uint8_t>> modes;
        std::unordered_map<ParameterID, std::atomic<bool>>         latched;
        mutable juce::CriticalSection mapLock;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationModeState)
    };
}
