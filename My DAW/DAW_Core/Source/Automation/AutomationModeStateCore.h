#pragma once

#include "AutomationTypes.h"
#include "AutomationParameterKeyCore.h"
#include <JuceHeader.h>
#include <unordered_map>
#include <atomic>
#include <memory>

namespace apex::automation
{
    /**
        Holds the current AutomationMode per parameter, plus a global default.

        Threading:
          - Message thread mutates via setMode/clearOverride/setLatchHeld etc.
            under mapLock; every structural change publishes an immutable
            copy-on-write snapshot of the two maps.
          - The audio thread reads ONLY through getModeRT/latchHeldRT/
            isReadingRT, which are lock-free and allocation-free: they find
            the parameter's shared atomic in the published snapshot, then
            read the atomic itself. Mode value changes need no publish —
            the same shared atomics are visible through any snapshot.
    */
    class AutomationModeState
    {
        struct StateSnapshot
        {
            std::unordered_map<ParameterID, std::shared_ptr<std::atomic<std::uint8_t>>> modes;
            std::unordered_map<ParameterID, std::shared_ptr<std::atomic<bool>>>         latched;
        };

    public:
        struct ModeEpoch
        {
            std::uint64_t parameter = 0;
            std::uint64_t globalDefault = 0;

            bool operator!= (const ModeEpoch& rhs) const noexcept
            {
                return parameter != rhs.parameter || globalDefault != rhs.globalDefault;
            }
        };

        AutomationModeState() = default;

        // ----- Global default -------------------------------------------

        void setGlobalDefaultMode (AutomationMode m) noexcept
        {
            const auto next = static_cast<std::uint8_t> (m);
            if (globalDefault.exchange (next, std::memory_order_acq_rel) != next)
                defaultModeEpoch.fetch_add (1, std::memory_order_release);
        }

        AutomationMode getGlobalDefaultMode() const noexcept
        {
            return static_cast<AutomationMode> (
                globalDefault.load (std::memory_order_acquire));
        }

        // ----- Per-parameter override (message thread) ------------------

        void setMode (ParameterID id, AutomationMode m)
        {
            const juce::ScopedLock sl (mapLock);
            auto it = modes.find (id);
            const auto next = static_cast<std::uint8_t> (m);
            if (it == modes.end())
            {
                it = modes.emplace (id, std::make_shared<std::atomic<std::uint8_t>> (next)).first;
                ++modeEpochs[id];  // Adding an override is a state transition.
                publishLocked();
            }
            else if (it->second->load (std::memory_order_acquire) != next)
            {
                it->second->store (next, std::memory_order_release);
                ++modeEpochs[id];
            }
        }

        // Message-thread recorder probe: unlike the current mode enum, this
        // detects Read->Latch->Read (or Latch->Read->Latch) that completes
        // entirely between two 90 Hz recorder polls. The epoch of an
        // overridden parameter excludes global-default changes; unchanged
        // parameters are therefore not quarantined by an unrelated update.
        ModeEpoch getModeEpoch (ParameterID id) const noexcept
        {
            const juce::ScopedLock sl (mapLock);
            ModeEpoch result;
            const auto it = modeEpochs.find (id);
            if (it != modeEpochs.end())
                result.parameter = it->second;
            if (modes.find (id) == modes.end())
                result.globalDefault = defaultModeEpoch.load (std::memory_order_acquire);
            return result;
        }

        AutomationMode getMode (ParameterID id) const noexcept
        {
            {
                const juce::ScopedLock sl (mapLock);
                auto it = modes.find (id);
                if (it != modes.end())
                    return static_cast<AutomationMode> (
                        it->second->load (std::memory_order_acquire));
            }
            return getGlobalDefaultMode();
        }

        // ----- Audio-thread reads (lock-free, allocation-free) ----------

        AutomationMode getModeRT (ParameterID id) const noexcept
        {
            auto snap = getStateSnapshotRT();
            if (snap != nullptr)
            {
                auto it = snap->modes.find (id);
                if (it != snap->modes.end())
                    return static_cast<AutomationMode> (
                        it->second->load (std::memory_order_acquire));
            }
            return getGlobalDefaultMode();
        }

