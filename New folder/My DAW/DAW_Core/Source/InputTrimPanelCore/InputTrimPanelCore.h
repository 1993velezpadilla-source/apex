#pragma once

/**
 * InputTrimPanelCore — master include for the panel lifecycle and persistence
 * family. Three sub-files:
 *
 *   InputTrimPanelEntryModel  - per-track state POD (open, minimized, x, y)
 *   InputTrimPanelStateCore   - singleton + ValueTree serialization
 *   InputTrimPanelManager     - owns live panels, syncs with StateCore,
 *                               listens to TrackManager + ComponentListener
 *
 * --- WIRING IN THE SHELL ---
 *
 *  class DawShell : public juce::Component
 *  {
 *  public:
 *      DawShell(TrackManager& tm)
 *          : trackManager_(tm),
 *            panelManager_(tm, this)   // pass `this` so panels are children of the shell
 *      {
 *          addAndMakeVisible(taskbar_);
 *      }
 *
 *      void resized() override
 *      {
 *          auto b = getLocalBounds();
 *          taskbar_.setBounds(b.removeFromBottom(80));
 *          // ...rest of shell layout...
 *      }
 *
 *      // Called from wherever the user clicks "show input trim" for a track:
 *      void showInputTrimFor(Track& track) { panelManager_.showForTrack(track); }
 *
 *      InputTrimPanelManager& getPanelManager() { return panelManager_; }
 *
 *  private:
 *      TrackManager&             trackManager_;
 *      InputTrimPanelManager     panelManager_;
 *      BubblegumTaskbarComponent taskbar_;
 *  };
 *
 * --- SESSION SAVE / LOAD ---
 *
 *  // On project save (after capturing positions so the saved x/y are fresh):
 *  panelManager_.captureLivePositions();
 *  juce::ValueTree projectRoot = ...;
 *  projectRoot.appendChild(InputTrimPanelStateCore::getGlobalInstance().getState(), nullptr);
 *
 *  // On project load (after restoring tracks):
 *  auto savedPanelStates = projectRoot.getChildWithName("InputTrimPanelStates");
 *  if (savedPanelStates.isValid())
 *  {
 *      InputTrimPanelStateCore::getGlobalInstance().restoreState(savedPanelStates);
 *      panelManager_.restoreFromSession();
 *  }
 *
 * --- LIFECYCLE GUARANTEES ---
 *
 *   - showForTrack on a track that already has a panel restores it instead
 *     of creating a duplicate.
 *   - Closing a panel (via X or via taskbar chip X) deletes the panel
 *     asynchronously and clears the StateCore entry.
 *   - Deleting a track via TrackManager auto-closes its panel and clears
 *     its StateCore entry (no orphan rows on session save).
 *   - Mid-session move/minimize/restore are all reflected in StateCore in
 *     real time, so even an unexpected save (autosave) captures the latest
 *     panel positions.
 *
 * --- PER-TRACK UNIQUENESS ---
 *
 *   The manager keys panels by TrackID. Even if two tracks share a label
 *   they cannot share a panel — track A and track B always get distinct
 *   InputTrimFloatingPanel instances with distinct value bindings closing
 *   over distinct Track references.
 */

#include "InputTrimPanelEntryModel.h"
#include "InputTrimPanelStateCore.h"
#include "InputTrimPanelManager.h"
