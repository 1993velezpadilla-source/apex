#pragma once
#include <JuceHeader.h>
#include "../MeteringCore/MeteringFacadeCore.h"
#include "BubblegumInfoPopupCore.h"
#include "MasterStripFeatureInfoCatalog.h"

namespace DAW {

/**
 * MasterStripMeteringSection
 *
 * UI section for the metering suite. Lives on the FRONT side of the master
 * strip (mixing view). Displays comprehensive level information from the
 * post-fader MeteringFacadeCore: stereo bar meters + numeric readouts for
 * sample peak, true peak, RMS.
 *
 * The LUFS readout (M/S/I) is rendered as a separate sub-row below.
 *
 * Layout:
 *
 *   ┌───────────────────────────────┐
 *   │ METERING · POST               │
 *   │ ┌─┐ ┌─┐  PEAK    -1.4         │
 *   │ │L│ │R│  TRUE PK +0.2 (red)   │
 *   │ │ │ │ │  RMS     -12.3        │
 *   │ └─┘ └─┘                       │
 *   │  L   R                        │
 *   │ ─────────────────────────     │
 *   │ LUFS                          │
 *   │   M       S       I           │
 *   │ -14.2  -14.8  -14.0           │
 *   └───────────────────────────────┘
 *
 * The bar meter has three color zones:
 *   green:   < -18 dBFS   (safe)
 *   yellow:  -18 to -3    (active)
 *   red top: > -3 dBFS    (warning)
 *
 * True peak readout turns red when it exceeds 0 dBTP (intersample clip).
 *
 * Threading: UI thread, repaints at 30 Hz. Reads from the audio-thread
 * MeteringFacadeCore which exposes atomic Metrics struct.
 */
class MasterStripMeteringSection : public juce::Component,
                                   private juce::Timer
{
public:
    MasterStripMeteringSection()
    {
        setOpaque(false);
        configureHelpButtons();
        addAndMakeVisible(helpBtnMetering_);
        addAndMakeVisible(helpBtnLufs_);
        startTimerHz(60);
    }

    ~MasterStripMeteringSection() override
    {
        stopTimer();
    }

    /** Bind the post-fader meter facade. Optionally, also a pre-fader one. */
    void setPostMeter(MeteringFacadeCore* post) { postMeter_ = post; repaint(); }
    void setPreMeter (MeteringFacadeCore* pre)  { preMeter_  = pre;  repaint(); }

    // ── Component ────────────────────────────────────────────────────

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);

        g.setColour(juce::Colour(0xFF06080D));
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(juce::Colour(0xFF2A2F3D));
        g.drawRoundedRectangle(bounds, 5.0f, 0.5f);

        // Title
        g.setColour(juce::Colour(0xFFFF4F8A));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText("METERING / POST", bounds.toNearestInt().reduced(8, 4),
                   juce::Justification::topLeft, false);

        if (postMeter_ == nullptr)
        {
            return;
        }

        const auto m = postMeter_->getMetrics();

        paintBar(g, leftBarBounds_,  m.left.samplePeakDb,  m.left.truePeakDb);
        paintBar(g, rightBarBounds_, m.right.samplePeakDb, m.right.truePeakDb);

        // L/R labels under the bars
        g.setColour(juce::Colour(0xFF7A8094));
        g.setFont(juce::Font(8.0f, juce::Font::plain));
        g.drawText("L", lLabelBounds_, juce::Justification::centred, false);
        g.drawText("R", rLabelBounds_, juce::Justification::centred, false);

        // ── Numeric readouts ────────────────────────────────────────
        const float maxPeakDb     = juce::jmax(m.left.samplePeakDb, m.right.samplePeakDb);
        const float maxTruePeakDb = juce::jmax(m.left.truePeakDb,  m.right.truePeakDb);
        const float maxRmsDb      = juce::jmax(m.left.rmsDb,        m.right.rmsDb);

        paintReadoutRow(g, peakRowBounds_,    "PEAK",    maxPeakDb,
                        false);
        paintReadoutRow(g, truePeakRowBounds_, "TRUE PK", maxTruePeakDb,
                        maxTruePeakDb > 0.0f);  // red if > 0 dBTP
        paintReadoutRow(g, rmsRowBounds_,     "RMS",     maxRmsDb, false);

