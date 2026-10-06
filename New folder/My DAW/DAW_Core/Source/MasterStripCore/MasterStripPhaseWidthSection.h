#pragma once
#include <JuceHeader.h>
#include "../MeteringCore/CorrelationMeterCore.h"
#include "../MeteringCore/StereoWidthCore.h"
#include "../MeteringCore/MonoCheckProcessor.h"
#include "BubblegumInfoPopupCore.h"
#include "MasterStripFeatureInfoCatalog.h"

namespace DAW {

/**
 * MasterStripPhaseWidthSection
 *
 * UI section showing stereo health metrics. Lives on the FRONT side of the
 * master strip (mixing view). Three pieces of information visualised:
 *
 *   1. Goniometer dot/ellipse: shows L/R signal as an angled ellipse —
 *      vertical = mono, horizontal = full stereo, leaning right = phase OK,
 *      leaning left = phase issue.
 *
 *   2. Width % bar: linear bar showing stereo width 0%-100%, with the 50%
 *      "balanced stereo" anchor visible as a tick.
 *
 *   3. MONO CHECK button: toggles audible L+R mono summing for compatibility
 *      check. Lives in this section because it's the natural place — user
 *      sees phase issue, hits MONO to verify it sounds OK in mono.
 *
 * The mono check button is the ONLY interactive element in this section.
 * Everything else is read-only metering.
 *
 * Layout:
 *
 *   ┌───────────────────────────────┐
 *   │ PHASE · WIDTH                 │
 *   │  ◯       ▓▓▓▓▓░░░░░ 64%       │
 *   │ +0.78    ┌─MONO CHECK──┐ PASS │
 *   └───────────────────────────────┘
 *
 * Threading: UI thread, repaints at 30 Hz. Reads atomics from the meter
 * cores. The mono toggle pushes setEnabled() to the MonoCheckProcessor.
 */
class MasterStripPhaseWidthSection : public juce::Component,
                                     private juce::Timer
{
public:
    MasterStripPhaseWidthSection()
    {
        setOpaque(false);
        setInterceptsMouseClicks(true, false);
        startTimerHz(60);
        configureHelpButtons();
        addAndMakeVisible(helpBtnPhase_);
        addAndMakeVisible(helpBtnMono_);
    }

    ~MasterStripPhaseWidthSection() override
    {
        stopTimer();
    }

    void setSources(CorrelationMeterCore* corr,
                    StereoWidthCore*      width,
                    MonoCheckProcessor*   mono)
    {
        correlation_ = corr;
        width_       = width;
        monoCheck_   = mono;
        repaint();
    }

    // ── Component ────────────────────────────────────────────────────

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);

        g.setColour(juce::Colour(0xFF06080D));
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(juce::Colour(0xFF2A2F3D));
        g.drawRoundedRectangle(bounds, 6.0f, 0.5f);

        // Title
        g.setColour(juce::Colour(0xFFFF4F8A));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText("PHASE / WIDTH", bounds.toNearestInt().reduced(8, 4),
                   juce::Justification::topLeft, false);

        // Goniometer
        paintGoniometer(g);

        // Correlation numeric below goniometer
        const float corr = sanitizeCorr((correlation_ != nullptr) ? correlation_->getCorrelation() : 0.0f);
        g.setColour(corrColor(corr));
        g.setFont(juce::Font(8.0f, juce::Font::plain));
        g.drawText(formatCorr(corr), corrLabelBounds_,
                   juce::Justification::centred, false);

        // Width bar
        paintWidthBar(g);

        // Width numeric
        const float widthPct = sanitizeWidth((width_ != nullptr) ? width_->getWidthPercent() : 50.0f);
        g.setColour(juce::Colour(0xFF7A8094));
        g.setFont(juce::Font(8.0f, juce::Font::plain));
        g.drawText("WIDTH " + juce::String((int) std::round(widthPct)) + "%",
                   widthLabelBounds_, juce::Justification::centredLeft, false);

