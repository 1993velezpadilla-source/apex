#pragma once
#include <JuceHeader.h>
#include "../FloatingWindowCore/FloatingWindowBase.h"
#include "../ClipCore/Clip.h"
#include "../MidiCore/MidiClip.h"
#include "../AudioEngineCore/AudioFileManager.h"
#include "../PluginHostCore/ClipRegionPluginCore.h"
#include "../AutomationCore/AutomationManagerCore.h"
#include "../AutomationCore/AutomationLaneCore.h"
#include "../TransportCore/TransportController.h"
#include "../ThemeCore/Theme.h"
#include "ClipFxFloatingPanel.h"
#include "../../Builds/VisualStudio2026/ArrangementEditor/ClipAutomationPanel.h"
#include "CursorThemeCore.h"

namespace DAW {

// ============================================================================
//  Design tokens
// ============================================================================
namespace CPW {
    inline juce::Colour C(uint32_t argb) { return juce::Colour(argb); }
    constexpr uint32_t kBg        = 0xFF111113;
    constexpr uint32_t kSurface   = 0xFF191920;
    constexpr uint32_t kSurface2  = 0xFF222228;
    constexpr uint32_t kBorder    = 0xFF2C2C34;
    constexpr uint32_t kBorderHi  = 0xFF44444F;
    constexpr uint32_t kText      = 0xFFE8E8EC;
    constexpr uint32_t kTextDim   = 0xFF666672;
    constexpr uint32_t kAccent    = 0xFF5BC8FF;   // icy cyan
    constexpr uint32_t kGold      = 0xFFFFB347;   // warm amber
    constexpr uint32_t kGreen     = 0xFF52E09A;
    constexpr uint32_t kPink      = 0xFFFF6B9D;
    constexpr uint32_t kPurple    = 0xFFAA88FF;

    inline void microLabel(juce::Graphics& g, juce::Rectangle<float> r,
                           const juce::String& t, uint32_t col = kTextDim)
    {
        g.setColour(C(col));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.5f).withStyle("Bold")));
        g.drawText(t.toUpperCase(), r, juce::Justification::centred, false);
    }
}

// ============================================================================
//  Premium knob LookAndFeel
// ============================================================================
class CPWKnobLF : public DAW::ApexCursorLookAndFeel
{
public:
    uint32_t arcColor = CPW::kAccent;

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int w, int h,
                          float pos, float startA, float endA,
                          juce::Slider&) override
    {
        const float cx = x + w * 0.5f, cy = y + h * 0.5f;
        const float r  = juce::jmin(w, h) * 0.5f - 5.f;

        // Track (darker, more refined)
        juce::Path track;
        track.addCentredArc(cx, cy, r, r, 0.f, startA, endA, true);
        g.setColour(CPW::C(0xFF1A1A22));
        g.strokePath(track, juce::PathStrokeType(2.8f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Fill arc (refined gradient)
        const float fillEnd = startA + (endA - startA) * pos;
        if (fillEnd > startA + 0.001f)
        {
            juce::Path fill;
            fill.addCentredArc(cx, cy, r, r, 0.f, startA, fillEnd, true);
            juce::ColourGradient grad(CPW::C(arcColor).withAlpha(1.0f), cx, cy - r,
                                      CPW::C(arcColor).withAlpha(0.55f), cx, cy + r, false);
            g.setGradientFill(grad);
            g.strokePath(fill, juce::PathStrokeType(2.8f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Tip dot (refined)
        const float tx = cx + r * std::sin(fillEnd);
        const float ty = cy - r * std::cos(fillEnd);
        g.setColour(CPW::C(arcColor).brighter(0.15f));
        g.fillEllipse(tx - 3.2f, ty - 3.2f, 6.4f, 6.4f);
        g.setColour(CPW::C(arcColor).withAlpha(0.28f));
        g.fillEllipse(tx - 6.5f, ty - 6.5f, 13.f, 13.f);

        // Cap (refined darker matte premium)
        juce::ColourGradient cap(CPW::C(0xFF0E0E12), cx, cy - r * 0.4f,
                                 CPW::C(0xFF060609), cx, cy + r * 0.5f, false);
        g.setGradientFill(cap);
        g.fillEllipse(cx - r * 0.54f, cy - r * 0.54f, r * 1.08f, r * 1.08f);
        g.setColour(CPW::C(0xFF1C1C24));
        g.drawEllipse(cx - r * 0.54f, cy - r * 0.54f, r * 1.08f, r * 1.08f, 1.2f);
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.drawEllipse(cx - r * 0.54f + 1.f, cy - r * 0.54f + 1.f, r * 1.08f - 2.f, r * 1.08f - 2.f, 0.8f);
    }

    juce::Label* createSliderTextBox(juce::Slider& s) override
    {
        auto* lbl = LookAndFeel_V4::createSliderTextBox(s);
        lbl->setColour(juce::Label::textColourId, CPW::C(CPW::kText));
        lbl->setColour(juce::Label::backgroundColourId, CPW::C(0xFF0A0A0E));
        lbl->setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
        return lbl;
    }
};

class CPWActivePluginRow : public juce::Component
{
public:
    juce::String name, manufacturer, format;
    bool bypassed = false;
    std::function<void()> onOpen;
    std::function<void()> onToggleBypass;
    std::function<void()> onRemove;

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(2.f, 1.5f);
        const auto accent = bypassed ? CPW::C(CPW::kTextDim) : CPW::C(CPW::kAccent);

        juce::ColourGradient bg(CPW::C(0xFF14151A), b.getX(), b.getY(),
                                CPW::C(0xFF0A0B0E), b.getX(), b.getBottom(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(b, 4.f);

        g.setColour(accent.withAlpha(isMouseOver() ? 0.85f : 0.52f));
        g.drawRoundedRectangle(b, 4.f, 1.f);
        g.setColour(accent.withAlpha(bypassed ? 0.2f : 0.52f));
        g.fillRoundedRectangle(b.withWidth(2.5f), 1.5f);

        auto area = getLocalBounds().reduced(9, 0);
        auto right = area.removeFromRight(60);
        auto bypass = right.removeFromLeft(34).reduced(2, 5);
        auto remove = right.removeFromLeft(22).reduced(2, 5);

        g.setColour(bypassed ? CPW::C(CPW::kTextDim) : CPW::C(CPW::kText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.8f).withStyle("Bold")));
        g.drawText(name, area.removeFromTop(getHeight() / 2 + 1), juce::Justification::centredLeft, true);
        g.setColour(CPW::C(CPW::kTextDim).withAlpha(0.82f));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.0f)));
        g.drawText(format + " - " + manufacturer, area, juce::Justification::centredLeft, true);

        g.setColour(bypassed ? CPW::C(CPW::kGold).withAlpha(0.2f) : CPW::C(CPW::kGreen).withAlpha(0.22f));
        g.fillRoundedRectangle(bypass.toFloat(), 3.f);
        g.setColour(bypassed ? CPW::C(CPW::kGold) : CPW::C(CPW::kGreen));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(7.4f).withStyle("Bold")));
        g.drawText(bypassed ? "BYP" : "ON", bypass, juce::Justification::centred);

        g.setColour(CPW::C(CPW::kPink).withAlpha(0.16f));
        g.fillRoundedRectangle(remove.toFloat(), 3.f);
        g.setColour(CPW::C(CPW::kPink).withAlpha(0.95f));
        g.drawText("x", remove, juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto right = getLocalBounds().reduced(9, 0).removeFromRight(60);
        auto bypass = right.removeFromLeft(34).reduced(2, 5);
        auto remove = right.removeFromLeft(22).reduced(2, 5);
        if (remove.contains(e.position.toInt())) { if (onRemove) onRemove(); return; }
        if (bypass.contains(e.position.toInt())) { if (onToggleBypass) onToggleBypass(); return; }
        if (onOpen) onOpen();
    }

    void mouseDoubleClick(const juce::MouseEvent&) override { if (onOpen) onOpen(); }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
};

// ============================================================================
//  Knob cell
// ============================================================================
class CPWKnobCell : public juce::Component
{
public:
    juce::Slider slider;

    CPWKnobCell(const juce::String& lbl, uint32_t col = CPW::kAccent)
        : label_(lbl)
    {
        lf_.arcColor = col;
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 14);
        slider.setLookAndFeel(&lf_);
        addAndMakeVisible(slider);
    }

    ~CPWKnobCell() override { slider.setLookAndFeel(nullptr); }

    void resized() override { slider.setBounds(getLocalBounds().withTrimmedBottom(13)); }
    void paint(juce::Graphics& g) override
    {
        CPW::microLabel(g, juce::Rectangle<float>(0.f, (float)(getHeight()-13), (float)getWidth(), 13.f), label_);
    }

private:
    juce::String label_;
    CPWKnobLF lf_;
};

// ============================================================================
//  Pill toggle button
// ============================================================================
class CPWPillBtn : public juce::Component
{
public:
    bool active = false;
    uint32_t activeCol;
    juce::String label;
    std::function<void()> onClick;

    CPWPillBtn(const juce::String& l, uint32_t col = CPW::kAccent)
        : label(l), activeCol(col) {}

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        const bool hot = isMouseOver();
        juce::ColourGradient bg(
            active ? CPW::C(activeCol).withAlpha(0.32f) : CPW::C(CPW::kSurface2).withAlpha(hot ? 1.f : 0.8f),
            b.getX(), b.getY(),
            active ? CPW::C(activeCol).withAlpha(0.14f) : CPW::C(CPW::kSurface).withAlpha(hot ? 1.f : 0.8f),
            b.getX(), b.getBottom(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(b, b.getHeight() * 0.5f);
        g.setColour(active ? CPW::C(activeCol).withAlpha(0.75f) : CPW::C(CPW::kBorderHi).withAlpha(hot ? 0.65f : 0.38f));
        g.drawRoundedRectangle(b, b.getHeight() * 0.5f, 1.f);
        g.setColour(active ? CPW::C(activeCol) : CPW::C(hot ? CPW::kText : CPW::kTextDim));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.5f).withStyle("Bold")));
        g.drawText(label.toUpperCase(), getLocalBounds(), juce::Justification::centred, false);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseUp   (const juce::MouseEvent&) override { repaint(); }
};

class CPWCurveBtn : public juce::Component
{
public:
    std::function<void()> onClick;

    void setCurve(int curve)
    {
        curve_ = juce::jlimit(0, 3, curve);
        repaint();
    }

    int getCurve() const noexcept { return curve_; }

    static juce::String getCurveName(int curve)
    {
        switch (juce::jlimit(0, 3, curve))
        {
            case 1:  return "EXP";
            case 2:  return "LOG";
            case 3:  return "S";
            default: return "LIN";
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        const auto accent = CPW::C(CPW::kGold);
        g.setColour(CPW::C(CPW::kSurface2).withAlpha(isMouseOver() ? 1.0f : 0.78f));
        g.fillRoundedRectangle(b, 3.0f);
        g.setColour(accent.withAlpha(isMouseOver() ? 0.8f : 0.45f));
        g.drawRoundedRectangle(b, 3.0f, 1.0f);
        g.setColour(accent);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.2f).withStyle("Bold")));
        g.drawText(getCurveName(curve_), getLocalBounds(), juce::Justification::centred, false);
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }

private:
    int curve_ = 0;
};

// ============================================================================
//  Separator
// ============================================================================
class CPWSep : public juce::Component
{
public:
    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        juce::ColourGradient gr(CPW::C(CPW::kBorderHi).withAlpha(0.0f), b.getX(), 0,
                                CPW::C(CPW::kBorderHi).withAlpha(0.4f),  b.getCentreX(), 0, false);
        gr.addColour(1.0, CPW::C(CPW::kBorderHi).withAlpha(0.0f));
        g.setGradientFill(gr);
        g.fillRect(b.withHeight(1.f));
    }
};

// ============================================================================
//  Waveform preview
// ============================================================================
class CPWWavePreview : public juce::Component
{
public:
    CPWWavePreview()
    {
        setInterceptsMouseClicks(true, true);
    }

    void setClip(Clip* c, AudioFileManager* afm, double sr)
    { clip_ = c; afm_ = afm; sr_ = sr; repaint(); }

    void setAutomationOverlay(AutomationManagerCore* manager,
                              const TrackID& trackId,
                              const juce::String& clipId,
                              bool enabled,
                              const juce::String& selectedParameterId = {},
                              const juce::String& selectedDisplayName = {})
    {
        automationManager_ = manager;
        automationTrackId_ = trackId;
        automationClipId_ = clipId;
        automationEnabled_ = enabled;
        selectedAutomationParameterId_ = selectedParameterId;
        selectedAutomationDisplayName_ = selectedDisplayName;
        repaint();
    }

