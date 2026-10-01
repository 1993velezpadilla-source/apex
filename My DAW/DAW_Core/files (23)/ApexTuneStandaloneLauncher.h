// =============================================================================
//  ApexTuneStandaloneLauncher.h
//  Standalone smoke-test entry point for the VocalTune editor.
//
//  Drop-in: Source/VocalTuneUI/ApexTuneStandaloneLauncher.h
//  Depends on: JUCE.
//  Touches:    nothing else in the APEX codebase.
//
//  Phase 7 will replace this with proper right-click clip menu integration.
//  For now, wire ApexTuneStandaloneLauncher::launch() to any menu item or
//  debug button to smoke-test the editor end-to-end without project glue.
//
//  Flow when launch() is called:
//   1. Async file chooser ("Pick a WAV / AIFF").
//   2. Load + downmix to mono.
//   3. Run YIN analysis + note segmentation + sibilant detection
//      (blocking on the message thread for the smoke test -- a long clip
//       will freeze the UI for a few seconds; Phase 7 will move this to a
//       background worker thread).
//   4. Wrap state + editor in a DialogWindow that owns everything for the
//      lifetime of the window.
//   5. Wire the editor's render-requested callback to ApexTuneRenderCore
//      and dump a brief result via DBG().
// =============================================================================

#pragma once

#include <JuceHeader.h>

namespace apex { namespace vocaltune {

class ApexTuneStandaloneLauncher
{
public:
    // Async. Returns immediately; window opens on user file selection.
    static void launch();
};

}} // namespace apex::vocaltune
