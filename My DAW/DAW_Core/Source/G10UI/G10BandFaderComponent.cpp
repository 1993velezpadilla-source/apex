#include "G10BandFaderComponent.h"
#include "G10LookAndFeel.h"
#include "../G10Core/G10Processor.h"

namespace APEX {
namespace G10 {

namespace
{
// The editor places each fader column at editor y = kFaderTop - 34, so the
// component's local origin corresponds to that editor y. All fader geometry
// below is translated into LOCAL coordinates (local = editor - kFaderColumnTop).
constexpr int kFaderColumnTop  = G10Layout::kFaderTop - 34;   // editor y of the column top
constexpr int kLocalFaderTop   = G10Layout::kFaderTop - kFaderColumnTop;    // +12 dB
constexpr int kLocalFaderZero  = G10Layout::kFaderZero - kFaderColumnTop;   //  0 dB
constexpr int kLocalFaderBottom = G10Layout::kFaderBottom - kFaderColumnTop; // -12 dB
constexpr int kLocalGainTextY  = G10Layout::kGainTextY - kFaderColumnTop;   // gain label

// One conventional wheel notch moves 0.1 dB; Shift moves 0.025 dB. Convert
// those product-unit increments into normalized space so the real hosted
// parameter remains the only authority.
constexpr float kWheelSensitivity = 0.1f / (kBandMaxDb - kBandMinDb);
constexpr float kFineWheelSensitivity = 0.025f / (kBandMaxDb - kBandMinDb);

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

G10BandFaderComponent::G10BandFaderComponent (G10Parameter* param, int bandIndex)
    : param_ (param), bandIndex_ (bandIndex), eye_ (bandIndex)
{
    jassert (param != nullptr);
    jassert (bandIndex >= 0 && bandIndex < kNumBands);

    addAndMakeVisible (eye_);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

G10BandFaderComponent::~G10BandFaderComponent()
{
    // The processor outlives its editor. Close a drag transaction if the host
    // removes the editor before JUCE can deliver the matching mouseUp.
    if (gestureActive_ && param_ != nullptr)
        param_->endChangeGesture();
}

float G10BandFaderComponent::yForDb (float db) const noexcept
{
    const float t = (db - kBandMinDb) / (kBandMaxDb - kBandMinDb); // 0..1, +12=1
    return (float) kLocalFaderBottom - t * (float) (kLocalFaderBottom - kLocalFaderTop);
}

float G10BandFaderComponent::dbForY (float y) const noexcept
{
    const float t = (y - (float) kLocalFaderTop) / (float) (kLocalFaderBottom - kLocalFaderTop);
    return kBandMaxDb - t * (kBandMaxDb - kBandMinDb);
}

void G10BandFaderComponent::refreshFromParameter()
{
    if (param_ == nullptr)
        return;

    const float db = param_->getUnitsValue();
    const float y = yForDb (db);

    const int eyeW = 24;
    const int eyeH = 24;
    eye_.setBounds (getWidth() / 2 - eyeW / 2, (int) y - eyeH / 2, eyeW, eyeH);

    repaint();
}

void G10BandFaderComponent::resized()
{
    refreshFromParameter();
}

void G10BandFaderComponent::paint (juce::Graphics& g)
{
    const int w = getWidth();
    const int x = w / 2;

    // ---- Track (2px, fino) ---------------------------------------------------
    {
        juce::Path track;
        track.addRoundedRectangle ((float) x - 1.0f, (float) kLocalFaderTop,
                                   2.0f, (float) (kLocalFaderBottom - kLocalFaderTop),
                                   1.0f, 1.0f);
        g.setColour (G10Colors::ctlTrack());
        g.fillPath (track);
    }

    // ---- Gain fill (0 dB -> handle, sutil, color de banda) -------------------
    {
        const float db = param_ != nullptr ? param_->getUnitsValue() : 0.0f;
        const float yHandle = yForDb (db);
        const float yZero = yForDb (0.0f);
        const float top = juce::jmin (yHandle, yZero);
        const float hgt = std::abs (yHandle - yZero);
        if (hgt > 0.5f)
        {
            g.setColour (G10Colors::bandCore (bandIndex_).withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> ((float) x - 1.0f, top, 2.0f, hgt));
        }
    }

    // ---- Scale lines + labels (+12 / 0 / -12) -------------------------------
    {
        g.setColour (G10Colors::panelLine());
        for (int db = -12; db <= 12; db += 6)
        {
            const float y = yForDb ((float) db);
            g.drawHorizontalLine ((int) y, (float) x - 8.0f, (float) x + 8.0f);
        }

        g.setFont (G10Fonts::db());
        g.setColour (G10Colors::textDim());
        g.drawText ("+12", juce::Rectangle<int> (0, kLocalFaderTop - 12, w, 10),
                    juce::Justification::centred);
        g.drawText ("0",   juce::Rectangle<int> (0, kLocalFaderZero - 10, w, 10),
                    juce::Justification::centred);
        g.drawText ("-12", juce::Rectangle<int> (0, kLocalFaderBottom - 12, w, 10),
                    juce::Justification::centred);
    }

    // ---- Frequency + musical name + gain ------------------------------------
    {
        const G10BandInfo& info = kBandInfos[bandIndex_];
        const juce::Colour core = G10Colors::bandCore (bandIndex_);
        const juce::Colour halo = G10Colors::bandHalo (bandIndex_);

        g.setFont (G10Fonts::freq());
        g.setColour (core.getPerceivedBrightness() > 0.6f ? core : halo);
        g.drawText (info.frequencyLabel,
                    juce::Rectangle<int> (0, kLocalFaderTop - 28, w, 14),
                    juce::Justification::centred);

        g.setFont (G10Fonts::bandName());
        g.setColour (G10Colors::textSecondary());
        g.drawText (info.musicalName,
                    juce::Rectangle<int> (0, kLocalFaderTop - 13, w, 11),
                    juce::Justification::centred);

        g.setFont (G10Fonts::gain());
        g.setColour (dragging_ ? core : G10Colors::textSecondary());
        const float db = param_ != nullptr ? param_->getUnitsValue() : 0.0f;
        const juce::String gainText = juce::String (db, db > 9.0f ? 0 : 1) + " dB";
        g.drawText (gainText,
                    juce::Rectangle<int> (0, kLocalGainTextY, w, 14),
                    juce::Justification::centred);
    }
}

void G10BandFaderComponent::mouseDown (const juce::MouseEvent& e)
{
    // JUCE sends mouseDown before mouseDoubleClick on the second click. Do not
    // arm a drag for that click; the dedicated reset handler owns the edit.
    if (e.getNumberOfClicks() > 1)
    {
        dragging_ = false;
        return;
    }

    // A plain click must NOT modify the parameter. Only a drag edits it;
    // the value is first adopted in mouseDrag when the pointer moves.
    dragging_ = true;
    lastDragY_ = e.getPosition().y;
    refreshFromParameter();
}

void G10BandFaderComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging_ || param_ == nullptr)
        return;