    std::function<void()> onAutomationEdited;

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        juce::ColourGradient bg(CPW::C(0xFF0C0C0E), b.getX(), b.getY(),
                                CPW::C(0xFF101012), b.getX(), b.getBottom(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(b, 5.f);
        g.setColour(CPW::C(CPW::kBorder));
        g.drawRoundedRectangle(b.reduced(0.5f), 5.f, 1.f);

        if (!clip_) { CPW::microLabel(g, b, "No clip loaded"); return; }

        if (auto* ac = dynamic_cast<AudioClip*>(clip_))
        {
            if (afm_ && afm_->hasAudio(ac->getID()))
            {
                auto sRate  = afm_->getSourceSampleRate(ac->getID());
                auto span   = (SamplePosition)std::round((double)ac->getLength() * sRate / juce::jmax(1.0, sr_));
                AudioFileManager::WaveformVisualOptions visual;
                visual.gainLinear = ac->getGain();
                visual.pitchSemitones = ac->getPitch();
                visual.timeStretchRatio = ac->getTimeStretch();
                visual.reversed = ac->isReversed();
                visual.clipTimelineSamples = ac->getLength();
                visual.fadeInSamples = ac->getFadeInLength();
                visual.fadeOutSamples = ac->getFadeOutLength();
                afm_->drawWaveform(ac->getID(), g, b.reduced(8.f, 10.f),
                                   ac->getSourceOffset(), span,
                                   CPW::C(CPW::kAccent).withAlpha(0.7f), visual);
                // top glow
                juce::ColourGradient tg(CPW::C(CPW::kAccent).withAlpha(0.06f), b.getX(), b.getY(),
                                        juce::Colours::transparentBlack, b.getX(), b.getY() + 20.f, false);
                g.setGradientFill(tg);
                g.fillRoundedRectangle(b, 5.f);
                drawAutomationOverlay(g, b.reduced(8.f, 10.f));
                return;
            }
        }
        // MIDI placeholder
        if (clip_->getType() == ClipType::MIDI)
        {
            g.setColour(CPW::C(CPW::kPurple).withAlpha(0.18f));
            for (int i = 0; i < 14; ++i)
            {
                float px = (float)(getWidth() / 15) * (i + 1);
                float ph = 10.f + (float)((i * 53 + 11) % 34);
                g.fillRoundedRectangle(px - 3.f, getHeight() * 0.5f - ph * 0.5f, 5.f, ph, 2.f);
            }
            CPW::microLabel(g, b, "MIDI - open Piano Roll to edit", CPW::kTextDim);
        }
        else
            CPW::microLabel(g, b, "No audio loaded", CPW::kTextDim);

        drawAutomationOverlay(g, b.reduced(8.f, 10.f));
    }

private:
    float normaliseAutomationForDisplay(const juce::String& parameterId, float value) const noexcept
    {
        if (parameterId.startsWith("clip.") && parameterId.endsWith(AutomationLaneCore::clipGainSuffix))
            return juce::jlimit(0.0f, 1.0f, value / 4.0f);

        if (parameterId.startsWith("clip.") && parameterId.endsWith(AutomationLaneCore::clipPanSuffix))
            return juce::jlimit(0.0f, 1.0f, (value + 1.0f) * 0.5f);

        if (parameterId.startsWith("clip.") && (parameterId.endsWith(AutomationLaneCore::clipFadeInSuffix)
            || parameterId.endsWith(AutomationLaneCore::clipFadeOutSuffix)))
        {
            const float clipLength = clip_ != nullptr ? (float) juce::jmax((int64_t) 1, (int64_t) clip_->getLength()) : 1.0f;
            return juce::jlimit(0.0f, 1.0f, value / clipLength);
        }

        if (parameterId.startsWith("clip.") && parameterId.endsWith(AutomationLaneCore::clipTapeStopSuffix))
            return juce::jlimit(0.0f, 1.0f, 1.0f - value);

        if (parameterId == AutomationLaneCore::trackPanParameterId)
            return juce::jlimit(0.0f, 1.0f, (value + 1.0f) * 0.5f);

        if (parameterId == AutomationLaneCore::trackTapeStopParameterId)
            return juce::jlimit(0.0f, 1.0f, value);

        if (parameterId.startsWith("clip.") && parameterId.endsWith(AutomationLaneCore::clipPitchSuffix))
            return juce::jlimit(0.0f, 1.0f, (value + 36.0f) / 72.0f);

        if (parameterId.startsWith("clip.") && parameterId.endsWith(AutomationLaneCore::clipStretchSuffix))
            return juce::jlimit(0.0f, 1.0f, (value - 0.1f) / 3.9f);

        return juce::jlimit(0.0f, 1.0f, value * 0.5f);
    }

    float automationValueFromDisplay(const juce::String& parameterId, float normalised) const noexcept
    {
        const float n = juce::jlimit(0.0f, 1.0f, normalised);

        if (parameterId.startsWith("clip.") && parameterId.endsWith(AutomationLaneCore::clipGainSuffix))
            return n * 4.0f;

        if (parameterId.startsWith("clip.") && parameterId.endsWith(AutomationLaneCore::clipPanSuffix))
            return n * 2.0f - 1.0f;

        if (parameterId.startsWith("clip.") && (parameterId.endsWith(AutomationLaneCore::clipFadeInSuffix)
            || parameterId.endsWith(AutomationLaneCore::clipFadeOutSuffix)))
        {
            const float clipLength = clip_ != nullptr ? (float) juce::jmax((int64_t) 1, (int64_t) clip_->getLength()) : 1.0f;
            return n * clipLength;
        }

        if (parameterId.startsWith("clip.") && parameterId.endsWith(AutomationLaneCore::clipTapeStopSuffix))
            return 1.0f - n;

        if (parameterId == AutomationLaneCore::trackPanParameterId)
            return n * 2.0f - 1.0f;

        if (parameterId == AutomationLaneCore::trackTapeStopParameterId)
            return n;

        if (parameterId.startsWith("clip.") && parameterId.endsWith(AutomationLaneCore::clipPitchSuffix))
            return n * 72.0f - 36.0f;

        if (parameterId.startsWith("clip.") && parameterId.endsWith(AutomationLaneCore::clipStretchSuffix))
            return 0.1f + n * 3.9f;

        return n * 2.0f;
    }

    juce::Colour selectedAutomationColour() const noexcept
    {
        if (selectedAutomationParameterId_.endsWith(AutomationLaneCore::clipGainSuffix)) return juce::Colour(0xFFFFD84D);
        if (selectedAutomationParameterId_.endsWith(AutomationLaneCore::clipPanSuffix)) return juce::Colour(0xFF62D8FF);
        if (selectedAutomationParameterId_.endsWith(AutomationLaneCore::clipFadeInSuffix)) return juce::Colour(0xFFC08BFF);
        if (selectedAutomationParameterId_.endsWith(AutomationLaneCore::clipFadeOutSuffix)) return juce::Colour(0xFF52E09A);
        if (selectedAutomationParameterId_.endsWith(AutomationLaneCore::clipTapeStopSuffix)) return juce::Colour(0xFFFF8A5B);
        if (selectedAutomationParameterId_ == AutomationLaneCore::trackVolumeParameterId) return juce::Colour(0xFFFFD84D);
        if (selectedAutomationParameterId_ == AutomationLaneCore::trackPanParameterId) return juce::Colour(0xFF62D8FF);
        if (selectedAutomationParameterId_ == AutomationLaneCore::trackTapeStopParameterId) return juce::Colour(0xFFC08BFF);
        if (selectedAutomationParameterId_.endsWith(AutomationLaneCore::clipPitchSuffix)) return juce::Colour(0xFFFF7AB8);
        if (selectedAutomationParameterId_.endsWith(AutomationLaneCore::clipStretchSuffix)) return juce::Colour(0xFF52E09A);
        return CPW::C(CPW::kGold);
    }

    void drawLaneOverlay(juce::Graphics& g,
                         juce::Rectangle<float> area,
                         const AutomationLaneCore& lane,
                         juce::Colour colour) const
    {
        if (clip_ == nullptr)
            return;

        const auto clipStart = clip_->getStartPosition();
        const auto clipLength = juce::jmax<SamplePosition>(1, clip_->getLength());
        const int samples = juce::jmax(32, (int) area.getWidth());

        juce::Path path;
        for (int i = 0; i < samples; ++i)
        {
            const float t = samples > 1 ? (float) i / (float) (samples - 1) : 0.0f;
            const auto samplePos = clipStart + (SamplePosition) std::llround((double) clipLength * t);
            const float norm = normaliseAutomationForDisplay(lane.parameterId,
                lane.getValueAtSample(samplePos, lane.getDefaultValue()));
            const float x = area.getX() + t * area.getWidth();
            const float y = area.getBottom() - norm * area.getHeight();
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }

        g.setColour(colour.withAlpha(0.18f));
        g.strokePath(path, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(colour.withAlpha(0.92f));
        g.strokePath(path, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Draw points
        g.setColour(colour);
        for (const auto& point : lane.points)
        {
            if (point.timeSamples >= clipStart && point.timeSamples <= clipStart + clipLength)
            {
                const float t = (float)(point.timeSamples - clipStart) / (float)clipLength;
                const float norm = normaliseAutomationForDisplay(lane.parameterId, point.value);
                const float x = area.getX() + t * area.getWidth();
                const float y = area.getBottom() - norm * area.getHeight();
                g.fillEllipse(x - 3.f, y - 3.f, 6.f, 6.f);
            }
        }
    }

    void drawAutomationOverlay(juce::Graphics& g, juce::Rectangle<float> area) const
    {
        if (!automationEnabled_ || automationManager_ == nullptr || clip_ == nullptr || automationTrackId_.isEmpty())
            return;

        g.setColour(juce::Colours::black.withAlpha(0.28f));
        g.fillRoundedRectangle(area.expanded(2.0f), 4.0f);

        g.setColour(CPW::C(CPW::kGold).withAlpha(0.16f));
        for (int i = 1; i < 4; ++i)
        {
            const float y = area.getY() + area.getHeight() * (float)i / 4.0f;
            g.drawHorizontalLine((int)std::round(y), area.getX(), area.getRight());
        }

        if (selectedAutomationParameterId_.isEmpty())
            return;

        const auto colour = selectedAutomationColour();
        if (const auto* lane = automationManager_->findLane(automationTrackId_, selectedAutomationParameterId_))
            drawLaneOverlay(g, area, *lane, colour);
        else
        {
            const float y = area.getCentreY();
            g.setColour(colour.withAlpha(0.20f));
            g.drawHorizontalLine((int)std::round(y), area.getX(), area.getRight());
        }

        g.setColour(colour.withAlpha(0.92f));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f).withStyle("Bold")));
        g.drawText("AUTOMATION: " + selectedAutomationDisplayName_.toUpperCase(),
                   area.withTop(area.getY() + 2.0f).withHeight(12.0f), juce::Justification::topRight, false);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.0f)));
        g.setColour(CPW::C(CPW::kTextDim).withAlpha(0.82f));
        g.drawText("draw points directly on the waveform", area.withBottom(area.getBottom() - 2.0f).withHeight(12.0f),
                   juce::Justification::bottomLeft, false);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Grab an existing dot when the gesture starts on one; otherwise draw
        // a new dot. Mirrors the timeline lane behaviour.
        grabbedPointTime_ = findAutomationPointTimeAt(e.position);

        // Right-click on a dot: per-dot actions (delete / reset to default).
        if (e.mods.isPopupMenu())
        {
            if (grabbedPointTime_ >= 0)
                showDotMenu();
            return;
        }

        if (grabbedPointTime_ < 0)
            writeAutomationPoint(e.position);
    }

    /** Native-unit default for a clip automation parameter. */
    static float defaultAutomationValueFor(const juce::String& parameterId,
                                           const juce::String& clipId)
    {
        if (parameterId == DAW::AutomationLaneCore::makeClipStretchParameterId(clipId))  return 1.0f;
        if (parameterId == DAW::AutomationLaneCore::makeClipGainParameterId(clipId))     return 1.0f;
        return 0.0f;   // pitch (semitones), pan (-1..1), tape stop (0..1)
    }

    void showDotMenu()
    {
        if (automationManager_ == nullptr || grabbedPointTime_ < 0)
            return;

        const int64_t pointTime = grabbedPointTime_;
        grabbedPointTime_ = -1;

        juce::PopupMenu menu;
        menu.addItem(1, "Delete Dot");
        menu.addItem(2, "Reset Dot to Default");

        juce::Component::SafePointer<CPWWavePreview> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [safe, pointTime](int result)
            {
                auto* self = safe.getComponent();
                if (self == nullptr || result <= 0)
                    return;

                auto* manager = self->automationManager_;
                if (manager == nullptr || self->selectedAutomationParameterId_.isEmpty())
                    return;

                const auto* lane = manager->findLane(self->automationTrackId_,
                                                     self->selectedAutomationParameterId_);
                if (lane == nullptr)
                    return;

                int index = -1;
                for (int i = 0; i < (int) lane->points.size(); ++i)
                    if (lane->points[(size_t) i].timeSamples == pointTime) { index = i; break; }
                if (index < 0)
                    return;

                if (result == 1)
                {
                    manager->removePoint(self->automationTrackId_,
                                         self->selectedAutomationParameterId_, index);
                }
                else
                {
                    manager->addOrReplacePoint(
                        self->automationTrackId_, self->selectedAutomationParameterId_,
                        pointTime,
                        defaultAutomationValueFor(self->selectedAutomationParameterId_,
                                                  self->automationClipId_));
                }

                if (self->onAutomationEdited) self->onAutomationEdited();
                self->repaint();
            });
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        // Only a grabbed dot moves. Drawing mode never adds a dot on every
        // mouse move — that left a trail of unwanted dots.
        if (grabbedPointTime_ >= 0)
            moveGrabbedPoint(e.position);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (grabbedPointTime_ >= 0)
        {
            moveGrabbedPoint(e.position);
            grabbedPointTime_ = -1;
        }
    }

    int64_t findAutomationPointTimeAt(juce::Point<float> pos) const
    {
        if (!automationEnabled_ || automationManager_ == nullptr || clip_ == nullptr
            || automationTrackId_.isEmpty() || selectedAutomationParameterId_.isEmpty())
            return -1;

        const auto* lane = automationManager_->findLane(automationTrackId_, selectedAutomationParameterId_);
        if (lane == nullptr || lane->points.empty())
            return -1;

        const auto area = getLocalBounds().toFloat().reduced(8.f, 10.f);
        const auto length = juce::jmax((SamplePosition) 1, clip_->getLength());
        const int64_t clipStart = (int64_t) clip_->getStartPosition();

        int64_t bestTime = -1;
        float bestDist = 10.0f;   // grab radius in pixels
        for (const auto& p : lane->points)
        {
            const float xNorm = juce::jlimit(0.0f, 1.0f,
                (float) ((double) (p.timeSamples - clipStart) / (double) length));
            const float x = area.getX() + xNorm * area.getWidth();
            const float d = std::abs(x - pos.x);
            if (d < bestDist) { bestDist = d; bestTime = p.timeSamples; }
        }
        return bestTime;
    }

    void moveGrabbedPoint(juce::Point<float> pos)
    {
        if (grabbedPointTime_ < 0 || automationManager_ == nullptr || clip_ == nullptr)
            return;

        const auto area = getLocalBounds().toFloat().reduced(8.f, 10.f);
        pos.x = juce::jlimit(area.getX(), area.getRight(), pos.x);
        pos.y = juce::jlimit(area.getY(), area.getBottom(), pos.y);

        const float xNorm = juce::jlimit(0.0f, 1.0f, (pos.x - area.getX()) / juce::jmax(1.0f, area.getWidth()));
        const float yNorm = juce::jlimit(0.0f, 1.0f, 1.0f - ((pos.y - area.getY()) / juce::jmax(1.0f, area.getHeight())));
        const auto time = clip_->getStartPosition() + (SamplePosition)std::llround((double)clip_->getLength() * xNorm);
        const float value = automationValueFromDisplay(selectedAutomationParameterId_, yNorm);

        // Remove the grabbed dot at its old time, then re-add it at the new
        // position — the drag moves ONE dot instead of drawing a new one.
        if (const auto* lane = automationManager_->findLane(automationTrackId_, selectedAutomationParameterId_))
        {
            for (int i = 0; i < (int) lane->points.size(); ++i)
            {
                if (lane->points[(size_t) i].timeSamples == grabbedPointTime_)
                {
                    automationManager_->removePoint(automationTrackId_, selectedAutomationParameterId_, i);
                    break;
                }
            }
        }

        automationManager_->addOrReplacePoint(automationTrackId_, selectedAutomationParameterId_, (int64_t) time, value);
        grabbedPointTime_ = (int64_t) time;
        if (onAutomationEdited) onAutomationEdited();
        repaint();
    }

    int64_t grabbedPointTime_ = -1;

    void writeAutomationPoint(juce::Point<float> pos)
    {
        if (!automationEnabled_ || automationManager_ == nullptr || clip_ == nullptr
            || automationTrackId_.isEmpty() || selectedAutomationParameterId_.isEmpty())
            return;

        const auto area = getLocalBounds().toFloat().reduced(8.f, 10.f);
        pos.x = juce::jlimit(area.getX(), area.getRight(), pos.x);
        pos.y = juce::jlimit(area.getY(), area.getBottom(), pos.y);

        const float xNorm = juce::jlimit(0.0f, 1.0f, (pos.x - area.getX()) / juce::jmax(1.0f, area.getWidth()));
        const float yNorm = juce::jlimit(0.0f, 1.0f, 1.0f - ((pos.y - area.getY()) / juce::jmax(1.0f, area.getHeight())));
        const auto time = clip_->getStartPosition() + (SamplePosition)std::llround((double)clip_->getLength() * xNorm);
        const float value = automationValueFromDisplay(selectedAutomationParameterId_, yNorm);

        auto& lane = automationManager_->getOrCreateLane(automationTrackId_, selectedAutomationParameterId_);
        lane.setVisible(true);
        lane.setEnabled(true);
        automationManager_->addOrReplacePoint(automationTrackId_, selectedAutomationParameterId_, (int64_t)time, value);
        if (onAutomationEdited) onAutomationEdited();
        repaint();
    }

    Clip* clip_ = nullptr; AudioFileManager* afm_ = nullptr; double sr_ = 44100.0;
    AutomationManagerCore* automationManager_ = nullptr;
    TrackID automationTrackId_;
    juce::String automationClipId_;
    juce::String selectedAutomationParameterId_;
    juce::String selectedAutomationDisplayName_;
    bool automationEnabled_ = false;
};

