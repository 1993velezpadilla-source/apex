#pragma once
#include <JuceHeader.h>

#include "../C4Core/C4Processor.h"
#include "../C4Core/C4ResponseCurveCore.h"

namespace APEX {
namespace C4 {

// ============================================================================
// C4Editor — Phase 7: the final APEX C4 console interface.
//
// Product identity: CONSOLE CHARACTER EQ. The four bands are FOUR TALL
// VERTICAL CONSOLE STRIPS — the plugin reads TOP TO BOTTOM, like a channel
// EQ section (ergonomic reference: SSL / Harrison / Elysia module flow,
// reimagined as APEX, not a copy). The top utility row (INPUT / HPF / LPF /
// OUTPUT / AUTO GAIN / BYPASS / SPECTRUM) is deliberately SUPPORTING and
// visually quiet; the four vertical strips are the hero; BLOOM sits in its
// own bottom rail supporting the console architecture; the Spectrum Flag
// expands UNDERNEATH the console section.
//
// Performance: 60 FPS repaint from the analyzer snapshot; the analytic
// response curve recomputes at 30 Hz; no FFT on the GUI thread.
// ============================================================================

// ---------------------------------------------------------------------------
// The C4 visual identity (console brass on near-black; band roles).
// ---------------------------------------------------------------------------

struct C4Colors
{
    static const juce::Colour shellOuter()   { return juce::Colour (0xFF0A0908); }
    static const juce::Colour shellInner()   { return juce::Colour (0xFF12100E); }
    static const juce::Colour panel()        { return juce::Colour (0xFF17140F); }
    static const juce::Colour panelLine()    { return juce::Colour (0xFF2A241C); }
    static const juce::Colour brass()        { return juce::Colour (0xFFC9A45C); }
    static const juce::Colour brassDim()     { return juce::Colour (0xFF7A6338); }
    static const juce::Colour textPrimary()  { return juce::Colour (0xFFF2EDE2); }
    static const juce::Colour textSecondary(){ return juce::Colour (0xFF93897A); }

    // Band identities (foundation / body / incisive / air).
    static const juce::Colour weightCore()   { return juce::Colour (0xFF8C5A2E); }
    static const juce::Colour weightHalo()   { return juce::Colour (0xFFC88E54); }
    static const juce::Colour sculptCore()   { return juce::Colour (0xFFC8922E); }
    static const juce::Colour sculptHalo()   { return juce::Colour (0xFFEDC265); }
    static const juce::Colour biteCore()     { return juce::Colour (0xFFD8503C); }
    static const juce::Colour biteHalo()     { return juce::Colour (0xFFF07A5A); }
    static const juce::Colour openCore()     { return juce::Colour (0xFF4FB6D8); }
    static const juce::Colour openHalo()     { return juce::Colour (0xFF8FD8EE); }

    // BLOOM — the signature system control.
    static const juce::Colour bloomCore()    { return juce::Colour (0xFFE8C34A); }
    static const juce::Colour bloomHalo()    { return juce::Colour (0xFFF6E3A0); }
    static const juce::Colour bloomGlow()    { return juce::Colour (0xFF3A2E10); }

    // Analyzer panel.
    static const juce::Colour spectrumGrid() { return juce::Colour (0xFF1E1A14); }
    static const juce::Colour spectrumPre()  { return juce::Colour (0xFFB9C6CE); }
    static const juce::Colour spectrumPost() { return juce::Colour (0xFFE8A04C); }
    static const juce::Colour spectrumCurve(){ return juce::Colour (0xFFE8C34A); }

    static juce::Colour bandCore (int band)
    {
        switch (band)
        {
        case 0:  return weightCore();
        case 1:  return sculptCore();
        case 2:  return biteCore();
        default: return openCore();
        }
    }

    static juce::Colour bandHalo (int band)
    {
        switch (band)
        {
        case 0:  return weightHalo();
        case 1:  return sculptHalo();
        case 2:  return biteHalo();
        default: return openHalo();
        }
    }
};

// ---------------------------------------------------------------------------
// C4Knob — compact rotary with label + value readout (angle drag).
// ---------------------------------------------------------------------------

class C4Knob final : public juce::Component
{
public:
    using ChangeCallback = std::function<void (float normalized)>;

    C4Knob (const juce::String& label, juce::Colour core, juce::Colour halo,
            double minValue = 0.0, double maxValue = 1.0)
        : label_ (label), core_ (core), halo_ (halo), min_ (minValue), max_ (maxValue)
    {
        setRepaintsOnMouseActivity (true);
    }

    void setValue (float norm)
    {
        value_ = juce::jlimit (0.0f, 1.0f, norm);
        repaint();
    }

    float getValue() const noexcept { return value_; }

