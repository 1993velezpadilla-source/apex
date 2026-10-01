#pragma once
#include <JuceHeader.h>

namespace APEX {
namespace G10 {

class G10Parameter;

// ============================================================================
// G10RotaryKnobComponent — the INPUT / OUTPUT trim knobs (-18..+18 dB).
//
// Drawn with native JUCE graphics (arc + pointer), driven through the
// canonical G10Parameter::setValue() path. Vertical drag edits; a plain
// click without movement does NOT change the value; double-click resets
// to the parameter default.
//
// Visual contract (Internal Color):
//   - the signed value is ALWAYS visible below the body ("+18.0 dB",
//     "0.0 dB", "-18.0 dB") and reflects the REAL parameter value
//     (automation/state changes repaint it via refreshFromParameter);
//   - 0 dB (unity) sits at the TOP; the bipolar value arc extends from
//     unity toward the negative side for cuts and toward the positive
//     side for boosts (thin maroon accent);
//   - a clear unity tick marks 0 dB;
//   - subtle hover/drag feedback (no decorative fixed ring).
// ============================================================================

class G10RotaryKnobComponent final : public juce::Component
{
public:
    G10RotaryKnobComponent (G10Parameter* param, const juce::String& label);
    ~G10RotaryKnobComponent() override;

    G10Parameter* getParameter() const noexcept { return param_; }

    /** GUI thread: repaint from the parameter's current value. */
    void refreshFromParameter();

    void paint (juce::Graphics&) override;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    float angleForValue (float normalized) const noexcept;

    G10Parameter* param_;
    juce::String label_;
    float value_ = 0.0f; // last normalized value (0..1)
    int lastDragY_ = 0;
    bool dragging_ = false;
    bool gestureActive_ = false;
    bool hovered_ = false;
};

} // namespace G10
} // namespace APEX
