#pragma once
#include <JuceHeader.h>

namespace APEX {
namespace G10 {

// ============================================================================
// G10LevelMeterComponent — a compact vertical peak level meter in the G10
// visual language (dark premium chassis, slim bar lane, quiet dB scale,
// green → yellow → orange → red progression with a subtle glow).
//
// Used for the INPUT and OUTPUT meters in the control strip.  Header-only so
// no project-file changes are needed in either workspace.
//
// Data flow: the audio thread stores a per-block channel-peak dBFS value in
// the processor (relaxed atomics — no locks, no allocation, no FIFO).  The
// editor's 30 Hz timer calls setLevelDb() with that value; this component
// applies the display ballistics (instant peak-style attack, smooth ~280 ms
// release, ~600 ms peak hold) and repaints ONLY when the quantized display
// actually changed — no repaint storms.
//
// All state is message-thread-owned; the component never touches the audio
// thread.
// ============================================================================

class G10LevelMeterComponent final : public juce::Component
{
public:
    // Logical design-space size (scaled by the editor's Content transform,
    // exactly like the knobs — stays readable across all supported sizes).
    // Larger instrument-style presence: 48x92 (the body extends a little
    // below the knob row so the dB scale / bar / peak-hold read at a glance).
    static constexpr int kMeterW = 48;
    static constexpr int kMeterH = 92;

    // Visible scale landmarks (0, -4, -8, -12, -16, -20, -24 dB).
    static constexpr float kMaxDb = 0.0f;
    static constexpr float kVisibleMinDb = -24.0f;
    static constexpr float kBallisticsFloorDb = -60.0f;

    // Bar lane geometry (logical px inside the component) — wider and taller
    // for instant readability while keeping the recessed premium look.
    static constexpr int kBarX = 33;
    static constexpr int kBarY = 4;
    static constexpr int kBarW = 14;
    static constexpr int kBarH = 78;

    explicit G10LevelMeterComponent()
    {
        setOpaque (false);
        setInterceptsMouseClicks (false, false);
        setSize (kMeterW, kMeterH);
        reset();
    }

    /** Message thread (editor timer, ~30 Hz): feed the latest audio-thread
        level (dBFS) and advance the display ballistics one tick. */
    void setLevelDb (float targetDb)
    {
        targetDb = juce::jmax (targetDb, kBallisticsFloorDb);
        lastTargetDb_ = targetDb;

        // Peak-style attack: rises instantly to the newest peak.
        if (targetDb > displayDb_)
            displayDb_ = targetDb;
        if (targetDb > holdDb_)
        {
            holdDb_ = targetDb;
            holdTicks_ = kHoldTicks; // ~600 ms peak hold at 30 Hz
        }

        if (holdTicks_ > 0)
        {
            --holdTicks_;
        }
        else
        {
            // Smooth release toward the current target (~280 ms time const).
            const float c = 1.0f - std::exp (-1.0f / (30.0f * 0.28f));
            displayDb_ += (targetDb - displayDb_) * c;
            holdDb_    += (targetDb - holdDb_) * c;
            if (std::abs (displayDb_ - targetDb) < 0.05f)
                displayDb_ = targetDb;
        }

        // Repaint only when the quantized bar / hold marker actually moved.
        const int newFill = barHeightPx (displayDb_);
        const int newHold = barHeightPx (holdDb_);
        if (newFill != lastFillPx_ || newHold != lastHoldPx_)
        {
            lastFillPx_ = newFill;
            lastHoldPx_ = newHold;
            repaint();
        }
    }

    void reset() noexcept
    {
        displayDb_ = kBallisticsFloorDb;
        holdDb_ = kBallisticsFloorDb;
        holdTicks_ = 0;
        lastFillPx_ = 0;
        lastHoldPx_ = 0;
        repaint();
    }

    // Test introspection.
    float getDisplayDb() const noexcept { return displayDb_; }
    float getHoldDb() const noexcept { return holdDb_; }
    float getTargetDb() const noexcept { return lastTargetDb_; }
    int getHoldTicks() const noexcept { return holdTicks_; }

    // ---- Visual style -------------------------------------------------------

    /** Level → colour progression (premium, restrained — no neon). */
    static juce::Colour levelColour (float db) noexcept
    {
        if (db >= -3.0f)  return juce::Colour (0xFFC85050); // red (hot)
        if (db >= -6.0f)  return juce::Colour (0xFFD08048); // orange
        if (db >= -12.0f) return juce::Colour (0xFFD0B24F); // yellow
        return juce::Colour (0xFF4FA87C);                   // green
    }

    static int barHeightPx (float db) noexcept
    {
        const float frac = juce::jlimit (0.0f, 1.0f,
            (db - kVisibleMinDb) / (kMaxDb - kVisibleMinDb));
        return juce::roundToInt (frac * (float) kBarH);
    }

