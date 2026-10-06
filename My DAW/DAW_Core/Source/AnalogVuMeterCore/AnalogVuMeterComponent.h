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
 *   - the shared audio-clock VU reading (no second UI smoothing pass)
 *   - a per-instance OverloadCore (latch state)
 *   - the six stateless renderers (cream face, red zone, scale markings,
 *     logo, overload bulb, needle)
 *
 * Per-track uniqueness: the source pointer + the per-instance ballistics
 * and overload state are reset whenever setSource() rebinds the meter to
 * a different InputMeterCore — switching tracks does not bleed needle
 * history or OL latch.
 *
 * UI rate: 60 Hz only requests a repaint; audio rate determines ballistics.
 *
 * Click-to-reset: clicking anywhere clears held VU/peak maxima and OL.
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
        if (source_ != nullptr) source_->setVuChannelMode(m);
        ballistics_.resetAll();
        overload_.reset();
        repaint();
    }

    VuChannelMode getChannelMode() const noexcept
    {
        return source_ != nullptr ? source_->getVuChannelMode() : channelMode_;
    }

    float getNeedleDb() const noexcept
    {
        if (source_ == nullptr) return -120.0f;
        return ballistics_.getMode() == AnalogVuBallisticsCore::Mode::ClassicVu
            ? source_->getVuDb() : ballistics_.getNeedleDb();
    }
    float getVuMaxDb() const noexcept { return source_ != nullptr ? source_->getVuMaxDb() : -120.0f; }

    /** Latched peak max in dB. The panel can show this in a readout. */
    float getPeakMaxDb() const { return source_ != nullptr ? source_->getSamplePeakMaxDb() : -120.0f; }

    /** Reset the dBFS peak maximum and the overload latch. */
    void resetPeakHold()
    {
        if (source_ != nullptr)
        {
            source_->resetPeakHold();
            source_->resetVuMax();
        }
        ballistics_.resetPeakHold();
        overload_.reset();
        repaint();
    }

    /** True if the OL bulb is currently latched. */
    bool isOverloadLatched() const { return overload_.isLatched(); }

    void mouseDown(const juce::MouseEvent&) override
    {
        resetPeakHold();
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
        const float reference = source_ != nullptr ? source_->getVuReferenceDb() : -18.0f;
        logo_.paint(g, faceBounds, "0 VU = " + juce::String(reference, 0) + " dBFS");
        overloadRenderer_.paint(g, faceBounds, overload_);
        needle_.paint(g, pivot, getNeedleDb());
    }

private:
    void timerCallback() override
    {
        if (source_ != nullptr)
        {
            const float peakL = source_->getPeakMaxLevelL();
            const float peakR = source_->getPeakMaxLevelR();
            if (ballistics_.getMode() != AnalogVuBallisticsCore::Mode::ClassicVu)
                ballistics_.feed(combineChannelPeaks(source_->getRmsLevelL(), source_->getRmsLevelR(),
                                                     getChannelMode()) * 1.41421356237f,
                                 juce::jmax(peakL, peakR));
            overload_.feed(juce::jmax(peakL, peakR));
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
    VuChannelMode                 channelMode_ { VuChannelMode::Average };
    AnalogVuCreamFaceRenderer     creamFace_;
    AnalogVuRedZoneRenderer       redZone_;
    AnalogVuScaleMarkingsRenderer markings_;
    AnalogVuLogoRenderer          logo_;
    AnalogVuOverloadRenderer      overloadRenderer_;
    AnalogVuNeedleRenderer        needle_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVuMeterComponent)
};

} // namespace DAW