        // MONO CHECK button
        paintMonoButton(g);

        // Pass / Warn LED
        paintPhaseStatusLed(g, corr);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(6, 4);

        const int phaseHeaderH = 14;
        const int phaseBtnSize = juce::jmax(10, juce::jmin(16, phaseHeaderH - 2));
        helpBtnPhase_.setBounds(bounds.getRight() - phaseBtnSize - 2,
                                bounds.getY() + (phaseHeaderH - phaseBtnSize) / 2,
                                phaseBtnSize, phaseBtnSize);

        bounds.removeFromTop(14);  // title

        // Top row: goniometer (left) + width display (right)
        auto topRow = bounds.removeFromTop(50);

        // Goniometer takes left 60px
        auto gonioArea = topRow.removeFromLeft(58);
        goniometerBounds_ = gonioArea.withSizeKeepingCentre(40, 40);
        // Correlation label under goniometer
        corrLabelBounds_ = juce::Rectangle<int>(
            gonioArea.getX(), goniometerBounds_.getBottom() - 4,
            gonioArea.getWidth(), 10);

        topRow.removeFromLeft(4);

        // Width section on the right
        auto widthArea = topRow;
        widthBarBounds_   = widthArea.removeFromTop(8).reduced(2, 1);
        widthArea.removeFromTop(2);
        widthLabelBounds_ = widthArea.removeFromTop(12);
        widthArea.removeFromTop(2);

        // Bottom row: MONO CHECK button + pass led
        bounds.removeFromTop(4);
        auto btnRow = bounds.removeFromTop(20);
        btnRow.removeFromLeft(58 + 4);
        ledBounds_ = btnRow.removeFromRight(26);
        btnRow.removeFromRight(4);
        monoBtnBounds_ = btnRow.reduced(0, 2);

        const int monoBtnH = monoBtnBounds_.getHeight();
        const int monoHelpSize = juce::jmax(10, juce::jmin(16, monoBtnH - 4));
        helpBtnMono_.setBounds(monoBtnBounds_.getRight() + 2,
                               monoBtnBounds_.getY() + (monoBtnH - monoHelpSize) / 2,
                               monoHelpSize, monoHelpSize);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (monoCheck_ == nullptr) return;
        if (monoBtnBounds_.contains(e.getPosition()))
        {
            monoCheck_->setEnabled(! monoCheck_->isEnabled());
            repaint();
        }
    }