    void setText (const juce::String& t) { valueText_ = t; repaint(); }
    void setChangeCallback (ChangeCallback cb) { onChange_ = std::move (cb); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (2.0f);
        const float d = juce::jmin (r.getWidth(), r.getHeight() - 34.0f);
        const float cx = r.getCentreX();
        const float cy = r.getY() + 4.0f + d * 0.5f;

        g.setColour (core_.withAlpha (0.16f));
        g.fillEllipse (cx - d * 0.5f, cy - d * 0.5f, d, d);
        g.setColour (core_.withAlpha (0.85f));
        g.drawEllipse (cx - d * 0.5f, cy - d * 0.5f, d, d, 2.0f);

        const float start = juce::MathConstants<float>::pi * 0.75f;
        const float end = -juce::MathConstants<float>::pi * 0.75f;
        const float angle = start + (end - start) * value_;
        const float radius = d * 0.38f;

        juce::Path arc;
        const int steps = 40;
        for (int i = 0; i <= (int) (std::abs (value_) * steps); ++i)
        {
            const float a = start + (end - start) * ((float) i / steps);
            const float px = cx + radius * std::cos (a);
            const float py = cy - radius * std::sin (a);
            if (i == 0)
                arc.startNewSubPath (px, py);
            else
                arc.lineTo (px, py);
        }
        g.setColour (isMouseOverOrDragging() ? halo_ : core_);
        g.strokePath (arc, juce::PathStrokeType (3.0f));

        const float px = cx + radius * 0.85f * std::cos (angle);
        const float py = cy - radius * 0.85f * std::sin (angle);
        g.setColour (halo_.withAlpha (0.9f));
        g.drawLine (cx, cy, px, py, 2.5f);
        g.setColour (C4Colors::textPrimary());
        g.fillEllipse (cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);

        g.setFont (9.5f);
        g.setColour (C4Colors::textSecondary());
        g.drawText (label_, r.getX(), r.getBottom() - 32.0f, r.getWidth(), 12.0f,
                    juce::Justification::centred, false);
        g.setColour (C4Colors::textPrimary());
        g.drawText (valueText_.isNotEmpty() ? valueText_ : juce::String (value_, 2),
                    r.getX(), r.getBottom() - 18.0f, r.getWidth(), 12.0f,
                    juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent&) override { dragStartValue_ = value_; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float delta = (float) (e.getDistanceFromDragStartY() * 0.005f);
        value_ = juce::jlimit (0.0f, 1.0f, dragStartValue_ - delta);
        if (onChange_)
            onChange_ (value_);
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        value_ = (float) ((((max_ + min_) * 0.5) - min_) / (max_ - min_));
        if (onChange_)
            onChange_ (value_);
        repaint();
    }

private:
    juce::String label_;
    juce::Colour core_, halo_;
    double min_, max_;
    float value_ = 0.0f;
    float dragStartValue_ = 0.0f;
    juce::String valueText_;
    ChangeCallback onChange_;
};

// ---------------------------------------------------------------------------
// C4ConsoleFader — the real console gain control. Vertical by default
// (band GAIN, -15..+15 with tick marks + readout); horizontal variant for
// BLOOM (0..10, gold). Custom-drawn track/thumb; drag to move; double-click
// centers.
// ---------------------------------------------------------------------------

class C4ConsoleFader final : public juce::Component
{
public:
    using ChangeCallback = std::function<void (float normalized)>;

    C4ConsoleFader (juce::Colour core, juce::Colour halo, bool horizontal = false,
                    float valueMin = -15.0f, float valueMax = 15.0f)
        : core_ (core), halo_ (halo), horizontal_ (horizontal),
          min_ (valueMin), max_ (valueMax)
    {
        setRepaintsOnMouseActivity (true);
    }

    void setValue (float norm)
    {
        value_ = juce::jlimit (0.0f, 1.0f, norm);
        repaint();
    }

    float getValue() const noexcept { return value_; }
    void setChangeCallback (ChangeCallback cb) { onChange_ = std::move (cb); }

    /** Live readout (e.g. "+3.0 dB") drawn next to the thumb. Vertical
        faders only; the horizontal BLOOM fader has its own rail readout. */
    void setValueText (const juce::String& t)
    {
        valueText_ = t;
        repaint();
    }

    const juce::String& getValueText() const noexcept { return valueText_; }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (2.0f);
        const float trackW = horizontal_ ? juce::jmin (r.getHeight() * 0.32f, 12.0f)
                                         : juce::jmin (r.getWidth() * 0.30f, 14.0f);

        juce::Rectangle<float> track, thumb;
        if (horizontal_)
        {
            track = { r.getX(), r.getCentreY() - trackW * 0.5f, r.getWidth(), trackW };
            const float tx = r.getX() + (r.getWidth() - 26.0f) * value_;
            thumb = { tx, track.getY() - 8.0f, 26.0f, track.getHeight() + 16.0f };
        }
        else
        {
            track = { r.getCentreX() - trackW * 0.5f, r.getY(), trackW, r.getHeight() - 30.0f };
            const float ty = track.getY() + (track.getHeight() - 26.0f) * (1.0f - value_);
            thumb = { track.getX() - 12.0f, ty, track.getWidth() + 24.0f, 26.0f };
        }

