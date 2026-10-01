#include "G10EyeHandleComponent.h"
#include "G10LookAndFeel.h"

namespace APEX {
namespace G10 {

namespace
{
juce::Path makePredatoryEye (juce::Point<float> centre,
                             float halfWidth,
                             float halfHeight)
{
    // Two asymmetric cubic lids retain sharp inner/outer corners while the
    // slightly lower left corner gives the eye a tense, predatory rake.
    juce::Path eye;
    eye.startNewSubPath (centre.x - halfWidth, centre.y + halfHeight * 0.10f);
    eye.cubicTo (centre.x - halfWidth * 0.48f, centre.y - halfHeight * 1.03f,
                 centre.x + halfWidth * 0.45f, centre.y - halfHeight * 0.88f,
                 centre.x + halfWidth, centre.y - halfHeight * 0.12f);
    eye.cubicTo (centre.x + halfWidth * 0.42f, centre.y + halfHeight * 0.92f,
                 centre.x - halfWidth * 0.46f, centre.y + halfHeight * 1.02f,
                 centre.x - halfWidth, centre.y + halfHeight * 0.10f);
    eye.closeSubPath();
    return eye;
}
}

G10EyeHandleComponent::G10EyeHandleComponent (int bandIndex)
    : bandIndex_ (bandIndex)
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false); // the fader column owns dragging
    DAW::ApexPresentationClock::instance().addReceiver (this);
}

G10EyeHandleComponent::~G10EyeHandleComponent()
{
    // Message thread (editor lifecycle). removeReceiver tombstones the
    // registration and removes any active continuous-update contribution.
    DAW::ApexPresentationClock::instance().removeReceiver (this);
}

void G10EyeHandleComponent::startBlink() noexcept
{
    if (blinking_)
        return;
    blinking_ = true;
    blinkStartMs_ = juce::Time::getMillisecondCounterHiRes();
    DAW::ApexPresentationClock::instance().requestContinuousUpdate (this);
}

void G10EyeHandleComponent::cancelBlink() noexcept
{
    if (! blinking_)
        return;
    blinking_ = false;
    blink_ = 0.0f;
    repaint();
    DAW::ApexPresentationClock::instance().releaseContinuousUpdate (this);
}

void G10EyeHandleComponent::onPresentationTick (double)
{
    if (! blinking_)
        return; // static between blinks — cheap no-op

    // Wall-clock elapsed: if the DAW is busy and frames are skipped, the
    // blink still completes on time (no drift, no catch-up burst).
    const double elapsed = juce::Time::getMillisecondCounterHiRes() - blinkStartMs_;
    if (elapsed >= kBlinkDurationMs)
    {
        // Blink complete: fully static open until the next sparse trigger.
        blinking_ = false;
        blink_ = 0.0f;
        repaint();
        DAW::ApexPresentationClock::instance().releaseContinuousUpdate (this);
        return;
    }

    const float t = (float) (elapsed / kBlinkDurationMs);
    const float p = t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f; // 0 -> 1 -> 0
    if (std::abs (p - blink_) > 0.001f)
    {
        blink_ = p;
        repaint(); // ONLY this eye's local bounds
    }
}

void G10EyeHandleComponent::paint (juce::Graphics& g)
{
    const juce::Rectangle<float> bounds = getLocalBounds().toFloat();
    const juce::Point<float> c = bounds.getCentre();

    const juce::Colour core = G10Colors::bandCore (bandIndex_);
    const juce::Colour halo = G10Colors::bandHalo (bandIndex_);

    // Vertical squash for the blink (scaleY around the eye centre).
    const float open = 1.0f - blink_;
    const float squash = 0.08f + 0.92f * open; // 8% minimum height when closed

    // ---- Aura (soft almond halo) -----------------------------------------
    {
        juce::ColourGradient aura (halo.withAlpha (0.28f * open + 0.04f),
                                   c.x, c.y,
                                   halo.withAlpha (0.0f),
                                   c.x, c.y + 7.4f * squash,
                                   false);
        g.setGradientFill (aura);
        g.fillPath (makePredatoryEye (c, 11.2f, 7.4f * squash));
    }

    // ---- Colored lid / shell ---------------------------------------------
    {
        // Near-black material keeps the handle from reading as a colored LED;
        // the band's identity is concentrated in the rim and iris.
        const auto lidTop = G10Colors::shellInner().interpolatedWith (core, 0.52f).brighter (0.10f);
        const auto lidBottom = G10Colors::shellOuter().interpolatedWith (core, 0.30f);
        juce::ColourGradient shell = juce::ColourGradient::vertical (lidTop, lidBottom, bounds);
        g.setGradientFill (shell);
        const auto shellPath = makePredatoryEye (c, 10.2f, 6.15f * squash);
        g.fillPath (shellPath);
        g.setColour (halo.withAlpha (0.70f * open + 0.10f));
        g.strokePath (shellPath, juce::PathStrokeType (0.95f));
    }

    // ---- Pale sclera ------------------------------------------------------
    {
        const auto sclera = makePredatoryEye (c, 8.45f, 4.25f * squash);
        juce::ColourGradient whiteDepth = juce::ColourGradient::vertical (
            G10Colors::textPrimary().withAlpha (0.94f * open),
            juce::Colour (0xFFB9B7C4).withAlpha (0.86f * open), bounds);
        g.setGradientFill (whiteDepth);
        g.fillPath (sclera);
    }

    // ---- Band-colored iris -----------------------------------------------
    {
        const float irisW = 4.8f;
        const float irisH = 7.0f * squash;
        juce::ColourGradient iris (halo.brighter (0.25f).withAlpha (0.96f * open),
                                   c.x - 0.8f, c.y - 1.0f * squash,
                                   core.darker (0.45f).withAlpha (0.96f * open),
                                   c.x + 1.8f, c.y + 2.8f * squash,
                                   false);
        g.setGradientFill (iris);
        g.fillEllipse (juce::Rectangle<float> (c.x - irisW * 0.5f, c.y - irisH * 0.5f,
                                               irisW, irisH));
    }

    // ---- Vertical slit pupil ---------------------------------------------
    {
        const float pupilW = 1.3f;
        const float pupilH = 5.2f * squash;
        g.setColour (juce::Colours::black.withAlpha (0.93f * open));
        g.fillEllipse (juce::Rectangle<float> (c.x - pupilW * 0.5f, c.y - pupilH * 0.5f,
                                               pupilW, pupilH));
    }
}

} // namespace G10
} // namespace APEX