        bool latchHeldRT (ParameterID id) const noexcept
        {
            auto snap = getStateSnapshotRT();
            if (snap != nullptr)
            {
                auto it = snap->latched.find (id);
                if (it != snap->latched.end())
                    return it->second->load (std::memory_order_acquire);
            }
            return false;
        }

        bool isReadingRT (ParameterID id) const noexcept
        {
            const auto m = getModeRT (id);
            return m == AutomationMode::Read
                || m == AutomationMode::Write
                || m == AutomationMode::Touch
                || m == AutomationMode::Latch
                || m == AutomationMode::Trim;
        }

        /** Immutable copy-on-write snapshot of both maps. */
        std::shared_ptr<const StateSnapshot> getStateSnapshotRT() const noexcept
        {
            return std::atomic_load_explicit (&published_, std::memory_order_acquire);
        }

        // ----- Message-thread mutations ---------------------------------

        void clearOverride (ParameterID id)
        {
            const juce::ScopedLock sl (mapLock);
            if (modes.erase (id) != 0)
            {
                ++modeEpochs[id];
                publishLocked();
            }
        }

        void clearAll()
        {
            const juce::ScopedLock sl (mapLock);
            for (const auto& entry : modes)
                ++modeEpochs[entry.first];
            modes.clear();
            latched.clear();
            publishLocked();
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
            auto it = latched.find (id);
            if (it == latched.end())
            {
                it = latched.emplace (id, std::make_shared<std::atomic<bool>> (false)).first;
                publishLocked();
            }
            it->second->store (held, std::memory_order_release);
        }

        bool latchHeld (ParameterID id) const noexcept
        {
            const juce::ScopedLock sl (mapLock);
            auto it = latched.find (id);
            return it != latched.end()
                && it->second->load (std::memory_order_acquire);
        }

        void clearAllLatches()
        {
            // Mutates existing shared atomics only — no structural change,
            // so no publish is required; RT readers see the same atomics.
            const juce::ScopedLock sl (mapLock);
            for (auto& [id, flag] : latched)
                flag->store (false, std::memory_order_release);
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
                entry.setProperty ("mode", (int) modeAtomic->load (std::memory_order_acquire), nullptr);
                state.addChild (entry, -1, nullptr);
            }
            return state;
        }

        void restoreState (const juce::ValueTree& state)
        {
            if (! state.isValid()) return;
            ++publishBatchDepth_;
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
            --publishBatchDepth_;
            const juce::ScopedLock sl (mapLock);
            publishLocked();
        }

        static AutomationModeState& getInstance()
        {
            static AutomationModeState instance;
            return instance;
        }

    private:
        // Copy-on-write publication (message thread only, mapLock held).
        // Copies only shared_ptrs — the underlying atomics are shared, so
        // value changes after publication are visible through any snapshot.
        void publishLocked()
        {
            if (publishBatchDepth_ > 0)
                return;
            auto snap = std::make_shared<StateSnapshot>();
            snap->modes   = modes;
            snap->latched = latched;
            std::shared_ptr<const StateSnapshot> immutableSnap = std::move (snap);
            auto old = std::atomic_exchange_explicit (&published_, std::move (immutableSnap), std::memory_order_acq_rel);
            retiredModeSnapshot_ = std::move (old);   // destroyed at the NEXT publish (message thread)
        }

        std::atomic<std::uint8_t> globalDefault {
            static_cast<std::uint8_t> (AutomationMode::Read) };
        std::atomic<std::uint64_t> defaultModeEpoch { 0 };

        std::unordered_map<ParameterID, std::shared_ptr<std::atomic<std::uint8_t>>> modes;
        std::unordered_map<ParameterID, std::shared_ptr<std::atomic<bool>>>         latched;
        // Message-thread-only transition counters under mapLock. Keep the
        // counter after clearOverride so removing/re-adding the same mode
        // cannot masquerade as a continuous recording session.
        std::unordered_map<ParameterID, std::uint64_t> modeEpochs;
        mutable juce::CriticalSection mapLock;

        std::shared_ptr<const StateSnapshot> published_;
        std::shared_ptr<const StateSnapshot> retiredModeSnapshot_;   // message-thread retire slot
        int publishBatchDepth_ = 0;   // message thread only

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationModeState)
    };
}