    // Only edit once the pointer has actually moved, so a click that is
    // released without movement leaves the parameter untouched.
    if (std::abs (e.getPosition().y - lastDragY_) < 2)
        return;

    // Proper host gesture semantics: begin once, notify during the drag.
    if (! gestureActive_)
    {
        param_->beginChangeGesture();
        gestureActive_ = true;
    }

    const float db = juce::jlimit (kBandMinDb, kBandMaxDb, dbForY ((float) e.getPosition().y));
    param_->setValueNotifyingHost ((db - kBandMinDb) / (kBandMaxDb - kBandMinDb));
    lastDragY_ = e.getPosition().y;
    refreshFromParameter();
}

void G10BandFaderComponent::mouseUp (const juce::MouseEvent&)
{
    if (gestureActive_)
    {
        param_->endChangeGesture();
        gestureActive_ = false;
    }
    dragging_ = false;
    refreshFromParameter();
}

void G10BandFaderComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param_ == nullptr)
        return;

    if (gestureActive_)
    {
        param_->endChangeGesture();
        gestureActive_ = false;
    }

    dragging_ = false;

    // Reset to the product's exact unity value, independently of any future
    // change to the parameter's general-purpose default policy.
    constexpr float unityNormalized = (0.0f - kBandMinDb) / (kBandMaxDb - kBandMinDb);
    if (param_->getValue() == unityNormalized)
    {
        refreshFromParameter();
        return;
    }

    param_->beginChangeGesture();
    param_->setValueNotifyingHost (unityNormalized);
    param_->endChangeGesture();
    refreshFromParameter();
}

void G10BandFaderComponent::mouseWheelMove (const juce::MouseEvent& e,
                                            const juce::MouseWheelDetails& wheel)
{
    if (param_ == nullptr || gestureActive_ || e.mods.isAnyMouseButtonDown())
        return;

    const float oldValue = param_->getValue();
    const float newValue = juce::jlimit (0.0f, 1.0f,
                                         oldValue + normalizedWheelDelta (e, wheel));
    if (newValue == oldValue)
        return;

    // One wheel event is one complete, balanced host edit. This cannot leave
    // a gesture open if inertial scrolling ends or the editor is destroyed.
    param_->beginChangeGesture();
    param_->setValueNotifyingHost (newValue);
    param_->endChangeGesture();
    refreshFromParameter();
}

} // namespace G10
} // namespace APEX
