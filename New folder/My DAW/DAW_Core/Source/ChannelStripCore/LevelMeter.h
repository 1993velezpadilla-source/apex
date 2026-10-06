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

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (clipLatched_ && clipBoxBounds_.contains(e.position))
        {
            clipLatched_ = false;
            clipOverDb_ = 0.0f;
            repaint();
        }
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

        const float clipGain = juce::jmax(peakHoldL_, peakHoldR_);
        if (clipGain > 1.0f)
        {
            clipLatched_ = true;
            clipOverDb_ = juce::jmax(clipOverDb_, 20.0f * std::log10(clipGain));
        }

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

        auto clipIndicatorArea = b.removeFromTop(30.0f);

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

        // Helper: sample the continuous gradient colour at a given normalised position (0=bottom, 1=top)
        auto sampleMeterGradient = [&](float normPos) -> juce::Colour
        {
            // normPos 0=bottom(loud), 1=top(quiet). Zones mirror dB zones.
            if (normPos > 0.95f) return t.colors.ember;
            if (normPos > 0.88f) return t.colors.ember.interpolatedWith(t.colors.amber, (1.0f - normPos) / 0.07f);
            if (normPos > 0.75f) return t.colors.amber.interpolatedWith(t.colors.jade, (0.88f - normPos) / 0.13f);
            if (normPos > 0.60f) return t.colors.jade.interpolatedWith(t.colors.moss,  (0.75f - normPos) / 0.15f);
            return t.colors.moss;
        };

        // Rebuild the liquid-fill gradient if bounds or theme changed.
        // Keyed on meterBounds + theme colours so it survives resize / theme switch.
        if (gradientDirty_ || cachedGradientBounds_ != meterBounds
            || cachedEmber_ != t.colors.ember || cachedMoss_ != t.colors.moss)
        {
            cachedMeterGradient_ = juce::ColourGradient(
                t.colors.ember, meterBounds.getCentreX(), meterBounds.getY(),
                t.colors.moss,  meterBounds.getCentreX(), meterBounds.getBottom(), false);
            cachedMeterGradient_.addColour(0.05, t.colors.ember);
            cachedMeterGradient_.addColour(0.12, t.colors.ember.interpolatedWith(t.colors.amber, 0.5f));
            cachedMeterGradient_.addColour(0.25, t.colors.amber);
            cachedMeterGradient_.addColour(0.40, t.colors.jade);
            cachedMeterGradient_.addColour(0.60, t.colors.moss);
            cachedMeterGradient_.addColour(1.00, t.colors.moss);
            cachedGradientBounds_ = meterBounds;
            cachedEmber_ = t.colors.ember;
            cachedMoss_  = t.colors.moss;
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
            const float normAtTop = juce::jlimit(0.0f, 1.0f,
                1.0f - (topY - meterBounds.getY()) / juce::jmax(1.0f, meterBounds.getHeight()));
            const juce::Colour meniscusCol = sampleMeterGradient(normAtTop).brighter(0.35f);
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
            float normPos = juce::jlimit(0.0f, 1.0f,
                1.0f - (getYForLevel(gain, meterBounds, core) - meterBounds.getY())
                      / juce::jmax(1.0f, meterBounds.getHeight()));
            juce::ignoreUnused(db);
            if (normPos > 0.95f) return t.colors.ember.brighter(0.4f);
            if (normPos > 0.75f) return t.colors.amber.brighter(0.4f);
            if (normPos > 0.40f) return t.colors.jade.brighter(0.4f);
            return t.colors.moss.brighter(0.4f);
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

        if (clipLatched_)
        {
            clipBoxBounds_ = clipIndicatorArea.reduced(1.0f, 4.0f);
            g.setColour(t.colors.ember);
            g.fillRoundedRectangle(clipBoxBounds_, 2.0f);
            g.setColour(juce::Colours::black.withAlpha(0.2f));
            g.drawRoundedRectangle(clipBoxBounds_, 2.0f, 1.2f);
            g.setColour(t.colors.pearl);
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(juce::String(clipOverDb_, 1), clipBoxBounds_, juce::Justification::centred);
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
    mutable bool                        gradientDirty_ = true;

    // Cached scale tick image — rebuilt only on resize or fader-range change
    mutable juce::Image                 cachedScaleTicks_;
    mutable juce::Rectangle<float>      cachedScaleBounds_;
    mutable float                       cachedScaleMaxDb_ = -999.f;
    mutable bool                        scaleTicksDirty_  = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

} // namespace DAW
