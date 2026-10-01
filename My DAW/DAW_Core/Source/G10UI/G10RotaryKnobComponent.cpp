#include "G10RotaryKnobComponent.h"
#include "G10LookAndFeel.h"
#include "../G10Core/G10Processor.h"

namespace APEX {
namespace G10 {

namespace
{
constexpr float kRotaryStartDegrees = -135.0f;
constexpr float kRotaryEndDegrees = 135.0f;
constexpr float kUnityDegrees = 0.0f; // JUCE Path arcs: zero is 12 o'clock
constexpr float kWheelSensitivity = 0.1f / (kTrimMaxDb - kTrimMinDb);
constexpr float kFineWheelSensitivity = 0.025f / (kTrimMaxDb - kTrimMinDb);

float normalizedWheelDelta (const juce::MouseEvent& e,
                            const juce::MouseWheelDetails& wheel) noexcept
{
    float amount = std::abs (wheel.deltaX) > std::abs (wheel.deltaY)
        ? -wheel.deltaX
        : wheel.deltaY;

    if (wheel.isReversed)
        amount = -amount;

    return amount * (e.mods.isShiftDown() ? kFineWheelSensitivity
                                           : kWheelSensitivity);
}
}

G10RotaryKnobComponent::G10RotaryKnobComponent (G10Parameter* param, const juce::String& label)
    : param_ (param), label_ (label)
{
    jassert (param != nullptr);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    refreshFromParameter();
}

G10RotaryKnobComponent::~G10RotaryKnobComponent()
{
    // The processor owns the parameter for longer than the editor lives.
    if (gestureActive_ && param_ != nullptr)
        param_->endChangeGesture();
}

float G10RotaryKnobComponent::angleForValue (float normalized) const noexcept
{
    // JUCE Path arc angles are clockwise with zero at 12 o'clock. Keeping the
    // pointer and both arcs in that convention prevents the value arc from
    // starting at 9 o'clock while the pointer correctly shows unity at top.
    const float t = juce::jlimit (0.0f, 1.0f, normalized);
    return juce::degreesToRadians (kRotaryStartDegrees
                                   + (kRotaryEndDegrees - kRotaryStartDegrees) * t);
}

void G10RotaryKnobComponent::refreshFromParameter()
{
    if (param_ != nullptr)
        value_ = param_->getValue();
    repaint();
}

void G10RotaryKnobComponent::paint (juce::Graphics& g)
{
    const juce::Rectangle<float> bounds = getLocalBounds().toFloat();
    const float cx = bounds.getCentreX();
    const float cy = 35.0f;
    const float bodyRadius = 18.5f;
    const float arcRadius = 20.0f;

    // ---- Label (always visible, above the body) ---------------------------
    g.setFont (G10Fonts::ctl());
    g.setColour (G10Colors::textSecondary());
    g.drawText (label_, juce::Rectangle<float> (0.0f, 0.0f, bounds.getWidth(), 11.0f),
                juce::Justification::centred);

    // ---- Body ---------------------------------------------------------------
    {
        juce::ColourGradient body = juce::ColourGradient::vertical (
            G10Colors::ctlInactive().brighter (0.25f), G10Colors::ctlInactive().darker (0.35f), bounds);
        g.setGradientFill (body);
        g.fillEllipse (juce::Rectangle<float> (cx - bodyRadius, cy - bodyRadius,
                                               bodyRadius * 2.0f, bodyRadius * 2.0f));
    }

    // ---- Track arc (full 270 deg sweep; no decorative fixed ring) ----------
    {
        const float start = juce::degreesToRadians (kRotaryStartDegrees);
        const float end   = juce::degreesToRadians (kRotaryEndDegrees);
        juce::Path track;
        track.addArc (cx - arcRadius, cy - arcRadius, arcRadius * 2.0f, arcRadius * 2.0f,
                      start, end, true);
        g.setColour (hovered_ ? G10Colors::panelLine() : G10Colors::ctlTrack());
        g.strokePath (track, juce::PathStrokeType (2.0f));
    }

    // ---- Bipolar value arc (thin maroon accent, unity at top) --------------
    {
        const float unity = juce::degreesToRadians (kUnityDegrees);
        const float angle = angleForValue (value_);
        const float sweep = angle - unity; // negative = cut, positive = boost
        juce::Path arc;
        if (sweep < 0.0f)
            arc.addArc (cx - arcRadius, cy - arcRadius, arcRadius * 2.0f, arcRadius * 2.0f,
                         unity + sweep, unity, true);
        else if (sweep > 0.0f)
            arc.addArc (cx - arcRadius, cy - arcRadius, arcRadius * 2.0f, arcRadius * 2.0f,
                         unity, unity + sweep, true);
        g.setColour (G10Colors::analyzerGlow()); // maroon accent
        g.strokePath (arc, juce::PathStrokeType (dragging_ ? 3.0f : 2.5f));
    }

    // ---- Unity tick (0 dB at the top) ---------------------------------------
    {
        const float unity = juce::degreesToRadians (kUnityDegrees);
        const float ux = cx + std::sin (unity) * (arcRadius + 3.0f);
        const float uy = cy - std::cos (unity) * (arcRadius + 3.0f);
        g.setColour (G10Colors::ctlActive());
        g.drawLine (cx + std::sin (unity) * (arcRadius - 2.0f),
                    cy - std::cos (unity) * (arcRadius - 2.0f),
                    ux, uy, 1.5f);
    }

    // ---- Pointer -------------------------------------------------------------
    {
        const float angle = angleForValue (value_);
        const float px = cx + std::sin (angle) * (bodyRadius - 3.0f);
        const float py = cy - std::cos (angle) * (bodyRadius - 3.0f);
        g.setColour (dragging_ ? G10Colors::textPrimary() : G10Colors::ctlActive());
        g.drawLine (cx, cy, px, py, 2.0f);
    }

    // ---- Value (ALWAYS visible, never clipped, real parameter value) --------
    {
        g.setFont (G10Fonts::ctl());
        g.setColour (G10Colors::textPrimary());
        const float db = param_ != nullptr ? param_->getUnitsValue() : 0.0f;
        const juce::String text = (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
        g.drawText (text, juce::Rectangle<float> (0.0f, 59.0f, bounds.getWidth(), 14.0f),
                    juce::Justification::centred);
    }
}

void G10RotaryKnobComponent::mouseEnter (const juce::MouseEvent&)
{
    hovered_ = true;
    repaint();
}

void G10RotaryKnobComponent::mouseExit (const juce::MouseEvent&)
{
    hovered_ = false;
    repaint();
}

void G10RotaryKnobComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.getNumberOfClicks() > 1)
    {
        dragging_ = false;
        repaint();
        return;
    }

