#pragma once
#include <JuceHeader.h>
#include "../ActionCore/ActionID.h"

namespace DAW {

/**
 * KeyBinding — maps a key press to a canonical ActionID.
 */
struct KeyBinding
{
    ActionID       action  = ActionID::None;
    juce::KeyPress key;
    juce::String   context = "global"; // "global", "arrangement", "mixer", etc.
};

/**
 * KeyBindingProfile — a named set of key bindings inspired by a target DAW.
 *
 * Built-in profiles provide familiar shortcuts for users switching from
 * other DAWs. The user can edit bindings and save custom profiles.
 */
struct KeyBindingProfile
{
    juce::String             name;     // "DAW Core", "Pro Tools", "Logic Pro", etc.
    std::vector<KeyBinding>  bindings;

    /** Create the default DAW Core key binding profile. */
    static KeyBindingProfile createDefault()
    {
        KeyBindingProfile p;
        p.name = "DAW Core";
        p.bindings = {
            { ActionID::TransportPlayStop,     juce::KeyPress(juce::KeyPress::spaceKey) },
            { ActionID::TransportRecord,       juce::KeyPress('R') },
            { ActionID::ViewToggleSettings,    juce::KeyPress('G') },
            { ActionID::ViewToggleMixer,       juce::KeyPress('M') },
            { ActionID::MarkerPrevious,        juce::KeyPress(juce::KeyPress::leftKey) },
            { ActionID::MarkerNext,            juce::KeyPress(juce::KeyPress::rightKey) },
            { ActionID::ViewToggleFullScreen,  juce::KeyPress(juce::KeyPress::F11Key) },
            { ActionID::EditUndo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::EditRedo,              juce::KeyPress('Y', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::EditSelectAll,         juce::KeyPress('A', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::MarkerAdd,             juce::KeyPress('M', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::TransportJumpStart,    juce::KeyPress(juce::KeyPress::homeKey) },
            { ActionID::TransportJumpEnd,      juce::KeyPress(juce::KeyPress::endKey) },
            { ActionID::TransportToggleLoop,   juce::KeyPress('L') },
            { ActionID::ProjectNew,            juce::KeyPress('N', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectOpen,           juce::KeyPress('O', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectSave,           juce::KeyPress('S', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectSaveAs,         juce::KeyPress('S', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::ImportFiles,          juce::KeyPress('I', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::TrackAddAudio,         juce::KeyPress('T', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::TrackDuplicate,        juce::KeyPress('D', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::TrackDeleteSelected,   juce::KeyPress(juce::KeyPress::deleteKey, juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::TrackMuteSelected,     juce::KeyPress('M', juce::ModifierKeys::altModifier, 0) },
            { ActionID::TrackSoloSelected,     juce::KeyPress('S', juce::ModifierKeys::altModifier, 0) },
            { ActionID::TrackArmSelected,      juce::KeyPress('R', juce::ModifierKeys::altModifier, 0) },
            { ActionID::ViewToggleTrackList,   juce::KeyPress('T', juce::ModifierKeys::noModifiers, 0) },
            { ActionID::ViewToggleHistory,     juce::KeyPress('H', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::ClipSplit,             juce::KeyPress('E', juce::ModifierKeys::noModifiers, 0) },
            { ActionID::ClipDuplicate,         juce::KeyPress('D', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ViewToggleKeyboard,     juce::KeyPress('K', juce::ModifierKeys::noModifiers, 0) },
        };
        return p;
    }

    /** Create a Pro Tools-inspired profile. */
    static KeyBindingProfile createProTools()
    {
        KeyBindingProfile p;
        p.name = "Pro Tools";
        p.bindings = {
            { ActionID::TransportPlayStop,     juce::KeyPress(juce::KeyPress::spaceKey) },
            { ActionID::TransportRecord,       juce::KeyPress(juce::KeyPress::F12Key) },
            { ActionID::TransportStop,         juce::KeyPress('.', juce::ModifierKeys::noModifiers, 0) },
            { ActionID::EditUndo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::EditRedo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::ViewToggleMixer,       juce::KeyPress('=', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::MarkerPrevious,        juce::KeyPress(juce::KeyPress::leftKey) },
            { ActionID::MarkerNext,            juce::KeyPress(juce::KeyPress::rightKey) },
            { ActionID::ViewToggleFullScreen,  juce::KeyPress(juce::KeyPress::F11Key) },
            { ActionID::TransportJumpStart,    juce::KeyPress(juce::KeyPress::returnKey) },
            { ActionID::TransportToggleLoop,   juce::KeyPress('L', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::TrackAddAudio,         juce::KeyPress('T', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::TrackDeleteSelected,   juce::KeyPress(juce::KeyPress::deleteKey, juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::TrackMuteSelected,     juce::KeyPress('M', juce::ModifierKeys::altModifier, 0) },
            { ActionID::TrackSoloSelected,     juce::KeyPress('S', juce::ModifierKeys::altModifier, 0) },
            { ActionID::TrackArmSelected,      juce::KeyPress('R', juce::ModifierKeys::altModifier, 0) },
            { ActionID::ViewToggleTrackList,   juce::KeyPress('T', juce::ModifierKeys::noModifiers, 0) },
            { ActionID::ViewToggleKeyboard,     juce::KeyPress('K', juce::ModifierKeys::noModifiers, 0) },
        };
        return p;
    }

    /** Create a Logic Pro-inspired profile. */
    static KeyBindingProfile createLogicPro()
    {
        KeyBindingProfile p;
        p.name = "Logic Pro";
        p.bindings = {
            { ActionID::TransportPlayStop,     juce::KeyPress(juce::KeyPress::spaceKey) },
            { ActionID::TransportRecord,       juce::KeyPress('R') },
            { ActionID::TransportStop,         juce::KeyPress('.', juce::ModifierKeys::noModifiers, 0) },
            { ActionID::EditUndo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::EditRedo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::ViewToggleMixer,       juce::KeyPress('X') },
            { ActionID::MarkerPrevious,        juce::KeyPress(juce::KeyPress::leftKey) },
            { ActionID::MarkerNext,            juce::KeyPress(juce::KeyPress::rightKey) },
            { ActionID::ViewToggleFullScreen,  juce::KeyPress(juce::KeyPress::F11Key) },
            { ActionID::TransportJumpStart,    juce::KeyPress(juce::KeyPress::returnKey) },
            { ActionID::ViewToggleKeyboard,     juce::KeyPress('K', juce::ModifierKeys::noModifiers, 0) },
        };
        return p;
    }

    /** Create an FL Studio-inspired profile. */
    static KeyBindingProfile createFLStudio()
    {
        KeyBindingProfile p;
        p.name = "FL Studio";
        p.bindings = {
            { ActionID::TransportPlayStop,     juce::KeyPress(juce::KeyPress::spaceKey) },
            { ActionID::TransportRecord,       juce::KeyPress('R', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::EditUndo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ViewToggleMixer,       juce::KeyPress(juce::KeyPress::F9Key) },
            { ActionID::MarkerPrevious,        juce::KeyPress(juce::KeyPress::leftKey) },
            { ActionID::MarkerNext,            juce::KeyPress(juce::KeyPress::rightKey) },
            { ActionID::ViewToggleFullScreen,  juce::KeyPress(juce::KeyPress::F11Key) },
            { ActionID::ViewToggleKeyboard,     juce::KeyPress('K', juce::ModifierKeys::noModifiers, 0) },
        };
        return p;
    }

    /** Create an Ableton Live-inspired profile. */
    static KeyBindingProfile createAbletonLive()
    {
        KeyBindingProfile p;
        p.name = "Ableton Live";
        p.bindings = {
            { ActionID::TransportPlayStop,     juce::KeyPress(juce::KeyPress::spaceKey) },
            { ActionID::TransportRecord,       juce::KeyPress(juce::KeyPress::F9Key) },
            { ActionID::TransportStop,         juce::KeyPress(juce::KeyPress::spaceKey) },
            { ActionID::EditUndo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::EditRedo,              juce::KeyPress('Y', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::EditSelectAll,         juce::KeyPress('A', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ViewToggleMixer,       juce::KeyPress(juce::KeyPress::F9Key) },
            { ActionID::TransportJumpStart,    juce::KeyPress(juce::KeyPress::homeKey) },
            { ActionID::TransportToggleLoop,   juce::KeyPress('L', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::MarkerAdd,             juce::KeyPress('A', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::MarkerPrevious,        juce::KeyPress(juce::KeyPress::leftKey) },
            { ActionID::MarkerNext,            juce::KeyPress(juce::KeyPress::rightKey) },
            { ActionID::ViewToggleFullScreen,  juce::KeyPress(juce::KeyPress::F11Key) },
            { ActionID::ProjectNew,            juce::KeyPress('N', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectOpen,           juce::KeyPress('O', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectSave,           juce::KeyPress('S', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectSaveAs,         juce::KeyPress('S', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::ViewToggleKeyboard,     juce::KeyPress('K', juce::ModifierKeys::noModifiers, 0) },
        };
        return p;
    }

    /** Create a Reaper-inspired profile. */
    static KeyBindingProfile createReaper()
    {
        KeyBindingProfile p;
        p.name = "Reaper";
        p.bindings = {
            { ActionID::TransportPlayStop,     juce::KeyPress(juce::KeyPress::spaceKey) },
            { ActionID::TransportRecord,       juce::KeyPress('R', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::TransportStop,         juce::KeyPress(juce::KeyPress::spaceKey) },
            { ActionID::EditUndo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::EditRedo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::EditSelectAll,         juce::KeyPress('A', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ViewToggleMixer,       juce::KeyPress('M', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::TransportJumpStart,    juce::KeyPress(juce::KeyPress::homeKey) },
            { ActionID::TransportJumpEnd,      juce::KeyPress(juce::KeyPress::endKey) },
            { ActionID::TransportToggleLoop,   juce::KeyPress('L', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::MarkerAdd,             juce::KeyPress('M') },
            { ActionID::MarkerPrevious,        juce::KeyPress(juce::KeyPress::leftKey) },
            { ActionID::MarkerNext,            juce::KeyPress(juce::KeyPress::rightKey) },
            { ActionID::ViewToggleFullScreen,  juce::KeyPress(juce::KeyPress::F11Key) },
            { ActionID::ProjectNew,            juce::KeyPress('N', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectOpen,           juce::KeyPress('O', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectSave,           juce::KeyPress('S', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectSaveAs,         juce::KeyPress('S', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::ImportFiles,           juce::KeyPress('I', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ClipSplit,             juce::KeyPress('S') },
            { ActionID::ClipDuplicate,         juce::KeyPress('D', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::TrackAddAudio,         juce::KeyPress('T', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::TrackDuplicate,        juce::KeyPress('D', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::TrackDeleteSelected,   juce::KeyPress(juce::KeyPress::deleteKey, juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::TrackMuteSelected,     juce::KeyPress('M', juce::ModifierKeys::altModifier, 0) },
            { ActionID::TrackSoloSelected,     juce::KeyPress('S', juce::ModifierKeys::altModifier, 0) },
            { ActionID::TrackArmSelected,      juce::KeyPress('R', juce::ModifierKeys::altModifier, 0) },
            { ActionID::ViewToggleTrackList,   juce::KeyPress('T', juce::ModifierKeys::noModifiers, 0) },
            { ActionID::ViewToggleKeyboard,     juce::KeyPress('K', juce::ModifierKeys::noModifiers, 0) },
        };
        return p;
    }

    /** Create a Studio One-inspired profile. */
    static KeyBindingProfile createStudioOne()
    {
        KeyBindingProfile p;
        p.name = "Studio One";
        p.bindings = {
            { ActionID::TransportPlayStop,     juce::KeyPress(juce::KeyPress::spaceKey) },
            { ActionID::TransportRecord,       juce::KeyPress(juce::KeyPress::F10Key) },
            { ActionID::TransportStop,         juce::KeyPress('.', juce::ModifierKeys::noModifiers, 0) },
            { ActionID::EditUndo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::EditRedo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::EditSelectAll,         juce::KeyPress('A', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ViewToggleMixer,       juce::KeyPress('3', juce::ModifierKeys::noModifiers, 0) },
            { ActionID::TransportJumpStart,    juce::KeyPress(juce::KeyPress::homeKey) },
            { ActionID::TransportJumpEnd,      juce::KeyPress(juce::KeyPress::endKey) },
            { ActionID::TransportToggleLoop,   juce::KeyPress('L') },
            { ActionID::MarkerAdd,             juce::KeyPress('M') },
            { ActionID::MarkerPrevious,        juce::KeyPress(juce::KeyPress::leftKey) },
            { ActionID::MarkerNext,            juce::KeyPress(juce::KeyPress::rightKey) },
            { ActionID::ViewToggleFullScreen,  juce::KeyPress(juce::KeyPress::F11Key) },
            { ActionID::ProjectNew,            juce::KeyPress('N', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectOpen,           juce::KeyPress('O', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectSave,           juce::KeyPress('S', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectSaveAs,         juce::KeyPress('S', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::ClipSplit,             juce::KeyPress('Y', juce::ModifierKeys::noModifiers, 0) },
            { ActionID::ClipDuplicate,         juce::KeyPress('D', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ViewToggleKeyboard,     juce::KeyPress('K', juce::ModifierKeys::noModifiers, 0) },
        };
        return p;
    }

    /** Create a Cubase-inspired profile. */
    static KeyBindingProfile createCubase()
    {
        KeyBindingProfile p;
        p.name = "Cubase";
        p.bindings = {
            { ActionID::TransportPlayStop,     juce::KeyPress(juce::KeyPress::spaceKey) },
            { ActionID::TransportRecord,       juce::KeyPress(juce::KeyPress::F12Key) },
            { ActionID::TransportStop,         juce::KeyPress('0', juce::ModifierKeys::noModifiers, 0) },
            { ActionID::EditUndo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::EditRedo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::EditSelectAll,         juce::KeyPress('A', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ViewToggleMixer,       juce::KeyPress(juce::KeyPress::F3Key) },
            { ActionID::TransportJumpStart,    juce::KeyPress(juce::KeyPress::homeKey) },
            { ActionID::TransportJumpEnd,      juce::KeyPress(juce::KeyPress::endKey) },
            { ActionID::TransportToggleLoop,   juce::KeyPress('/', juce::ModifierKeys::noModifiers, 0) },
            { ActionID::MarkerAdd,             juce::KeyPress(juce::KeyPress::insertKey) },
            { ActionID::MarkerPrevious,        juce::KeyPress(juce::KeyPress::leftKey) },
            { ActionID::MarkerNext,            juce::KeyPress(juce::KeyPress::rightKey) },
            { ActionID::ViewToggleFullScreen,  juce::KeyPress(juce::KeyPress::F11Key) },
            { ActionID::ProjectNew,            juce::KeyPress('N', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectOpen,           juce::KeyPress('O', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectSave,           juce::KeyPress('S', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectSaveAs,         juce::KeyPress('S', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::ClipSplit,             juce::KeyPress('X', juce::ModifierKeys::noModifiers, 0) },
            { ActionID::ViewToggleKeyboard,     juce::KeyPress('K', juce::ModifierKeys::noModifiers, 0) },
        };
        return p;
    }

    /** Create a Cakewalk / Sonar-inspired profile. */
    static KeyBindingProfile createCakewalk()
    {
        KeyBindingProfile p;
        p.name = "Cakewalk";
        p.bindings = {
            { ActionID::TransportPlayStop,     juce::KeyPress(juce::KeyPress::spaceKey) },
            { ActionID::TransportRecord,       juce::KeyPress('R') },
            { ActionID::TransportStop,         juce::KeyPress(juce::KeyPress::spaceKey) },
            { ActionID::EditUndo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::EditRedo,              juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::EditSelectAll,         juce::KeyPress('A', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ViewToggleMixer,       juce::KeyPress('2', juce::ModifierKeys::altModifier, 0) },
            { ActionID::ViewToggleTrackList,   juce::KeyPress('1', juce::ModifierKeys::altModifier, 0) },
            { ActionID::TransportJumpStart,    juce::KeyPress(juce::KeyPress::homeKey) },
            { ActionID::TransportJumpEnd,      juce::KeyPress(juce::KeyPress::endKey) },
            { ActionID::TransportToggleLoop,   juce::KeyPress('L', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::MarkerAdd,             juce::KeyPress(juce::KeyPress::F5Key) },
            { ActionID::MarkerPrevious,        juce::KeyPress(juce::KeyPress::leftKey) },
            { ActionID::MarkerNext,            juce::KeyPress(juce::KeyPress::rightKey) },
            { ActionID::ViewToggleFullScreen,  juce::KeyPress(juce::KeyPress::F11Key) },
            { ActionID::ProjectNew,            juce::KeyPress('N', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectOpen,           juce::KeyPress('O', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectSave,           juce::KeyPress('S', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ProjectSaveAs,         juce::KeyPress('S', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) },
            { ActionID::ImportFiles,           juce::KeyPress('I', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ClipSplit,             juce::KeyPress('S', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ClipDuplicate,         juce::KeyPress('D', juce::ModifierKeys::ctrlModifier, 0) },
            { ActionID::ViewToggleKeyboard,     juce::KeyPress('K', juce::ModifierKeys::noModifiers, 0) },
        };
        return p;
    }
};

} // namespace DAW
