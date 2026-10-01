#include "ApplicationState.h"

namespace DAW {

namespace IDs
{
    static const juce::Identifier STATE("State");
    static const juce::Identifier IS_PLAYING("isPlaying");
    static const juce::Identifier IS_RECORDING("isRecording");
    static const juce::Identifier IS_PAUSED("isPaused");
    static const juce::Identifier IS_LOOPING("isLooping");
    static const juce::Identifier TEMPO("tempo");
    static const juce::Identifier TRANSPORT_POSITION("transportPosition");
    static const juce::Identifier BAR_BEAT_POSITION("barBeatPosition");
    static const juce::Identifier SELECTED_TRACK_ID("selectedTrackID");
    static const juce::Identifier SELECTED_CLIP_ID("selectedClipID");
    static const juce::Identifier FOCUSED_WINDOW("focusedWindow");
    static const juce::Identifier PLAY_EDIT_MODE("playEditMode");
    static const juce::Identifier SNAP_ENABLED("snapEnabled");
    static const juce::Identifier GRID_DIVISION("gridDivision");
    static const juce::Identifier PROJECT_DIRTY("projectDirty");
    static const juce::Identifier PROJECT_FILE_PATH("projectFilePath");
    static const juce::Identifier PROJECT_NAME("projectName");
    static const juce::Identifier PROJECT_KEY("projectKey");
    static const juce::Identifier TIMELINE_ZOOM("timelineZoom");
    static const juce::Identifier TIMELINE_SCROLL("timelineScroll");
    static const juce::Identifier BUBBLEGUM_OFFSCREEN_AUTO_SCROLL("bubblegumOffscreenAutoScroll");
    static const juce::Identifier BUBBLEGUM_OFFSCREEN_AUTO_CLOSE("bubblegumOffscreenAutoClose");
    static const juce::Identifier BUBBLEGUM_KEEP_CABLES_VISIBLE_WHEN_CLOSED("bubblegumKeepCablesVisibleWhenClosed");
    static const juce::Identifier BUBBLEGUM_KEEP_OFFSCREEN_VISIBLE_WHEN_CLOSED("bubblegumKeepOffscreenVisibleWhenClosed");
    static const juce::Identifier BUBBLEGUM_SELECTED_TRACK_PALETTE_FAMILY("bubblegumSelectedTrackPaletteFamily");
    static const juce::Identifier BUBBLEGUM_SELECTED_TRACK_PALETTE_INDEX("bubblegumSelectedTrackPaletteIndex");
    static const juce::Identifier BUBBLEGUM_SELECTED_TRACK_COLOUR("bubblegumSelectedTrackColour");
    static const juce::Identifier BUBBLEGUM_CABLE_PALETTE_FAMILY("bubblegumCablePaletteFamily");
    static const juce::Identifier BUBBLEGUM_CABLE_PALETTE_INDEX("bubblegumCablePaletteIndex");
    static const juce::Identifier BUBBLEGUM_CABLE_COLOUR("bubblegumCableColour");
    static const juce::Identifier BUBBLEGUM_SIDECHAIN_CABLE_COLOUR("bubblegumSidechainCableColour");
    static const juce::Identifier BUBBLEGUM_ROUTING_TARGET_COLOUR("bubblegumRoutingTargetColour");
    static const juce::Identifier BUBBLEGUM_SELECTED_SPHERE_COLOUR("bubblegumSelectedSphereColour");
    static const juce::Identifier BUBBLEGUM_SELECTED_PARTICLE_COLOUR("bubblegumSelectedParticleColour");
    static const juce::Identifier BUBBLEGUM_CABLE_BODY_TOP_COLOUR("bubblegumCableBodyTopColour");
    static const juce::Identifier BUBBLEGUM_CABLE_BODY_BOTTOM_COLOUR("bubblegumCableBodyBottomColour");
    static const juce::Identifier BUBBLEGUM_CABLE_CORE_TOP_COLOUR("bubblegumCableCoreTopColour");
    static const juce::Identifier BUBBLEGUM_CABLE_CORE_BOTTOM_COLOUR("bubblegumCableCoreBottomColour");
    static const juce::Identifier BUBBLEGUM_CABLE_MIST_TOP_COLOUR("bubblegumCableMistTopColour");
    static const juce::Identifier BUBBLEGUM_CABLE_MIST_BOTTOM_COLOUR("bubblegumCableMistBottomColour");
    static const juce::Identifier BUBBLEGUM_CABLE_DROPLET_COLOUR("bubblegumCableDropletColour");
    static const juce::Identifier BUBBLEGUM_CABLE_SHADOW_COLOUR("bubblegumCableShadowColour");
    static const juce::Identifier BUBBLEGUM_CABLE_THICKNESS("bubblegumCableThickness");
    static const juce::Identifier BUBBLEGUM_SEND_PILL_MODE("bubblegumSendPillMode");
    static const juce::Identifier BUBBLEGUM_MASTER_ACCENT_COLOUR("bubblegumMasterAccentColour");
    static const juce::Identifier SHOW_SEND_BADGES("showSendBadges");
    static const juce::Identifier SHOW_SIDECHAIN_BADGES("showSidechainBadges");
    static const juce::Identifier SIDECHAIN_CABLE_THICKNESS("sidechainCableThickness");
    static const juce::Identifier SETTINGS_SELECTED_TAB("settingsSelectedTab");
    static const juce::Identifier AUTO_FADE_ON_CLIP_SPLIT("autoFadeOnClipSplit");
    static const juce::Identifier FOLDER_DROP_MODE("folderDropMode");
}

ApplicationState::ApplicationState()
    : state_(IDs::STATE)
{
    // Initialize default values
    state_.setProperty(IDs::IS_PLAYING, false, nullptr);
    state_.setProperty(IDs::IS_RECORDING, false, nullptr);
    state_.setProperty(IDs::IS_PAUSED, false, nullptr);
    state_.setProperty(IDs::IS_LOOPING, false, nullptr);
    state_.setProperty(IDs::TEMPO, 120.0, nullptr);
    state_.setProperty(IDs::TRANSPORT_POSITION, (int64_t)0, nullptr);
    state_.setProperty(IDs::BAR_BEAT_POSITION, 0.0, nullptr);
    state_.setProperty(IDs::SELECTED_TRACK_ID, "", nullptr);
    state_.setProperty(IDs::SELECTED_CLIP_ID, "", nullptr);
    state_.setProperty(IDs::FOCUSED_WINDOW, "", nullptr);
    state_.setProperty(IDs::PLAY_EDIT_MODE, true, nullptr);
    state_.setProperty(IDs::SNAP_ENABLED, true, nullptr);
    state_.setProperty(IDs::GRID_DIVISION, 4, nullptr);
    state_.setProperty(IDs::PROJECT_DIRTY, false, nullptr);
    state_.setProperty(IDs::PROJECT_FILE_PATH, "", nullptr);
    state_.setProperty(IDs::PROJECT_NAME, "Untitled", nullptr);
    state_.setProperty(IDs::PROJECT_KEY, "", nullptr);
    state_.setProperty(IDs::TIMELINE_ZOOM, 1.0, nullptr);
    state_.setProperty(IDs::TIMELINE_SCROLL, 0.0, nullptr);
    state_.setProperty(IDs::BUBBLEGUM_OFFSCREEN_AUTO_SCROLL, true, nullptr);
    state_.setProperty(IDs::BUBBLEGUM_OFFSCREEN_AUTO_CLOSE, false, nullptr);
    state_.setProperty(IDs::BUBBLEGUM_KEEP_CABLES_VISIBLE_WHEN_CLOSED, true, nullptr);
    state_.setProperty(IDs::BUBBLEGUM_KEEP_OFFSCREEN_VISIBLE_WHEN_CLOSED, true, nullptr);
    state_.setProperty(IDs::BUBBLEGUM_SELECTED_TRACK_PALETTE_FAMILY, 0, nullptr);
    state_.setProperty(IDs::BUBBLEGUM_SELECTED_TRACK_PALETTE_INDEX, 0, nullptr);
    state_.setProperty(IDs::BUBBLEGUM_SELECTED_TRACK_COLOUR, juce::String("ffff2d78"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_CABLE_PALETTE_FAMILY, 0, nullptr);
    state_.setProperty(IDs::BUBBLEGUM_CABLE_PALETTE_INDEX, 0, nullptr);
    state_.setProperty(IDs::BUBBLEGUM_CABLE_COLOUR, juce::String("ffff2d78"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_SIDECHAIN_CABLE_COLOUR, juce::String("ff3a7bd5"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_ROUTING_TARGET_COLOUR, juce::String("ffff2d78"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_SELECTED_SPHERE_COLOUR, juce::String("ff1a1a1a"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_SELECTED_PARTICLE_COLOUR, juce::String("ffff69b4"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_CABLE_BODY_TOP_COLOUR, juce::String("e0ffdbed"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_CABLE_BODY_BOTTOM_COLOUR, juce::String("f0f270a8"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_CABLE_CORE_TOP_COLOUR, juce::String("a8fff5fa"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_CABLE_CORE_BOTTOM_COLOUR, juce::String("47ffccd9"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_CABLE_MIST_TOP_COLOUR, juce::String("08ffebf7"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_CABLE_MIST_BOTTOM_COLOUR, juce::String("1affb8db"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_CABLE_DROPLET_COLOUR, juce::String("6bffedf7"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_CABLE_SHADOW_COLOUR, juce::String("2931082e"), nullptr);
    state_.setProperty(IDs::BUBBLEGUM_CABLE_THICKNESS, 5.0, nullptr);
    state_.setProperty(IDs::BUBBLEGUM_SEND_PILL_MODE, 0, nullptr);
    state_.setProperty(IDs::BUBBLEGUM_MASTER_ACCENT_COLOUR, juce::String("ffcc9900"), nullptr);
    state_.setProperty(IDs::SHOW_SEND_BADGES, true, nullptr);
    state_.setProperty(IDs::SHOW_SIDECHAIN_BADGES, true, nullptr);
    state_.setProperty(IDs::SIDECHAIN_CABLE_THICKNESS, 1.5, nullptr);
    state_.setProperty(IDs::SETTINGS_SELECTED_TAB, 0, nullptr);
    state_.setProperty(IDs::AUTO_FADE_ON_CLIP_SPLIT, true, nullptr);
    state_.setProperty(IDs::FOLDER_DROP_MODE, 0, nullptr);
    
    // Connect Value objects to ValueTree properties
    isPlaying.referTo(state_.getPropertyAsValue(IDs::IS_PLAYING, nullptr));
    isRecording.referTo(state_.getPropertyAsValue(IDs::IS_RECORDING, nullptr));
    isPaused.referTo(state_.getPropertyAsValue(IDs::IS_PAUSED, nullptr));
    isLooping.referTo(state_.getPropertyAsValue(IDs::IS_LOOPING, nullptr));
    tempo.referTo(state_.getPropertyAsValue(IDs::TEMPO, nullptr));
    lastTempo_ = (double) state_.getProperty(IDs::TEMPO, 120.0);
    transportPosition.referTo(state_.getPropertyAsValue(IDs::TRANSPORT_POSITION, nullptr));
    barBeatPosition.referTo(state_.getPropertyAsValue(IDs::BAR_BEAT_POSITION, nullptr));
    selectedTrackID.referTo(state_.getPropertyAsValue(IDs::SELECTED_TRACK_ID, nullptr));
    selectedClipID.referTo(state_.getPropertyAsValue(IDs::SELECTED_CLIP_ID, nullptr));
    focusedWindow.referTo(state_.getPropertyAsValue(IDs::FOCUSED_WINDOW, nullptr));
    playEditMode.referTo(state_.getPropertyAsValue(IDs::PLAY_EDIT_MODE, nullptr));
    snapEnabled.referTo(state_.getPropertyAsValue(IDs::SNAP_ENABLED, nullptr));
    gridDivision.referTo(state_.getPropertyAsValue(IDs::GRID_DIVISION, nullptr));
    projectDirty.referTo(state_.getPropertyAsValue(IDs::PROJECT_DIRTY, nullptr));
    projectFilePath.referTo(state_.getPropertyAsValue(IDs::PROJECT_FILE_PATH, nullptr));
    projectName.referTo(state_.getPropertyAsValue(IDs::PROJECT_NAME, nullptr));
    projectKey.referTo(state_.getPropertyAsValue(IDs::PROJECT_KEY, nullptr));
    timelineZoom.referTo(state_.getPropertyAsValue(IDs::TIMELINE_ZOOM, nullptr));
    timelineScroll.referTo(state_.getPropertyAsValue(IDs::TIMELINE_SCROLL, nullptr));
    bubblegumOffscreenAutoScroll.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_OFFSCREEN_AUTO_SCROLL, nullptr));
    bubblegumOffscreenAutoClose.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_OFFSCREEN_AUTO_CLOSE, nullptr));
    bubblegumKeepCablesVisibleWhenClosed.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_KEEP_CABLES_VISIBLE_WHEN_CLOSED, nullptr));
    bubblegumKeepOffscreenVisibleWhenClosed.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_KEEP_OFFSCREEN_VISIBLE_WHEN_CLOSED, nullptr));
    bubblegumSelectedTrackPaletteFamily.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_SELECTED_TRACK_PALETTE_FAMILY, nullptr));
    bubblegumSelectedTrackPaletteIndex.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_SELECTED_TRACK_PALETTE_INDEX, nullptr));
    bubblegumSelectedTrackColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_SELECTED_TRACK_COLOUR, nullptr));
    bubblegumCablePaletteFamily.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_CABLE_PALETTE_FAMILY, nullptr));
    bubblegumCablePaletteIndex.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_CABLE_PALETTE_INDEX, nullptr));
    bubblegumCableColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_CABLE_COLOUR, nullptr));
    bubblegumSidechainCableColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_SIDECHAIN_CABLE_COLOUR, nullptr));
    bubblegumRoutingTargetColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_ROUTING_TARGET_COLOUR, nullptr));
    bubblegumSelectedSphereColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_SELECTED_SPHERE_COLOUR, nullptr));
    bubblegumSelectedParticleColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_SELECTED_PARTICLE_COLOUR, nullptr));
    bubblegumCableBodyTopColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_CABLE_BODY_TOP_COLOUR, nullptr));
    bubblegumCableBodyBottomColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_CABLE_BODY_BOTTOM_COLOUR, nullptr));
    bubblegumCableCoreTopColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_CABLE_CORE_TOP_COLOUR, nullptr));
    bubblegumCableCoreBottomColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_CABLE_CORE_BOTTOM_COLOUR, nullptr));
    bubblegumCableMistTopColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_CABLE_MIST_TOP_COLOUR, nullptr));
    bubblegumCableMistBottomColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_CABLE_MIST_BOTTOM_COLOUR, nullptr));
    bubblegumCableDropletColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_CABLE_DROPLET_COLOUR, nullptr));
    bubblegumCableShadowColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_CABLE_SHADOW_COLOUR, nullptr));
    bubblegumCableThickness.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_CABLE_THICKNESS, nullptr));
    bubblegumSendPillMode.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_SEND_PILL_MODE, nullptr));
    bubblegumMasterAccentColour.referTo(state_.getPropertyAsValue(IDs::BUBBLEGUM_MASTER_ACCENT_COLOUR, nullptr));
    showSendBadges.referTo(state_.getPropertyAsValue(IDs::SHOW_SEND_BADGES, nullptr));
    showSidechainBadges.referTo(state_.getPropertyAsValue(IDs::SHOW_SIDECHAIN_BADGES, nullptr));
    sidechainCableThickness.referTo(state_.getPropertyAsValue(IDs::SIDECHAIN_CABLE_THICKNESS, nullptr));
    settingsSelectedTab.referTo(state_.getPropertyAsValue(IDs::SETTINGS_SELECTED_TAB, nullptr));
    autoFadeOnClipSplit.referTo(state_.getPropertyAsValue(IDs::AUTO_FADE_ON_CLIP_SPLIT, nullptr));
    folderDropMode.referTo(state_.getPropertyAsValue(IDs::FOLDER_DROP_MODE, nullptr));
    
    state_.addListener(this);
}

ApplicationState::~ApplicationState()
{
    state_.removeListener(this);
}

void ApplicationState::restoreState(const juce::ValueTree& newState)
{
    state_.copyPropertiesAndChildrenFrom(newState, nullptr);
}

void ApplicationState::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property)
{
    // This is where you can add logging or broadcast state changes
    DBG("State changed: " + property.toString() + " = " + tree[property].toString());

    if (property == IDs::TEMPO)
    {
        const double newTempo = (double) tree.getProperty(IDs::TEMPO, 120.0);
        if (!juce::approximatelyEqual(newTempo, lastTempo_))
        {
            lastTempo_ = newTempo;
            if (onTempoChanged)
                onTempoChanged(newTempo);
        }
    }
}

} // namespace DAW