        // Track + ticks.
        g.setColour (C4Colors::panelLine());
        g.fillRoundedRectangle (track, trackW * 0.5f);
        g.setFont (9.0f);
        const float tickMin = horizontal_ ? 0.0f : -15.0f;
        const float tickMax = horizontal_ ? 10.0f : 15.0f;
        const float tickStep = horizontal_ ? 1.0f : 3.0f;
        for (float v = tickMin; v <= tickMax + 0.01f; v += tickStep)
        {
            const float t = (v - min_) / (max_ - min_);
            if (horizontal_)
            {
                const float x = track.getX() + t * track.getWidth();
                g.setColour (C4Colors::brassDim());
                g.drawVerticalLine ((int) x, (int) track.getY() - 4, (int) track.getBottom() + 4);
                if ((int) v % 2 == 0)
                {
                    g.setColour (C4Colors::textSecondary());
                    g.drawText (juce::String ((int) v),
                                juce::Rectangle<float> (x - 14.0f, track.getBottom() + 6.0f, 28.0f, 12.0f),
                                juce::Justification::centred, false);
                }
            }
            else
            {
                const float y = track.getY() + (1.0f - t) * track.getHeight();
                g.setColour (C4Colors::brassDim());
                g.drawHorizontalLine ((int) y, (int) track.getX() - 4, (int) track.getRight() + 4);
                if ((int) v % 6 == 0)
                {
                    g.setColour (C4Colors::textSecondary());
                    g.drawText ((v >= 0.0f ? "+" : "-") + juce::String ((int) std::abs (v)),
                                juce::Rectangle<float> (track.getRight() + 4.0f, y - 7.0f, 26.0f, 14.0f),
                                juce::Justification::centredLeft, false);
                }
            }
        }

        // 0 dB line emphasized.
        {
            const float t = (0.0f - min_) / (max_ - min_);
            if (horizontal_)
            {
                const float x = track.getX() + t * track.getWidth();
                g.setColour (C4Colors::brass().withAlpha (0.6f));
                g.drawVerticalLine ((int) x, (int) track.getY() - 6, (int) track.getBottom() + 6);
            }
            else
            {
                const float y = track.getY() + (1.0f - t) * track.getHeight();
                g.setColour (C4Colors::brass().withAlpha (0.6f));
                g.drawHorizontalLine ((int) y, (int) track.getX() - 6, (int) track.getRight() + 6);
            }
        }

        // Thumb.
        g.setColour (isMouseOverOrDragging() ? halo_ : core_);
        g.fillRoundedRectangle (thumb, 4.0f);
        g.setColour (core_.darker (0.4f));
        g.drawRoundedRectangle (thumb, 4.0f, 1.0f);

        // Live dB readout, right-aligned immediately left of the thumb so it
        // travels WITH the handle while dragging (vertical faders only).
        if (! horizontal_ && valueText_.isNotEmpty())
        {
            const float textW = 52.0f;
            const float tx = thumb.getX() - textW - 6.0f;
            if (tx >= r.getX() - 2.0f)
            {
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.setColour (C4Colors::textPrimary());
                g.drawText (valueText_, juce::Rectangle<float> (tx, thumb.getCentreY() - 8.0f,
                                                               textW, 16.0f),
                            juce::Justification::centredRight, false);
            }
        }
    }

