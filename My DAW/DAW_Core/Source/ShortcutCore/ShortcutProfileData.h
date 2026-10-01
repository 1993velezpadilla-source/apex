#pragma once
#include "ShortcutModels.h"

namespace DAW {

/**
 * ShortcutProfileData — factory-default shortcut data for all 8 supported DAW profiles.
 *
 * One source of truth: the same data powers both the floating help window display
 * and the ShortcutEngineCore key binding system.
 *
 * Shortcut strings are stored in Windows modifier style ("Ctrl+Z", "Alt+X").
 * PlatformStyleManager / ModifierTranslator converts them to Apple style on display.
 */
namespace ShortcutProfileData {

inline ShortcutProfile createProTools()
{
    ShortcutProfile p;
    p.profileId   = "pro_tools";
    p.displayName = "Pro Tools";
    p.entries = {
        // Transport / Session
        { "Transport / Session", "Play / Stop",                   "Space",            "global" },
        { "Transport / Session", "Pause",                         "Ctrl+Space",       "global" },
        { "Transport / Session", "Record",                        "F12 or NumPad 3",  "global" },
        { "Transport / Session", "Loop Playback",                 "Shift+Space",      "global" },
        { "Transport / Session", "New Session",                   "Ctrl+N",           "global" },
        { "Transport / Session", "Open Session",                  "Ctrl+O",           "global" },
        { "Transport / Session", "Save",                          "Ctrl+S",           "global" },
        { "Transport / Session", "Save As",                       "Ctrl+Shift+S",     "global" },
        { "Transport / Session", "Import Audio",                  "Ctrl+Shift+I",     "global" },
        { "Transport / Session", "Open Keyboard Shortcuts window","Win+Shift+K",      "global" },
        // Editing
        { "Editing", "Cut",              "Ctrl+X",           "global" },
        { "Editing", "Copy",             "Ctrl+C",           "global" },
        { "Editing", "Paste",            "Ctrl+V",           "global" },
        { "Editing", "Undo",             "Ctrl+Z",           "global" },
        { "Editing", "Redo",             "Ctrl+Shift+Z",     "global" },
        { "Editing", "Select All",       "Ctrl+A",           "global" },
        { "Editing", "Separate Clip",    "Ctrl+E",           "global" },
        { "Editing", "Heal Clip",        "Ctrl+H",           "global" },
        { "Editing", "Duplicate",        "Alt+Shift+D",      "global" },
        { "Editing", "Consolidate Clip", "Alt+Shift+3",      "global" },
        // Modes / Tools / Navigation
        { "Modes / Tools / Navigation", "Shuffle Mode",  "F1",             "global" },
        { "Modes / Tools / Navigation", "Slip Mode",     "F2",             "global" },
        { "Modes / Tools / Navigation", "Spot Mode",     "F3",             "global" },
        { "Modes / Tools / Navigation", "Grid Mode",     "F4",             "global" },
        { "Modes / Tools / Navigation", "Trim Tool",     "F6",             "global" },
        { "Modes / Tools / Navigation", "Selector Tool", "F7",             "global" },
        { "Modes / Tools / Navigation", "Grabber Tool",  "F8",             "global" },
        { "Modes / Tools / Navigation", "Pencil Tool",   "F10",            "global" },
        { "Modes / Tools / Navigation", "Smart Tool",    "F6+F7+F8",       "global" },
        { "Modes / Tools / Navigation", "Zoom In",       "Ctrl+]",         "global" },
        { "Modes / Tools / Navigation", "Zoom Out",      "Ctrl+[",         "global" },
        { "Modes / Tools / Navigation", "Cycle Modes",   "`",              "global" },
    };
    return p;
}

inline ShortcutProfile createLogicPro()
{
    ShortcutProfile p;
    p.profileId   = "logic_pro";
    p.displayName = "Logic Pro";
    p.entries = {
        // Transport / Project
        { "Transport / Project", "Play / Stop",  "Space",          "global" },
        { "Transport / Project", "Record",       "R",              "global" },
        { "Transport / Project", "Cycle Mode",   "C",              "global" },
        { "Transport / Project", "Open",         "Cmd+O",         "global" },
        { "Transport / Project", "Save",         "Cmd+S",         "global" },
        { "Transport / Project", "Save As",      "Cmd+Shift+S",   "global" },
        { "Transport / Project", "Undo",         "Cmd+Z",         "global" },
        { "Transport / Project", "Redo",         "Cmd+Shift+Z",   "global" },
        { "Transport / Project", "Open Key Commands", "Option+K", "global" },
        { "Transport / Project", "Open Mixer",   "X",              "global" },
        // Editing / Arrangement
        { "Editing / Arrangement", "Cut",                               "Cmd+X",    "global" },
        { "Editing / Arrangement", "Copy",                              "Cmd+C",    "global" },
        { "Editing / Arrangement", "Paste",                             "Cmd+V",    "global" },
        { "Editing / Arrangement", "Split Region / Events at Playhead", "Cmd+T",    "global" },
        { "Editing / Arrangement", "Delete selected",                   "Delete",   "global" },
        { "Editing / Arrangement", "Mute selected region",              "Ctrl+M",   "global" },
        { "Editing / Arrangement", "Join regions / events",             "J",        "global" },
        { "Editing / Arrangement", "Open Piano Roll",                   "P",        "global" },
        { "Editing / Arrangement", "Open Event List",                   "D",        "global" },
        { "Editing / Arrangement", "Show/Hide Smart Controls",          "B",        "global" },
        // Navigation / Views
        { "Navigation / Views", "Previous marker",                   "Option+Left",       "global" },
        { "Navigation / Views", "Next marker",                       "Option+Right",      "global" },
        { "Navigation / Views", "Go to beginning",                   "Return",            "global" },
        { "Navigation / Views", "Set Locators / enable cycle area",  "U",                 "global" },
        { "Navigation / Views", "Open Library",                      "Y",                 "global" },
        { "Navigation / Views", "Open Editors area",                 "E",                 "global" },
        { "Navigation / Views", "Zoom horizontally",                 "Cmd+Left / Cmd+Right", "global" },
        { "Navigation / Views", "Zoom vertically",                   "Cmd+Up / Cmd+Down",    "global" },
        { "Navigation / Views", "Solo selected track",               "S",                 "global" },
        { "Navigation / Views", "Mute selected track",               "M",                 "global" },
    };
    return p;
}

inline ShortcutProfile createFLStudio()
{
    ShortcutProfile p;
    p.profileId   = "fl_studio";
    p.displayName = "FL Studio";
    p.entries = {
        // Transport / General
        { "Transport / General", "Play / Stop",         "Space",        "global" },
        { "Transport / General", "Record",              "R",            "global" },
        { "Transport / General", "Save",                "Ctrl+S",       "global" },
        { "Transport / General", "Open",                "Ctrl+O",       "global" },
        { "Transport / General", "New",                 "Ctrl+N",       "global" },
        { "Transport / General", "Undo",                "Ctrl+Z",       "global" },
        { "Transport / General", "Redo",                "Ctrl+Alt+Z",   "global" },
        { "Transport / General", "Cut",                 "Ctrl+X",       "global" },
        { "Transport / General", "Copy",                "Ctrl+C",       "global" },
        { "Transport / General", "Paste",               "Ctrl+V",       "global" },
        { "Transport / General", "Duplicate selection",  "Ctrl+B",       "global" },
        { "Transport / General", "Rename selected",     "F2",           "global" },
        // Windows / Navigation
        { "Windows / Navigation", "Playlist",                           "F5",       "global" },
        { "Windows / Navigation", "Piano Roll",                         "F7",       "global" },
        { "Windows / Navigation", "Mixer",                              "F9",       "global" },
        { "Windows / Navigation", "Browser",                            "Alt+F8",   "global" },
        { "Windows / Navigation", "Plugin Picker",                      "F8",       "global" },
        { "Windows / Navigation", "Project Picker",                     "Alt+F8",   "global" },
        { "Windows / Navigation", "Close all windows / switch focus",   "Esc / Tab","global" },
        { "Windows / Navigation", "Context search",                     "Ctrl+F",   "global" },
        // Playlist / Piano Roll tools
        { "Playlist / Piano Roll", "Draw tool",               "P",                      "global" },
        { "Playlist / Piano Roll", "Paint tool",              "B",                      "global" },
        { "Playlist / Piano Roll", "Delete tool",             "D",                      "global" },
        { "Playlist / Piano Roll", "Slice tool",              "C",                      "global" },
        { "Playlist / Piano Roll", "Mute tool",               "T",                      "global" },
        { "Playlist / Piano Roll", "Select all",              "Ctrl+A",                 "global" },
        { "Playlist / Piano Roll", "Deselect all",            "Ctrl+D",                 "global" },
        { "Playlist / Piano Roll", "Quantize",                "Alt+Q",                  "global" },
        { "Playlist / Piano Roll", "Transpose selected up",   "Shift+Up",               "global" },
        { "Playlist / Piano Roll", "Transpose selected down", "Shift+Down",             "global" },
        { "Playlist / Piano Roll", "Zoom",                    "Ctrl+Mouse Wheel",       "global" },
    };
    return p;
}

inline ShortcutProfile createAbletonLive()
{
    ShortcutProfile p;
    p.profileId   = "ableton_live";
    p.displayName = "Ableton Live";
    p.entries = {
        // Transport / Set
        { "Transport / Set", "Play / Stop",                       "Space",          "global" },
        { "Transport / Set", "Record",                            "F9",             "global" },
        { "Transport / Set", "Session / Arrangement view toggle", "Tab",            "global" },
        { "Transport / Set", "Device / Clip view toggle",         "Shift+Tab or F12","global" },
        { "Transport / Set", "New Live Set",                      "Ctrl+N",         "global" },
        { "Transport / Set", "Open",                              "Ctrl+O",         "global" },
        { "Transport / Set", "Save",                              "Ctrl+S",         "global" },
        { "Transport / Set", "Undo",                              "Ctrl+Z",         "global" },
        { "Transport / Set", "Redo",                              "Ctrl+Shift+Z",   "global" },
        { "Transport / Set", "Capture MIDI",                      "Shift+Ctrl+C",   "global" },
        // Editing
        { "Editing", "Cut",              "Ctrl+X",         "global" },
        { "Editing", "Copy",             "Ctrl+C",         "global" },
        { "Editing", "Paste",            "Ctrl+V",         "global" },
        { "Editing", "Duplicate",        "Ctrl+D",         "global" },
        { "Editing", "Delete",           "Delete",         "global" },
        { "Editing", "Consolidate",      "Ctrl+J",         "global" },
        { "Editing", "Loop selection",   "Ctrl+L",         "global" },
        { "Editing", "Crop Clip",        "Ctrl+Shift+X",   "global" },
        { "Editing", "Quantize",         "Ctrl+U",         "global" },
        { "Editing", "Quantize Settings","Ctrl+Shift+U",   "global" },
        // Views / Devices / Navigation
        { "Views / Devices / Navigation", "Show/Hide Browser",        "Ctrl+Alt+B",     "global" },
        { "Views / Devices / Navigation", "Show/Hide Detail View",    "Ctrl+Alt+L",     "global" },
        { "Views / Devices / Navigation", "Toggle Full Screen",       "F11",            "global" },
        { "Views / Devices / Navigation", "Toggle second window",     "Ctrl+Shift+W",   "global" },
        { "Views / Devices / Navigation", "Hot-Swap Mode",            "Q",              "global" },
        { "Views / Devices / Navigation", "Draw mode",                "B",              "global" },
        { "Views / Devices / Navigation", "Computer MIDI Keyboard",   "M",              "global" },
        { "Views / Devices / Navigation", "Zoom / adjust timeline",   "Ctrl+Mouse Wheel","global" },
        { "Views / Devices / Navigation", "Insert Audio Track",       "Ctrl+T",         "global" },
        { "Views / Devices / Navigation", "Insert MIDI Track",        "Ctrl+Shift+T",   "global" },
    };
    return p;
}

inline ShortcutProfile createReaper()
{
    ShortcutProfile p;
    p.profileId   = "reaper";
    p.displayName = "REAPER";
    p.entries = {
        // Transport / Project
        { "Transport / Project", "Play / Stop",      "Space",          "global" },
        { "Transport / Project", "Record",           "Ctrl+R",         "global" },
        { "Transport / Project", "Pause",            "Ctrl+Space",     "global" },
        { "Transport / Project", "Repeat",           "R",              "global" },
        { "Transport / Project", "Save project",     "Ctrl+S",         "global" },
        { "Transport / Project", "Open project",     "Ctrl+O",         "global" },
        { "Transport / Project", "New project tab",  "Ctrl+Alt+N",     "global" },
        { "Transport / Project", "Undo",             "Ctrl+Z",         "global" },
        { "Transport / Project", "Redo",             "Ctrl+Shift+Z",   "global" },
        { "Transport / Project", "Action List",      "?",              "global" },
        // Editing
        { "Editing", "Cut",                           "Ctrl+X",     "global" },
        { "Editing", "Copy",                          "Ctrl+C",     "global" },
        { "Editing", "Paste",                         "Ctrl+V",     "global" },
        { "Editing", "Split item at edit cursor",     "S",          "global" },
        { "Editing", "Duplicate",                     "Ctrl+D",     "global" },
        { "Editing", "Glue items",                    "G",          "global" },
        { "Editing", "Trim content behind media items","Alt+P",     "global" },
        { "Editing", "Toggle auto crossfade",         "Alt+X",      "global" },
        { "Editing", "Toggle snapping",               "Alt+S",      "global" },
        // Views / Navigation
        { "Views / Navigation", "Toggle mixer",           "Ctrl+M",            "global" },
        { "Views / Navigation", "Render project",         "Ctrl+Alt+R",        "global" },
        { "Views / Navigation", "Show routing matrix",    "Alt+R",             "global" },
        { "Views / Navigation", "Insert track",           "Ctrl+T",            "global" },
        { "Views / Navigation", "Track manager",          "Ctrl+Shift+M",      "global" },
        { "Views / Navigation", "Zoom horizontally",      "Mouse Wheel",       "global" },
        { "Views / Navigation", "Zoom vertically",        "Ctrl+Mouse Wheel",  "global" },
        { "Views / Navigation", "Scroll horizontally",    "Alt+Mouse Wheel",   "global" },
        { "Views / Navigation", "Toggle grid lines",      "Alt+G",             "global" },
        { "Views / Navigation", "Track height max/min",   "Shift+/",           "global" },
    };
    return p;
}

inline ShortcutProfile createStudioOne()
{
    ShortcutProfile p;
    p.profileId   = "studio_one";
    p.displayName = "Studio One";
    p.entries = {
        // Transport / Session
        { "Transport / Session", "Play / Stop",       "Space",           "global" },
        { "Transport / Session", "Record",            "NumPad *",        "global" },
        { "Transport / Session", "Loop on/off",       "P",               "global" },
        { "Transport / Session", "Undo",              "Ctrl+Z",          "global" },
        { "Transport / Session", "Redo",              "Ctrl+Shift+Z",    "global" },
        { "Transport / Session", "Save",              "Ctrl+S",          "global" },
        { "Transport / Session", "Open",              "Ctrl+O",          "global" },
        { "Transport / Session", "New Song / Project","Ctrl+N",          "global" },
        { "Transport / Session", "Add Audio Track",   "T",               "global" },
        { "Transport / Session", "Console",           "F3",              "global" },
        // Editing
        { "Editing", "Cut",              "Ctrl+X",        "global" },
        { "Editing", "Copy",             "Ctrl+C",        "global" },
        { "Editing", "Paste",            "Ctrl+V",        "global" },
        { "Editing", "Duplicate",        "D",             "global" },
        { "Editing", "Split at cursor",  "Alt+X",         "global" },
        { "Editing", "Merge Events",     "G",             "global" },
        { "Editing", "Select all",       "Ctrl+A",        "global" },
        { "Editing", "Deselect all",     "Ctrl+D",        "global" },
        { "Editing", "Rename selected",  "F2",            "global" },
        { "Editing", "Quantize",         "Q",             "global" },
        // Tools / Navigation
        { "Tools / Navigation", "Arrow tool",       "1",              "global" },
        { "Tools / Navigation", "Range tool",       "2",              "global" },
        { "Tools / Navigation", "Split tool",       "3",              "global" },
        { "Tools / Navigation", "Paint tool",       "4",              "global" },
        { "Tools / Navigation", "Mute tool",        "5",              "global" },
        { "Tools / Navigation", "Bend tool",        "6",              "global" },
        { "Tools / Navigation", "Audition tool",    "7",              "global" },
        { "Tools / Navigation", "Toggle Browser",   "F5",             "global" },
        { "Tools / Navigation", "Toggle Editor",    "F2",             "global" },
        { "Tools / Navigation", "Zoom",             "Mouse Wheel",    "global" },
    };
    return p;
}

inline ShortcutProfile createCubase()
{
    ShortcutProfile p;
    p.profileId   = "cubase";
    p.displayName = "Cubase";
    p.entries = {
        // Transport / Windows
        { "Transport / Windows", "Play / Stop",        "Space",          "global" },
        { "Transport / Windows", "Record",             "NumPad *",       "global" },
        { "Transport / Windows", "Cycle on/off",       "/",              "global" },
        { "Transport / Windows", "Metronome on/off",   "C",              "global" },
        { "Transport / Windows", "Undo",               "Ctrl+Z",         "global" },
        { "Transport / Windows", "Redo",               "Ctrl+Shift+Z",   "global" },
        { "Transport / Windows", "Save",               "Ctrl+S",         "global" },
        { "Transport / Windows", "Open",               "Ctrl+O",         "global" },
        { "Transport / Windows", "New Project",        "Ctrl+N",         "global" },
        { "Transport / Windows", "MixConsole",         "F3",             "global" },
        // Editing
        { "Editing", "Cut",                 "Ctrl+X",        "global" },
        { "Editing", "Copy",                "Ctrl+C",        "global" },
        { "Editing", "Paste",               "Ctrl+V",        "global" },
        { "Editing", "Duplicate",           "Ctrl+D",        "global" },
        { "Editing", "Split",               "Alt+X",         "global" },
        { "Editing", "Glue",                "Alt+G",         "global" },
        { "Editing", "Mute selected event", "Shift+M",       "global" },
        { "Editing", "Delete selected",     "Delete",        "global" },
        { "Editing", "Select all",          "Ctrl+A",        "global" },
        { "Editing", "Snap on/off",         "J",             "global" },
        // Tools / Navigation
        { "Tools / Navigation", "Object Selection",     "1",                      "global" },
        { "Tools / Navigation", "Range Selection",      "2",                      "global" },
        { "Tools / Navigation", "Draw",                 "3",                      "global" },
        { "Tools / Navigation", "Erase",                "4",                      "global" },
        { "Tools / Navigation", "Split",                "5",                      "global" },
        { "Tools / Navigation", "Glue",                 "6",                      "global" },
        { "Tools / Navigation", "Mute",                 "7",                      "global" },
        { "Tools / Navigation", "Zoom",                 "H / G",                  "global" },
        { "Tools / Navigation", "Locators to selection", "P",                     "global" },
        { "Tools / Navigation", "Open Key Commands",    "Edit > Key Commands",    "global" },
    };
    return p;
}

inline ShortcutProfile createCakewalk()
{
    ShortcutProfile p;
    p.profileId   = "cakewalk";
    p.displayName = "Cakewalk";
    p.entries = {
        // Transport / Project
        { "Transport / Project", "Play / Stop",    "Space",        "global" },
        { "Transport / Project", "Record",         "R",            "global" },
        { "Transport / Project", "Rewind",         "W",            "global" },
        { "Transport / Project", "Fast Forward",   "Shift+W",      "global" },
        { "Transport / Project", "Go to Now Time", "G",            "global" },
        { "Transport / Project", "Save",           "Ctrl+S",       "global" },
        { "Transport / Project", "Open",           "Ctrl+O",       "global" },
        { "Transport / Project", "New",            "Ctrl+N",       "global" },
        { "Transport / Project", "Undo",           "Ctrl+Z",       "global" },
        { "Transport / Project", "Redo",           "Ctrl+Y",       "global" },
        // Editing
        { "Editing", "Cut",             "Ctrl+X",    "global" },
        { "Editing", "Copy",            "Ctrl+C",    "global" },
        { "Editing", "Paste",           "Ctrl+V",    "global" },
        { "Editing", "Duplicate",       "Ctrl+D",    "global" },
        { "Editing", "Split",           "S",         "global" },
        { "Editing", "Delete",          "Delete",    "global" },
        { "Editing", "Select all",      "Ctrl+A",    "global" },
        { "Editing", "Bounce to clips", "Ctrl+B",    "global" },
        { "Editing", "Loop on/off",     "Ctrl+L",    "global" },
        { "Editing", "Snap on/off",     "N",         "global" },
        // Views / Tools
        { "Views / Tools", "Track view",              "Alt+1",        "global" },
        { "Views / Tools", "Console view",            "Alt+2",        "global" },
        { "Views / Tools", "Piano Roll view",         "Alt+3",        "global" },
        { "Views / Tools", "Staff view",              "Alt+9",        "global" },
        { "Views / Tools", "Show/Hide Control Bar",   "C",            "global" },
        { "Views / Tools", "Insert Audio Track",      "Ctrl+T",       "global" },
        { "Views / Tools", "Insert MIDI Track",       "Ctrl+Shift+T", "global" },
        { "Views / Tools", "Fit project in window",   "Shift+F",      "global" },
        { "Views / Tools", "Fit selection",           "Ctrl+Alt+H",   "global" },
        { "Views / Tools", "Show/hide Bus pane",      "Shift+B",      "global" },
    };
    return p;
}

/** Returns all 8 factory profiles. */
inline std::vector<ShortcutProfile> createAllProfiles()
{
    return {
        createProTools(),
        createLogicPro(),
        createFLStudio(),
        createAbletonLive(),
        createReaper(),
        createStudioOne(),
        createCubase(),
        createCakewalk()
    };
}

} // namespace ShortcutProfileData
} // namespace DAW
