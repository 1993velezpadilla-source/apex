#pragma once
#include <JuceHeader.h>
#include "AnalogVuMeterCore.h"
#include "VuChannelMode.h"
#include "../InputMonitorCore/InputMeterCore.h"

namespace DAW {

/**
 * AnalogVuMeterComponent
 *
 * The JUCE Component that assembles the analog VU meter from the nine
 * Batch 1 nucleos. Owns:
 *   - a non-owning pointer to the per-track InputMeterCore (the audio source)
 *   - a per-instance BallisticsCore (smoothing state)
 *   - a per-instance OverloadCore (latch state)
 *   - the six stateless renderers (cream face, red zone, scale markings,
 *     logo, overload bulb, needle)
 *
 * Per-track uniqueness: the source pointer + the per-instance ballistics
 * and overload state are reset whenever setSource() rebinds the meter to
 * a different InputMeterCore — switching tracks does not bleed needle
 * history or OL latch.
 *
 * UI rate: 60 Hz timer drives the smoothing and repaint.
 *
 * Click-to-reset: clicking on the OL bulb clears the overload latch.
 */
class AnalogVuMeterComponent : public juce::Component,
                               private juce::Timer
{
public:
    AnalogVuMeterComponent()
    {
        setOpaque(false);
        ballistics_.setTickRateHz(60.0f);
        startTimerHz(60);
    }

    ~AnalogVuMeterComponent() override
    {
        stopTimer();
    }

    /** Bind the meter to a per-track InputMeterCore. Resets all smoothing
     *  and OL state so the meter starts clean for the new track. */
    void setSource(InputMeterCore* src)
    {
        source_ = src;
        ballistics_.resetAll();
        overload_.reset();
        repaint();
    }

    /** Set the ballistics mode (ClassicVu / Ppm / Custom). */
    void setBallisticsMode(AnalogVuBallisticsCore::Mode m) { ballistics_.setMode(m); }

    void setChannelMode(VuChannelMode m)
    {
        channelMode_ = m;
        ballistics_.resetAll();
        overload_.reset();
        repaint();
    }

    VuChannelMode getChannelMode() const noexcept { return channelMode_; }

    /** Latched peak max in dB. The panel can show this in a readout. */
    float getPeakMaxDb() const { return ballistics_.getPeakMaxDb(); }

    /** Reset the peak max latch (called when user clicks the PEAK MAX readout). */
    void resetPeakHold() { ballistics_.resetPeakHold(); repaint(); }

    /** True if the OL bulb is currently latched. */
    bool isOverloadLatched() const { return overload_.isLatched(); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto faceBounds = getFaceBounds();
        if (overloadRenderer_.hitTest(e.position, faceBounds))
        {
            overload_.reset();
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto faceBounds = getFaceBounds();
        if (faceBounds.getWidth() < 4.0f || faceBounds.getHeight() < 4.0f) return;

        const auto pivot = juce::Point<float>(
            faceBounds.getCentreX(),
            faceBounds.getBottom() + creamFace_.getConfig().cornerRadius);

        creamFace_.paint(g, faceBounds);
        redZone_.paint(g, pivot);
        markings_.paint(g, pivot);
        logo_.paint(g, faceBounds, juce::String::fromUTF8("peak \xc2\xb7 -17 dB/s"));
        overloadRenderer_.paint(g, faceBounds, overload_);
        needle_.paint(g, pivot, ballistics_.getNeedleDb());
    }

private:
    void timerCallback() override
    {
        if (source_ != nullptr)
        {
            const float peakL = source_->getPeakLevelL();
            const float peakR = source_->getPeakLevelR();
            const float combined = combineChannelPeaks(peakL, peakR, channelMode_);
            ballistics_.feed(combined);
            overload_.feed(combined);
        }
        repaint();
    }

    juce::Rectangle<float> getFaceBounds() const
    {
        return getLocalBounds().toFloat();
    }

    InputMeterCore*               source_ { nullptr };
    AnalogVuBallisticsCore        ballistics_;
    AnalogVuOverloadCore          overload_;
    VuChannelMode                 channelMode_ { VuChannelMode::MaxLR };
    AnalogVuCreamFaceRenderer     creamFace_;
    AnalogVuRedZoneRenderer       redZone_;
    AnalogVuScaleMarkingsRenderer markings_;
    AnalogVuLogoRenderer          logo_;
    AnalogVuOverloadRenderer      overloadRenderer_;
    AnalogVuNeedleRenderer        needle_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVuMeterComponent)
};

} // namespace DAW