// ============================================================================
//  Plugin list row
// ============================================================================
class CPWPluginRow : public juce::Component
{
public:
    juce::PluginDescription desc;
    bool selected = false;
    std::function<void()> onSelect;
    std::function<void()> onOpen;

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        if (selected)
        {
            g.setColour(CPW::C(CPW::kAccent).withAlpha(0.22f));
            g.fillRoundedRectangle(b.reduced(2.f, 1.f), 3.f);
            g.setColour(CPW::C(CPW::kAccent).withAlpha(0.65f));
            g.drawRoundedRectangle(b.reduced(2.f, 1.f), 3.f, 1.2f);
        }
        else if (isMouseOver())
        {
            g.setColour(CPW::C(CPW::kSurface2).withAlpha(0.8f));
            g.fillRoundedRectangle(b.reduced(2.f, 1.f), 3.f);
        }

        // Format badge
        const juce::String fmt = desc.pluginFormatName.contains("3") ? "VST3" : "VST";
        auto badgeCol = fmt == "VST3" ? CPW::C(CPW::kGreen) : CPW::C(CPW::kGold);
        g.setColour(badgeCol.withAlpha(0.22f));
        g.fillRoundedRectangle(6.f, b.getCentreY() - 7.f, 34.f, 14.f, 3.f);
        g.setColour(badgeCol);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.5f).withStyle("Bold")));
        g.drawText(fmt, juce::Rectangle<float>(6.f, b.getCentreY() - 7.f, 34.f, 14.f),
                   juce::Justification::centred, false);

        // Name
        g.setColour(selected ? CPW::C(CPW::kText) : CPW::C(0xFFCCCCD4));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.5f)));
        g.drawText(desc.name, 46, 0, getWidth() - 100, getHeight(),
                   juce::Justification::centredLeft, true);

        // Manufacturer
        g.setColour(CPW::C(CPW::kTextDim));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.5f)));
        g.drawText(desc.manufacturerName, getWidth() - 100, 0, 94, getHeight(),
                   juce::Justification::centredRight, true);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { if (onSelect) onSelect(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { if (onOpen) onOpen(); }
};

// ============================================================================
//  ClipPropertiesPanel
// ============================================================================
class ClipPropertiesPanel : public juce::Component,
                            private juce::Slider::Listener,
                            private juce::Label::Listener,
                            private juce::TextEditor::Listener,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    // Callbacks set by MainComponent
    std::function<void(MidiClip&)>                                   onOpenPianoRoll;
    std::function<void(Clip&, const juce::PluginDescription&)>       onOpenClipRegionPlugin;
    std::function<void(Clip&, const juce::String&)>                  onOpenActiveClipRegionPlugin;
    std::function<void(int width, int height)>                       onResizeRequest;
    /** Message-thread notification after a successful clip-FX chain edit. */
    std::function<void()>                                             onClipFxChanged;

    ClipPropertiesPanel()
    {
        // ── Name editor ──────────────────────────────────────────────────────
        nameLabel_.setFont(juce::Font(juce::FontOptions{}.withHeight(14.f).withStyle("Bold")));
        nameLabel_.setColour(juce::Label::textColourId, CPW::C(CPW::kText));
        nameLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        nameLabel_.setColour(juce::Label::outlineWhenEditingColourId, CPW::C(CPW::kAccent).withAlpha(0.55f));
        nameLabel_.setEditable(true, true, false);
        nameLabel_.addListener(this);
        addAndMakeVisible(nameLabel_);
        nameLabel_.setVisible(false);

        typeBadge_.setFont(juce::Font(juce::FontOptions{}.withHeight(9.5f).withStyle("Bold")));
        typeBadge_.setColour(juce::Label::textColourId, CPW::C(CPW::kTextDim));
        typeBadge_.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(typeBadge_);
        typeBadge_.setVisible(false);

        // ── Waveform ─────────────────────────────────────────────────────────
        addAndMakeVisible(wave_);
        wave_.onAutomationEdited = [this]
        {
            refreshAutomationRowStates();
            if (automationCallbacks_.showLane && selectedAutomationParameterId_.isNotEmpty())
            {
                auto target = makeAutomationTarget(selectedAutomationParameterId_, selectedAutomationDisplayName_);
                automationCallbacks_.showLane(target);
            }
        };

        // ── Knobs ────────────────────────────────────────────────────────────
        pitchCell_   = std::make_unique<CPWKnobCell>("Pitch (st)", CPW::kPurple);
        fineTuneCell_ = std::make_unique<CPWKnobCell>("Fine Tune",  CPW::kPurple);
        gainCell_    = std::make_unique<CPWKnobCell>("Gain",       CPW::kAccent);
        panCell_     = std::make_unique<CPWKnobCell>("Pan",        CPW::kPink);
        stretchCell_ = std::make_unique<CPWKnobCell>("Stretch",    CPW::kGreen);
        fadeInCell_  = std::make_unique<CPWKnobCell>("Fade In",    CPW::kGold);
        fadeOutCell_ = std::make_unique<CPWKnobCell>("Fade Out",   CPW::kGold);

        // Clip GAIN is a static pre-automation offset.
        // Track volume automation (AutomationManagerCore) writes to track volume,
        // NOT to clip gain. Final gain = clipGain * trackAutomatedVolume.
        // Do not merge these two concepts.
        gainCell_->slider.setRange(0.0, 4.0, 0.001);
        gainCell_->slider.setDoubleClickReturnValue(true, 1.0);
        gainCell_->slider.setTextValueSuffix(" x");
        gainCell_->slider.addListener(this);

        panCell_->slider.setRange(-1.0, 1.0, 0.001);
        panCell_->slider.setDoubleClickReturnValue(true, 0.0);
        panCell_->slider.textFromValueFunction = [](double value)
        {
            const int scaled = (int)std::round(std::abs(value) * 100.0);
            if (scaled == 0)
                return juce::String("C");
            return juce::String(scaled) + (value < 0.0 ? "L" : "R");
        };
        panCell_->slider.addListener(this);

        pitchCell_->slider.setRange(-36.0, 24.0, 0.01);
        pitchCell_->slider.setDoubleClickReturnValue(true, 0.0);
        pitchCell_->slider.setTextValueSuffix(" st");
        pitchCell_->slider.addListener(this);

        fineTuneCell_->slider.setRange(-100.0, 100.0, 1.0);
        fineTuneCell_->slider.setDoubleClickReturnValue(true, 0.0);
        fineTuneCell_->slider.textFromValueFunction = [](double value)
        {
            const int cents = (int)std::round(value);
            return juce::String(cents >= 0 ? "+" : "") + juce::String(cents) + " ct";
        };
        fineTuneCell_->slider.addListener(this);

        stretchCell_->slider.setRange(0.1, 4.0, 0.01);
        stretchCell_->slider.setDoubleClickReturnValue(true, 1.0);
        stretchCell_->slider.setTextValueSuffix(" x");
        stretchCell_->slider.addListener(this);

        fadeInCell_->slider.setRange(0.0, 10000000.0, 1.0);
        fadeInCell_->slider.setDoubleClickReturnValue(true, 0.0);
        fadeInCell_->slider.addListener(this);

        fadeOutCell_->slider.setRange(0.0, 10000000.0, 1.0);
        fadeOutCell_->slider.setDoubleClickReturnValue(true, 0.0);
        fadeOutCell_->slider.addListener(this);

        fadeInCurveBtn_ = std::make_unique<CPWCurveBtn>();
        fadeOutCurveBtn_ = std::make_unique<CPWCurveBtn>();
        fadeInCurveBtn_->onClick = [this] { cycleFadeCurve(true); };
        fadeOutCurveBtn_->onClick = [this] { cycleFadeCurve(false); };

        for (auto* c : { pitchCell_.get(), fineTuneCell_.get(), fadeInCell_.get(),
                         gainCell_.get(), panCell_.get(), fadeOutCell_.get(), stretchCell_.get() })
            addAndMakeVisible(*c);
        addAndMakeVisible(*fadeInCurveBtn_);
        addAndMakeVisible(*fadeOutCurveBtn_);

        // ── Colour button ─────────────────────────────────────────────────────
        colourBtn_ = std::make_unique<CPWPillBtn>("", CPW::kAccent);
        colourBtn_->onClick = [this] { showColourPopup(); };
        addAndMakeVisible(*colourBtn_);

        // ── Plugin search ─────────────────────────────────────────────────────
        pluginSearch_.setTextToShowWhenEmpty("Search plugins...",
                                             CPW::C(CPW::kTextDim));
        pluginSearch_.setFont(juce::Font(juce::FontOptions{}.withHeight(11.5f)));
        pluginSearch_.setColour(juce::TextEditor::backgroundColourId, CPW::C(CPW::kSurface2));
        pluginSearch_.setColour(juce::TextEditor::textColourId,       CPW::C(CPW::kText));
        pluginSearch_.setColour(juce::TextEditor::outlineColourId,    CPW::C(CPW::kBorder));
        pluginSearch_.setColour(juce::TextEditor::focusedOutlineColourId, CPW::C(CPW::kAccent).withAlpha(0.6f));
        pluginSearch_.addListener(this);
        addAndMakeVisible(pluginSearch_);

        addAndMakeVisible(activePluginListBox_);
        activePluginListBox_.setModel(this->getActiveListBoxModel());
        activePluginListBox_.setColour(juce::ListBox::backgroundColourId, CPW::C(0xFF101014));
        activePluginListBox_.setColour(juce::ListBox::outlineColourId,    CPW::C(CPW::kBorder));
        activePluginListBox_.setOutlineThickness(1);
        activePluginListBox_.setRowHeight(30);

        addAndMakeVisible(pluginListBox_);
        pluginListBox_.setModel(this->getListBoxModel());
        pluginListBox_.setColour(juce::ListBox::backgroundColourId, CPW::C(CPW::kSurface));
        pluginListBox_.setColour(juce::ListBox::outlineColourId,    CPW::C(CPW::kBorder));
        pluginListBox_.setOutlineThickness(1);
        pluginListBox_.setRowHeight(23);

        // ── Separators ────────────────────────────────────────────────────────
        addAndMakeVisible(sep1_); addAndMakeVisible(sep2_);
        addAndMakeVisible(sep3_); addAndMakeVisible(sep4_);

        // ── Action buttons ────────────────────────────────────────────────────
        muteBtn_      = std::make_unique<CPWPillBtn>("Mute",       CPW::kGold);
        pianoBtn_     = std::make_unique<CPWPillBtn>("Piano Roll", CPW::kPurple);
        fxPanelBtn_   = std::make_unique<CPWPillBtn>("FX",         CPW::kAccent);
        waveBtn_      = std::make_unique<CPWPillBtn>("Wave",       CPW::kAccent);
        normalizeBtn_ = std::make_unique<CPWPillBtn>("Normalize",  CPW::kGreen);
        reverseBtn_   = std::make_unique<CPWPillBtn>("Reverse",    CPW::kPurple);
        automateBtn_  = std::make_unique<CPWPillBtn>("Automate",   0xFFFFB347);  // amber
        automationBypassBtn_ = std::make_unique<CPWPillBtn>("Auto On", CPW::kPink);

        muteBtn_->onClick      = [this] { if (clip_) { clip_->setMuted(!clip_->isMuted()); muteBtn_->active = clip_->isMuted(); muteBtn_->repaint(); }};
        pianoBtn_->onClick     = [this] { if (auto* mc = dynamic_cast<MidiClip*>(clip_); mc && onOpenPianoRoll) onOpenPianoRoll(*mc); };
        fxPanelBtn_->onClick   = [this] { toggleFxFloatingPanel(); };
        waveBtn_->onClick      = [this] { toggleWaveform(); };
        normalizeBtn_->onClick = [this] { normalizeClip(); };
        reverseBtn_->onClick   = [this] { reverseClip(); };
        automateBtn_->onClick  = [this] { showAutomationDropdown(); };
    autoVisBtn_  = std::make_unique<CPWPillBtn>("Lanes On", 0xFF5BC8FF);  // cyan
    autoVisBtn_->onClick = [this] { toggleClipAutomationVisibility(); };
        automationBypassBtn_->onClick = [this] { toggleSelectedAutomationBypass(); };

        addAndMakeVisible(*muteBtn_);
        addAndMakeVisible(*pianoBtn_);
        addAndMakeVisible(*fxPanelBtn_);
        addAndMakeVisible(*waveBtn_);
        addAndMakeVisible(*normalizeBtn_);
        addAndMakeVisible(*reverseBtn_);
        addAndMakeVisible(*automateBtn_);
    addAndMakeVisible(*autoVisBtn_);
        addAndMakeVisible(*automationBypassBtn_);

        waveBtn_->active = true; // Initially visible
        waveformVisible_ = true;

        // ── Floating FX Panel ──────────────────────────────────────────────────
        fxFloatingPanel_ = std::make_unique<ClipFxFloatingPanel>();
        fxFloatingPanel_->onAddPluginClicked = [this] { showAddPluginMenu(); };
        fxFloatingPanel_->onOpenPlugin = [this](const juce::String& instanceId)
        {
            if (clip_ && onOpenActiveClipRegionPlugin)
                onOpenActiveClipRegionPlugin(*clip_, instanceId);
        };
        fxFloatingPanel_->onToggleBypass = [this](const juce::String& instanceId)
        {
            if (!clip_ || !clipRegionPluginCore_) return;
            if (auto* entry = clipRegionPluginCore_->findEntryById(clip_->getID(), instanceId))
            {
                clipRegionPluginCore_->setBypassed(clip_->getID(), instanceId, !entry->bypassed);
                if (onClipFxChanged) onClipFxChanged();
            }
            refreshActivePlugins();
        };
        fxFloatingPanel_->onRemovePlugin = [this](const juce::String& instanceId)
        {
            if (!clip_ || !clipRegionPluginCore_) return;
            if (clipRegionPluginCore_->findEntryById(clip_->getID(), instanceId) == nullptr)
                return;
            clipRegionPluginCore_->removeEntry(clip_->getID(), instanceId);
            if (onClipFxChanged) onClipFxChanged();
            refreshActivePlugins();
        };
        fxFloatingPanel_->onReorderPlugin = [this](int fromIndex, int toIndex)
        {
            if (!clip_ || !clipRegionPluginCore_)
                return;

            if (clipRegionPluginCore_->moveEntry(clip_->getID(), fromIndex, toIndex))
            {
                if (onClipFxChanged) onClipFxChanged();
                refreshActivePlugins();
            }
        };
        fxFloatingPanel_->onClose = [this]
        {
            hideFxFloatingPanel();
        };
        // Add to parent later when we know the parent component
        fxFloatingPanel_->setVisible(false);
        fxCableOverlay_ = std::make_unique<ClipFxCableOverlay>();
        fxCableOverlay_->setVisible(false);

        setSize(430, 390);
        updateRefreshTimerRate();
    }

    ~ClipPropertiesPanel() override
    {
        pluginListBox_.setModel(nullptr);
    }

    void parentHierarchyChanged() override
    {
        // Update FX panel position when parent changes
        if (fxFloatingPanel_ && fxFloatingPanel_->isVisible())
            updateFxPanelPosition();
    }

    void setClip(Clip* clip, AudioFileManager* afm, double sr)
    {
        clip_ = clip; afm_ = afm; sr_ = sr;
        wave_.setClip(clip_, afm, sr);
        refresh();
    }

    Clip* getClip() const noexcept { return clip_; }

    void setKnownPlugins(const juce::KnownPluginList* list)
    {
        knownPlugins_ = list;
        rebuildPluginList();
    }

    void setClipRegionPluginCore(ClipRegionPluginCore* core)
    {
        clipRegionPluginCore_ = core;
        refreshActivePlugins();
    }

    void activateAutomationMode(const DAW::TrackID& trackId,
                                const juce::String& clipId,
                                const ArrangementEditor::ClipAutomationPanelCallbacks& callbacks,
                                AutomationManagerCore* automationManager,
                                TransportController* transportController = nullptr)
    {
        automationTrackId_   = trackId;
        automationClipId_    = clipId;
        automationCallbacks_ = callbacks;
        automationManager_   = automationManager;
        transportController_ = transportController;
        automationMode_      = automationManager_ != nullptr && !automationTrackId_.isEmpty();
        if (selectedAutomationParameterId_.isEmpty()
            || selectedAutomationParameterId_.endsWith(AutomationLaneCore::clipTapeStopSuffix))
            setSelectedAutomationTarget(AutomationLaneCore::makeClipPitchParameterId(automationClipId_), "Clip Pitch", false);
        applyAutomationOverlay();
        if (onResizeRequest)
            onResizeRequest(430, 390);
        updateRefreshTimerRate();
        resized();
        repaint();
    }

    void deactivateAutomationMode()
    {
        automationMode_ = false;
        automationRows_.clear();
        automationManager_ = nullptr;
        transportController_ = nullptr;
        selectedAutomationParameterId_.clear();
        selectedAutomationDisplayName_.clear();
        wave_.setAutomationOverlay(nullptr, {}, {}, false);

        if (onResizeRequest)
            onResizeRequest(430, waveformVisible_ ? 390 : 180);

        updateRefreshTimerRate();
        resized();
        repaint();
    }

    void resized() override
    {
        const int P = 10;
        auto area = getLocalBounds().reduced(P);

        // ── Top utility row: mute on the right ───────────────────────────────
        auto hdr = area.removeFromTop(20);
        muteBtn_->setBounds(hdr.removeFromRight(52).reduced(0, 1));
        area.removeFromTop(5); sep1_.setBounds(area.removeFromTop(1)); area.removeFromTop(6);

        // ── Knobs up top ─────────────────────────────────────────────────────
        const int kH = 66;
        auto kRow = area.removeFromTop(kH);
        const int gap = 4;
        const int curveW = 26;
        const int cellW = (kRow.getWidth() - curveW * 2 - gap * 8) / 7;
        auto placeCell = [&](juce::Component& comp)
        {
            auto cell = kRow.removeFromLeft(cellW);
            comp.setBounds(cell);
            if (kRow.getWidth() > 0)
                kRow.removeFromLeft(gap);
        };
        auto placeCurve = [&](juce::Component& comp)
        {
            auto cell = kRow.removeFromLeft(curveW);
            comp.setBounds(cell.withTrimmedTop(32).withHeight(16));
            if (kRow.getWidth() > 0)
                kRow.removeFromLeft(gap);
        };

        placeCell(*pitchCell_);
        placeCell(*fineTuneCell_);
        placeCell(*fadeInCell_);
        placeCurve(*fadeInCurveBtn_);
        placeCell(*gainCell_);
        placeCell(*panCell_);
        placeCell(*fadeOutCell_);
        placeCurve(*fadeOutCurveBtn_);
        placeCell(*stretchCell_);
        area.removeFromTop(4); sep2_.setBounds(area.removeFromTop(1)); area.removeFromTop(7);

        // ── Action bar ────────────────────────────────────────────────────────
        auto bar = area.removeFromTop(24);
        // Colour swatch (small square pill showing clip colour)
        colourBtn_->setBounds(bar.removeFromLeft(22).reduced(0, 1));
        bar.removeFromLeft(5);
        fxPanelBtn_->setBounds(bar.removeFromLeft(34).reduced(0, 1));
        bar.removeFromLeft(5);
        waveBtn_->setBounds(bar.removeFromLeft(46).reduced(0, 1));
        bar.removeFromLeft(5);
        normalizeBtn_->setBounds(bar.removeFromLeft(72).reduced(0, 1));
        bar.removeFromLeft(5);
        reverseBtn_->setBounds(bar.removeFromLeft(60).reduced(0, 1));
        bar.removeFromLeft(5);
        if (automateBtn_ && automateBtn_->isVisible())
        {
            automateBtn_->setBounds(bar.removeFromLeft(64).reduced(0, 1));
        if (autoVisBtn_ && autoVisBtn_->isVisible())
            autoVisBtn_->setBounds(bar.removeFromLeft(64).reduced(0, 1));
            bar.removeFromLeft(5);
        }
        if (automationBypassBtn_ && automationBypassBtn_->isVisible())
        {
            automationBypassBtn_->setBounds(bar.removeFromLeft(72).reduced(0, 1));
            bar.removeFromLeft(5);
        }
        if (pianoBtn_->isVisible())
            pianoBtn_->setBounds(bar.removeFromLeft(76).reduced(0, 1));
        area.removeFromTop((waveformVisible_ || automationMode_) ? 8 : 0);

        // ── Waveform / Automation at the bottom ───────────────────────────────
        if (waveformVisible_ || automationMode_)
            wave_.setBounds(area);
        else
            wave_.setBounds({});

        // Hide old inline elements (replaced by floating panel)
        activePluginListBox_.setBounds({});
        pluginSearch_.setBounds({});
        pluginListBox_.setBounds({});
        sep3_.setBounds({});
        sep4_.setBounds({});

        // ── Position floating FX panel ────────────────────────────────────────
        updateFxPanelPosition();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(CPW::C(CPW::kBg));
        g.fillRect(b);

        // Clip colour top stripe
        if (clip_)
        {
            juce::ColourGradient sg(clip_->getColor().withAlpha(0.9f), b.getX(), b.getY(),
                                    clip_->getColor().withAlpha(0.0f), b.getRight() * 0.5f, b.getY(), false);
            g.setGradientFill(sg);
            g.fillRect(b.withHeight(2.f));
        }

        // Colour swatch in the button
        if (colourBtn_ && colourBtn_->isVisible() && clip_)
        {
            auto sb = colourBtn_->getBounds().toFloat().reduced(3.f);
            g.setColour(clip_->getColor());
            g.fillRoundedRectangle(sb, 3.f);
            g.setColour(clip_->getColor().brighter(0.4f).withAlpha(0.7f));
            g.drawRoundedRectangle(sb, 3.f, 1.f);
        }
    }

    bool isFxFloatingPanelOpen() const
    {
        return fxFloatingPanel_ != nullptr && fxFloatingPanel_->isVisible();
    }

    ClipFxFloatingPanel* getFxFloatingPanel() const noexcept { return fxFloatingPanel_.get(); }

    void setFxFloatingPanelOpen(bool shouldBeOpen)
    {
        if (shouldBeOpen)
        {
            if (fxFloatingPanel_ == nullptr || !fxFloatingPanel_->isVisible())
                toggleFxFloatingPanel();
        }
        else
        {
            hideFxFloatingPanel();
        }
    }

private:
    // ── ListBoxModel proxy ────────────────────────────────────────────────────
    struct PluginListModel : public juce::ListBoxModel
    {
        std::vector<juce::PluginDescription>* rows = nullptr;
        int selectedRow = -1;
        std::function<void(int)> onRowSelected;

        int getNumRows() override { return rows ? (int)rows->size() : 0; }
        void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
        juce::Component* refreshComponentForRow(int row, bool, juce::Component* existing) override
        {
            auto* comp = dynamic_cast<CPWPluginRow*>(existing);
            if (!comp) comp = new CPWPluginRow();
            if (rows && row < (int)rows->size())
            {
                comp->desc     = (*rows)[row];
                comp->selected = (row == selectedRow);
                comp->onSelect = [this, row] { selectedRow = row; if (onRowSelected) onRowSelected(row); };
                comp->onOpen   = [this, row] { selectedRow = row; if (onRowDoubleClicked) onRowDoubleClicked(row); };
            }
            return comp;
        }
        void listBoxItemClicked(int row, const juce::MouseEvent&) override
        {
            selectedRow = row;
            if (onRowSelected) onRowSelected(row);
        }
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
        {
            selectedRow = row;
            if (onRowDoubleClicked) onRowDoubleClicked(row);
        }
        std::function<void(int)> onRowDoubleClicked;
    } listModel_;

    struct ActivePluginListModel : public juce::ListBoxModel
    {
        std::vector<ClipRegionPluginCore::EntryInfo>* rows = nullptr;
        std::function<void(const juce::String&)> onOpen;
        std::function<void(const juce::String&)> onToggleBypass;
        std::function<void(const juce::String&)> onRemove;

        int getNumRows() override { return rows ? (int)rows->size() : 0; }
        void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
        juce::Component* refreshComponentForRow(int row, bool, juce::Component* existing) override
        {
            auto* comp = dynamic_cast<CPWActivePluginRow*>(existing);
            if (!comp) comp = new CPWActivePluginRow();
            if (rows && row >= 0 && row < (int)rows->size())
            {
                auto info = (*rows)[row];
                comp->name = info.name;
                comp->manufacturer = info.manufacturer;
                comp->format = info.format;
                comp->bypassed = info.bypassed;
                comp->onOpen = [this, id = info.instanceId] { if (onOpen) onOpen(id); };
                comp->onToggleBypass = [this, id = info.instanceId] { if (onToggleBypass) onToggleBypass(id); };
                comp->onRemove = [this, id = info.instanceId] { if (onRemove) onRemove(id); };
            }
            return comp;
        }
    } activeListModel_;

    juce::ListBoxModel* getListBoxModel()
    {
        listModel_.onRowDoubleClicked = [this](int row)
        {
            listModel_.selectedRow = row;
            pluginListBox_.repaintRow(row);
            openSelectedPlugin();
        };
        return &listModel_;
    }

    juce::ListBoxModel* getActiveListBoxModel()
    {
        activeListModel_.rows = &activePlugins_;
        activeListModel_.onOpen = [this](const juce::String& instanceId)
        {
            if (clip_ && onOpenActiveClipRegionPlugin) onOpenActiveClipRegionPlugin(*clip_, instanceId);
        };
        activeListModel_.onToggleBypass = [this](const juce::String& instanceId)
        {
            if (!clip_ || !clipRegionPluginCore_) return;
            if (auto* entry = clipRegionPluginCore_->findEntryById(clip_->getID(), instanceId))
            {
                clipRegionPluginCore_->setBypassed(clip_->getID(), instanceId, !entry->bypassed);
                if (onClipFxChanged) onClipFxChanged();
            }
            refreshActivePlugins();
        };
        activeListModel_.onRemove = [this](const juce::String& instanceId)
        {
            if (!clip_ || !clipRegionPluginCore_) return;
            if (clipRegionPluginCore_->findEntryById(clip_->getID(), instanceId) == nullptr)
                return;
            clipRegionPluginCore_->removeEntry(clip_->getID(), instanceId);
            if (onClipFxChanged) onClipFxChanged();
            refreshActivePlugins();
        };
        return &activeListModel_;
    }

    // ── Colour popup ──────────────────────────────────────────────────────────
    void showColourPopup()
    {
        static const juce::Colour kSwatches[] = {
            juce::Colour(0xFFE07B39), juce::Colour(0xFF4EC94E), juce::Colour(0xFFE0559A),
            juce::Colour(0xFF60A0E0), juce::Colour(0xFFE0D060), juce::Colour(0xFFB060E0),
            juce::Colour(0xFFFF6060), juce::Colour(0xFF40D0C0), juce::Colour(0xFFFFAA00),
            juce::Colour(0xFF5BC8FF), juce::Colour(0xFF52E09A), juce::Colour(0xFFAAAAAA),
        };
        constexpr int kN = 12;

        // Build a self-owning popup panel
        struct SwatchPanel : public juce::Component {
            juce::OwnedArray<juce::TextButton> buttons;
        };
        auto panel = std::make_unique<SwatchPanel>();
        panel->setSize(260, 80);

        // Swatch grid
        const int sw = 260 / kN;
        for (int i = 0; i < kN; ++i)
        {
            auto* btn = panel->buttons.add(new juce::TextButton(""));
            btn->setBounds(i * sw + 2, 6, sw - 4, 26);
            btn->setColour(juce::TextButton::buttonColourId, kSwatches[i]);
            const juce::Colour col = kSwatches[i];
            btn->onClick = [this, col] {
                if (clip_) clip_->setColor(col);
                colourBtn_->repaint();
                repaint();
                juce::PopupMenu::dismissAllActiveMenus();
            };
            panel->addAndMakeVisible(btn);
        }

        // Custom colour button
        auto* customBtn = panel->buttons.add(new juce::TextButton("Custom\u2026"));
        customBtn->setBounds(4, 38, 252, 26);
        customBtn->setColour(juce::TextButton::buttonColourId, CPW::C(CPW::kSurface2));
        customBtn->setColour(juce::TextButton::textColourOffId, CPW::C(CPW::kText));
        customBtn->onClick = [this] {
            juce::PopupMenu::dismissAllActiveMenus();
            auto* cs = new juce::ColourSelector(
                juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders |
                juce::ColourSelector::showColourspace);
            cs->setCurrentColour(clip_ ? clip_->getColor() : juce::Colours::orange);
            cs->setSize(300, 320);
            cs->addChangeListener(this);
            colourSelector_.reset(cs);
            juce::CallOutBox::launchAsynchronously(
                std::unique_ptr<juce::Component>(cs),
                colourBtn_->getScreenBounds(), nullptr);
        };
        panel->addAndMakeVisible(customBtn);

        juce::CallOutBox::launchAsynchronously(
            std::move(panel),
            colourBtn_->getScreenBounds(), nullptr);
    }

    void changeListenerCallback(juce::ChangeBroadcaster* src) override
    {
        if (auto* cs = dynamic_cast<juce::ColourSelector*>(src))
        {
            if (clip_) clip_->setColor(cs->getCurrentColour());
            colourBtn_->repaint();
            repaint();
        }
    }

    // ── Refresh ───────────────────────────────────────────────────────────────
    void refresh()
    {
        if (!clip_) return;
        const juce::ScopedValueSetter<bool> refreshGuard(isRefreshingFromModel_, true);
        nameLabel_.setText(clip_->getName(), juce::dontSendNotification);
        typeBadge_.setText(clip_->getType() == ClipType::MIDI ? "MIDI" : "AUDIO",
                           juce::dontSendNotification);

        const bool isAudio = dynamic_cast<AudioClip*>(clip_) != nullptr;
        const bool isMidi  = clip_->getType() == ClipType::MIDI;
        for (auto* c : { pitchCell_.get(), fineTuneCell_.get(), fadeInCell_.get(),
                         gainCell_.get(), panCell_.get(), fadeOutCell_.get(), stretchCell_.get() })
            c->setVisible(isAudio);
        fadeInCurveBtn_->setVisible(isAudio);
        fadeOutCurveBtn_->setVisible(isAudio);
        pianoBtn_->setVisible(isMidi);
        normalizeBtn_->setVisible(isAudio);
        reverseBtn_->setVisible(isAudio);
        fxPanelBtn_->setVisible(isAudio);
        waveBtn_->setVisible(isAudio);
        if (automateBtn_) automateBtn_->setVisible(isAudio);
    if (autoVisBtn_) autoVisBtn_->setVisible(isAudio);
        if (automationBypassBtn_) automationBypassBtn_->setVisible(isAudio && automationMode_ && selectedAutomationParameterId_.isNotEmpty());

        if (auto* ac = dynamic_cast<AudioClip*>(clip_))
        {
            gainCell_->slider.setValue(ac->getGain(), juce::dontSendNotification);
            pitchCell_->slider.setValue(ac->getPitchTargetSemitones(), juce::dontSendNotification);
            fineTuneCell_->slider.setValue(ac->getClipFineTune(), juce::dontSendNotification);
            panCell_->slider.setValue(ac->getClipPan(), juce::dontSendNotification);
            stretchCell_->slider.setValue(ac->getTimeStretch(), juce::dontSendNotification);
            const double maxFade = (double)ac->getLength();
            fadeInCell_->slider.setRange(0.0, maxFade, 1.0);
            fadeOutCell_->slider.setRange(0.0, maxFade, 1.0);
            fadeInCell_->slider.setValue((double)ac->getFadeInLength(),  juce::dontSendNotification);
            fadeOutCell_->slider.setValue((double)ac->getFadeOutLength(), juce::dontSendNotification);
            fadeInCurveBtn_->setCurve(ac->getFadeInCurve());
            fadeOutCurveBtn_->setCurve(ac->getFadeOutCurve());
            normalizeBtn_->active = afm_ != nullptr && afm_->isPeakNormalized(ac->getID());
            reverseBtn_->active = ac->isReversed();
        }
        else
        {
            normalizeBtn_->active = false;
            reverseBtn_->active = false;
        }

        muteBtn_->active = clip_->isMuted();
        waveBtn_->active = waveformVisible_;
        if (automateBtn_)
        {
            automateBtn_->active = automationMode_ && selectedAutomationParameterId_.isNotEmpty();
            automateBtn_->label = automateBtn_->active ? ("Auto: " + selectedAutomationDisplayName_) : "Automate";
        }
        updateAutomationBypassButton();
        colourBtn_->repaint(); // colour stripe redrawn in paint()
        wave_.repaint();
        if (automationMode_)
        {
            wave_.setAutomationOverlay(automationManager_, automationTrackId_, automationClipId_, true,
                                       selectedAutomationParameterId_, selectedAutomationDisplayName_);
        }
        resized();
        refreshActivePlugins();
        repaint();
    }

    void refreshActivePlugins()
    {
        activePlugins_.clear();
        if (clip_ != nullptr && clipRegionPluginCore_ != nullptr)
            activePlugins_ = clipRegionPluginCore_->getEntriesForClip(clip_->getID());
        fxPanelBtn_->active = !activePlugins_.empty();

        // Update floating panel
        if (fxFloatingPanel_)
            fxFloatingPanel_->setPlugins(activePlugins_);

        // Old inline list (hidden now)
        activePluginListBox_.updateContent();
        activePluginListBox_.repaint();
    }

    void timerCallback() override
    {
        updateRefreshTimerRate();

        // Update FX panel position if it's visible (follows window movement)
        if (fxFloatingPanel_ && fxFloatingPanel_->isVisible())
        {
            updateFxPanelPosition();
            if (fxCableOverlay_)
                fxCableOverlay_->advanceAnimation();
        }
        syncAutomationKnobsFromPlayback();
    }

    void updateRefreshTimerRate()
    {
        const bool shouldRunAt60Hz = automationMode_
            && clip_ != nullptr
            && transportController_ != nullptr
            && transportController_->isPlaying();
        const int targetHz = 60;  // Always 60 Hz for smooth visual presentation

        if (refreshTimerHz_ != targetHz)
        {
            startTimerHz(targetHz);
            refreshTimerHz_ = targetHz;
        }
    }

    void syncAutomationKnobsToClipBaseValues(AudioClip& ac)
    {
        const juce::ScopedValueSetter<bool> refreshGuard(isRefreshingFromModel_, true);

        gainCell_->slider.setValue(ac.getGain(), juce::dontSendNotification);
        panCell_->slider.setValue(ac.getClipPan(), juce::dontSendNotification);
        fadeInCell_->slider.setValue((double) ac.getFadeInLength(), juce::dontSendNotification);
        fadeOutCell_->slider.setValue((double) ac.getFadeOutLength(), juce::dontSendNotification);
        pitchCell_->slider.setValue(ac.getPitchTargetSemitones(), juce::dontSendNotification);
        stretchCell_->slider.setValue(ac.getTimeStretch(), juce::dontSendNotification);
    }

    void syncAutomationKnobsFromPlayback()
    {
        if (!automationMode_ || clip_ == nullptr)
            return;

        if (isAnyAutomationKnobUserControlled())
            return;

        auto* ac = dynamic_cast<AudioClip*>(clip_);
        if (ac == nullptr)
            return;

        if (automationManager_ == nullptr || transportController_ == nullptr || !transportController_->isPlaying())
        {
            syncAutomationKnobsToClipBaseValues(*ac);
            return;
        }

        auto automationSnap = automationManager_->getSnapshotPublisher().get();
        if (automationSnap == nullptr)
        {
            syncAutomationKnobsToClipBaseValues(*ac);
            return;
        }

        const auto samplePosition = (int64_t) transportController_->getPosition();
        const auto trackId = automationTrackId_.isEmpty() ? ac->getTrackID() : automationTrackId_;
        const auto clipId = automationClipId_.isNotEmpty() ? automationClipId_ : ac->getID();

        const juce::ScopedValueSetter<bool> refreshGuard(isRefreshingFromModel_, true);

        auto syncSliderFromLane = [&](juce::Slider& slider,
                                      const juce::String& parameterId,
                                      double baseValue,
                                      double minValue,
                                      double maxValue)
        {
            double displayValue = baseValue;
            if (auto* lane = automationSnap->findLane(trackId, parameterId))
                if (lane->enabled)
                    displayValue = (double) lane->getValueAtSample(samplePosition, (float) baseValue);

            slider.setValue(juce::jlimit(minValue, maxValue, displayValue), juce::dontSendNotification);
        };

        syncSliderFromLane(gainCell_->slider,
                           AutomationLaneCore::makeClipGainParameterId(clipId),
                           ac->getGain(), 0.0, 4.0);
        syncSliderFromLane(panCell_->slider,
                           AutomationLaneCore::makeClipPanParameterId(clipId),
                           ac->getClipPan(), -1.0, 1.0);
        syncSliderFromLane(fadeInCell_->slider,
                           AutomationLaneCore::makeClipFadeInParameterId(clipId),
                           (double) ac->getFadeInLength(), 0.0, (double) ac->getLength());
        syncSliderFromLane(fadeOutCell_->slider,
                           AutomationLaneCore::makeClipFadeOutParameterId(clipId),
                           (double) ac->getFadeOutLength(), 0.0, (double) ac->getLength());
        syncSliderFromLane(pitchCell_->slider,
                           AutomationLaneCore::makeClipPitchParameterId(clipId),
                           ac->getPitchTargetSemitones(), -36.0, 36.0);
        syncSliderFromLane(stretchCell_->slider,
                           AutomationLaneCore::makeClipStretchParameterId(clipId),
                           ac->getTimeStretch(), 0.1, 4.0);
    }

    bool isAnyAutomationKnobUserControlled() const
    {
        for (auto* slider : { &gainCell_->slider, &panCell_->slider, &fadeInCell_->slider, &fadeOutCell_->slider, &pitchCell_->slider, &stretchCell_->slider })
            if (slider != nullptr && (slider->isMouseOverOrDragging(true) || (slider->hasKeyboardFocus(true) && juce::Component::isMouseButtonDownAnywhere())))
                return true;

        return false;
    }

    void toggleFxFloatingPanel()
    {
        if (!fxFloatingPanel_) return;

        // Ensure the panel and non-interactive cable overlay are added to a visible parent
        if (!fxFloatingPanel_->getParentComponent())
        {
            // Try to add to top-level component
            auto* topLevel = getTopLevelComponent();
            if (topLevel)
            {
                if (fxCableOverlay_ && !fxCableOverlay_->getParentComponent())
                {
                    topLevel->addAndMakeVisible(fxCableOverlay_.get());
                    fxCableOverlay_->setBounds(topLevel->getLocalBounds());
                    fxCableOverlay_->setVisible(false);
                }
                topLevel->addAndMakeVisible(fxFloatingPanel_.get());
                DBG("[ClipPropertiesPanel] FX panel added to top-level component");
            }
            else
            {
                DBG("[ClipPropertiesPanel] Warning: No top-level component found for FX panel");
                return;
            }
        }

        if (fxFloatingPanel_->isShowing())
        {
            hideFxFloatingPanel();
            DBG("[ClipPropertiesPanel] FX panel hidden");
        }
        else
        {
            updateFxPanelPosition(true);
            if (fxCableOverlay_)
            {
                fxCableOverlay_->setVisible(true);
                fxCableOverlay_->toFront(false);
            }
            fxFloatingPanel_->show(fxPanelAnchor_);
            fxFloatingPanel_->toFront(true);
            DBG("[ClipPropertiesPanel] FX panel shown at " << fxFloatingPanel_->getBounds().toString());
        }
    }

    void hideFxFloatingPanel()
    {
        if (fxFloatingPanel_)
            fxFloatingPanel_->hide();
        if (fxCableOverlay_)
            fxCableOverlay_->setVisible(false);
    }

    void updateFxPanelPosition(bool snap = false)
    {
        if (!fxFloatingPanel_ || !fxPanelBtn_) return;

        // Get button position in screen coordinates
        auto btnBounds = fxPanelBtn_->getScreenBounds();

        // Get top-level component
        auto* topLevel = getTopLevelComponent();
        if (!topLevel) return;

        // Convert button center to top-level coordinates for anchor
        fxPanelAnchor_ = topLevel->getLocalPoint(nullptr, juce::Point<int>(btnBounds.getCentreX(), btnBounds.getCentreY()));

        // Position the visible pane to the LEFT of clip properties.
        const int visiblePanelWidth = ClipFxFloatingPanel::kPanelWidth;
        const int offsetX = -visiblePanelWidth - 16;
        const int offsetY = -100;

        // Get clip properties window position in top-level coordinates
        auto windowBounds = getScreenBounds();
        auto windowTopLeft = topLevel->getLocalPoint(nullptr, windowBounds.getTopLeft());

        auto panelPos = juce::Point<int>(
            windowTopLeft.x + offsetX,
            windowTopLeft.y + offsetY
        );

        auto baseTarget = panelPos.toFloat();
        auto windowTravel = baseTarget - fxPanelLastBaseTarget_;

        if (snap || !fxSpringInitialised_)
        {
            fxPanelPullOffset_ = {};
            fxPanelLastBaseTarget_ = baseTarget;
        }
        else
        {
            // Fun elastic pull: when Clip Properties is dragged, the FX pane
            // separates more from it before rubber-banding back.
            juce::Point<float> injectedPull(-windowTravel.x * 2.35f, -windowTravel.y * 1.15f);
            fxPanelPullOffset_ = fxPanelPullOffset_ * 0.90f + injectedPull;
            fxPanelPullOffset_.x = juce::jlimit(-260.f, 260.f, fxPanelPullOffset_.x);
            fxPanelPullOffset_.y = juce::jlimit(-160.f, 160.f, fxPanelPullOffset_.y);
            fxPanelLastBaseTarget_ = baseTarget;
        }

        fxPanelPullOffset_ *= 0.955f;
        auto panelTarget = baseTarget + fxPanelPullOffset_;

        if (snap || !fxSpringInitialised_)
        {
            fxPanelSpringPos_ = panelTarget;
            fxPanelVelocity_ = {};
            fxSpringInitialised_ = true;
        }
        else
        {
            auto delta = panelTarget - fxPanelSpringPos_;
            fxPanelVelocity_ = fxPanelVelocity_ * 0.90f + delta * 0.055f;
            fxPanelSpringPos_ += fxPanelVelocity_;
        }

        auto animatedPanelPos = juce::Point<int>((int) std::round(fxPanelSpringPos_.x),
                                                (int) std::round(fxPanelSpringPos_.y));

        fxFloatingPanel_->setTopLeftPosition(animatedPanelPos);
        if (fxCableOverlay_)
        {
            fxCableOverlay_->setBounds(topLevel->getLocalBounds());
            fxCableOverlay_->setCable({ animatedPanelPos.x + visiblePanelWidth, animatedPanelPos.y + 24 }, fxPanelAnchor_, 1.0f);
        }

        DBG("[ClipPropertiesPanel] FX panel positioned at " << animatedPanelPos.toString() << ", anchor at " << fxPanelAnchor_.toString());
    }

    // ── Automate dropdown / waveform overlay ─────────────────────────────────
    struct AutomationTargetDef
    {
        int menuId = 0;
        juce::String parameterId;
        juce::String displayName;
    };

    std::vector<AutomationTargetDef> getAutomationTargets() const
    {
        const juce::String clipId = automationClipId_.isNotEmpty()
            ? automationClipId_ : (clip_ ? clip_->getID() : juce::String());
        return {
            { 1, AutomationLaneCore::makeClipGainParameterId(clipId), "Clip Gain" },
            { 2, AutomationLaneCore::makeClipPanParameterId(clipId), "Clip Pan" },
            { 3, AutomationLaneCore::makeClipFadeInParameterId(clipId), "Fade In" },
            { 4, AutomationLaneCore::makeClipFadeOutParameterId(clipId), "Fade Out" },
            { 5, AutomationLaneCore::makeClipPitchParameterId(clipId), "Clip Pitch" },
        };
    }

    DAW::AutomationQuickCreateCore::ControlTarget makeAutomationTarget(const juce::String& parameterId,
                                                                        const juce::String& displayName) const
    {
        DAW::AutomationQuickCreateCore::ControlTarget target;
        target.trackId = automationTrackId_;
        target.parameterId = parameterId;
        target.displayName = displayName;
        target.regionStartSample = clip_ ? (int64_t)clip_->getStartPosition() : 0;
        target.regionLengthSamples = clip_ ? (int64_t)clip_->getLength() : 0;
        target.sampleRate = sr_;
        target.defaultValue = getCurrentAutomationDefaultValue(parameterId);
        return target;
    }

    float getCurrentAutomationDefaultValue(const juce::String& parameterId) const
    {
        if (auto* ac = dynamic_cast<AudioClip*>(clip_))
        {
            if (parameterId.endsWith(AutomationLaneCore::clipGainSuffix))
                return ac->getGain();

            if (parameterId.endsWith(AutomationLaneCore::clipPanSuffix))
                return ac->getClipPan();

            if (parameterId.endsWith(AutomationLaneCore::clipFadeInSuffix))
                return (float) ac->getFadeInLength();

            if (parameterId.endsWith(AutomationLaneCore::clipFadeOutSuffix))
                return (float) ac->getFadeOutLength();

            if (parameterId.endsWith(AutomationLaneCore::clipTapeStopSuffix))
                return 0.0f;

            if (parameterId.endsWith(AutomationLaneCore::clipPitchSuffix))
                return ac->getPitchTargetSemitones();
        }

        return 0.0f;
    }

    static bool isClipPropertiesOnlyAutomationParameter(const juce::String& parameterId) noexcept
    {
        return parameterId.endsWith(AutomationLaneCore::clipGainSuffix)
            || parameterId.endsWith(AutomationLaneCore::clipPanSuffix)
            || parameterId.endsWith(AutomationLaneCore::clipFadeInSuffix)
            || parameterId.endsWith(AutomationLaneCore::clipFadeOutSuffix)
            || parameterId.endsWith(AutomationLaneCore::clipTapeStopSuffix)
            || parameterId.endsWith(AutomationLaneCore::clipPitchSuffix);
    }

    void initialiseLaneForCurrentClipValue(AutomationLaneCore& lane, float defaultValue) const
    {
        lane.setDefaultValue(defaultValue);

        if (!lane.points.empty() || clip_ == nullptr)
            return;

        const auto start = (int64_t) clip_->getStartPosition();
        const auto mid = start + juce::jmax<int64_t>(1, (int64_t) clip_->getLength()) / 2;
        const auto end = start + juce::jmax<int64_t>(1, (int64_t) clip_->getLength());
        lane.addPoint(start, defaultValue);

        if (lane.getParameterId().endsWith(AutomationLaneCore::clipTapeStopSuffix))
        {
            lane.addPoint(mid, 0.75f);
            lane.addPoint(end, 1.0f);
            lane.setCurveToNext(0, AutomationCurveType::TapeBrake);
            lane.setCurveToNext(1, AutomationCurveType::TapeBrake);
            lane.setTensionToNext(0, 0.2f);
            lane.setTensionToNext(1, 0.65f);
        }
        else
        {
            lane.addPoint(mid, defaultValue);
            lane.addPoint(end, defaultValue);
        }
    }

    void setSelectedAutomationTarget(const juce::String& parameterId,
                                     const juce::String& displayName,
                                     bool createIfNeeded)
    {
        if (parameterId.isEmpty())
            return;

        selectedAutomationParameterId_ = parameterId;
        selectedAutomationDisplayName_ = displayName;
        automationMode_ = automationManager_ != nullptr && !automationTrackId_.isEmpty();
        waveformVisible_ = true;
        waveBtn_->active = true;

        auto target = makeAutomationTarget(parameterId, displayName);
        if (createIfNeeded && automationCallbacks_.createLane
            && !isClipPropertiesOnlyAutomationParameter(parameterId))
            automationCallbacks_.createLane(target);

        if (automationManager_ != nullptr)
        {
            auto& lane = automationManager_->getOrCreateLane(automationTrackId_, parameterId);
            initialiseLaneForCurrentClipValue(lane, target.defaultValue);
            lane.setVisible(true);
            lane.setEnabled(true);
        }
        if (automationCallbacks_.showLane)
            automationCallbacks_.showLane(target);

        applyAutomationOverlay();
        if (automateBtn_)
        {
            automateBtn_->label = "Auto: " + displayName;
            automateBtn_->active = true;
            automateBtn_->repaint();
        }
        if (onResizeRequest)
            onResizeRequest(430, 390);
        resized();
        repaint();
    }

    void applyAutomationOverlay()
    {
        wave_.setVisible(true);
        wave_.toFront(false);
        wave_.setAutomationOverlay(automationManager_, automationTrackId_, automationClipId_,
                                   automationMode_, selectedAutomationParameterId_, selectedAutomationDisplayName_);
        updateAutomationBypassButton();
        wave_.repaint();
    }

    void toggleSelectedAutomationBypass()
    {
        if (automationManager_ == nullptr || automationTrackId_.isEmpty() || selectedAutomationParameterId_.isEmpty())
            return;

        bool enabled = true;
        if (const auto* lane = automationManager_->findLane(automationTrackId_, selectedAutomationParameterId_))
            enabled = lane->isEnabled();

        automationManager_->setLaneEnabled(automationTrackId_, selectedAutomationParameterId_, !enabled);
        updateAutomationBypassButton();
        wave_.repaint();
    }

    void updateAutomationBypassButton()
    {
        if (!automationBypassBtn_)
            return;

        const bool hasSelection = automationMode_ && selectedAutomationParameterId_.isNotEmpty();
        automationBypassBtn_->setVisible(clip_ != nullptr && hasSelection);

        bool enabled = true;
        if (hasSelection && automationManager_ != nullptr)
            if (const auto* lane = automationManager_->findLane(automationTrackId_, selectedAutomationParameterId_))
                enabled = lane->isEnabled();

        automationBypassBtn_->label = enabled ? "Auto On" : "Bypassed";
        automationBypassBtn_->active = !enabled;
        automationBypassBtn_->repaint();
    }

    /** Clip-properties counterpart of the track "automation visibility"
     *  button: shows/hides this clip's automation lane rows in the
     *  arrangement timeline. */
    void toggleClipAutomationVisibility()
    {
        if (automationManager_ == nullptr || automationTrackId_.isEmpty()
            || automationCallbacks_.showLane == nullptr
            || automationCallbacks_.hideLane == nullptr)
            return;

        autoLanesVisible_ = ! autoLanesVisible_;

        const auto applyParam = [this](const juce::String& parameterId)
        {
            if (parameterId.isEmpty())
                return;

            DAW::AutomationQuickCreateCore::ControlTarget t;
            t.trackId = automationTrackId_;
            t.parameterId = parameterId;
            t.displayName = parameterId;

            if (autoLanesVisible_)
            {
                if (automationCallbacks_.laneExists
                    && ! automationCallbacks_.laneExists(automationTrackId_, parameterId))
                    return;   // nothing to reveal
                automationCallbacks_.showLane(t);
            }
            else
            {
                automationCallbacks_.hideLane(t);
            }
        };

        applyParam(DAW::AutomationLaneCore::makeClipPitchParameterId(automationClipId_));
        applyParam(DAW::AutomationLaneCore::makeClipStretchParameterId(automationClipId_));
        applyParam(DAW::AutomationLaneCore::makeClipGainParameterId(automationClipId_));
        applyParam(DAW::AutomationLaneCore::makeClipPanParameterId(automationClipId_));
        applyParam(DAW::AutomationLaneCore::makeClipTapeStopParameterId(automationClipId_));

        // Mirror the same state inside this window: the automation overlay and
        // the Automate button follow the Lanes button.
        if (autoLanesVisible_)
        {
            if (automationManager_ != nullptr)
            {
                automationMode_ = true;
                if (selectedAutomationParameterId_.isEmpty())
                    setSelectedAutomationTarget(
                        DAW::AutomationLaneCore::makeClipPitchParameterId(automationClipId_),
                        "Clip Pitch", false);
                applyAutomationOverlay();
            }
        }
        else
        {
            automationMode_ = false;
            selectedAutomationParameterId_.clear();
            selectedAutomationDisplayName_.clear();
            wave_.setAutomationOverlay(nullptr, {}, {}, false);
        }

        if (automateBtn_)
        {
            automateBtn_->active = automationMode_ && selectedAutomationParameterId_.isNotEmpty();
            automateBtn_->label = automateBtn_->active
                ? ("Auto: " + selectedAutomationDisplayName_) : "Automate";
            automateBtn_->repaint();
        }

        if (autoVisBtn_)
        {
            autoVisBtn_->label  = autoLanesVisible_ ? "Lanes On" : "Lanes Off";
            autoVisBtn_->active = autoLanesVisible_;
            autoVisBtn_->repaint();
        }

        resized();
        repaint();
    }

    bool autoLanesVisible_ = true;

    void showAutomationDropdown()
    {
        if (!clip_)
            return;

        if (!automationMode_ || automationManager_ == nullptr || automationTrackId_.isEmpty())
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Automation not connected", false, false);
            menu.addSeparator();
            menu.addItem(2, "Select a timeline clip again to connect automation", false, false);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(automateBtn_.get()), nullptr);
            return;
        }

        juce::PopupMenu menu;
        for (const auto& target : getAutomationTargets())
        {
            const bool selected = selectedAutomationParameterId_ == target.parameterId;
            const bool exists = automationCallbacks_.laneExists
                ? automationCallbacks_.laneExists(automationTrackId_, target.parameterId)
                : automationManager_->findLane(automationTrackId_, target.parameterId) != nullptr;
            menu.addItem(target.menuId, (exists ? "✓ " : "") + target.displayName, true, selected);
        }
        menu.addSeparator();
        menu.addItem(101, "Clear Current Automation", !selectedAutomationParameterId_.isEmpty());
        menu.addItem(102, "Clear All Clip Automation", true);
        menu.addItem(100, "Hide Automation Overlay", true, false);
        menu.addItem(103, "Show Automation Overlay",
                     automationManager_ != nullptr && automationTrackId_.isNotEmpty());

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(automateBtn_.get()),
            [this](int result)
            {
                if (result == 103)
                {
                    if (automationManager_ != nullptr && automationTrackId_.isNotEmpty())
                    {
                        automationMode_ = true;
                        if (selectedAutomationParameterId_.isEmpty())
                            setSelectedAutomationTarget(
                                DAW::AutomationLaneCore::makeClipPitchParameterId(automationClipId_),
                                "Clip Pitch", false);
                        applyAutomationOverlay();
                        if (automateBtn_)
                        {
                            automateBtn_->active = true;
                            automateBtn_->label = "Auto: " + selectedAutomationDisplayName_;
                            automateBtn_->repaint();
                        }
                        resized();
                        repaint();
                    }
                    return;
                }
                if (result == 100)
                {
                    automationMode_ = false;
                    selectedAutomationParameterId_.clear();
                    selectedAutomationDisplayName_.clear();
                    wave_.setAutomationOverlay(nullptr, {}, {}, false);

                    // Hide the clip's automation lanes in the arrangement too —
                    // this used to hide only this window's overlay, leaving the
                    // timeline lines visible with no way to dismiss them.
                    if (automationCallbacks_.hideLane && automationTrackId_.isNotEmpty())
                    {
                        const auto hideParam = [this](const juce::String& parameterId)
                        {
                            if (parameterId.isEmpty())
                                return;
                            DAW::AutomationQuickCreateCore::ControlTarget t;
                            t.trackId = automationTrackId_;
                            t.parameterId = parameterId;
                            t.displayName = parameterId;
                            automationCallbacks_.hideLane(t);
                        };
                        hideParam(DAW::AutomationLaneCore::makeClipPitchParameterId(automationClipId_));
                        hideParam(DAW::AutomationLaneCore::makeClipStretchParameterId(automationClipId_));
                        hideParam(DAW::AutomationLaneCore::makeClipGainParameterId(automationClipId_));
                        hideParam(DAW::AutomationLaneCore::makeClipPanParameterId(automationClipId_));
                        hideParam(DAW::AutomationLaneCore::makeClipTapeStopParameterId(automationClipId_));
                    }

                    if (automateBtn_)
                    {
                        automateBtn_->label = "Automate";
                        automateBtn_->active = false;
                        automateBtn_->repaint();
                    }
                    resized();
                    repaint();
                    return;
                }

                if (result == 101)
                {
                    clearSelectedAutomationLane();
                    return;
                }

                if (result == 102)
                {
                    clearAllClipAutomationLanes();
                    return;
                }

                for (const auto& target : getAutomationTargets())
                {
                    if (target.menuId == result)
                    {
                        setSelectedAutomationTarget(target.parameterId, target.displayName, true);
                        return;
                    }
                }
            });
    }

    void clearSelectedAutomationLane()
    {
        if (automationManager_ == nullptr || automationTrackId_.isEmpty() || selectedAutomationParameterId_.isEmpty())
            return;

        auto& lane = automationManager_->getOrCreateLane(automationTrackId_, selectedAutomationParameterId_);
        lane.clear();
        lane.setVisible(true);
        lane.setEnabled(true);
        initialiseLaneForCurrentClipValue(lane, getCurrentAutomationDefaultValue(selectedAutomationParameterId_));
        automationManager_->publishSnapshot();
        updateAutomationBypassButton();
        wave_.repaint();
    }

    void clearAllClipAutomationLanes()
    {
        if (automationManager_ == nullptr || automationTrackId_.isEmpty())
            return;

        for (const auto& target : getAutomationTargets())
        {
            auto& lane = automationManager_->getOrCreateLane(automationTrackId_, target.parameterId);
            lane.clear();
            lane.setVisible(true);
            lane.setEnabled(true);
            initialiseLaneForCurrentClipValue(lane, getCurrentAutomationDefaultValue(target.parameterId));
        }

        automationManager_->publishSnapshot();
        updateAutomationBypassButton();
        wave_.repaint();
    }

    void toggleWaveform()
    {
        waveformVisible_ = !waveformVisible_;
        waveBtn_->active = waveformVisible_;
        wave_.setVisible(waveformVisible_);

        // Request window resize via callback
        if (onResizeRequest)
        {
            const int waveformHeight = 200; // Approximate waveform height
            const int knobsAndButtonsHeight = 180; // Height without waveform
            const int newHeight = waveformVisible_ ? (knobsAndButtonsHeight + waveformHeight) : knobsAndButtonsHeight;
            onResizeRequest(430, newHeight); // Keep same width, adjust height
        }

        resized();
        repaint();
    }

    struct AutomationRow
    {
        juce::String parameterId;
        std::unique_ptr<juce::Label>      nameLbl;
        std::unique_ptr<juce::TextButton> createBtn;
        std::unique_ptr<juce::TextButton> showBtn;
        std::unique_ptr<juce::TextButton> hideBtn;
        std::unique_ptr<juce::TextButton> clearBtn;
    };

    void rebuildAutomationRows()
    {
        for (auto& r : automationRows_)
        {
            if (r.nameLbl)   removeChildComponent(r.nameLbl.get());
            if (r.createBtn) removeChildComponent(r.createBtn.get());
            if (r.showBtn)   removeChildComponent(r.showBtn.get());
            if (r.hideBtn)   removeChildComponent(r.hideBtn.get());
            if (r.clearBtn)  removeChildComponent(r.clearBtn.get());
        }
        automationRows_.clear();

        if (!automationMode_ || automationCallbacks_.createLane == nullptr)
            return;

        const juce::String clipPitchId   = AutomationLaneCore::makeClipPitchParameterId(automationClipId_);
        const juce::String clipStretchId = AutomationLaneCore::makeClipStretchParameterId(automationClipId_);

        struct ParamDef { juce::String id; juce::String name; };
        const ParamDef params[] = {
            { AutomationLaneCore::trackVolumeParameterId, "Track Volume" },
            { AutomationLaneCore::trackPanParameterId,    "Track Pan"    },
            { AutomationLaneCore::makeClipTapeStopParameterId(automationClipId_), "Tape Stop"  },
            { clipPitchId,   "Clip Pitch"   },
            { clipStretchId, "Clip Stretch" },
        };

        auto makeBtn = [this](const juce::String& txt, juce::Colour bg) {
            auto b = std::make_unique<juce::TextButton>(txt);
            b->setColour(juce::TextButton::buttonColourId,  bg);
            b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            addAndMakeVisible(*b);
            return b;
        };

        for (const auto& p : params)
        {
            AutomationRow row;
            row.parameterId = p.id;

            row.nameLbl = std::make_unique<juce::Label>();
            row.nameLbl->setText(p.name, juce::dontSendNotification);
            row.nameLbl->setFont(juce::Font(11.f));
            row.nameLbl->setColour(juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible(*row.nameLbl);

            row.createBtn = makeBtn("Create", juce::Colour(0xff2a6e3a));
            row.showBtn   = makeBtn("Show",   juce::Colour(0xff1e4a7a));
            row.hideBtn   = makeBtn("Hide",   juce::Colour(0xff3a3a3a));
            row.clearBtn  = makeBtn("Clear",  juce::Colour(0xff7a2a2a));

            const juce::String paramId = p.id;
            DAW::TrackID tid = automationTrackId_;

            row.createBtn->onClick = [this, paramId, tid] {
                if (automationCallbacks_.createLane) {
                    DAW::AutomationQuickCreateCore::ControlTarget t;
                    t.trackId = tid; t.parameterId = paramId; t.displayName = paramId;
                    t.regionStartSample = clip_ ? (int64_t)clip_->getStartPosition() : 0;
                    t.regionLengthSamples = clip_ ? (int64_t)clip_->getLength() : 0;
                    t.sampleRate = sr_;
                    automationCallbacks_.createLane(t);
                }
                if (automationManager_) {
                    automationManager_->setLaneVisible(tid, paramId, true);
                    wave_.setAutomationOverlay(automationManager_, automationTrackId_, automationClipId_, true);
                }
                wave_.repaint();
                refreshAutomationRowStates();
            };
            row.showBtn->onClick = [this, paramId, tid] {
                if (automationManager_) {
                    automationManager_->setLaneVisible(tid, paramId, true);
                    wave_.setAutomationOverlay(automationManager_, automationTrackId_, automationClipId_, true);
                }
                wave_.repaint();
                refreshAutomationRowStates();
            };
            row.hideBtn->onClick = [this, paramId, tid] {
                if (automationManager_)
                    automationManager_->setLaneVisible(tid, paramId, false);
                wave_.repaint();
                refreshAutomationRowStates();
            };
            row.clearBtn->onClick = [this, paramId, tid] {
                if (automationCallbacks_.clearLane) {
                    DAW::AutomationQuickCreateCore::ControlTarget t;
                    t.trackId = tid; t.parameterId = paramId;
                    automationCallbacks_.clearLane(t);
                }
                wave_.repaint();
                refreshAutomationRowStates();
            };

            automationRows_.push_back(std::move(row));
        }

        refreshAutomationRowStates();
    }

    void refreshAutomationRowStates()
    {
        for (auto& r : automationRows_)
        {
            const bool exists = automationCallbacks_.laneExists
                ? automationCallbacks_.laneExists(automationTrackId_, r.parameterId)
                : false;
            if (r.createBtn) r.createBtn->setAlpha(exists ? 0.5f : 1.0f);
            if (r.showBtn)   r.showBtn->setEnabled(exists);
            if (r.hideBtn)   r.hideBtn->setEnabled(exists);
            if (r.clearBtn)  r.clearBtn->setEnabled(exists);
        }
    }

private:
    // Returns category for visual sorting only. All installed plugins are valid clip FX.
    static int pluginCategory(const juce::PluginDescription& d)
    {
        const juce::String n  = d.name.toLowerCase();
        const juce::String mf = d.manufacturerName.toLowerCase();
        // ARA: Melodyne, Auto-Tune, Revoice, VocAlign, Vivo, Elastic Audio
        if (n.contains("melodyne"))  return 0;
        if (n.contains("autotune") || n.contains("auto-tune") || n.contains("auto tune")) return 0;
        if (n.contains("revoice") || n.contains("vocalign"))   return 0;
        if (n.contains("vivo") || mf.contains("vivo"))         return 0;
        if (n.contains("elastic") && n.contains("audio"))      return 0;
        if (n.contains("zynaptiq") || mf.contains("zynaptiq")) return 0;
        if (n.contains("repitch") || n.contains("re-pitch"))   return 0;
        // Time stretch / varispeed
        if (n.contains("stretch") || n.contains("timestretch") ||
            n.contains("time stretch") || n.contains("varispeed") ||
            n.contains("elastique"))                           return 1;
        // Pitch / tune / voice
        if (n.contains("pitch") || n.contains("tune") ||
            n.contains("vocal") || n.contains("voice") ||
            n.contains("harmony") || n.contains("choir"))      return 2;
        return 3;
    }

    static const char* categoryName(int cat)
    {
        switch (cat) {
            case 0: return "ARA & Pitch Correction";
            case 1: return "Time Stretch";
            case 2: return "Vocal / Pitch Tools";
            default: return "";
        }
    }

    void rebuildPluginList(const juce::String& filter = {})
    {
        filteredPlugins_.clear();
        listModel_.selectedRow = -1;

        if (!knownPlugins_) { pluginListBox_.updateContent(); return; }

        const juce::String lo = filter.toLowerCase();
        const auto types = knownPlugins_->getTypes();
        for (int i = 0; i < types.size(); ++i)
        {
            const auto& t = types.getReference(i);
            if (!lo.isEmpty() &&
                !t.name.toLowerCase().contains(lo) &&
                !t.manufacturerName.toLowerCase().contains(lo))
                continue;
            filteredPlugins_.push_back(t);
        }

        // Sort by category then name
        std::stable_sort(filteredPlugins_.begin(), filteredPlugins_.end(),
            [](const juce::PluginDescription& a, const juce::PluginDescription& b) {
                int ca = pluginCategory(a), cb = pluginCategory(b);
                return ca != cb ? ca < cb : a.name < b.name;
            });

        listModel_.rows = &filteredPlugins_;
        pluginListBox_.updateContent();
        pluginListBox_.repaint();
    }

    void openSelectedPlugin()
    {
        if (!clip_ || !onOpenClipRegionPlugin) return;
        if (listModel_.selectedRow < 0 || listModel_.selectedRow >= (int)filteredPlugins_.size()) return;
        onOpenClipRegionPlugin(*clip_, filteredPlugins_[listModel_.selectedRow]);
        refreshActivePlugins();
    }

    void showAddPluginMenu()
    {
        if (!clip_ || !knownPlugins_ || !onOpenClipRegionPlugin) return;

        rebuildPluginList();
        if (filteredPlugins_.empty())
            return;

        juce::PopupMenu menu;
        juce::String currentManufacturer;
        juce::PopupMenu manufacturerMenu;
        int itemId = 1;

        auto flushManufacturer = [&]()
        {
            if (currentManufacturer.isNotEmpty())
                menu.addSubMenu(currentManufacturer, manufacturerMenu);
            manufacturerMenu = juce::PopupMenu();
        };

        std::stable_sort(filteredPlugins_.begin(), filteredPlugins_.end(),
            [](const juce::PluginDescription& a, const juce::PluginDescription& b)
            {
                const auto am = a.manufacturerName.isNotEmpty() ? a.manufacturerName : juce::String("Other");
                const auto bm = b.manufacturerName.isNotEmpty() ? b.manufacturerName : juce::String("Other");
                return am != bm ? am < bm : a.name < b.name;
            });

        for (const auto& plugin : filteredPlugins_)
        {
            auto manufacturer = plugin.manufacturerName.isNotEmpty() ? plugin.manufacturerName : juce::String("Other");
            if (manufacturer != currentManufacturer)
            {
                flushManufacturer();
                currentManufacturer = manufacturer;
            }

            manufacturerMenu.addItem(itemId++, plugin.name + "  [" + plugin.pluginFormatName + "]");
        }
        flushManufacturer();

        auto options = juce::PopupMenu::Options();
        if (fxFloatingPanel_ != nullptr && fxFloatingPanel_->isVisible())
            options = options.withTargetScreenArea(fxFloatingPanel_->getAddButtonScreenBounds());
        else
            options = options.withTargetComponent(fxPanelBtn_.get());

        menu.showMenuAsync(options,
            [this](int result)
            {
                if (result <= 0) return;
                const int index = result - 1;
                if (index >= 0 && index < (int) filteredPlugins_.size())
                {
                    onOpenClipRegionPlugin(*clip_, filteredPlugins_[(size_t) index]);
                    refreshActivePlugins();
                }
            });
    }

    // juce::Slider::Listener
    void sliderValueChanged(juce::Slider* s) override
    {
        if (!clip_ || isRefreshingFromModel_) return;
        if (auto* ac = dynamic_cast<AudioClip*>(clip_))
        {
            DAW::PitchWriteAudit::ScopedSource pitchWriteScope("ClipPropertiesUI");
            if      (s == &gainCell_->slider)    ac->setGain((float)s->getValue());
            else if (s == &pitchCell_->slider)
            {
                // Pitch adjustments are pitch-only: if the clip is still in the
                // coupled Resample (tape/DJ) mode, move it to PitchOnly so the
                // knob shifts pitch without changing duration. Mirrors the
                // ArrangementEditor's autoSwitchModeForPitchEdit() pattern and
                // is idempotent — after the first touch the mode is PitchOnly.
                if (ac->getTimePitchMode() == TimePitchModeIds::Resample)
                    ac->setTimePitchMode(TimePitchModeIds::PitchOnly);
                ac->setPitchTargetFromUI((float)s->getValue());
            }
            else if (s == &fineTuneCell_->slider)
            {
                if (ac->getTimePitchMode() == TimePitchModeIds::Resample)
                    ac->setTimePitchMode(TimePitchModeIds::PitchOnly);
                ac->setClipFineTune((float)s->getValue());
            }
            else if (s == &panCell_->slider)     ac->setClipPan((float)s->getValue());
            else if (s == &stretchCell_->slider) ac->setTimeStretch((float)s->getValue());
            else if (s == &fadeInCell_->slider)  ac->setFadeInLength((SamplePosition)std::llround(s->getValue()));
            else if (s == &fadeOutCell_->slider) ac->setFadeOutLength((SamplePosition)std::llround(s->getValue()));

            // NOTE: setTimePitchMode() is intentionally NOT called for the
            // gain/pan/stretch/fade knobs. The mode must be set explicitly by
            // the user via a mode selector, never implicitly by a knob move.
            // Calling setTimePitchMode(0) here was the ghost writer that forced
            // Resample on every knob touch. The pitch/fine-tune exception above
            // exists because this window has no mode selector and the user
            // expects pitch edits to be pitch-only (same as the ArrangementEditor's
            // autoSwitchModeForPitchEdit).
        }
        wave_.repaint();
    }

    // juce::Label::Listener
    void labelTextChanged(juce::Label*) override
    { if (clip_) clip_->setName(nameLabel_.getText()); }

    // juce::TextEditor::Listener
    void textEditorTextChanged(juce::TextEditor& e) override
    { if (&e == &pluginSearch_) rebuildPluginList(pluginSearch_.getText()); }

    void normalizeClip()
    {
        if (!clip_ || !afm_) return;
        auto* ac = dynamic_cast<AudioClip*>(clip_);
        if (!ac) return;

        auto* buf = afm_->getBuffer(ac->getID());
        if (!buf || buf->getNumSamples() == 0) return;

        const auto sourceStart = juce::jlimit<SamplePosition>(0, (SamplePosition)buf->getNumSamples() - 1,
            (SamplePosition)juce::jmax<int64_t>((int64_t)ac->getSourceOffset(), ac->getSourceStartSample()));
        const auto sourceEnd = ac->getSourceEndSample() > 0
            ? juce::jlimit<SamplePosition>(sourceStart + 1, (SamplePosition)buf->getNumSamples(), (SamplePosition)ac->getSourceEndSample())
            : (SamplePosition)buf->getNumSamples();

        const auto result = afm_->togglePeakNormalize(ac->getID(), sourceStart, sourceEnd, 0.0f);
        if (result == AudioFileManager::NormalizeResult::Applied)
        {
            const bool normalizedNow = afm_->isPeakNormalized(ac->getID());
            if (normalizedNow)
                ac->setGain(1.0f);

            normalizeBtn_->active = normalizedNow;
            normalizeBtn_->repaint();
            wave_.repaint();
            if (auto* parent = getParentComponent())
                parent->repaint();
            refresh();
        }
    }

    void reverseClip()
    {
        if (!clip_) return;
        auto* ac = dynamic_cast<AudioClip*>(clip_);
        if (!ac) return;

        ac->setReversed(!ac->isReversed());
        reverseBtn_->active = ac->isReversed();
        wave_.repaint();
        reverseBtn_->repaint();
        repaint();
    }

    void cycleFadeCurve(bool fadeIn)
    {
        auto* ac = dynamic_cast<AudioClip*>(clip_);
        if (!ac) return;

        if (fadeIn)
        {
            const int next = (ac->getFadeInCurve() + 1) % 4;
            ac->setFadeInCurve(next);
            fadeInCurveBtn_->setCurve(next);
        }
        else
        {
            const int next = (ac->getFadeOutCurve() + 1) % 4;
            ac->setFadeOutCurve(next);
            fadeOutCurveBtn_->setCurve(next);
        }

        wave_.repaint();
        repaint();
    }

    std::unique_ptr<juce::ColourSelector> colourSelector_; // owned externally via CallOutBox

    // ── Members ───────────────────────────────────────────────────────────────
    Clip*                    clip_         = nullptr;
    AudioFileManager*        afm_          = nullptr;
    double                   sr_           = 44100.0;
    const juce::KnownPluginList* knownPlugins_ = nullptr;
    ClipRegionPluginCore* clipRegionPluginCore_ = nullptr;

    juce::Label       nameLabel_, typeBadge_;
    CPWWavePreview    wave_;

    std::unique_ptr<CPWKnobCell> gainCell_, pitchCell_, fineTuneCell_, panCell_, stretchCell_, fadeInCell_, fadeOutCell_;
    std::unique_ptr<CPWCurveBtn> fadeInCurveBtn_, fadeOutCurveBtn_;
    std::unique_ptr<CPWPillBtn>  muteBtn_, pianoBtn_, colourBtn_, fxPanelBtn_, waveBtn_, normalizeBtn_, reverseBtn_, automateBtn_, automationBypassBtn_;
    // Clip-properties counterpart of the track "automation visibility" button.
    std::unique_ptr<CPWPillBtn>  autoVisBtn_;

    CPWSep sep1_, sep2_, sep3_, sep4_;

    juce::TextEditor pluginSearch_;
    juce::ListBox    activePluginListBox_;  // Legacy (hidden, kept for compatibility)
    juce::ListBox    pluginListBox_;        // Legacy (hidden, kept for compatibility)
    std::vector<ClipRegionPluginCore::EntryInfo> activePlugins_;
    std::vector<juce::PluginDescription> filteredPlugins_;
    ArrangementEditor::ClipAutomationPanelCallbacks automationCallbacks_;
    std::vector<AutomationRow> automationRows_;
    DAW::TrackID automationTrackId_;
    juce::String automationClipId_;
    juce::String selectedAutomationParameterId_;
    juce::String selectedAutomationDisplayName_;
    AutomationManagerCore* automationManager_ = nullptr;
    TransportController* transportController_ = nullptr;

    // Floating FX panel
    std::unique_ptr<ClipFxFloatingPanel> fxFloatingPanel_;
    std::unique_ptr<ClipFxCableOverlay> fxCableOverlay_;
    juce::Point<int>   fxPanelAnchor_;
    juce::Point<float> fxPanelSpringPos_;
    juce::Point<float> fxPanelVelocity_;
    juce::Point<float> fxPanelPullOffset_;
    juce::Point<float> fxPanelLastBaseTarget_;
    bool fxSpringInitialised_ = false;

    bool isRefreshingFromModel_ = false;
    bool waveformVisible_ = true;
    bool automationMode_ = false;
    int refreshTimerHz_ = 0;
};