    static float yForDb (float db) noexcept
    {
        return (float) kBarY + (float) kBarH - (float) barHeightPx (db);
    }

private:
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);

        // ---- Premium dark chassis ------------------------------------------
        g.setColour (juce::Colour (0xE0060609));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (juce::Colour (0x30FFFFFF));
        g.drawRoundedRectangle (b, 4.0f, 1.0f);
        g.setColour (juce::Colour (0x0FFFFFFF));
        g.drawLine (b.getX() + 3.0f, b.getY() + 1.0f, b.getRight() - 3.0f, b.getY() + 1.0f, 0.7f);

        // ---- Recessed bar lane ----------------------------------------------
        const juce::Rectangle<float> lane ((float) kBarX, (float) kBarY,
                                           (float) kBarW, (float) kBarH);
        g.setColour (juce::Colour (0xFF040308));
        g.fillRoundedRectangle (lane, 2.0f);
        g.setColour (juce::Colour (0x1FFFFFFF));
        g.drawRoundedRectangle (lane, 2.0f, 0.6f);

        // ---- Bar fill (segmented colour progression, clipped to the level) --
        const int fillH = lastFillPx_;
        if (fillH > 0)
        {
            const float fillTop = (float) (kBarY + kBarH - fillH);
            // Segment thresholds from the bottom: -24..-12 green,
            // -12..-6 yellow, -6..-3 orange, -3..0 red.
            struct Seg { float lowDb, highDb; juce::Colour c; };
            const Seg segs[] =
            {
                { -24.0f, -12.0f, juce::Colour (0xFF4FA87C) },
                { -12.0f,  -6.0f, juce::Colour (0xFFD0B24F) },
                {  -6.0f,  -3.0f, juce::Colour (0xFFD08048) },
                {  -3.0f,   0.0f, juce::Colour (0xFFC85050) },
            };
            for (const auto& s : segs)
            {
                const float segTop = yForDb (s.highDb);
                const float segBot = yForDb (s.lowDb);
                const float top = juce::jmax (segTop, fillTop);
                if (top < segBot)
                {
                    g.setColour (s.c);
                    g.fillRect ((float) kBarX + 1.5f, top,
                                (float) (kBarW - 3), segBot - top);
                }
            }

            // Subtle glow at the leading edge of the fill.
            g.setColour (levelColour (displayDb_).withAlpha (0.28f));
            g.drawLine ((float) (kBarX + 1), fillTop - 0.5f,
                        (float) (kBarX + kBarW - 1), fillTop - 0.5f, 1.4f);
        }

        // ---- Peak-hold marker ----------------------------------------------
        if (lastHoldPx_ > 0)
        {
            const float hy = (float) (kBarY + kBarH - lastHoldPx_);
            g.setColour (juce::Colour (0xFFE8E8F2).withAlpha (0.85f));
            g.drawLine ((float) (kBarX + 1), hy, (float) (kBarX + kBarW - 1), hy, 1.0f);
        }

        // ---- dB scale (0, -4, -8, -12, -16, -20, -24) ------------------------
        // Slightly larger than the tiny version, still thin/elegant.
        g.setFont (juce::Font (7.0f, juce::Font::plain));
        constexpr float labels[] = { 0.0f, -4.0f, -8.0f, -12.0f, -16.0f, -20.0f, -24.0f };
        for (const float db : labels)
        {
            const float y = yForDb (db);
            g.setColour (juce::Colour (0x26FFFFFF));
            g.drawLine ((float) (kBarX - 3), y, (float) (kBarX - 1), y, 0.6f);
            g.setColour (juce::Colour (0xFF8A8A99));
            const juce::String text (juce::roundToInt (db));
            const float tw = g.getCurrentFont().getStringWidth (text);
            g.drawSingleLineText (text, juce::roundToInt (3.0f + (28.0f - tw)),
                                  juce::roundToInt (y + 2.0f));
        }

        // ---- "dB" caption ----------------------------------------------------
        g.setFont (juce::Font (6.5f, juce::Font::bold));
        g.setColour (juce::Colour (0x808A8A99));
        g.drawText ("dB", juce::Rectangle<float> (3.0f, 83.0f, 28.0f, 8.0f),
                    juce::Justification::centredLeft);
    }

    static constexpr int kHoldTicks = 18; // ~600 ms at 30 Hz

    float displayDb_ = kBallisticsFloorDb;
    float holdDb_ = kBallisticsFloorDb;
    float lastTargetDb_ = kBallisticsFloorDb;
    int holdTicks_ = 0;
    int lastFillPx_ = 0;
    int lastHoldPx_ = 0;
};

} // namespace G10
} // namespace APEX