        // ── LUFS divider ────────────────────────────────────────────
        g.setColour(juce::Colour(0xFFFF4F8A).withAlpha(0.4f));
        g.fillRect(juce::Rectangle<float>(
            (float) bounds.getX() + 8.0f,
            (float) lufsDividerY_,
            (float) bounds.getWidth() - 16.0f, 0.5f));

        // LUFS title
        g.setColour(juce::Colour(0xFFFF4F8A));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText("LUFS", lufsTitleBounds_, juce::Justification::centredLeft, false);

        // M / S / I labels
        g.setColour(juce::Colour(0xFF7A8094));
        g.setFont(juce::Font(8.0f, juce::Font::plain));
        g.drawText("M", lufsMLabelBounds_, juce::Justification::centred, false);
        g.drawText("S", lufsSLabelBounds_, juce::Justification::centred, false);
        g.drawText("I", lufsILabelBounds_, juce::Justification::centred, false);

        // M / S / I values — use the louder of L/R for each
        g.setColour(juce::Colour(0xFFD8DCE6));
        g.setFont(juce::Font(11.0f, juce::Font::plain));

        const float momLufs = combineLufs(m.left.momentaryLufs,  m.right.momentaryLufs);
        const float stLufs  = combineLufs(m.left.shortTermLufs,  m.right.shortTermLufs);
        const float intLufs = combineLufs(m.left.integratedLufs, m.right.integratedLufs);

        g.drawText(formatLufs(momLufs), lufsMValueBounds_, juce::Justification::centred, false);
        g.drawText(formatLufs(stLufs),  lufsSValueBounds_, juce::Justification::centred, false);
        g.drawText(formatLufs(intLufs), lufsIValueBounds_, juce::Justification::centred, false);

    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(6, 4);

        const int meteringHeaderH = 14;
        const int meteringBtnSize = juce::jmax(10, juce::jmin(16, meteringHeaderH - 2));
        helpBtnMetering_.setBounds(bounds.getRight() - meteringBtnSize - 2,
                                   bounds.getY() + (meteringHeaderH - meteringBtnSize) / 2,
                                   meteringBtnSize, meteringBtnSize);

        // Title row at top, height 14
        bounds.removeFromTop(14);

        // ── Bars + readouts area (height 90) ────────────────────────
        auto barsArea = bounds.removeFromTop(90);

        // Two narrow bars on the left (each 18 wide)
        auto barsBlock = barsArea.removeFromLeft(46);
        leftBarBounds_  = barsBlock.removeFromLeft(20).reduced(1, 4);
        barsBlock.removeFromLeft(2);
        rightBarBounds_ = barsBlock.removeFromLeft(20).reduced(1, 4);

        // Labels under bars (re-overlap the bottom of the bars area)
        const int labelY = leftBarBounds_.getBottom();
        lLabelBounds_ = juce::Rectangle<int>(leftBarBounds_.getX(),  labelY, leftBarBounds_.getWidth(),  10);
        rLabelBounds_ = juce::Rectangle<int>(rightBarBounds_.getX(), labelY, rightBarBounds_.getWidth(), 10);

        // Readouts column — right side of bars area
        barsArea.removeFromLeft(8);
        auto readoutCol = barsArea;
        const int rowH = 22;
        peakRowBounds_     = readoutCol.removeFromTop(rowH);
        truePeakRowBounds_ = readoutCol.removeFromTop(rowH);
        rmsRowBounds_      = readoutCol.removeFromTop(rowH);

        // ── LUFS area ───────────────────────────────────────────────
        bounds.removeFromTop(4);
        lufsDividerY_ = bounds.getY();
        bounds.removeFromTop(2);

        auto lufsTitleRow = bounds.removeFromTop(12);
        lufsTitleBounds_ = lufsTitleRow.reduced(8, 0);

