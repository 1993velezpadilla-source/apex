#pragma once
#include <JuceHeader.h>
#include <cmath>
#include "AnalogVuScaleCore.h"
#include "AnalogVuBallisticsCore.h"
#include "../InputMonitorCore/InputMeterCore.h"

namespace DAW {

/**
 * CompactVuNeedle
 *
 * A small per-track analog VU needle used in track rows and mixer strips so
 * EVERY track shows its personal trim level in sync with the trim panel.
 *
 * It binds to the SAME per-track InputMeterCore that the
 * InputTrimFloatingPanel's AnalogVuMeterComponent binds to, so all three
 * surfaces (timeline row, floating-mixer strip, trim panel) agree by
 * construction — one audio-thread source, three UI reads.
 *
 * Deliberately has NO timer of its own: the host's existing 60 Hz tick
 * (TrackRow timer / MixerPanel timer) calls tick(). This keeps the per-track
 * meter cost at one tiny repaint per host tick instead of one JUCE Timer per
 * strip per row — important in projects with many tracks.
 *
 * Non-owning source pointer; the host is responsible for clearing it
 * (clearSource()) before the owning Track is destroyed.
 */
class CompactVuNeedle : public juce::Component
{
public:
    CompactVuNeedle()
    {
        setOpaque(false);
        setInterceptsMouseClicks(false, false);
    }

    /** Bind to the track's existing measurement without restarting it. */
    void setSource(InputMeterCore* src) noexcept
    {
        source_ = src;
    }

    /** Break the binding before the owning Track is destroyed. */
    void clearSource() noexcept { source_ = nullptr; }

    /** One UI tick — repaint the audio-clock value.
     *  Call from the host's existing timer; safe when unbound. */
    void tick() noexcept
    {
        repaint();
    }

    float getNeedleDb() const noexcept { return source_ != nullptr ? source_->getVuDb() : -120.0f; }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        if (b.getWidth() < 6.0f || b.getHeight() < 6.0f)
            return;

        const float pivotX = b.getCentreX();
        const float pivotY = b.getBottom();
        const auto  pivot  = juce::Point<float>(pivotX, pivotY);

        // ── Mini scale arc (mirrors the classic -20..+3 dB VU curve) ──────
        const float r = juce::jmax(2.0f, juce::jmin(b.getWidth() * 0.5f,
                                                    b.getHeight() * 0.9f));
        {
            juce::Path arc;
            arc.addCentredArc(pivotX, pivotY, r, r, 0.0f,
                              juce::degreesToRadians(-70.0f),
                              juce::degreesToRadians(70.0f), true);
            g.setColour(juce::Colour(0xFF2A2F3D));
            g.strokePath(arc, juce::PathStrokeType(1.2f));
        }

        // ── Needle (clamped by AnalogVuScaleCore, can never leave the dial) ─
        const float angleDeg = AnalogVuScaleCore::dbToAngleDeg(getNeedleDb());
        const float angleRad = juce::degreesToRadians(angleDeg);
        const float len = juce::jmax(1.0f, r - 2.0f);
        const juce::Point<float> tip(pivot.x + std::sin(angleRad) * len,
                                     pivot.y - std::cos(angleRad) * len);
        g.setColour(juce::Colour(0xFFE7C98D)); // warm vintage needle ink
        g.drawLine({ pivot, tip }, 1.6f);

        // ── Pivot cap ─────────────────────────────────────────────────────
        g.setColour(juce::Colour(0xFF3D424F));
        g.fillEllipse(pivot.x - 2.0f, pivot.y - 2.0f, 4.0f, 4.0f);
    }

private:
    InputMeterCore*        source_ { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompactVuNeedle)
};

} // namespace DAW
