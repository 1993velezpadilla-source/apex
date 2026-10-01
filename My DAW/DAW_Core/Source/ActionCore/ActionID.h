#pragma once

namespace DAW {

/**
 * ActionID — canonical enum of every dispatchable DAW action.
 *
 * All input sources (keyboard, gamepad, bubbles, menus, future AI/voice)
 * must translate their raw input into one of these IDs and call
 * ActionManager::dispatch(). No subsystem should wire behaviour directly
 * to a raw key or button code.
 */
enum class ActionID
{
    // ── Transport ────────────────────────────────────────────────────────────
    TransportPlay,
    TransportStop,
    TransportPlayStop,      // toggle
    TransportRecord,        // toggle arm
    TransportPause,
    TransportToggleLoop,
    TransportJumpStart,
    TransportJumpEnd,

    // ── Navigation / Zoom ────────────────────────────────────────────────────
    ScrollUp,
    ScrollDown,
    ScrollLeft,
    ScrollRight,
    ZoomIn,
    ZoomOut,

    // ── Track control ────────────────────────────────────────────────────────
    TrackNext,
    TrackPrevious,
    TrackMuteSelected,
    TrackSoloSelected,
    TrackArmSelected,
    TrackVolumeUp,          // analog-safe
    TrackVolumeDown,
    TrackPanLeft,
    TrackPanRight,
    TrackAddAudio,
    TrackAddMIDI,
    TrackDuplicate,
    TrackDeleteSelected,
    TrackRenameSelected,

    // ── Clip ─────────────────────────────────────────────────────────────────
    ClipSelect,
    ClipDelete,
    ClipDuplicate,
    ClipSplit,

    // ── Window / View ─────────────────────────────────────────────────────────
    ViewToggleMixer,        // dock↔undock / show-hide
    ViewToggleTrackList,
    ViewToggleSettings,
    ViewToggleMixerBubble,
    ViewToggleFullScreen,   // borderless exclusive full-screen (hides taskbar)
    ViewToggleHistory,      // show/hide undo history panel
    ViewToggleKeyboard,     // show/hide virtual MIDI keyboard
    ViewToggleStepSequencer, // show/hide step sequencer (beat maker)

    // ── Edit ──────────────────────────────────────────────────────────────────
    EditUndo,
    EditRedo,
    EditSelectAll,

    // ── Marker / Timeline ─────────────────────────────────────────────────────
    MarkerAdd,
    MarkerDelete,
    MarkerNext,
    MarkerPrevious,
    MarkerJumpByName,

    // ── Routing (future-ready) ────────────────────────────────────────────────
    RoutingCreateSend,
    RoutingCreateSidechain,
    RoutingRemoveSelected,

    // ── Booth / Self-Recording (future-ready) ─────────────────────────────────
    BoothToggle,
    BoothRetake,
    BoothKeepTake,
    BoothPlayLastTake,
    BoothFarView,

    // ── Bubble (future-ready) ─────────────────────────────────────────────────
    BubbleOpenMasterHub,

    // ── Project ───────────────────────────────────────────────────────────────
    ProjectNew,
    ProjectOpen,
    ProjectSave,
    ProjectSaveAs,

    // ── Import / Export (future-ready) ─────────────────────────────────────────
    ImportFiles,
    RenderBounce,
    RenderExportMix,
    RenderExportStems,

    // ── Custom / Macro ────────────────────────────────────────────────────────
    Custom1,
    Custom2,
    Custom3,
    Custom4,

    None
};

} // namespace DAW