    void mouseDown (const juce::MouseEvent&) override { dragStartValue_ = value_; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float delta = (float) ((horizontal_ ? e.getDistanceFromDragStartX()
                                                  : -e.getDistanceFromDragStartY())
                                     * 0.004f);
        value_ = juce::jlimit (0.0f, 1.0f, dragStartValue_ + delta);
        if (onChange_)
            onChange_ (value_);
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        value_ = (float) ((((max_ + min_) * 0.5) - min_) / (max_ - min_));
        if (onChange_)
            onChange_ (value_);
        repaint();
    }

private:
    juce::Colour core_, halo_;
    bool horizontal_ = false;
    float min_ = -15.0f, max_ = 15.0f;
    float value_ = 0.5f;
    float dragStartValue_ = 0.5f;
    juce::String valueText_;
    ChangeCallback onChange_;
};

// ---------------------------------------------------------------------------
// C4BandStrip — ONE TALL VERTICAL CONSOLE STRIP. Header, FREQ knob, tall
// GAIN fader, then Q (SCULPT/BITE) or mode toggle (WEIGHT/OPEN). Reads top
// to bottom like a channel module.
// ---------------------------------------------------------------------------

class C4BandStrip final : public juce::Component
{
public:
    C4BandStrip (int band, C4Processor& proc, std::function<void()> onGesture)
        : band_ (band), proc_ (proc), onGesture_ (std::move (onGesture))
    {
        const juce::Colour core = C4Colors::bandCore (band);
        const juce::Colour halo = C4Colors::bandHalo (band);
        const auto* info = &APEX::C4::kBandInfos[band];

        nameLabel_.setText (juce::String (info->musicalName), juce::dontSendNotification);
        nameLabel_.setJustificationType (juce::Justification::centred);
        nameLabel_.setColour (juce::Label::textColourId, halo);
        nameLabel_.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        addAndMakeVisible (nameLabel_);

        const auto freqParam = param (freqIndexOf (band));
        const auto gainParam = param (gainIndexOf (band));

        freqKnob_ = std::make_unique<C4Knob> ("FREQ", core, halo,
                                              info->freqMinHz, info->freqMaxHz);
        freqKnob_->setValue (freqParam->getValue());
        freqKnob_->setText (freqParam->getText (freqParam->getValue(), 0));
        freqKnob_->setChangeCallback ([this] (float v)
        {
            auto* p = param (freqIndexOf (band_));
            p->setValueNotifyingHost (v);
            freqKnob_->setText (p->getText (v, 0));
            if (onGesture_) onGesture_();
        });
        addAndMakeVisible (freqKnob_.get());

        gainFader_ = std::make_unique<C4ConsoleFader> (core, halo);
        gainFader_->setValue (gainParam->getValue());
        gainFader_->setValueText (gainParam->getText (gainParam->getValue(), 0));
        gainFader_->setChangeCallback ([this] (float v)
        {
            auto* p = param (gainIndexOf (band_));
            p->setValueNotifyingHost (v);
            gainFader_->setValueText (p->getText (v, 0)); // live dB readout
            if (onGesture_) onGesture_();
        });
        addAndMakeVisible (gainFader_.get());

        gainLabel_.setText ("GAIN", juce::dontSendNotification);
        gainLabel_.setJustificationType (juce::Justification::centred);
        gainLabel_.setColour (juce::Label::textColourId, C4Colors::textSecondary());
        addAndMakeVisible (gainLabel_);

        if (band == (int) C4BandId::Sculpt || band == (int) C4BandId::Bite)
        {
            const auto qParam = param (band == (int) C4BandId::Sculpt
                                       ? C4ParamIndex::kSculptQ : C4ParamIndex::kBiteQ);
            qKnob_ = std::make_unique<C4Knob> ("Q", core, halo,
                                               qParam->getMinValue(), qParam->getMaxValue());
            qKnob_->setValue (qParam->getValue());
            qKnob_->setText (qParam->getText (qParam->getValue(), 0));
            qKnob_->setChangeCallback ([this] (float v)
            {
                auto* p = param (band_ == (int) C4BandId::Sculpt
                                 ? C4ParamIndex::kSculptQ : C4ParamIndex::kBiteQ);
                p->setValueNotifyingHost (v);
                qKnob_->setText (p->getText (v, 0));
                if (onGesture_) onGesture_();
            });
            addAndMakeVisible (qKnob_.get());
        }
        else
        {
            const auto modeParam = param (band == (int) C4BandId::Weight
                                          ? C4ParamIndex::kWeightMode : C4ParamIndex::kOpenMode);
            modeButton_.setButtonText ("BELL");
            modeButton_.onClick = [this]
            {
                auto* p = param (band_ == (int) C4BandId::Weight
                                 ? C4ParamIndex::kWeightMode : C4ParamIndex::kOpenMode);
                p->setValueNotifyingHost (p->getValue() >= 0.5f ? 0.0f : 1.0f);
                refresh();
                if (onGesture_) onGesture_();
            };
            addAndMakeVisible (modeButton_);
        }
    }

    void refresh()
    {
        const auto freqParam = param (freqIndexOf (band_));
        const auto gainParam = param (gainIndexOf (band_));
        freqKnob_->setValue (freqParam->getValue());
        freqKnob_->setText (freqParam->getText (freqParam->getValue(), 0));
        gainFader_->setValue (gainParam->getValue());
        gainFader_->setValueText (gainParam->getText (gainParam->getValue(), 0));

        if (band_ == (int) C4BandId::Sculpt || band_ == (int) C4BandId::Bite)
        {
            auto* p = param (band_ == (int) C4BandId::Sculpt
                             ? C4ParamIndex::kSculptQ : C4ParamIndex::kBiteQ);
            qKnob_->setValue (p->getValue());
            qKnob_->setText (p->getText (p->getValue(), 0));
        }
        else
        {
            auto* p = param (band_ == (int) C4BandId::Weight
                             ? C4ParamIndex::kWeightMode : C4ParamIndex::kOpenMode);
            const bool shelf = p->getValue() >= 0.5f;
            modeButton_.setButtonText (band_ == (int) C4BandId::Open
                                       ? (shelf ? "HALO" : "BELL")
                                       : (shelf ? "SHELF" : "BELL"));
            modeButton_.setColour (juce::TextButton::buttonColourId,
                                   shelf ? C4Colors::bandCore (band_)
                                         : juce::Colour (0xFF221E18));
        }
    }

