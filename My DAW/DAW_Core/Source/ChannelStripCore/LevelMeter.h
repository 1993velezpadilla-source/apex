#pragma once
#include <JuceHeader.h>
#include <cmath>
#include "../ThemeCore/Theme.h"
#include "../MixerScaleCore/FaderRangeCore.h"

namespace DAW {

/**
 * LevelMeter — vertical VU meter with peak hold.
 *
 * Scale range and tick positions come from FaderRangeCore so they
 * automatically adapt when the user flips the +6/+12 format toggle.
 * Color thresholds are constant in dB (green→yellow→red), only their
 * pixel positions change.
 */
class LevelMeter : public juce::Component,
                   public FaderRangeCore::Listener
{
public:
    LevelMeter()
    {
        setOpaque(false);
        if (auto* c = FaderRangeCore::getGlobalInstance())
            c->addListener(this);
    }

    ~LevelMeter() override
    {
        if (auto* c = FaderRangeCore::getGlobalInstance())
            c->removeListener(this);
    }

    void faderRangeChanged() override { gradientDirty_ = true; scaleTicksDirty_ = true; repaint(); }

    void resized() override { gradientDirty_ = true; scaleTicksDirty_ = true; }

    void setLevel(float newLevel) { setLevels(newLevel, newLevel); }
    void setLevels(float left, float right)
    {
        targetLevelL_ = left;
        targetLevelR_ = right;
    }

    /** The per-strip clip box is optional: the mixer draws its own indicator
        above the TL button on regular strips, so the in-lane box (too narrow
        in the 6 px meter lane) is disabled there. The master keeps it. */
    void setClipIndicatorVisible(bool shouldBeVisible) noexcept
    {
        if (clipIndicatorVisible_ == shouldBeVisible)
            return;
        clipIndicatorVisible_ = shouldBeVisible;
        repaint();
    }

    bool isClipLatched() const noexcept { return clipLatched_; }
    float getClipOverDb() const noexcept { return clipOverDb_; }

    /** Pull the latest clip maximum from its owning track. */
    void setClipOverDb(float overDb) noexcept
    {
        if (!std::isfinite(overDb) || overDb <= 0.0f)
            return;
        if (!clipLatched_ || overDb > clipOverDb_)
        {
            clipLatched_ = true;
            clipOverDb_ = overDb;
            repaint();
        }
    }

    /** Clear the latched clip state (click-to-reset). */
    void clearClip() noexcept
    {
        clipLatched_ = false;
        clipOverDb_  = 0.0f;
        repaint();
    }

    /** True while signal or peak-hold state still needs a presentation tick. */
    bool needsAnimationTick() const noexcept
    {
        return targetLevelL_ > 0.001f || targetLevelR_ > 0.001f
            || smoothLevelL_ > 0.001f || smoothLevelR_ > 0.001f
            || peakHoldL_ > 0.01f || peakHoldR_ > 0.01f
            || peakTimerL_ > 0 || peakTimerR_ > 0
            || silenceFrames_ > 60;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        juce::ignoreUnused(e);
        // Clicking the meter clears a latched clip. The in-lane box can be
        // only 6 px wide, so a box-only hit target was impossible to hit.
        clearClip();
    }

    void tick()
    {
        auto tickChannel = [](float target, float& smooth, float& peakHold, int& peakTimer)
        {
            if (target > smooth)
                smooth = target;
            else
                smooth *= 0.94f;

            if (smooth > peakHold)
            {
                peakHold  = smooth;
                peakTimer = 45;
            }
            else if (peakTimer > 0)
                --peakTimer;
            else
                peakHold *= 0.96f;
        };

        tickChannel(targetLevelL_, smoothLevelL_, peakHoldL_, peakTimerL_);
        tickChannel(targetLevelR_, smoothLevelR_, peakHoldR_, peakTimerR_);

        // Idle breathing: silence detection
        const bool hasSig = (smoothLevelL_ > 0.001f || smoothLevelR_ > 0.001f);
        if (hasSig)
        {
            silenceFrames_ = 0;
            idleBreathAlpha_ = 0.0f;
        }
        else
        {
            ++silenceFrames_;
        }

        ++ticksSinceMeterRepaint_;
        ++animTickCount_;

        // Advance breath and droplet pulse phases in tick() so paint() reads
        // stable values — no getMillisecondCounterHiRes() in the paint path.
        if (silenceFrames_ > 60)
        {
            breathPhase_ += 0.0174533f; // ~1 Hz at 60 Hz (2π/360)
            if (breathPhase_ > juce::MathConstants<float>::twoPi)
                breathPhase_ -= juce::MathConstants<float>::twoPi;
        }
        dropletPhase_ += 0.0523599f; // ~0.5 Hz at 60 Hz (2π/120)
        if (dropletPhase_ > juce::MathConstants<float>::twoPi)
            dropletPhase_ -= juce::MathConstants<float>::twoPi;

        const bool clipStateChanged = clipLatched_ != lastDrawnClipLatched_;
        const bool breathChanged = (silenceFrames_ == 61); // first silent frame — start breathing
        const bool visualChanged = std::abs(smoothLevelL_ - lastDrawnLevelL_) > 0.004f
            || std::abs(smoothLevelR_ - lastDrawnLevelR_) > 0.004f
            || std::abs(peakHoldL_ - lastDrawnPeakL_) > 0.004f
            || std::abs(peakHoldR_ - lastDrawnPeakR_) > 0.004f
            || clipStateChanged
            || std::abs(clipOverDb_ - lastDrawnClipOverDb_) > 0.04f
            || breathChanged;

        // Drive breathing repaint: once breathing starts, repaint every 4 ticks (~15 Hz)
        // Uses animTickCount_ (never reset) so cadence is independent of repaints.
        const bool breathRepaint = (silenceFrames_ > 61) && ((animTickCount_ % 4) == 0);

        // Drive droplet pulse repaint every 3 ticks (~20 Hz) — only when peak hold
        // is clearly visible (> 0.05 to avoid near-zero ghost holds triggering this).
        // Uses animTickCount_ so the cadence is independent of repaint resets.
        const bool dropletRepaint = (peakHoldL_ > 0.05f || peakHoldR_ > 0.05f)
                                    && ((animTickCount_ % 3) == 0);

        if ((visualChanged || breathRepaint || dropletRepaint)
            && (ticksSinceMeterRepaint_ >= 1 || clipStateChanged))
        {
            lastDrawnLevelL_       = smoothLevelL_;
            lastDrawnLevelR_       = smoothLevelR_;
            lastDrawnPeakL_        = peakHoldL_;
            lastDrawnPeakR_        = peakHoldR_;
            lastDrawnClipLatched_  = clipLatched_;
            lastDrawnClipOverDb_   = clipOverDb_;
            ticksSinceMeterRepaint_ = 0;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto b = getLocalBounds().toFloat().reduced(1.0f);

        if (b.getHeight() < 4.0f)
            return;

        FaderRangeCore* core = FaderRangeCore::getGlobalInstance();
        const float maxDb     = core ? core->getMaxDb()    : 6.0f;
        const float minDb     = core ? core->getMinDb()    : -96.0f;
        const float unityNorm = core ? core->getUnityNorm() : 0.88f;
        juce::ignoreUnused(unityNorm, minDb);

        auto clipIndicatorArea = clipIndicatorVisible_ ? b.removeFromTop(30.0f)
                                                       : juce::Rectangle<float>();

        const float scaleW = b.getWidth() >= 32.0f ? 24.0f : 0.0f;
        auto scaleArea  = b.removeFromLeft(scaleW);
        auto meterBounds = b.reduced(0.0f, 3.0f);

        if (scaleW > 0.0f)
        {
            // Rebuild scale tick cache if stale
            if (scaleTicksDirty_ || cachedScaleBounds_ != scaleArea
                || cachedScaleMaxDb_ != maxDb || !cachedScaleTicks_.isValid())
            {
                const int sw = juce::jmax(1, (int)std::ceil(scaleArea.getWidth() + meterBounds.getRight() - scaleArea.getX()));
                const int sh = juce::jmax(1, (int)std::ceil(scaleArea.getHeight()));
                cachedScaleTicks_ = juce::Image(juce::Image::ARGB, sw, sh, true);
                juce::Graphics ig(cachedScaleTicks_);
                ig.addTransform(juce::AffineTransform::translation(-scaleArea.getX(), -scaleArea.getY()));

                struct Tick { float db; const char* label; };
                static const Tick kAllTicks[] = {
                    { 12.0f, "+12" }, { 6.0f, "+6" }, { 3.0f, "+3" },
                    { 0.0f,  "0"   }, { -3.0f, "-3"  }, { -6.0f, "-6"  },
                    { -12.0f, "-12"}, { -24.0f, "-24"}, { -48.0f, "-48"}
                };
                ig.setFont(juce::Font(9.2f, juce::Font::bold));
                for (const auto& tick : kAllTicks)
                {
                    if (tick.db > maxDb + 0.01f) continue;
                    const float y = getYForDb(tick.db, meterBounds, core);
                    ig.setColour(t.colors.platinum.withAlpha(0.90f));
                    ig.drawText(tick.label, scaleArea.withY(y - 6.0f).withHeight(12.0f),
                               juce::Justification::centredRight);
                    ig.setColour(t.colors.pewter.withAlpha(0.40f));
                    ig.drawHorizontalLine((int)std::round(y), meterBounds.getX(), meterBounds.getRight());
                }
                cachedScaleBounds_ = scaleArea;
                cachedScaleMaxDb_  = maxDb;
                scaleTicksDirty_   = false;
            }
            g.drawImageAt(cachedScaleTicks_, (int)scaleArea.getX(), (int)scaleArea.getY(), false);

            b.removeFromLeft(3.0f);
            meterBounds = b.reduced(0.0f, 3.0f);
        }

        const float meterVisualW = juce::jlimit(2.0f, 3.0f, meterBounds.getWidth() * 0.5f);
        auto centreX = meterBounds.getCentreX();
        auto leftMeter  = juce::Rectangle<float>(centreX - meterVisualW - 0.5f,
                                                 meterBounds.getY(),
                                                 meterVisualW,
                                                 meterBounds.getHeight());
        auto rightMeter = juce::Rectangle<float>(centreX + 0.5f,
                                                 meterBounds.getY(),
                                                 meterVisualW,
                                                 meterBounds.getHeight());

        // Meter colours are tied to dBFS, independent of the +6/+12 fader
        // taper: green below -6 dBFS, yellow from -6 to 0, red above 0.
        auto colourForDb = [&](float db) -> juce::Colour
        {
            if (db >= 0.0f) return t.colors.ember;
            if (db >= -6.0f) return t.colors.amber;
            if (db >= -18.0f) return t.colors.jade;
            return t.colors.moss;
        };

        // Rebuild the liquid-fill gradient if bounds or theme changed.
        // Keyed on meterBounds + theme colours so it survives resize / theme switch.
        if (gradientDirty_ || cachedGradientBounds_ != meterBounds
            || cachedEmber_ != t.colors.ember || cachedMoss_ != t.colors.moss
            || cachedAmber_ != t.colors.amber || cachedJade_ != t.colors.jade)
        {
            const float maxDb = core ? core->getMaxDb() : 6.0f;
            const float minDb = core ? core->getMinDb() : -96.0f;
            cachedMeterGradient_ = juce::ColourGradient(
                colourForDb(maxDb), meterBounds.getCentreX(), meterBounds.getY(),
                colourForDb(minDb), meterBounds.getCentreX(), meterBounds.getBottom(), false);
            auto addDbStop = [&](float db, juce::Colour colour)
            {
                if (db < minDb || db > maxDb)
                    return;
                const float y = getYForDb(db, meterBounds, core);
                const double position = juce::jlimit(0.0f, 1.0f,
                    (y - meterBounds.getY()) / juce::jmax(1.0f, meterBounds.getHeight()));
                cachedMeterGradient_.addColour(position, colour);
            };
            addDbStop(0.0f, t.colors.ember);
            addDbStop(-0.1f, t.colors.amber);
            addDbStop(-6.0f, t.colors.amber);
            addDbStop(-18.0f, t.colors.jade);
            addDbStop(-18.1f, t.colors.moss);
            cachedGradientBounds_ = meterBounds;
            cachedEmber_ = t.colors.ember;
            cachedMoss_  = t.colors.moss;
            cachedAmber_ = t.colors.amber;
            cachedJade_ = t.colors.jade;
            gradientDirty_ = false;
        }

        // Liquid meter bar: one continuous gradient fill, clipped to current level
        auto drawLiquidMeterBar = [&](juce::Rectangle<float> barBounds, float smoothLevel)
        {
            if (smoothLevel < 0.001f) return;

            const float topY      = getYForLevel(smoothLevel, meterBounds, core);
            const float barBottom = barBounds.getBottom();
            if (topY >= barBottom) return;

            const float fillH = barBottom - topY;

            // Use cached gradient — only the x-coordinate needs adjusting per channel
            auto fill = cachedMeterGradient_;
            fill.point1.x = barBounds.getCentreX();
            fill.point2.x = barBounds.getCentreX();

            g.setGradientFill(fill);
            g.fillRoundedRectangle(barBounds.getX(), topY, barBounds.getWidth(), fillH, 1.0f);

            // Meniscus: 2px brighter band at the top surface of the liquid
            const float topDb = smoothLevel <= 0.000001f ? -96.0f : 20.0f * std::log10(smoothLevel);
            const juce::Colour meniscusCol = colourForDb(topDb).brighter(0.35f);
            g.setColour(meniscusCol);
            g.fillRect(barBounds.getX(), topY - 1.0f, barBounds.getWidth(), 2.0f);
        };

        drawLiquidMeterBar(leftMeter,  smoothLevelL_);
        drawLiquidMeterBar(rightMeter, smoothLevelR_);

        // Idle breathing — silence > ~2 seconds: subtle moss glow from bottom 6% of meter.
        // Implemented as two solid-colour fills (no gradient brush) to avoid D2D brush
        // creation on every frame — visually identical at this alpha/height range.
        if (silenceFrames_ > 60)
        {
            const float breath = 0.04f + 0.04f * (0.5f + 0.5f * std::sin(breathPhase_));
            if (core)
            {
                const float glowH    = meterBounds.getHeight() * 0.06f;
                const float glowTopY = meterBounds.getBottom() - glowH;
                g.setColour(t.colors.moss.withAlpha(breath * 0.70f));
                g.fillRect(leftMeter.getX(),  glowTopY + glowH * 0.5f, leftMeter.getWidth(),  glowH * 0.5f);
                g.fillRect(rightMeter.getX(), glowTopY + glowH * 0.5f, rightMeter.getWidth(), glowH * 0.5f);
                g.setColour(t.colors.moss.withAlpha(breath * 0.25f));
                g.fillRect(leftMeter.getX(),  glowTopY, leftMeter.getWidth(),  glowH * 0.5f);
                g.fillRect(rightMeter.getX(), glowTopY, rightMeter.getWidth(), glowH * 0.5f);
            }
        }

        // Overrange top flash with ember
        if (smoothLevelL_ > 1.0f)
        {
            g.setColour(t.colors.ember.withAlpha(0.95f));
            g.fillRoundedRectangle(leftMeter.withHeight(3.0f), 1.5f);
        }
        if (smoothLevelR_ > 1.0f)
        {
            g.setColour(t.colors.ember.withAlpha(0.95f));
            g.fillRoundedRectangle(rightMeter.withHeight(3.0f), 1.5f);
        }

        // Peak hold — droplet shape (slightly wider than meter, rounded, zone-matched colour brightened)
        auto getPeakCol = [&](float gain) -> juce::Colour
        {
            const float db = gain <= 0.000001f ? -96.0f : 20.0f * std::log10(gain);
            // Sample continuous gradient at peak position
            juce::ignoreUnused(db);
            return colourForDb(db).brighter(0.4f);
        };

        const float dropletPulse = 0.85f + 0.15f * (0.5f + 0.5f * std::sin(dropletPhase_));

        auto drawDroplet = [&](juce::Rectangle<float> barBounds, float peakGain)
        {
            if (peakGain <= 0.01f) return;
            const float peakY = getYForLevel(peakGain, meterBounds, core);
            const float dropW = barBounds.getWidth() + 2.0f;
            const float dropH = 3.0f;
            juce::Rectangle<float> droplet(barBounds.getX() - 1.0f, peakY - 1.5f, dropW, dropH);
            g.setColour(getPeakCol(peakGain).withAlpha(dropletPulse));
            g.fillRoundedRectangle(droplet, 1.5f);
        };

        drawDroplet(leftMeter,  peakHoldL_);
        drawDroplet(rightMeter, peakHoldR_);

        if (clipLatched_ && clipIndicatorVisible_)
        {
            // Enlarged clip box: fills its reserved indicator area (the top
            // strip of the meter) so it is easy to see and to click, without
            // overlapping the meter, scale or any mixer control below it.
            clipBoxBounds_ = clipIndicatorArea.reduced(1.0f, 1.5f);
            g.setColour(t.colors.ember);
            g.fillRoundedRectangle(clipBoxBounds_, 2.0f);
            g.setColour(juce::Colours::black.withAlpha(0.2f));
            g.drawRoundedRectangle(clipBoxBounds_, 2.0f, 1.2f);
            g.setColour(t.colors.pearl);
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            // Always a positive amount: the latch only arms above 0 dBFS.
            g.drawText("+" + juce::String(clipOverDb_, 1), clipBoxBounds_, juce::Justification::centred);
        }
        else
        {
            clipBoxBounds_ = {};
        }

    }


private:
    static float gainFromDb(float db)  { return std::pow(10.0f, db / 20.0f); }

    /** Map a dB value to a Y pixel using FaderRangeCore taper. */
    static float getYForDb(float db, juce::Rectangle<float> bounds, FaderRangeCore* core)
    {
        float norm = core ? core->dbToNorm(db) : (db <= 0.0f
            ? juce::jmap(db, -96.0f, 0.0f, 0.0f, 0.88f)
            : juce::jmap(db, 0.0f,    6.0f, 0.88f, 1.0f));
        return bounds.getBottom() - bounds.getHeight() * norm;
    }

    static float getYForLevel(float gain, juce::Rectangle<float> bounds, FaderRangeCore* core)
    {
        const float db = gain <= 0.000001f ? -96.0f : 20.0f * std::log10(gain);
        return getYForDb(db, bounds, core);
    }

    float targetLevelL_ = 0.0f, targetLevelR_ = 0.0f;
    float smoothLevelL_ = 0.0f, smoothLevelR_ = 0.0f;
    float peakHoldL_    = 0.0f, peakHoldR_    = 0.0f;
    int   peakTimerL_   = 0,    peakTimerR_   = 0;
    float breathPhase_  = 0.f;
    float dropletPhase_ = 0.f;
    int   animTickCount_ = 0;  // always increments, never reset — for animation cadence
    float lastDrawnLevelL_ = 0.0f, lastDrawnLevelR_ = 0.0f;
    float lastDrawnPeakL_  = 0.0f, lastDrawnPeakR_  = 0.0f;
    int   ticksSinceMeterRepaint_ = 0;
    bool  clipLatched_           = false;
    bool  clipIndicatorVisible_  = true;
    bool  lastDrawnClipLatched_  = false;
    float clipOverDb_            = 0.0f;
    float lastDrawnClipOverDb_   = 0.0f;
    int   silenceFrames_         = 0;
    float idleBreathAlpha_       = 0.0f;
    mutable juce::Rectangle<float> clipBoxBounds_;

    // Cached liquid-fill gradient — rebuilt only when bounds or theme colours change
    mutable juce::ColourGradient        cachedMeterGradient_;
    mutable juce::Rectangle<float>      cachedGradientBounds_;
    mutable juce::Colour                cachedEmber_;
    mutable juce::Colour                cachedMoss_;
    mutable juce::Colour                cachedAmber_;
    mutable juce::Colour                cachedJade_;
    mutable bool                        gradientDirty_ = true;

    // Cached scale tick image — rebuilt only on resize or fader-range change
    mutable juce::Image                 cachedScaleTicks_;
    mutable juce::Rectangle<float>      cachedScaleBounds_;
    mutable float                       cachedScaleMaxDb_ = -999.f;
    mutable bool                        scaleTicksDirty_  = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

} // namespace DAW