        const int lufsHeaderH = 12;
        const int lufsBtnSize = juce::jmax(10, juce::jmin(16, lufsHeaderH - 2));
        helpBtnLufs_.setBounds(lufsTitleRow.getRight() - lufsBtnSize - 2,
                               lufsTitleRow.getY() + (lufsHeaderH - lufsBtnSize) / 2,
                               lufsBtnSize, lufsBtnSize);

        // Labels row + values row
        auto labelsRow = bounds.removeFromTop(10);
        const int colW = labelsRow.getWidth() / 3;
        lufsMLabelBounds_ = labelsRow.removeFromLeft(colW);
        lufsSLabelBounds_ = labelsRow.removeFromLeft(colW);
        lufsILabelBounds_ = labelsRow;

        auto valuesRow = bounds.removeFromTop(14);
        lufsMValueBounds_ = valuesRow.removeFromLeft(colW);
        lufsSValueBounds_ = valuesRow.removeFromLeft(colW);
        lufsIValueBounds_ = valuesRow;
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

        styleBtn(helpBtnMetering_, [this]
        {
            BubblegumInfoPopup::showFor(&helpBtnMetering_, MasterStripFeatureInfoCatalog::metering());
        });
        styleBtn(helpBtnLufs_, [this]
        {
            BubblegumInfoPopup::showFor(&helpBtnLufs_, MasterStripFeatureInfoCatalog::lufs());
        });
    }

    void timerCallback() override
    {
        if (postMeter_ == nullptr || !isShowing())
            return;

        const auto m = postMeter_->getMetrics();
        const float lpeak = m.left.samplePeakDb;
        const float rpeak = m.right.samplePeakDb;
        const float ltp   = m.left.truePeakDb;
        const float rtp   = m.right.truePeakDb;
        const float lrms  = m.left.rmsDb;
        const float rrms  = m.right.rmsDb;
        const float lmom  = m.left.momentaryLufs;
        const float rmom  = m.right.momentaryLufs;
        const float lst   = m.left.shortTermLufs;
        const float rst   = m.right.shortTermLufs;
        const float lint  = m.left.integratedLufs;
        const float rint  = m.right.integratedLufs;

        if (lpeak == lastLeft_.samplePeakDb  && rpeak == lastRight_.samplePeakDb &&
            ltp   == lastLeft_.truePeakDb    && rtp   == lastRight_.truePeakDb   &&
            lrms  == lastLeft_.rmsDb         && rrms  == lastRight_.rmsDb        &&
            lmom  == lastLeft_.momentaryLufs && rmom  == lastRight_.momentaryLufs &&
            lst   == lastLeft_.shortTermLufs && rst   == lastRight_.shortTermLufs &&
            lint  == lastLeft_.integratedLufs && rint == lastRight_.integratedLufs)
            return;

        lastLeft_  = m.left;
        lastRight_ = m.right;
        repaint();
    }

    void visibilityChanged() override
    {
        if (isVisible())
            startTimerHz(60);
        else
            stopTimer();
    }

    /** Paints one stereo bar with green/yellow/red zones. */
    void paintBar(juce::Graphics& g, juce::Rectangle<int> bar,
                  float samplePeakDb, float truePeakDb) const
    {
        // Background
        g.setColour(juce::Colour(0xFF0F1219));
        g.fillRoundedRectangle(bar.toFloat(), 2.0f);

        if (samplePeakDb <= -60.0f) return;

        // Map dB to height: -60 dB = bottom, 0 dB = top
        const float peakNorm = juce::jlimit(0.0f, 1.0f,
            (samplePeakDb + 60.0f) / 60.0f);
        const int fillHeight = (int)(bar.getHeight() * peakNorm);
        const int fillY = bar.getBottom() - fillHeight;

        // Paint green/yellow/red zones
        const int yellowY = bar.getBottom() - (int)(bar.getHeight() * (-3.0f + 60.0f) / 60.0f);
        const int redY    = bar.getBottom() - (int)(bar.getHeight() * ( 0.0f + 60.0f) / 60.0f);

        // Green zone: from fillY (or yellowY) up to bar bottom
        const int greenTop = juce::jmax(fillY, yellowY);
        if (greenTop < bar.getBottom())
        {
            g.setColour(juce::Colour(0xFF22C55E).withAlpha(0.85f));
            g.fillRect(bar.getX() + 1, greenTop,
                       bar.getWidth() - 2, bar.getBottom() - greenTop);
        }

        // Yellow zone
        if (fillY < yellowY)
        {
            const int yelTop = juce::jmax(fillY, redY);
            g.setColour(juce::Colour(0xFFEAB308).withAlpha(0.85f));
            g.fillRect(bar.getX() + 1, yelTop,
                       bar.getWidth() - 2, yellowY - yelTop);
        }

        // Red zone
        if (fillY < redY)
        {
            g.setColour(juce::Colour(0xFFFF6B6B).withAlpha(0.95f));
            g.fillRect(bar.getX() + 1, fillY,
                       bar.getWidth() - 2, redY - fillY);
        }

        // True peak indicator: small line at the true peak level
        if (truePeakDb > -60.0f)
        {
            const float tpNorm = juce::jlimit(0.0f, 1.0f,
                (truePeakDb + 60.0f) / 60.0f);
            const int tpY = bar.getBottom() - (int)(bar.getHeight() * tpNorm);
            g.setColour(juce::Colour(0xFFFF6B6B));
            g.fillRect(bar.getX() + 1, tpY - 1, bar.getWidth() - 2, 2);
        }
    }

    void paintReadoutRow(juce::Graphics& g, juce::Rectangle<int> row,
                         const juce::String& label, float dbValue,
                         bool warningRed) const
    {
        g.setColour(juce::Colour(0xFF6B7080));
        g.setFont(juce::Font(8.0f, juce::Font::plain));
        const int labelH = 9;
        auto labelArea = row.removeFromTop(labelH);
        g.drawText(label, labelArea, juce::Justification::topLeft, false);

        g.setColour(warningRed ? juce::Colour(0xFFFF6B6B) : juce::Colour(0xFFD8DCE6));
        g.setFont(juce::Font(11.0f, juce::Font::plain));
        g.drawText(formatDb(dbValue), row, juce::Justification::topLeft, false);
    }

    static juce::String formatDb(float db)
    {
        if (! std::isfinite(db)) return "-inf";
        if (db <= -60.0f) return "-inf";
        return juce::String(db, 1);
    }

    static juce::String formatLufs(float lufs)
    {
        if (! std::isfinite(lufs) || lufs < -70.0f) return "-inf";
        return juce::String(lufs, 1);
    }

    /** Combine left/right LUFS by taking the louder one (max). */
    static float combineLufs(float l, float r)
    {
        if (! std::isfinite(l) && ! std::isfinite(r)) return -std::numeric_limits<float>::infinity();
        if (! std::isfinite(l)) return r;
        if (! std::isfinite(r)) return l;
        return juce::jmax(l, r);
    }

    MeteringFacadeCore* postMeter_ { nullptr };
    MeteringFacadeCore* preMeter_  { nullptr };

    // Cached last-painted values — used to gate repaint() calls in timerCallback.
    MeteringFacadeCore::ChannelMetrics lastLeft_  {};
    MeteringFacadeCore::ChannelMetrics lastRight_ {};
    juce::TextButton helpBtnMetering_ { "?" };
    juce::TextButton helpBtnLufs_ { "?" };

    // Layout
    juce::Rectangle<int> leftBarBounds_;
    juce::Rectangle<int> rightBarBounds_;
    juce::Rectangle<int> lLabelBounds_;
    juce::Rectangle<int> rLabelBounds_;
    juce::Rectangle<int> peakRowBounds_;
    juce::Rectangle<int> truePeakRowBounds_;
    juce::Rectangle<int> rmsRowBounds_;
    int                  lufsDividerY_ { 0 };
    juce::Rectangle<int> lufsTitleBounds_;
    juce::Rectangle<int> lufsMLabelBounds_;
    juce::Rectangle<int> lufsSLabelBounds_;
    juce::Rectangle<int> lufsILabelBounds_;
    juce::Rectangle<int> lufsMValueBounds_;
    juce::Rectangle<int> lufsSValueBounds_;
    juce::Rectangle<int> lufsIValueBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterStripMeteringSection)
};

} // namespace DAW