// ============================================================================
//  ClipPropertiesWindow
// ============================================================================
class ClipPropertiesWindow : public FloatingWindowBase
{
public:
    ClipPropertiesWindow() : FloatingWindowBase("CLIP PROPERTIES")
    {
        setMinimumSize(340, 180);
        setSize(420, 360);
        addAndMakeVisible(panel_);

        // Set up resize callback
        panel_.onResizeRequest = [this](int width, int height)
        {
            setSize(width, height);
        };
    }

    void setClip(Clip* clip, AudioFileManager* afm, double sr)
    {
        clip_ = clip;
        clipId_ = clip != nullptr ? clip->getID() : juce::String();
        audioFileManager_ = afm;
        sampleRate_ = sr;
        panel_.setClip(clip, afm, sr);
        setWindowTitle(clip ? clip->getName() : "Clip Properties");
    }

    juce::String getClipId() const noexcept { return clipId_; }

    bool isShowingClip(const juce::String& clipId) const noexcept
    {
        return clipId_.isNotEmpty() && clipId_ == clipId;
    }

    void clearClip()
    {
        clip_ = nullptr;
        clipId_.clear();
        deactivateAutomationMode();
        setFxPanelOpen(false);
        panel_.setClip(nullptr, audioFileManager_, sampleRate_);
        setWindowTitle("Clip Properties");
    }

