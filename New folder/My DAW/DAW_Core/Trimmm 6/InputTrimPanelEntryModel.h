#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * InputTrimPanelEntryModel
 *
 * Per-track state for the Input Trim floating panel. POD-ish struct stored
 * inside InputTrimPanelStateCore's map and serialized into the session
 * ValueTree. Default values mean "panel was never opened on this track".
 */
struct InputTrimPanelEntryModel
{
    bool open      { false };
    bool minimized { false };
    int  x         { 120 };
    int  y         { 120 };
};

} // namespace DAW
