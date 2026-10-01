#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * PluginEditorResizePolicy
 *
 * Nucleus: decides how a hosted AudioProcessorEditor tracks the user-resizable
 * floating plugin window, following the JUCE 8.0.12 host contract. One clean
 * policy replaces scattered special cases; the host never guesses per-plugin.
 *
 * Policy priority:
 *
 *   1. trueResize — the editor is natively resizable (AudioProcessorEditor::
 *                   isResizable()). The host gives it the full content area
 *                   and lets the editor's own constrainer (aspect ratio /
 *                   limits) negotiate the final size. The JUCE host-side
 *                   VST3/VST2 format windows negotiate with the plugin view.
 *
 *   2. hostScale  — the editor is fixed-size but can participate in the
 *                   JUCE-sanctioned host zoom: AudioProcessorEditor::
 *                   setScaleFactor(). For pure-JUCE editors this applies the
 *                   base-class transform (rendering + hit-testing scale
 *                   together); for the host-side VST3/VST2 format windows the
 *                   format layer negotiates content scaling with the plugin
 *                   (IPlugViewContentScaleSupport / DPI vendor message) and
 *                   snaps back safely when the plugin cannot scale. The host
 *                   therefore never forces bounds the plugin rejected.
 *
 *   3. fallback   — scaling is known-unsafe for this editor: center it at its
 *                   native size and clamp the window so it can never clip the
 *                   editor. This is the fallback, not the desired default.
 *
 * The floating window itself stays resizable in every mode; only the EDITOR
 * placement follows the policy.
 */
class PluginEditorResizePolicy
{
public:
    enum class Mode { trueResize, hostScale, fallback };

    Mode mode = Mode::fallback;
    int nativeWidth = 0;
    int nativeHeight = 0;

    /** Zoom bounds for the hostScale path. */
    static constexpr float kMinScale = 0.25f;
    static constexpr float kMaxScale = 4.0f;

    /** Classify an editor at attach time (message thread, once per open). */
    static PluginEditorResizePolicy evaluate (juce::AudioProcessorEditor* editor)
    {
        PluginEditorResizePolicy policy;

        if (editor == nullptr)
            return policy;

        policy.nativeWidth  = editor->getWidth();
        policy.nativeHeight = editor->getHeight();

        if (policy.nativeWidth <= 0 || policy.nativeHeight <= 0)
            return policy; // fallback: unknown geometry

        if (editor->isResizable())
        {
            policy.mode = Mode::trueResize;
            return policy;
        }

        // Every editor format APEX hosts (APEX Native editors, the host-side
        // VST3PluginWindow and VSTPluginWindow) implements
        // AudioProcessorEditor::setScaleFactor as the JUCE-approved host-zoom
        // path and degrades safely (snap-back to the native size) when the
        // plugin cannot scale its content.
        policy.mode = Mode::hostScale;
        return policy;
    }

    /** Clamp a requested zoom to the safe hostScale bounds. */
    static float clampScale (float scale)
    {
        return juce::jlimit (kMinScale, kMaxScale, scale);
    }
};

} // namespace DAW