    void resized() override
    {
        const auto r = getLocalBounds().reduced (6, 6);
        nameLabel_.setBounds (r.getX(), r.getY(), r.getWidth(), 24);

        const int knobW = juce::jmin (78, r.getWidth() - 8);
        const int knobX = r.getCentreX() - knobW / 2;

        freqKnob_->setBounds (knobX, r.getY() + 26, knobW, 84);

        gainLabel_.setBounds (r.getX(), r.getY() + 108, r.getWidth(), 14);
        const int faderTop = r.getY() + 122;
        const int faderBottom = r.getBottom() - 74;
        gainFader_->setBounds (r.getX() + 8, faderTop, r.getWidth() - 16, faderBottom - faderTop);

        if (qKnob_ != nullptr)
            qKnob_->setBounds (knobX, r.getBottom() - 78, knobW, 72);
        else
            modeButton_.setBounds (r.getX() + 14, r.getBottom() - 56, r.getWidth() - 28, 34);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (C4Colors::panel());
        g.fillRoundedRectangle (r, 8.0f);

        // Console module rails: brass side edges + identity accent.
        g.setColour (C4Colors::bandCore (band_).withAlpha (0.9f));
        g.fillRoundedRectangle (r.getX() + 8.0f, r.getY() + 2.0f,
                                r.getWidth() - 16.0f, 3.0f, 1.5f);
        g.setColour (C4Colors::brassDim().withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (C4Colors::panelLine());
        g.drawVerticalLine ((int) r.getX() + 4, (int) r.getY() + 6, (int) r.getBottom() - 6);
        g.drawVerticalLine ((int) r.getRight() - 4, (int) r.getY() + 6, (int) r.getBottom() - 6);
    }

private:
    static int freqIndexOf (int band)
    {
        return (int) C4ParamIndex::kWeightFreq + band * 3;
    }
    static int gainIndexOf (int band)
    {
        return (int) C4ParamIndex::kWeightGain + band * 3;
    }

    C4Parameter* param (int semanticIndex)
    {
        return proc_.getC4Parameter (semanticIndex);
    }

    int band_ = 0;
    C4Processor& proc_;
    std::function<void()> onGesture_;

    juce::Label nameLabel_;
    juce::Label gainLabel_;
    std::unique_ptr<C4Knob> freqKnob_;
    std::unique_ptr<C4Knob> qKnob_;
    std::unique_ptr<C4ConsoleFader> gainFader_;
    juce::TextButton modeButton_;
};

// ---------------------------------------------------------------------------
// C4BloomSection — the bottom console rail: BLOOM 0..10 as a wide gold
// fader with a soft glow. Important, but supports the strip architecture.
// ---------------------------------------------------------------------------

class C4BloomSection final : public juce::Component
{
public:
    C4BloomSection (C4Processor& proc)
        : proc_ (proc),
          fader_ (C4Colors::bloomCore(), C4Colors::bloomHalo(), true, 0.0f, 10.0f)
    {
        titleLabel_.setText ("BLOOM", juce::dontSendNotification);
        titleLabel_.setJustificationType (juce::Justification::centredLeft);
        titleLabel_.setColour (juce::Label::textColourId, C4Colors::bloomHalo());
        titleLabel_.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        addAndMakeVisible (titleLabel_);

        auto* bloom = proc.getC4Parameter (C4ParamIndex::kBloom);
        fader_.setValue (bloom->getValue());
        fader_.setChangeCallback ([this] (float v)
        {
            auto* p = proc_.getC4Parameter (C4ParamIndex::kBloom);
            p->setValueNotifyingHost (v);
            valueLabel_.setText (p->getText (v, 0), juce::dontSendNotification);
        });
        addAndMakeVisible (fader_);

        valueLabel_.setText (bloom->getText (bloom->getValue(), 0), juce::dontSendNotification);
        valueLabel_.setJustificationType (juce::Justification::centred);
        valueLabel_.setColour (juce::Label::textColourId, C4Colors::textPrimary());
        valueLabel_.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (valueLabel_);

        hintLabel_.setText ("the C4 system character - not a drive control", juce::dontSendNotification);
        hintLabel_.setJustificationType (juce::Justification::centred);
        hintLabel_.setColour (juce::Label::textColourId, C4Colors::textSecondary());
        hintLabel_.setFont (juce::FontOptions (9.0f));
        addAndMakeVisible (hintLabel_);
    }

    void refresh()
    {
        auto* p = proc_.getC4Parameter (C4ParamIndex::kBloom);
        fader_.setValue (p->getValue());
        valueLabel_.setText (p->getText (p->getValue(), 0), juce::dontSendNotification);
    }

