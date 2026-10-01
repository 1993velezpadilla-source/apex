#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW {

// Central observable state for the application
// This is the single source of truth for non-audio state
class ApplicationState : public juce::ValueTree::Listener
{
public:
    ApplicationState();
    ~ApplicationState() override;

    std::function<void(double)> onTempoChanged;
    
    // Transport state
    juce::Value isPlaying;
    juce::Value isRecording;
    juce::Value isPaused;
    juce::Value isLooping;
    juce::Value tempo;
    juce::Value transportPosition; // In samples
    juce::Value barBeatPosition;
    
    // Selection state
    juce::Value selectedTrackID;
    juce::Value selectedClipID;
    juce::Value focusedWindow;
    
    // Edit state
    juce::Value playEditMode; // true = play, false = edit
    juce::Value snapEnabled;
    juce::Value gridDivision;
    
    // Project state
    juce::Value projectDirty;
    juce::Value projectFilePath;
    juce::Value projectName;
    juce::Value projectKey; // Master project key, e.g. "C Maj" / "A Min"
    
    // Timeline state
    juce::Value timelineZoom;
    juce::Value timelineScroll;

    // Bubblegum routing settings
    juce::Value bubblegumOffscreenAutoScroll;
    juce::Value bubblegumOffscreenAutoClose;
    juce::Value bubblegumKeepCablesVisibleWhenClosed;
    juce::Value bubblegumKeepOffscreenVisibleWhenClosed;
    juce::Value bubblegumSelectedTrackPaletteFamily;
    juce::Value bubblegumSelectedTrackPaletteIndex;
    juce::Value bubblegumSelectedTrackColour;
    juce::Value bubblegumCablePaletteFamily;
    juce::Value bubblegumCablePaletteIndex;
    juce::Value bubblegumCableColour;
    juce::Value bubblegumSidechainCableColour;
    juce::Value bubblegumRoutingTargetColour;
    juce::Value bubblegumSelectedSphereColour;
    juce::Value bubblegumSelectedParticleColour;
    juce::Value bubblegumCableBodyTopColour;
    juce::Value bubblegumCableBodyBottomColour;
    juce::Value bubblegumCableCoreTopColour;
    juce::Value bubblegumCableCoreBottomColour;
    juce::Value bubblegumCableMistTopColour;
    juce::Value bubblegumCableMistBottomColour;
    juce::Value bubblegumCableDropletColour;
    juce::Value bubblegumCableShadowColour;
    juce::Value bubblegumCableThickness;
    juce::Value bubblegumSendPillMode;
    juce::Value bubblegumMasterAccentColour;
    juce::Value showSendBadges;
    juce::Value showSidechainBadges;
    juce::Value sidechainCableThickness;
    juce::Value settingsSelectedTab;
    juce::Value autoFadeOnClipSplit;

    // Folder bus drag/drop settings
    juce::Value folderDropMode;
    
    // Serialization
    juce::ValueTree getState() const { return state_; }
    void restoreState(const juce::ValueTree& newState);
    
private:
    juce::ValueTree state_;

    double lastTempo_ = 120.0;
    
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;
};

} // namespace DAW
