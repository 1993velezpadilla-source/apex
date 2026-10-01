#pragma once
#include <JuceHeader.h>
#include "IBubblegumTaskbarHost.h"
#include "BubblegumTaskbarChipModel.h"

namespace DAW {

/**
 * BubblegumTaskbarCore
 *
 * Global singleton that owns the chip list. Panels register themselves on
 * construction, unregister on destruction, and call setHostActive() when
 * they show/hide so the taskbar's active state stays in sync.
 *
 * The Component subscribes via Listener and repaints on chipsChanged().
 *
 * Thread model: UI thread only. All registration, state changes, and reads
 * happen on the message thread.
 *
 * Per-track concern: the taskbar is global by design — one chip per panel,
 * many panels can be alive simultaneously (one Input Trim panel per track,
 * plus EQ, plus comp, etc). Each chip's host points to a distinct panel
 * instance whose internal state is per-track. The taskbar itself stays
 * track-agnostic.
 */
class BubblegumTaskbarCore
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void taskbarChipsChanged() = 0;
    };

    static BubblegumTaskbarCore& getGlobalInstance()
    {
        static BubblegumTaskbarCore instance;
        return instance;
    }

    /** Add a chip for this host. Called by the panel's constructor. */
    void registerHost(IBubblegumTaskbarHost* host)
    {
        if (host == nullptr) return;
        for (const auto& chip : chips_)
            if (chip.host == host) return;

        BubblegumTaskbarChipModel m;
        m.host     = host;
        m.isActive = true;
        chips_.add(m);
        notify();
    }

    /** Remove the chip. MUST be called from the panel's destructor. */
    void unregisterHost(IBubblegumTaskbarHost* host)
    {
        for (int i = 0; i < chips_.size(); ++i)
        {
            if (chips_.getReference(i).host == host)
            {
                chips_.remove(i);
                notify();
                return;
            }
        }
    }

    /** Mark the chip active (panel just shown) or idle (panel just hidden). */
    void setHostActive(IBubblegumTaskbarHost* host, bool active)
    {
        for (auto& chip : chips_)
        {
            if (chip.host == host)
            {
                if (chip.isActive != active)
                {
                    chip.isActive = active;
                    notify();
                }
                return;
            }
        }
    }

    /** User clicked the chip body. Toggles active state via host callbacks. */
    void onChipClicked(IBubblegumTaskbarHost* host)
    {
        if (host == nullptr) return;
        for (auto& chip : chips_)
        {
            if (chip.host == host)
            {
                if (chip.isActive) host->minimizeFromTaskbar();
                else               host->restoreFromTaskbar();
                return;
            }
        }
    }

    /** User clicked the chip's X. Tells host to destroy itself. */
    void onCloseClicked(IBubblegumTaskbarHost* host)
    {
        if (host) host->closeFromTaskbar();
    }

    /** Mutable access — Component reads bounds/hover, Layout writes bounds. */
    juce::Array<BubblegumTaskbarChipModel>&       getChips()       { return chips_; }
    const juce::Array<BubblegumTaskbarChipModel>& getChips() const { return chips_; }

    void addListener(Listener* l)    { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

private:
    BubblegumTaskbarCore() = default;

    void notify()
    {
        listeners_.call([](Listener& l) { l.taskbarChipsChanged(); });
    }

    juce::Array<BubblegumTaskbarChipModel> chips_;
    juce::ListenerList<Listener>           listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumTaskbarCore)
};

} // namespace DAW