    bool isFxPanelOpen() const noexcept { return panel_.isFxFloatingPanelOpen(); }
    void setFxPanelOpen(bool shouldBeOpen) { panel_.setFxFloatingPanelOpen(shouldBeOpen); }

    void setKnownPlugins(const juce::KnownPluginList* list) { panel_.setKnownPlugins(list); }
    void setClipRegionPluginCore(ClipRegionPluginCore* core) { panel_.setClipRegionPluginCore(core); }
    void activateAutomationMode(const DAW::TrackID& trackId,
                                const juce::String& clipId,
                                const ArrangementEditor::ClipAutomationPanelCallbacks& callbacks,
                                AutomationManagerCore* automationManager = nullptr,
                                TransportController* transportController = nullptr)
    {
        panel_.activateAutomationMode(trackId, clipId, callbacks, automationManager, transportController);
    }
    void deactivateAutomationMode() { panel_.deactivateAutomationMode(); }

    ClipPropertiesPanel& getPanel() noexcept { return panel_; }

protected:
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (!isEmbedded() && e.y <= kTitleH && clip_ != nullptr)
        {
            struct RenamePopup final : public juce::Component
            {
                RenamePopup(ClipPropertiesWindow& owner, Clip& clip)
                    : owner_(owner), clip_(clip)
                {
                    addAndMakeVisible(editor_);
                    addAndMakeVisible(ok_);
                    addAndMakeVisible(cancel_);
                    editor_.setText(clip_.getName());
                    editor_.selectAll();
                    ok_.setButtonText("OK");
                    cancel_.setButtonText("Cancel");
                    ok_.onClick = [this]
                    {
                        auto newName = editor_.getText().trim();
                        if (newName.isNotEmpty())
                        {
                            clip_.setName(newName);
                            owner_.setWindowTitle(newName);
                            owner_.panel_.setClip(owner_.clip_, owner_.audioFileManager_, owner_.sampleRate_);
                        }
                        if (auto* parent = findParentComponentOfClass<juce::CallOutBox>())
                            parent->exitModalState(0);
                    };
                    cancel_.onClick = [this]
                    {
                        if (auto* parent = findParentComponentOfClass<juce::CallOutBox>())
                            parent->exitModalState(0);
                    };
                    setSize(240, 70);
                }

                void resized() override
                {
                    auto r = getLocalBounds().reduced(8);
                    editor_.setBounds(r.removeFromTop(24));
                    r.removeFromTop(8);
                    auto buttons = r.removeFromTop(22);
                    cancel_.setBounds(buttons.removeFromRight(70));
                    buttons.removeFromRight(6);
                    ok_.setBounds(buttons.removeFromRight(60));
                }

                ClipPropertiesWindow& owner_;
                Clip& clip_;
                juce::TextEditor editor_;
                juce::TextButton ok_, cancel_;
            };

            juce::CallOutBox::launchAsynchronously(
                std::make_unique<RenamePopup>(*this, *clip_),
                getScreenBounds().withHeight(kTitleH),
                nullptr);

            return;
        }

        FloatingWindowBase::mouseDoubleClick(e);
    }

    void layoutContent() override
    {
        panel_.setBounds(isEmbedded() ? getLocalBounds() : getContentArea());
    }

    void moved() override
    {
        FloatingWindowBase::moved();
        // Notify panel that window moved (will update FX panel position)
        panel_.parentHierarchyChanged();
    }

private:
    ClipPropertiesPanel panel_;
    Clip* clip_ = nullptr;
    juce::String clipId_;
    AudioFileManager* audioFileManager_ = nullptr;
    double sampleRate_ = 44100.0;
};

} // namespace DAW
