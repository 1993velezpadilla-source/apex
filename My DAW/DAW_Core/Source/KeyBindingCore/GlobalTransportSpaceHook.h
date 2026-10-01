#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * GlobalTransportSpaceHook — guarantees the space bar toggles transport
 * (ActionID::TransportPlayStop) no matter which window has keyboard focus,
 * including native plugin editor HWNDs (VST2/VST3 GUIs that create their own
 * child window). Keys targeted at those native HWNDs never reach JUCE's
 * component dispatch, so the timeline-style keyPressed handlers cannot fire.
 *
 * Mechanism: a thread-scoped WH_GETMESSAGE hook — the same technique JUCE
 * itself uses inside its plugin-client builds — installed on the JUCE message
 * thread. Every key message for every window owned by this thread passes
 * through the hook before dispatch, so we see space presses that JUCE would
 * otherwise never observe.
 *
 * The hook routes through KeyBindingManager::handleKeyPress — the same
 * canonical path the timeline spacebar uses (profile lookup -> ActionManager
 * -> TransportController). It never dispatches behaviour directly.
 *
 * Safety rules (each verified against JUCE 8.0.12 dispatch semantics):
 *  - Auto-repeat (held key, lParam bit 30) never re-triggers the toggle.
 *  - Ctrl/Alt/Shift+Space passes through untouched (reserved for other uses).
 *  - If the focused JUCE component is a text input target (TextEditor), the
 *    key passes through so typing a space still works.
 *  - When the binding is handled, the original key message is replaced with a
 *    harmless WM_USER — exactly what JUCE's own hook does — so neither the
 *    native plugin editor nor JUCE's per-window keyPressed handlers ever see
 *    the key. This guarantees a single toggle per press (no double-fire with
 *    PluginEditorWindow::keyPressed, ClipRegionPluginWindow::keyPressed, the
 *    clip-properties onKeyPress handlers, or MainComponent::keyPressed).
 *
 * The DAW host installs no other WH_GETMESSAGE hook (JUCE only installs one
 * in plugin-client builds), so hook ordering is not a concern.
 *
 * Trade-off (documented): a plugin's own non-text space handling (e.g. a
 * JUCE step-sequencer that plays on space) is superseded by the transport
 * toggle, matching the global-space convention of Ableton Live / FL Studio.
 *
 * Windows-only. On other platforms the calls are no-ops.
 */
class GlobalTransportSpaceHook
{
public:
    /** Install the thread-scoped hook. Must be called on the message thread.
     *  Idempotent. */
    static void install();

    /** Uninstall the hook. Safe to call during app teardown. */
    static void shutdown();
};

} // namespace DAW