    void resized() override
    {
        const auto r = getLocalBounds().reduced (14, 6);
        titleLabel_.setBounds (r.getX(), r.getY(), 90, 26);
        valueLabel_.setBounds (r.getRight() - 70, r.getY(), 70, 26);
        fader_.setBounds (r.getX() + 100, r.getY() + 4, r.getWidth() - 190, r.getHeight() - 22);
        hintLabel_.setBounds (r.getX(), r.getBottom() - 16, r.getWidth(), 12);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        // Soft gold glow behind the rail (subtle — the console stays dark).
        juce::ColourGradient glow (C4Colors::bloomGlow().withAlpha (0.5f),
                                   r.getCentreX(), r.getCentreY(),
                                   C4Colors::bloomGlow().withAlpha (0.0f),
                                   r.getCentreX(), r.getY(), false);
        g.setGradientFill (glow);
        g.fillAll();
        g.setColour (C4Colors::panel());
        g.fillRoundedRectangle (r.reduced (2.0f), 7.0f);
        g.setColour (C4Colors::bloomCore().withAlpha (0.4f));
        g.drawRoundedRectangle (r.reduced (2.5f), 7.0f, 1.5f);
    }

private:
    C4Processor& proc_;
    C4ConsoleFader fader_;
    juce::Label titleLabel_;
    juce::Label valueLabel_;
    juce::Label hintLabel_;
};

// ---------------------------------------------------------------------------
// C4SpectrumPanel — unchanged (expands UNDERNEATH the console).
// ---------------------------------------------------------------------------

class C4SpectrumPanel final : public juce::Component,
                              private juce::Timer
{
public:
    static constexpr int kCurvePoints = 512;

    C4SpectrumPanel (C4Processor& proc)
        : proc_ (proc), curveDb_ (kCurvePoints, 0.0f)
    {
        startTimerHz (60);
    }

    ~C4SpectrumPanel() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (4.0f);
        g.setColour (C4Colors::spectrumGrid());
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (C4Colors::panelLine());
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        const float fMin = 20.0f, fMax = 20000.0f;
        const float dbTop = 12.0f, dbBottom = -84.0f;
        const float plotL = r.getX() + 8.0f, plotR = r.getRight() - 8.0f;
        const float plotT = r.getY() + 8.0f, plotB = r.getBottom() - 24.0f;

        const auto freqToX = [&] (float f)
        {
            const float t = std::log (f / fMin) / std::log (fMax / fMin);
            return plotL + t * (plotR - plotL);
        };
        const auto dbToY = [&] (float db)
        {
            const float t = (db - dbBottom) / (dbTop - dbBottom);
            return plotB - juce::jlimit (0.0f, 1.0f, t) * (plotB - plotT);
        };

        g.setFont (9.0f);
        for (int oct = 20; oct <= 20000; oct *= 10)
        {
            for (int f = oct; f < oct * 10; f += oct)
            {
                const float x = freqToX ((float) f);
                g.setColour (C4Colors::panelLine());
                g.drawVerticalLine ((int) x, (int) plotT, (int) plotB);
                g.setColour (C4Colors::textSecondary());
                g.drawText (f >= 1000 ? juce::String (f / 1000) + "k"
                                      : juce::String (f),
                            juce::Rectangle<float> (x - 14.0f, plotB + 4.0f, 28.0f, 12.0f),
                            juce::Justification::centred, false);
            }
        }
        g.setColour (C4Colors::brassDim());
        g.drawHorizontalLine ((int) dbToY (0.0f), (int) plotL, (int) plotR);

        juce::Path curve;
        for (int i = 0; i < kCurvePoints; ++i)
        {
            const float f = fMin * std::pow (fMax / fMin, (float) i / (kCurvePoints - 1));
            const float x = freqToX (f);
            const float y = dbToY (curveDb_[(size_t) i]);
            if (i == 0)
                curve.startNewSubPath (x, y);
            else
                curve.lineTo (x, y);
        }
        g.setColour (C4Colors::spectrumCurve());
        g.strokePath (curve, juce::PathStrokeType (1.8f));

        const auto snap = proc_.getSpectrumCore().getSnapshot();
        if (snap.index > 0 && snap.binHz > 0.0f)
        {
            auto drawSpectrum = [&] (const float* db, juce::Colour c)
            {
                if (db == nullptr)
                    return;
                juce::Path p;
                bool started = false;
                for (int b = 1; b < C4SpectrumCore::kSpectrumBins - 1; ++b)
                {
                    const float f = b * snap.binHz;
                    if (f < fMin || f > fMax)
                        continue;
                    const float x = freqToX (f);
                    const float y = dbToY (db[b]);
                    if (! started)
                    {
                        p.startNewSubPath (x, y);
                        started = true;
                    }
                    else
                        p.lineTo (x, y);
                }
                g.setColour (c.withAlpha (0.85f));
                g.strokePath (p, juce::PathStrokeType (1.2f));
            };

            if (snap.preActive)
                drawSpectrum (snap.preDb, C4Colors::spectrumPre());
            if (snap.postActive)
                drawSpectrum (snap.postDb, C4Colors::spectrumPost());
        }

        g.setFont (9.0f);
        g.setColour (C4Colors::textSecondary());
        g.drawText ("PRE (silver)  POST (amber)  RESPONSE (gold)  - observational only",
                    juce::Rectangle<float> (plotL, plotB + 16.0f, plotR - plotL, 12.0f),
                    juce::Justification::centredLeft, false);
    }

private:
    void timerCallback() override
    {
        static int tick = 0;
        if (++tick % 2 == 0)
        {
            using APEX::C4::ResponseCurve::BandState;
            using APEX::C4::ResponseCurve::FilterState;
            using APEX::C4::ResponseCurve::ContourState;

            BandState bands[kNumBands];
            FilterState filters;
            ContourState contours[kNumBands - 1];
            const auto& eng = proc_.getEngine();
            for (int b = 0; b < kNumBands; ++b)
            {
                bands[b].freqHz = eng.getSmoothedBandFreqHz (b);
                bands[b].gainDb = eng.getSmoothedBandGainDb (b);
                bands[b].q = eng.getSmoothedBandQ (b);
                bands[b].modeBlend = eng.getSmoothedBandModeBlend (b);
                bands[b].highShelf = (b == (int) C4BandId::Open);
            }
            filters.hpfHz = eng.getSmoothedHpfHz();
            filters.hpfMix = eng.getSmoothedHpfMix();
            filters.lpfHz = eng.getSmoothedLpfHz();
            filters.lpfMix = eng.getSmoothedLpfMix();
            for (int p = 0; p < kNumBands - 1; ++p)
            {
                contours[p].gainDb = eng.getContourGainDb (p);
                contours[p].freqHz = eng.getContourFreqHz (p);
                contours[p].q = eng.getCouplingQ();
            }
            APEX::C4::ResponseCurve::fillCurve (
                curveDb_.data(), kCurvePoints, 20.0, 20000.0, rateOf(),
                eng.getSmoothedInputDb(), eng.getSmoothedOutputDb(),
                filters, bands, contours);
        }
        repaint();
    }