    dragging_ = true;
    lastDragY_ = e.getPosition().y;
    repaint();
}

void G10RotaryKnobComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging_ || param_ == nullptr)
        return;

    if (std::abs (e.getPosition().y - lastDragY_) < 2)
        return; // no movement -> no edit

    // Proper host gesture semantics: begin once, notify during the drag.
    if (! gestureActive_)
    {
        param_->beginChangeGesture();
        gestureActive_ = true;
    }

    const float delta = (float) (lastDragY_ - e.getPosition().y) * 0.004f; // ~250px for full range
    param_->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, param_->getValue() + delta));
    lastDragY_ = e.getPosition().y;
    refreshFromParameter();
}

void G10RotaryKnobComponent::mouseUp (const juce::MouseEvent&)
{
    if (gestureActive_)
    {
        param_->endChangeGesture();
        gestureActive_ = false;
    }
    dragging_ = false;
    repaint();
}

void G10RotaryKnobComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param_ == nullptr)
        return;

    if (gestureActive_)
    {
        param_->endChangeGesture();
        gestureActive_ = false;
    }
    dragging_ = false;

    // Host-notified default-value change with balanced gesture semantics.
    param_->beginChangeGesture();
    param_->setValueNotifyingHost (param_->getDefaultValue());
    param_->endChangeGesture();
    refreshFromParameter();
}

void G10RotaryKnobComponent::mouseWheelMove (const juce::MouseEvent& e,
                                             const juce::MouseWheelDetails& wheel)
{
    if (param_ == nullptr || gestureActive_ || e.mods.isAnyMouseButtonDown())
        return;

    const float oldValue = param_->getValue();
    const float newValue = juce::jlimit (0.0f, 1.0f,
                                         oldValue + normalizedWheelDelta (e, wheel));
    if (newValue == oldValue)
        return;

    param_->beginChangeGesture();
    param_->setValueNotifyingHost (newValue);
    param_->endChangeGesture();
    refreshFromParameter();
}

} // namespace G10
} // namespace APEX