private:
    void configureHelpButtons()
    {
        auto styleBtn = [](juce::TextButton& btn, std::function<void()> onClick)
        {
            btn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xFFFF4F8A).withAlpha(0.20f));
            btn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFF4F8A).withAlpha(0.45f));
            btn.setColour(juce::TextButton::textColourOffId,  juce::Colour(0xFFFF4F8A));
            btn.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
            btn.onClick = std::move(onClick);
        };

        styleBtn(helpBtnPhase_, [this]
        {
            BubblegumInfoPopup::showFor(&helpBtnPhase_, MasterStripFeatureInfoCatalog::phaseWidth());
        });
        styleBtn(helpBtnMono_, [this]
        {
            BubblegumInfoPopup::showFor(&helpBtnMono_, MasterStripFeatureInfoCatalog::monoCheck());
        });
    }

    void timerCallback() override
    {
        if (!isShowing())
            return;
        if (correlation_ == nullptr && width_ == nullptr)
            return;

        const float corr  = correlation_ != nullptr ? correlation_->getCorrelation() : 0.0f;
        const float width = width_       != nullptr ? width_->getWidthPercent()       : 50.0f;

        if (corr == lastCorrelation_ && width == lastWidth_)
            return;

        lastCorrelation_ = corr;
        lastWidth_       = width;
        repaint();
    }

    void visibilityChanged() override
    {
        if (isVisible())
            startTimerHz(60);
        else
            stopTimer();
    }

    void paintGoniometer(juce::Graphics& g) const
    {
        // Outer circle background
        g.setColour(juce::Colour(0xFF0F1219));
        g.fillEllipse(goniometerBounds_.toFloat());
        g.setColour(juce::Colour(0xFF2A2F3D));
        g.drawEllipse(goniometerBounds_.toFloat(), 0.5f);

        // Cross axes
        const auto cx = (float) goniometerBounds_.getCentreX();
        const auto cy = (float) goniometerBounds_.getCentreY();
        const auto r  = (float) goniometerBounds_.getWidth() * 0.5f;
        g.setColour(juce::Colour(0xFF2A2F3D));
        g.drawLine(cx, cy - r * 0.85f, cx, cy + r * 0.85f, 0.4f);
        g.drawLine(cx - r * 0.85f, cy, cx + r * 0.85f, cy, 0.4f);

        // The "lissajous-like" ellipse — angle and shape derived from correlation + width
        if (correlation_ == nullptr || width_ == nullptr) return;

        const float corr     = sanitizeCorr(correlation_->getCorrelation());
        const float widthPct = sanitizeWidth(width_->getWidthPercent());

        // Width drives ellipse aspect ratio: 0% = vertical line (mono),
        // 50% = balanced ellipse, 100% = horizontal line (side-only).
        const float widthNorm = juce::jlimit(0.0f, 1.0f, widthPct / 100.0f);
        const float ellipseW = r * 0.35f * (0.3f + widthNorm * 1.4f);
        const float ellipseH = r * 0.55f * (1.4f - widthNorm * 1.0f);

        // Correlation drives rotation: +1 = vertical (perfect mono), 0 = leaning right (45°),
        // -1 = leaning left (135° = phase issue)
        const float angleDeg = juce::jmap(corr, -1.0f, 1.0f, 135.0f, 0.0f);
        const float angleRad = juce::degreesToRadians(angleDeg);

        // Color: green when phase healthy, pink when warning, red when problem
        juce::Colour ellipseColour;
        if (corr < -0.5f)        ellipseColour = juce::Colour(0xFFFF4F8A).withAlpha(0.85f);  // problem
        else if (corr < 0.2f)    ellipseColour = juce::Colour(0xFFFFB3D0).withAlpha(0.7f);   // borderline
        else                     ellipseColour = juce::Colour(0xFFFF4F8A).withAlpha(0.55f);  // healthy

        g.setColour(ellipseColour);
        juce::AffineTransform xform = juce::AffineTransform::rotation(angleRad, cx, cy);
        g.fillEllipse(juce::Rectangle<float>(cx - ellipseW, cy - ellipseH,
                                              ellipseW * 2.0f, ellipseH * 2.0f)
                          .transformedBy(xform));
    }

    void paintWidthBar(juce::Graphics& g) const
    {
        // Background
        g.setColour(juce::Colour(0xFF1A1F2A));
        g.fillRoundedRectangle(widthBarBounds_.toFloat(), 1.0f);

        if (width_ == nullptr) return;

        const float widthPct = sanitizeWidth(width_->getWidthPercent());
        const float fillNorm = juce::jlimit(0.0f, 1.0f, widthPct / 100.0f);
        const float fillW = widthBarBounds_.getWidth() * fillNorm;

        if (fillW > 0.5f)
        {
            g.setColour(juce::Colour(0xFFFF4F8A));
            g.fillRoundedRectangle(juce::Rectangle<float>(
                widthBarBounds_.getX(), widthBarBounds_.getY(),
                fillW, widthBarBounds_.getHeight()), 1.0f);
        }

        // 50% anchor tick
        const float anchorX = widthBarBounds_.getX() + widthBarBounds_.getWidth() * 0.5f;
        g.setColour(juce::Colour(0xFFFFB3D0).withAlpha(0.6f));
        g.drawLine(anchorX, (float) widthBarBounds_.getY(),
                   anchorX, (float) widthBarBounds_.getBottom(), 0.5f);
    }

    void paintMonoButton(juce::Graphics& g) const
    {
        const bool active = (monoCheck_ != nullptr) && monoCheck_->isEnabled();

        g.setColour(active ? juce::Colour(0xFFFF4F8A).withAlpha(0.45f)
                           : juce::Colour(0xFF0F1219));
        g.fillRoundedRectangle(monoBtnBounds_.toFloat(), 3.0f);

        g.setColour(juce::Colour(0xFFFF4F8A));
        g.drawRoundedRectangle(monoBtnBounds_.toFloat(), 3.0f, 0.6f);

        g.setColour(active ? juce::Colour(0xFFFFFFFF) : juce::Colour(0xFFFFB3D0));
        g.setFont(juce::Font(7.0f, juce::Font::bold));
        g.drawFittedText("MONO", monoBtnBounds_,
                         juce::Justification::centred, 1, 0.85f);
    }

    void paintPhaseStatusLed(juce::Graphics& g, float corr) const
    {
        // LED background
        g.setColour(juce::Colour(0xFF0F1219));
        g.fillRoundedRectangle(ledBounds_.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xFF2A2F3D));
        g.drawRoundedRectangle(ledBounds_.toFloat(), 3.0f, 0.4f);

        // Status dot
        const auto cx = (float) ledBounds_.getCentreX();
        const float dotY = (float) ledBounds_.getY() + 6.0f;

        juce::Colour dotColour;
        juce::String label;
        if (corr < -0.5f)        { dotColour = juce::Colour(0xFFFF6B6B); label = "WARN"; }
        else if (corr < 0.2f)    { dotColour = juce::Colour(0xFFEAB308); label = "OK"; }
        else                     { dotColour = juce::Colour(0xFF22C55E); label = "PASS"; }

        g.setColour(dotColour);
        g.fillEllipse(cx - 2.0f, dotY - 2.0f, 4.0f, 4.0f);

        g.setColour(juce::Colour(0xFF7A8094));
        g.setFont(juce::Font(7.0f, juce::Font::plain));
        g.drawText(label, ledBounds_.withTrimmedTop(10),
                   juce::Justification::centredTop, false);
    }

    static juce::String formatCorr(float c)
    {
        c = sanitizeCorr(c);
        return (c >= 0.0f ? "+" : "") + juce::String(c, 2);
    }

    static float sanitizeCorr(float c) noexcept
    {
        return std::isfinite(c) ? juce::jlimit(-1.0f, 1.0f, c) : 0.0f;
    }

    static float sanitizeWidth(float w) noexcept
    {
        return std::isfinite(w) ? juce::jlimit(0.0f, 100.0f, w) : 50.0f;
    }

    static juce::Colour corrColor(float c)
    {
        if (c < -0.5f) return juce::Colour(0xFFFF6B6B);
        if (c < 0.2f)  return juce::Colour(0xFFEAB308);
        return juce::Colour(0xFF7A8094);
    }

    CorrelationMeterCore* correlation_ { nullptr };
    StereoWidthCore*      width_       { nullptr };
    MonoCheckProcessor*   monoCheck_   { nullptr };

    // Cached last-painted values — used to gate repaint() calls in timerCallback.
    float lastCorrelation_ { -999.0f };
    float lastWidth_       { -999.0f };
    juce::TextButton helpBtnPhase_ { "?" };
    juce::TextButton helpBtnMono_ { "?" };

    juce::Rectangle<int> goniometerBounds_;
    juce::Rectangle<int> corrLabelBounds_;
    juce::Rectangle<int> widthBarBounds_;
    juce::Rectangle<int> widthLabelBounds_;
    juce::Rectangle<int> monoBtnBounds_;
    juce::Rectangle<int> ledBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterStripPhaseWidthSection)
};

} // namespace DAW