    double rateOf() const noexcept { return proc_.getSampleRate() > 0.0 ? proc_.getSampleRate() : 48000.0; }

    C4Processor& proc_;
    std::vector<float> curveDb_;
};

// ---------------------------------------------------------------------------
// C4Editor — the vertical console assembly.
// ---------------------------------------------------------------------------

class C4Editor final : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit C4Editor (C4Processor& proc)
        : juce::AudioProcessorEditor (proc), proc_ (proc)
    {
        auto addTrim = [&] (const juce::String& name, C4ParamIndex idx,
                            juce::Colour core, juce::Colour halo)
        {
            auto* p = proc.getC4Parameter (idx);
            auto knob = std::make_unique<C4Knob> (name, core, halo, p->getMinValue(), p->getMaxValue());
            knob->setValue (p->getValue());
            knob->setText (p->getText (p->getValue(), 0));
            auto* raw = knob.get();
            knobs_.push_back ({ idx, std::move (knob) });
            raw->setChangeCallback ([this, idx, raw] (float v)
            {
                auto* p = proc_.getC4Parameter (idx);
                p->setValueNotifyingHost (v);
                raw->setText (p->getText (v, 0));
            });
            addAndMakeVisible (raw);
            return raw;
        };

        inputKnob_ = addTrim ("INPUT", C4ParamIndex::kInput, C4Colors::brass(), C4Colors::brass());
        hpfKnob_ = addTrim ("HPF", C4ParamIndex::kHpf, C4Colors::openCore(), C4Colors::openHalo());
        lpfKnob_ = addTrim ("LPF", C4ParamIndex::kLpf, C4Colors::openCore(), C4Colors::openHalo());
        outputKnob_ = addTrim ("OUTPUT", C4ParamIndex::kOutput, C4Colors::brass(), C4Colors::brass());

        autoGainButton_.setButtonText ("AUTO GAIN");
        autoGainButton_.onClick = [this]
        {
            auto* p = proc_.getC4Parameter (C4ParamIndex::kAutoGain);
            p->setValueNotifyingHost (p->getValue() >= 0.5f ? 0.0f : 1.0f);
            refresh();
        };
        addAndMakeVisible (autoGainButton_);

        bypassButton_.setButtonText ("BYPASS");
        bypassButton_.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF3A1414));
        bypassButton_.onClick = [this]
        {
            auto* p = proc_.getC4Parameter (C4ParamIndex::kBypass);
            p->setValueNotifyingHost (p->getValue() >= 0.5f ? 0.0f : 1.0f);
            refresh();
        };
        addAndMakeVisible (bypassButton_);

        spectrumCombo_.addItem ("SPECTRUM: CLOSED", 1);
        spectrumCombo_.addItem ("SPECTRUM: PRE", 2);
        spectrumCombo_.addItem ("SPECTRUM: POST", 3);
        spectrumCombo_.addItem ("SPECTRUM: BOTH", 4);
        spectrumCombo_.setSelectedId ((int) proc.getSpectrumMode() + 1, juce::dontSendNotification);
        spectrumCombo_.onChange = [this]
        {
            applySpectrumMode ((C4SpectrumTapMode) (spectrumCombo_.getSelectedId() - 1));
        };
        addAndMakeVisible (spectrumCombo_);

        // THE FOUR TALL VERTICAL CONSOLE STRIPS — the hero of the plugin.
        for (int b = 0; b < kNumBands; ++b)
        {
            auto strip = std::make_unique<C4BandStrip> (b, proc_, [] {});
            bandStrips_.push_back (std::move (strip));
            addAndMakeVisible (bandStrips_.back().get());
        }

        bloomSection_ = std::make_unique<C4BloomSection> (proc_);
        addAndMakeVisible (bloomSection_.get());

        spectrumPanel_ = std::make_unique<C4SpectrumPanel> (proc_);
        addChildComponent (spectrumPanel_.get());
        spectrumPanel_->setVisible (proc.getSpectrumMode() != C4SpectrumTapMode::Closed);

        // Tall console proportions (compact CLOSED) and a sane resize range.
        // Minimum height = the FULL closed console (640): the host can never
        // shrink the strips below their complete control set.
        setResizable (true, nullptr);
        setResizeLimits (560, 640, 1100, 1000);

