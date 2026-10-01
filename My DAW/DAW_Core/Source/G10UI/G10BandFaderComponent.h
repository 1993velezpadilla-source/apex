#pragma once
#include <JuceHeader.h>
#include "G10EyeHandleComponent.h"

namespace APEX {
namespace G10 {

class G10Parameter;

// ============================================================================
// G10BandFaderComponent — one G10 fader column: the vertical track, the
// blinking eye handle at the current gain, the +12/0/-12 scale, and the
// frequency / musical-name / gain labels.
//
// Binding: the fader drags G10Parameter via the JUCE canonical API
// (setValue(normalized)), so every change flows through the standard
// parameter pipeline (automation, undo, host state). The eye handle moves
// from kFaderTop (+12 dB) to kFaderBottom (-12 dB); 0 dB sits at kFaderZero.
// ============================================================================

class G10BandFaderComponent final : public juce::Component
{
public:
    G10BandFaderComponent (G10Parameter* param, int bandIndex);
    ~G10BandFaderComponent() override;

    G10Parameter* getParameter() const noexcept { return param_; }
    int getBandIndex() const noexcept { return bandIndex_; }

    /** Refresh handle position / labels from the parameter (GUI thread). */
    void refreshFromParameter();

    /** Blink state for the eye handle (GUI thread). */
    void setBlinkProgress (float p) { eye_.setBlinkProgress (p); }

    /** Sparse blink trigger for the eye handle (editor scheduler). */
    void startBlink() { eye_.startBlink(); }

    /** Immediately end any in-progress eye blink (e.g. editor hidden). */
    void cancelBlink() { eye_.cancelBlink(); }

    void resized() override;
    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    float yForDb (float db) const noexcept;
    float dbForY (float y) const noexcept;

    G10Parameter* param_;
    const int bandIndex_;
    G10EyeHandleComponent eye_;
    bool dragging_ = false;
    bool gestureActive_ = false;
    int lastDragY_ = 0;
};

} // namespace G10
} // namespace APEX
