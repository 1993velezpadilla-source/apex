#pragma once
#include <JuceHeader.h>
#include <map>
#include "../TrackCore/Track.h"
#include "../UICore/InputTrimFloatingPanel.h"
#include "InputTrimPanelStateCore.h"

namespace DAW {

/**
 * InputTrimPanelManager
 *
 * Owns the live InputTrimFloatingPanel instances and coordinates their
 * lifecycle with InputTrimPanelStateCore. One manager per app, typically
 * instantiated alongside the shell.
 *
 * Responsibilities:
 *   - Deduplicate show requests: showForTrack(track) on a track that already
 *     has a panel restores/focuses it instead of creating a duplicate.
 *   - Track per-panel position changes and write them to StateCore.
 *   - Track per-panel minimize state via componentVisibilityChanged hook.
 *   - Capture initial position from StateCore when reopening a panel.
 *   - Reopen panels from saved session state via restoreFromSession().
 *   - Clean up panels for tracks that get deleted via TrackManager listener.
 *
 * Construction modes:
 *   shellHost == nullptr : panels float as desktop windows. Saved x/y are
 *                          screen coordinates.
 *   shellHost != nullptr : panels are children of shellHost. Saved x/y are
 *                          parent-relative coordinates.
 *
 * Mixing modes across sessions corrupts coordinates. Pick one and stick to it.
 */
class InputTrimPanelManager : public TrackManager::Listener,
                              public juce::ComponentListener,
                              public InputTrimFloatingPanel::ITrimPanelOwner
{
public:
    InputTrimPanelManager(TrackManager& tm,
                          juce::Component* shellHost = nullptr)
        : trackManager_(tm),
          shellHost_(shellHost)
    {
        trackManager_.addListener(this);
        // Single-owner unification: register as the shared trim-panel owner so
        // static InputTrimFloatingPanel::showForTrack() calls from ANY entry
        // point (mixer strip fallback, arranger row fallback, monitor toggle)
        // converge on this manager's per-track dedup map.
        InputTrimFloatingPanel::setSharedTrimPanelOwner(this);
    }

    ~InputTrimPanelManager() override
    {
        trackManager_.removeListener(this);

        if (InputTrimFloatingPanel::getSharedTrimPanelOwner() == this)
            InputTrimFloatingPanel::setSharedTrimPanelOwner(nullptr);

        for (auto& kv : panels_)
        {
            if (kv.second != nullptr)
            {
                kv.second->onCloseRequested = nullptr;
                kv.second->removeComponentListener(this);
                delete kv.second;
            }
        }
        panels_.clear();
    }

    /** Show a panel for the given track. If one already exists, restore and
     *  bring it forward. Returns the live panel pointer (never null on success). */
    InputTrimFloatingPanel* showForTrack(Track& track)
    {
        const auto& id = track.getID();

        auto it = panels_.find(id);
        if (it != panels_.end() && it->second != nullptr)
        {
            it->second->restoreFromTaskbar();
            return it->second;
        }

        auto* panel = new InputTrimFloatingPanel(track);
        panel->onCloseRequested = [this, id](InputTrimFloatingPanel* p) { handleClosed(id, p); };
        panel->addComponentListener(this);

        const auto entry = InputTrimPanelStateCore::getGlobalInstance().getEntry(id);

        if (shellHost_ != nullptr)
        {
            shellHost_->addAndMakeVisible(panel);
            panel->setTopLeftPosition(entry.x, entry.y);
        }
        else
        {
            panel->addToDesktop(juce::ComponentPeer::windowIsTemporary
                              | juce::ComponentPeer::windowHasDropShadow);
            panel->setTopLeftPosition(entry.x, entry.y);
            panel->setVisible(true);
        }

        panels_[id] = panel;

        InputTrimPanelEntryModel m = entry;
        m.open      = true;
        m.minimized = false;
        InputTrimPanelStateCore::getGlobalInstance().setEntry(id, m);

        return panel;
    }

    /** Close panel for a track if one is open. Idempotent — safe to call
     *  on tracks with no panel. */
    void closeForTrack(const TrackID& id)
    {
        auto it = panels_.find(id);
        if (it != panels_.end() && it->second != nullptr)
            handleClosed(id, it->second);
    }

    // --- ITrimPanelOwner (static showForTrack router) ---
    InputTrimFloatingPanel* showTrimPanelForTrack(Track& track) override
    {
        return showForTrack(track);
    }

    /** Reopen panels per the StateCore snapshot. Call after the project has
     *  finished restoring tracks AND after StateCore::restoreState() has
     *  been called with the saved subtree. */
    void restoreFromSession()
    {
        const auto& entries = InputTrimPanelStateCore::getGlobalInstance().getAllEntries();
        for (const auto& kv : entries)
        {
            if (! kv.second.open) continue;

            auto* track = trackManager_.getTrack(kv.first);
            if (track == nullptr) continue;

            auto* panel = showForTrack(*track);
            if (panel != nullptr && kv.second.minimized)
                panel->minimizeFromTaskbar();
        }
    }

    /** Snapshot live panel positions into StateCore. Call right before
     *  serializing the session so the saved x/y reflect the current layout. */
    void captureLivePositions()
    {
        for (auto& kv : panels_)
        {
            if (kv.second == nullptr) continue;

            auto m = InputTrimPanelStateCore::getGlobalInstance().getEntry(kv.first);
            m.x = kv.second->getX();
            m.y = kv.second->getY();
            // open stays true while we have a live panel pointer
            // minimized is kept up to date via componentVisibilityChanged
            InputTrimPanelStateCore::getGlobalInstance().setEntry(kv.first, m);
        }
    }

    InputTrimFloatingPanel* getPanelFor(const TrackID& id) const
    {
        auto it = panels_.find(id);
        return it != panels_.end() ? it->second : nullptr;
    }

    int getNumLivePanels() const noexcept { return static_cast<int>(panels_.size()); }

    // --- TrackManager::Listener ---
    void trackAdded(Track*) override {}

    void trackRemoved(const TrackID& id) override
    {
        auto it = panels_.find(id);
        if (it != panels_.end() && it->second != nullptr)
            handleClosed(id, it->second);

        InputTrimPanelStateCore::getGlobalInstance().clearEntry(id);
    }

    void trackOrderChanged() override {}

    // --- juce::ComponentListener ---
    void componentMovedOrResized(juce::Component& c, bool wasMoved, bool /*wasResized*/) override
    {
        if (! wasMoved) return;

        for (auto& kv : panels_)
        {
            if (kv.second == &c)
            {
                auto m = InputTrimPanelStateCore::getGlobalInstance().getEntry(kv.first);
                m.x = c.getX();
                m.y = c.getY();
                InputTrimPanelStateCore::getGlobalInstance().setEntry(kv.first, m);
                break;
            }
        }
    }

    void componentVisibilityChanged(juce::Component& c) override
    {
        for (auto& kv : panels_)
        {
            if (kv.second == &c)
            {
                auto m = InputTrimPanelStateCore::getGlobalInstance().getEntry(kv.first);
                m.minimized = ! c.isVisible();
                InputTrimPanelStateCore::getGlobalInstance().setEntry(kv.first, m);
                break;
            }
        }
    }

private:
    void handleClosed(const TrackID& id, InputTrimFloatingPanel* panel)
    {
        auto it = panels_.find(id);
        if (it != panels_.end()) panels_.erase(it);

        if (panel != nullptr)
        {
            // Break the panel's Track/InputMeterCore bindings NOW so the 60 Hz
            // VU timer can never touch a Track that TrackManager may destroy
            // synchronously before the deferred delete below runs.
            panel->detachFromTrack();
            panel->onCloseRequested = nullptr;
            panel->removeComponentListener(this);

            // Defer delete so we are safe to call from inside a click handler
            juce::MessageManager::callAsync([panel]() { delete panel; });
        }

        auto m = InputTrimPanelStateCore::getGlobalInstance().getEntry(id);
        m.open      = false;
        m.minimized = false;
        InputTrimPanelStateCore::getGlobalInstance().setEntry(id, m);
    }

    TrackManager&                              trackManager_;
    juce::Component*                           shellHost_ { nullptr };
    std::map<TrackID, InputTrimFloatingPanel*> panels_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InputTrimPanelManager)
};

} // namespace DAW