        setSize (660, 640);
        startTimerHz (30);
        refresh();
    }

    ~C4Editor() override
    {
        stopTimer();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (C4Colors::shellOuter());
        g.setColour (C4Colors::shellInner());
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 6.0f);
        g.setColour (C4Colors::brassDim());
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (4.0f), 6.0f, 1.0f);

        g.setColour (C4Colors::brass());
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("APEX C4", juce::Rectangle<int> (14, 2, 200, 24),
                    juce::Justification::centredLeft, false);
        g.setFont (juce::FontOptions (9.0f));
        g.setColour (C4Colors::textSecondary());
        g.drawText ("CONSOLE CHARACTER EQ", juce::Rectangle<int> (220, 6, 300, 20),
                    juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 30);
        const bool spectrumOpen = proc_.getSpectrumMode() != C4SpectrumTapMode::Closed;

        // ---- Top utility row (supporting, quiet) --------------------------
        auto rail = r.removeFromTop (64);
        const int knobW = 58;
        inputKnob_->setBounds (rail.removeFromLeft (knobW).reduced (2, 2));
        hpfKnob_->setBounds (rail.removeFromLeft (knobW).reduced (2, 2));
        lpfKnob_->setBounds (rail.removeFromLeft (knobW).reduced (2, 2));
        outputKnob_->setBounds (rail.removeFromLeft (knobW).reduced (2, 2));

        auto railRight = rail.removeFromRight (150);
        spectrumCombo_.setBounds (railRight.reduced (2, 16));
        railRight = rail.removeFromRight (150);
        bypassButton_.setBounds (railRight.reduced (8, 10).removeFromLeft (68));
        autoGainButton_.setBounds (railRight.reduced (8, 10).removeFromLeft (78));

        // ---- The console body: the four strips ALWAYS get the full console
        //      height (430). The Spectrum Flag NEVER compresses them — it
        //      takes the remaining space BELOW the BLOOM rail instead.
        const int bloomH = 104;
        const int bandAreaH = 430;
        auto bandsArea = r.removeFromTop (bandAreaH);
        const int stripW = (bandsArea.getWidth() - 24) / 4;
        for (int b = 0; b < kNumBands; ++b)
            bandStrips_[(size_t) b]->setBounds (
                bandsArea.getX() + 4 + b * (stripW + 8), bandsArea.getY(),
                stripW, bandsArea.getHeight());

        // ---- BLOOM bottom rail --------------------------------------------
        bloomSection_->setBounds (r.removeFromTop (bloomH).reduced (2, 2));

        // ---- Spectrum Flag: an EXTENSION of C4 — everything left below the
        //      console belongs to the analyzer (flex height).
        if (spectrumOpen)
            spectrumPanel_->setBounds (r.reduced (2, 2));
    }

    void refresh()
    {
        for (auto& kb : knobs_)
        {
            auto* p = proc_.getC4Parameter (kb.index);
            kb.knob->setValue (p->getValue());
            kb.knob->setText (p->getText (p->getValue(), 0));
        }
        for (auto& s : bandStrips_)
            s->refresh();
        bloomSection_->refresh();
        autoGainButton_.setToggleState (
            proc_.getC4Parameter (C4ParamIndex::kAutoGain)->getValue() >= 0.5f,
            juce::dontSendNotification);
        bypassButton_.setToggleState (
            proc_.getC4Parameter (C4ParamIndex::kBypass)->getValue() >= 0.5f,
            juce::dontSendNotification);
    }

private:
    void timerCallback() override { refresh(); }

public:
    // ---- Visual-review accessors (Phase 7 preview harness; read-only) ----

    void applySpectrumMode (C4SpectrumTapMode mode)
    {
        proc_.setSpectrumMode (mode);
        spectrumCombo_.setSelectedId ((int) mode + 1, juce::dontSendNotification);
        spectrumPanel_->setVisible (mode != C4SpectrumTapMode::Closed);

        // The analyzer EXTENDS C4 downward: opening grows the window to the
        // expanded height, closing restores the compact console height. The
        // user's width is always preserved; the strips are never compressed.
        if (mode == C4SpectrumTapMode::Closed)
            setSize (getWidth(), 640);
        else if (getHeight() < 880)
            setSize (getWidth(), 880);
        resized();
    }

    juce::Component* getBandStripForPreview (int band)
    {
        return band >= 0 && band < (int) bandStrips_.size()
            ? bandStrips_[(size_t) band].get() : nullptr;
    }

    juce::Component* getSpectrumPanelForPreview() { return spectrumPanel_.get(); }
    juce::Component* getBloomSectionForPreview() { return bloomSection_.get(); }

private:
    struct KnobEntry
    {
        C4ParamIndex index;
        std::unique_ptr<C4Knob> knob;
    };

    C4Processor& proc_;

    std::vector<KnobEntry> knobs_;
    C4Knob* inputKnob_ = nullptr;
    C4Knob* hpfKnob_ = nullptr;
    C4Knob* lpfKnob_ = nullptr;
    C4Knob* outputKnob_ = nullptr;
    juce::TextButton autoGainButton_;
    juce::TextButton bypassButton_;
    juce::ComboBox spectrumCombo_;
    std::vector<std::unique_ptr<C4BandStrip>> bandStrips_;
    std::unique_ptr<C4BloomSection> bloomSection_;
    std::unique_ptr<C4SpectrumPanel> spectrumPanel_;
};

} // namespace C4
} // namespace APEX
