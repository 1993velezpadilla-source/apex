#pragma once
// APEX EQ — clean-room, standalone native editor.
// UI is reconstructed from interaction principles in the Pro-Q 4 user manual.
// It does NOT import or subclass the retired Prism GUI, its theme, inspectors,
// toolbars or preset widgets. All DSP, IDs and serialized state stay APEX-owned.
#include "../ParametricEQCore/ParametricEQProcessor.h"
#include "../PluginHostCore/ApexEvoEyeStamp.h"
#include <array>
#include <cmath>
#include <optional>
#include <limits>

namespace APEX::ParametricEQ
{
// Real press-and-hold audition — never starts from touching an EQ node.
class ApexQ4SoloHold final : public juce::TextButton
{
public:
    std::function<void (bool)> setHolding;
    ApexQ4SoloHold() : juce::TextButton ("SOLO") {}
    void mouseDown (const juce::MouseEvent&) override
    {
        if (setHolding) setHolding (true);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (setHolding) setHolding (false);
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        if (setHolding) setHolding (false);
    }
};

class ApexQ4Editor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit ApexQ4Editor (Processor& processor)
        : juce::AudioProcessorEditor (processor), processor_ (processor)
    {
        setOpaque (true);
        setResizable (true, true);
        setLookAndFeel (&look_);

        auto attach = [this] (juce::TextButton& b, const char* label)
        {
            b.setButtonText (label);
            b.setClickingTogglesState (false);
            addAndMakeVisible (b);
        };
        attach (undo_, "UNDO"); attach (redo_, "REDO");
        attach (compare_, "A/B"); attach (copy_, "COPY");
        attach (preset_, "Default Setting");
        attach (phase_, "Zero Latency");
        attach (analyzer_, "ANALYZER: PRE + POST");
        attach (displayRange_, "12 dB");
        attach (globalBypass_, "POWER");
        attach (autoGain_, "AUTO GAIN");
        attach (bandPower_, "ON");
        attach (shape_, "Bell"); attach (slope_, "12 dB/oct");
        attach (placement_, "STEREO");
        attach (dynamic_, "DYN");
        addAndMakeVisible (soloHold_);
        soloHold_.setHolding = [this] (bool held)
        {
            if (held && selected_ >= 0)
            {
                ++auditionToken_;
                auditionHolding_ = processor_.beginAudition (selected_, auditionToken_);
            }
            else if (auditionHolding_)
            {
                processor_.endAudition (auditionToken_);
                auditionHolding_ = false;
            }
        };
        attach (advanced_, "MORE");
        attach (closeBand_, "X");
        // Dynamic EQ controls are contextual: no permanently visible sheet.
        // All values go to the existing APEX canonical hosted parameters,
        // preserving automation, project serialization and DSP behavior.
        attach (dynToggle_, "DYN ON");
        attach (dynSpectral_, "SPECTRAL");
        attach (dynFilter_, "BAND DETECTOR");
        attach (dynClose_, "CLOSE");
        constexpr const char* names[] = {
            "THRESHOLD", "RANGE", "ATTACK", "RELEASE"
        };
        constexpr DynamicBandOffset offsets[] = {
            DynamicBandOffset::Threshold, DynamicBandOffset::Range,
            DynamicBandOffset::Attack, DynamicBandOffset::Release
        };
        for (int i = 0; i < 4; ++i)
        {
            dynamicNames_[static_cast<std::size_t> (i)].setText (
                names[i], juce::dontSendNotification);
            dynamicNames_[static_cast<std::size_t> (i)].setColour (
                juce::Label::textColourId, juce::Colour (0xffd4cbd5));
            dynamicNames_[static_cast<std::size_t> (i)].setJustificationType (
                juce::Justification::centredLeft);
            addAndMakeVisible (dynamicNames_[static_cast<std::size_t> (i)]);
            setupDynamicSlider (
                dynamicSliders_[static_cast<std::size_t> (i)], offsets[i]);
        }
        dynToggle_.onClick = [this]
        {
            if (selected_ >= 0)
            {
                toggleParameter (dynamicParameterIndex (
                    selected_, DynamicBandOffset::Enable));
                sync();
            }
        };
        dynSpectral_.onClick = [this]
        {
            if (selected_ >= 0)
            {
                toggleParameter (spectralBandParameterIndex (selected_));
                sync();
            }
        };
        dynFilter_.onClick = [this]
        {
            if (selected_ >= 0)
            {
                toggleParameter (dynamicFilterParameterIndex (selected_));
                sync();
            }
        };
        dynClose_.onClick = [this] { setDynamicExpanded (false); };

        undo_.onClick = [this] { historyStep (-1); };
        redo_.onClick = [this] { historyStep (1); };
        compare_.onClick = [this] { switchCompare(); };
        copy_.onClick = [this] { copySnapshot(); };
        preset_.onClick = [this] { openPresetActions(); };
        phase_.onClick = [this] { toggleParameter (kPhaseModeParameter); sync(); };
        analyzer_.onClick = [this] { showAnalyzerMenu(); };
        displayRange_.onClick = [this]
        {
            rangeIndex_ = (rangeIndex_ + 1) % 4;
            const float ranges[] = { 3.0f, 6.0f, 12.0f, 30.0f };
            rangeDb_ = ranges[rangeIndex_];
            displayRange_.setButtonText (juce::String (static_cast<int> (rangeDb_))
                                         + " dB");
            curveDirty_ = true; repaint();
        };
        globalBypass_.onClick = [this]
        {
            toggleParameter (kGlobalBypassParameter); sync();
        };
        autoGain_.onClick = [this]
        {
            toggleParameter (kOutputAutoGainParameter); sync();
        };
        bandPower_.onClick = [this]
        {
            if (selected_ >= 0)
                toggleParameter (parameterIndex (selected_, BandParameterOffset::Bypass));
            sync(); repaint();
        };
        shape_.onClick = [this] { showShapeMenu(); };
        slope_.onClick = [this] { showSlopeMenu(); };
        placement_.onClick = [this] { showPlacementMenu(); };
        dynamic_.onClick = [this] { setDynamicExpanded (! dynamicExpanded_); };
        advanced_.onClick = [this] { showAdvancedMenu(); };
        closeBand_.onClick = [this] { select (-1); };

        setupDial (freq_, BandParameterOffset::Frequency, "FREQ");
        setupDial (gain_, BandParameterOffset::Gain, "GAIN");
        setupDial (q_, BandParameterOffset::Q, "Q");
        setSelectedVisible (false);
        setDynamicControlsVisible (false);

        lastAnalyzerMode_ = processor_.getAnalyzerMode();
        if (lastAnalyzerMode_ == Analysis::SpectrumTapMode::Closed)
        {
            processor_.setAnalyzerMode (Analysis::SpectrumTapMode::Both);
            analyzerAutoStarted_ = true;
        }
        setSize (1000, 640);
        history_[0] = capture();
        historyCount_ = 1;
        historyPosition_ = 0;
        startTimerHz (25);
    }

    ~ApexQ4Editor() override
    {
        stopTimer();
        if (dragBand_ >= 0)
            finishGestures();
        // Closing the plugin mid-ring-drag must balance the hosted
        // automation begin/end pair, even when no mouseUp is delivered.
        if (rangeDragBand_ >= 0)
        {
            param (dynamicParameterIndex (
                rangeDragBand_, DynamicBandOffset::Range))
                ->endChangeGesture();
            rangeDragBand_ = -1;
        }
        if (auditionHolding_) processor_.endAudition (auditionToken_);
        processor_.cancelAudition();
        if (analyzerAutoStarted_ &&
            processor_.getAnalyzerMode() == Analysis::SpectrumTapMode::Both)
            processor_.setAnalyzerMode (Analysis::SpectrumTapMode::Closed);
        setLookAndFeel (nullptr);
    }

    void resized() override
    {
        const int w = getWidth(), h = getHeight();
        const bool phone = w < 670;
        const bool narrow = w < 460;
        const int foot = narrow ? 84 : (h < 390 ? 40 : 44);
        graph_ = { 0, 42, w, std::max (140, h - 42 - foot) };
        const int b = 32;
        undo_.setBounds (w > 620 ? 222 : 137, 5, b, b);
        redo_.setBounds (w > 620 ? 256 : 169, 5, b, b);
        compare_.setBounds (w > 620 ? 301 : 209, 5, 42, b);
        copy_.setBounds (w > 620 ? 350 : 254, 5, 50, b);
        // The canonical boot-eye logo occupies its own reserved upper-right
        // space; unlike the rejected Prism UI it cannot cover live controls.
        const int logoWidth = w < 670 ? 61 : 82;
        bootEyeBounds_ = { w - logoWidth - 8, 6, logoWidth, 28 };
        if (phone)
            preset_.setBounds (165, 5,
                               std::max (44, w - 165 - logoWidth - 17), b);
        else
            preset_.setBounds (std::max (405, w / 2 - 110), 5,
                               std::min (220, std::max (92,
                                   w - std::max (405, w / 2 - 110)
                                   - logoWidth - 30)), b);
        for (auto* secondary : { &undo_, &redo_, &compare_, &copy_ })
            secondary->setVisible (! phone);
        if (narrow)
        {
            const int y = h - foot + 4;
            phase_.setBounds (5, y, 97, 35);
            analyzer_.setBounds (104, y, w - 185, 35);
            displayRange_.setBounds (w - 79, y, 74, 35);
            autoGain_.setBounds (5, y + 40, 114, 35);
            globalBypass_.setBounds (w - 120, y + 40, 115, 35);
        }
        else
        {
            phase_.setBounds (6, h - foot + 5, phone ? 118 : 152, 33);
            analyzer_.setBounds (phone ? 124 : 160, h - foot + 5,
                                 phone ? 158 : 216, 33);
            displayRange_.setBounds (phone ? 284 : 380, h - foot + 5,
                                     phone ? 63 : 70, 33);
            autoGain_.setBounds (phone ? 352 : std::max (465, w - 255),
                                 h - foot + 5, phone ? 75 : 104, 33);
            globalBypass_.setBounds (w - (phone ? 51 : 129), h - foot + 5,
                                     phone ? 49 : 117, 33);
        }

        // Context controls are genuinely floating OVER the frequency display,
        // directly beneath the selected EQ nodes: NO permanent bottom sheet.
        // The canvas does not shrink when switching bands.
        // Adversarial visual pass: the manual uses a SMALL transient
        // contextual strip, never a large persistent parameter sheet.
        // Preserve 44px+ touch controls and a fully-visible 20px text box.
        const int controlWidth = std::min (
            narrow ? 338 : (phone ? 390 : 450), w - 18);
        const int controlHeight = narrow ? 126
                                 : (h < 400 ? 104 : 116);
        // FabFilter's manual places selected-band controls at the bottom of
        // the frequency display. Keep them near the selected frequency but
        // never cover an actual hosted band node or its value tag. This GUI-
        // thread-only placement critic checks multiple legal positions and
        // favors the bottom edge, rather than blindly covering the response.
        const int minX = 9;
        const int maxX = std::max (minX, w - controlWidth - 9);
        const int minY = graph_.getY() + 8;
        const int maxY = std::max (minY, graph_.getBottom()
                                          - controlHeight - 29);
        const auto selectedPoint = selected_ < 0
            ? graph_.getCentre().toFloat()
            : nodePoint (selected_).toFloat();
        const int baseX = juce::jlimit (minX, maxX,
            juce::roundToInt (selectedPoint.x) - controlWidth / 2);
        const auto clampX = [=] (int x) { return juce::jlimit (minX, maxX, x); };
        const auto clampY = [=] (int y) { return juce::jlimit (minY, maxY, y); };
        const std::array<int, 6> positionsX {{
            baseX,
            clampX (juce::roundToInt (selectedPoint.x) - controlWidth + 32),
            clampX (juce::roundToInt (selectedPoint.x) + 32),
            minX, maxX, clampX ((w - controlWidth) / 2)
        }};
        const std::array<int, 7> positionsY {{
            maxY, clampY (maxY - 48), minY,
            clampY (juce::roundToInt (selectedPoint.y) + 47),
            clampY (juce::roundToInt (selectedPoint.y) - controlHeight - 49),
            clampY (graph_.getY() + graph_.getHeight() / 2
                    - controlHeight / 2),
            clampY (graph_.getY() + (graph_.getHeight() * 2) / 3
                    - controlHeight / 2)
        }};
        float bestPenalty = std::numeric_limits<float>::infinity();
        juce::Rectangle<int> bestPanel { baseX, maxY,
                                          controlWidth, controlHeight };
        for (const int y : positionsY)
        {
            for (const int x : positionsX)
            {
                const juce::Rectangle<int> candidate { x, y,
                    controlWidth, controlHeight };
                // A predominantly low/bottom contextual HUD is preferred,
                // provided it does not hide either a selected or other band.
                float penalty = (maxY - y) * 1.7f
                    + std::abs (candidate.getCentreX()
                        - juce::roundToInt (selectedPoint.x)) * 0.11f;
                for (int band = 0; band < kMaxBands; ++band)
                {
                    if (! param (parameterIndex (band,
                                BandParameterOffset::Enabled))->getBool())
                        continue;
                    const auto point = nodePoint (band);
                    if (candidate.expanded (24).contains (point))
                        penalty += band == selected_ ? 500000.0f : 250000.0f;
                    if (band == selected_)
                    {
                        const juce::Rectangle<int> valueTag (
                            point.x - 107, point.y - 37, 214, 22);
                        if (candidate.expanded (3).intersects (valueTag))
                            penalty += 120000.0f;
                    }
                }
                if (penalty < bestPenalty)
                {
                    bestPenalty = penalty;
                    bestPanel = candidate;
                }
            }
        }
        panel_ = bestPanel;
        const int px = panel_.getX(), py = panel_.getY();
        if (narrow)
        {
            // 320px portrait: prioritized touch gestures, other features in MORE.
            bandPower_.setBounds (px + 3, py + 5, 44, 40);
            shape_.setBounds (px + 49, py + 5, 90, 40);
            soloHold_.setBounds (px + 141, py + 5, 62, 40);
            advanced_.setBounds (px + 205, py + 5, 56, 40);
            closeBand_.setBounds (px + 263, py + 5, 34, 40);
            slope_.setVisible (false);
            placement_.setVisible (false);
            dynamic_.setVisible (false);
        }
        else if (phone)
        {
            bandPower_.setBounds (px + 5, py + 5, 39, 36);
            shape_.setBounds (px + 46, py + 5, 90, 36);
            slope_.setBounds (px + 138, py + 5, 80, 36);
            soloHold_.setBounds (px + 220, py + 5, 56, 36);
            advanced_.setBounds (px + 278, py + 5, 65, 36);
            closeBand_.setBounds (px + 345, py + 5, 37, 36);
            placement_.setVisible (false);
            dynamic_.setVisible (false);
        }
        else
        {
            // 450px desktop: one 30px header row, no floating widget overlap.
            bandPower_.setBounds (px + 7, py + 7, 28, 30);
            shape_.setBounds (px + 37, py + 7, 88, 30);
            slope_.setBounds (px + 127, py + 7, 87, 30);
            placement_.setBounds (px + 216, py + 7, 70, 30);
            dynamic_.setBounds (px + 288, py + 7, 44, 30);
            soloHold_.setBounds (px + 334, py + 7, 46, 30);
            advanced_.setBounds (px + 382, py + 7, 33, 30);
            closeBand_.setBounds (px + 417, py + 7, 26, 30);
            dynamic_.setVisible (selected_ >= 0);
        }
        const int sliderY = py + (narrow ? 49 : 43);
        const int sliderSpace = controlWidth - 18;
        const int cw = sliderSpace / 3;
        const int sliderHeight = controlHeight - (narrow ? 53 : 47);
        freq_.setBounds (px + 9, sliderY, cw, sliderHeight);
        gain_.setBounds (px + 9 + cw, sliderY, cw, sliderHeight);
        q_.setBounds (px + 9 + 2 * cw, sliderY,
                      sliderSpace - 2 * cw, sliderHeight);
        soloHold_.toFront (false);
        layoutDynamicControls();
        curveDirty_ = true;
    }

    void paint (juce::Graphics& g) override
    {
        const auto full = getLocalBounds();
        g.fillAll (juce::Colour (0xff17161b));
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff28232c), 0.0f, 0.0f,
            juce::Colour (0xff111115), 0.0f,
            static_cast<float> (getHeight()), false));
        g.fillRect (graph_);

        // Continuous low-contrast spectrum grid, without giant UI cards.
        constexpr std::array<double, 10> marks {{
            20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000
        }};
        g.setFont (juce::FontOptions (10.0f));
        for (double hz : marks)
        {
            const int x = hzToX (hz);
            g.setColour (juce::Colour (0xff39333d).withAlpha (0.60f));
            g.drawVerticalLine (x, static_cast<float> (graph_.getY()),
                               static_cast<float> (graph_.getBottom()));
            g.setColour (juce::Colour (0xff89818b));
            g.drawText (hz >= 1000
                      ? juce::String (static_cast<int> (hz / 1000)) + "k"
                      : juce::String (static_cast<int> (hz)),
                x - 20, graph_.getBottom() - 23, 40, 14,
                juce::Justification::centred);
        }
        for (int tick = -4; tick <= 4; ++tick)
        {
            const float db = tick * rangeDb_ / 4.0f;
            const int y = dbToY (db);
            g.setColour (tick == 0 ? juce::Colour (0xff666067)
                                    : juce::Colour (0xff38333d));
            g.drawHorizontalLine (y, 0.0f,
                                  static_cast<float> (getWidth()));
            g.setColour (juce::Colour (0xff948b96));
            g.drawText ((db > 0 ? "+" : "") + juce::String (db, 0),
                getWidth() - 43, y - 10, 38, 18, juce::Justification::centredRight);
        }
        drawSpectrum (g);
        if (curveDirty_)
            rebuildResponse();
        for (int band = 0; band < kMaxBands; ++band)
        {
            const auto& settings = cachedBands_[static_cast<std::size_t> (band)];
            if (! settings.enabled || settings.bypassed)
                continue;
            const auto colour = bandColour (band);
            juce::Path area (bandCurves_[static_cast<std::size_t> (band)]);
            area.lineTo (graph_.getRight(), dbToY (0));
            area.lineTo (graph_.getX(), dbToY (0));
            area.closeSubPath();
            g.setColour (colour.withAlpha (0.07f));
            g.fillPath (area);
            g.setColour (colour.withAlpha (0.34f));
            g.strokePath (bandCurves_[static_cast<std::size_t> (band)],
                          juce::PathStrokeType (1.2f));
        }
        g.setColour (juce::Colour (0xfff6c53e).withAlpha (0.19f));
        g.strokePath (response_, juce::PathStrokeType (5.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (0xfff4ce55));
        g.strokePath (response_, juce::PathStrokeType (2.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // Official workflow: a subtle EQ response preview appears on hover,
        // dashed if another band is selected (then a single tap deselects).
        if (hover_.has_value() && dragBand_ < 0 && hitBand (*hover_) < 0)
        {
            juce::Path preview, dashed;
            const float baseline = dbToY (0.0);
            const float extent = baseline - hover_->y;
            const float centreX = hover_->x;
            for (int step = 0; step <= 70; ++step)
            {
                const float x = std::max (0.0f, centreX - getWidth() * 0.17f)
                    + step * (getWidth() * 0.34f / 70.0f);
                const float scale = (x - centreX) / (getWidth() * 0.049f);
                const float y = baseline - extent * std::exp (-0.5f * scale * scale);
                if (step == 0) preview.startNewSubPath (x, y);
                else preview.lineTo (x, y);
            }
            g.setColour (juce::Colour (0xfff1d78b).withAlpha (0.45f));
            if (selected_ >= 0)
            {
                const float pattern[] = { 4.0f, 5.0f };
                juce::PathStrokeType (1.3f).createDashedStroke (
                    dashed, preview, pattern, 2);
                g.strokePath (dashed, juce::PathStrokeType (1.3f));
            }
            else
                g.strokePath (preview, juce::PathStrokeType (1.3f));
        }

        for (int band = 0; band < kMaxBands; ++band)
        {
            const auto& s = cachedBands_[static_cast<std::size_t> (band)];
            if (! s.enabled) continue;
            const auto pt = nodePoint (band);
            g.setColour (juce::Colours::black.withAlpha (0.63f));
            g.fillEllipse (pt.x - 10.0f, pt.y - 10.0f, 20.0f, 20.0f);
            g.setColour (s.bypassed ? juce::Colours::grey : bandColour (band));
            g.fillEllipse (pt.x - (band == selected_ ? 6.0f : 4.5f),
                           pt.y - (band == selected_ ? 6.0f : 4.5f),
                           band == selected_ ? 12.0f : 9.0f,
                           band == selected_ ? 12.0f : 9.0f);
            if (param (dynamicParameterIndex (
                    band, DynamicBandOffset::Enable))->getBool()
                && ! s.bypassed)
            {
                const float maxRange = std::abs (param (
                    dynamicParameterIndex (band, DynamicBandOffset::Range))
                        ->getUnitsValue());
                // Outer range arc is tied to the REAL hosted parameter.
                // A second, thinner arc represents the last coherent
                // audio-thread ResponseFrame gain, never a fake meter.
                const float maxAngle = juce::MathConstants<float>::pi
                                       * 2.0f * std::clamp (
                                           maxRange / 24.0f, 0.0f, 1.0f);
                if (maxAngle > 0.003f)
                {
                    juce::Path rangeArc;
                    const float startAngle =
                        juce::MathConstants<float>::pi * 0.25f;
                    rangeArc.addCentredArc (static_cast<float> (pt.x),
                        static_cast<float> (pt.y), 17.0f, 17.0f, 0.0f,
                        startAngle, startAngle + maxAngle, true);
                    g.setColour (bandColour (band).withAlpha (0.77f));
                    g.strokePath (rangeArc, juce::PathStrokeType (2.2f));
                }
                if (dynamicMeterValid_
                    && (lastDynamicActiveMask_ & (1u << band)) != 0)
                {
                    const float measuredDb = dynamicGainDbDisplay_[
                        static_cast<std::size_t> (band)];
                    const float gr = std::abs (measuredDb);
                    if (gr >= 0.1f)
                    {
                        juce::Path actualArc;
                        const float radians = juce::MathConstants<float>::pi
                            * 2.0f * std::clamp (gr / 24.0f, 0.0f, 1.0f);
                        const float start =
                            juce::MathConstants<float>::pi * 0.25f;
                        actualArc.addCentredArc (
                            static_cast<float> (pt.x),
                            static_cast<float> (pt.y), 21.0f, 21.0f, 0.0f,
                            start, start + radians, true);
                        g.setColour (measuredDb < 0.0f
                            ? juce::Colour (0xffee8e65)
                            : juce::Colour (0xff78d5af));
                        g.strokePath (actualArc, juce::PathStrokeType (2.7f));
                    }
                }
            }
            if (selectedMask_[static_cast<std::size_t> (band)])
            {
                // Multiple selected bands remain visibly linked while only
                // the primary band gets the detailed value tooltip.
                g.setColour (juce::Colours::white.withAlpha (
                    band == selected_ ? 0.86f : 0.54f));
                g.drawEllipse (pt.x - 10.0f, pt.y - 10.0f, 20.0f, 20.0f, 1.4f);
            }
            if (band == selected_)
            {
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                auto label = juce::String (s.frequencyHz, 0) + " Hz   "
                    + juce::String (s.gainDb, 2) + " dB   Q "
                    + juce::String (s.q, 2);
                if (dynamicMeterValid_
                    && (lastDynamicActiveMask_ & (1u << band)) != 0)
                    label += "   DYN "
                        + juce::String (dynamicGainDbDisplay_[
                            static_cast<std::size_t> (band)], 1) + " dB";
                auto tag = juce::Rectangle<int> (pt.x - 107, pt.y - 37,
                                                 214, 22)
                    .constrainedWithin (graph_.reduced (5));
                g.setColour (juce::Colour (0xff25212c));
                g.fillRoundedRectangle (tag.toFloat(), 5.0f);
                g.setColour (juce::Colours::white);
                g.drawFittedText (label, tag.reduced (4, 1),
                                  juce::Justification::centred, 1);
            }
        }

        // Optional expanded dynamics, only while explicitly requested.
        // Never occupies permanent graph height or changes filter response
        // coordinates. The four editable rows bind directly to the DSP.
        if (dynamicExpanded_ && selected_ >= 0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.39f));
            g.fillRoundedRectangle (
                dynamicPanel_.toFloat().translated (0, 3), 9.0f);
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0xff39313e),
                dynamicPanel_.getX(), dynamicPanel_.getY(),
                juce::Colour (0xff211d28),
                dynamicPanel_.getX(), dynamicPanel_.getBottom(), false));
            g.fillRoundedRectangle (dynamicPanel_.toFloat(), 9.0f);
            g.setColour (juce::Colour (0xff8b728f).withAlpha (0.75f));
            g.drawRoundedRectangle (
                dynamicPanel_.toFloat().reduced (0.5f), 9.0f, 1.0f);
            g.setColour (juce::Colour (0xfff4ce70));
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText ("DYNAMIC EQ", dynamicPanel_.getX() + 10,
                        dynamicPanel_.getY() + 5, 118, 22,
                        juce::Justification::centredLeft);
            // Display actual DSP gain sampled from the lock-free ResponseCore.
            // This is a signed number because APEX supports upward as well
            // as downward dynamics; never label synthetic values as GR.
            const auto db = dynamicGainDbDisplay_[
                static_cast<std::size_t> (selected_)];
            const auto metering = dynamicMeterValid_
                && (lastDynamicActiveMask_ & (1u << selected_)) != 0;
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.setColour (juce::Colour (0xffe8dfe5));
            const auto readout = metering
                ? juce::String (db >= 0.0f ? "+" : "")
                    + juce::String (db, 1) + " dB"
                : juce::String ("— dB");
            g.drawFittedText (readout, dynamicPanel_.getRight() - 133,
                dynamicPanel_.getY() + 6, 75, 22,
                juce::Justification::centredRight, 1);
        }

        // Small unobtrusive header — *not* the old Prism blue toolbar.
        g.setColour (juce::Colour (0xff201d24));
        g.fillRect (0, 0, getWidth(), 42);
        g.setColour (juce::Colour (0xff464048));
        g.drawHorizontalLine (41, 0.0f, static_cast<float> (getWidth()));
        g.setColour (juce::Colour (0xfff6cf69));
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawFittedText ("APEX", 12, 6, 87, 29,
                          juce::Justification::centredLeft, 1);
        g.setColour (juce::Colour (0xffdbd7dc));
        g.setFont (juce::FontOptions (13.5f));
        g.drawFittedText ("EQ", 94, 9, 43, 25,
                          juce::Justification::centredLeft, 1);
        // The SAME unanimated eye rendered by the APEX boot-up screen.
        // No invented replacement logo and no bitmap copy.
        APEX::Branding::EvoEyeStamp::paint (
            g, bootEyeBounds_.toFloat());

        // Thin floating control bar anchored beneath selected EQ point.
        if (selected_ >= 0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.26f));
            g.fillRoundedRectangle (panel_.toFloat().translated (0, 4), 12.0f);
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0xff37303c), panel_.getX(), panel_.getY(),
                juce::Colour (0xff211c27), panel_.getX(), panel_.getBottom(),
                false));
            g.fillRoundedRectangle (panel_.toFloat(), 11.0f);
            g.setColour (juce::Colour (0xff73677c).withAlpha (0.7f));
            g.drawRoundedRectangle (panel_.toFloat().reduced (0.5f), 11.0f, 0.8f);
            g.setColour (bandColour (selected_));
            g.fillRoundedRectangle (static_cast<float> (panel_.getX() + 11),
                static_cast<float> (panel_.getY() + 40),
                static_cast<float> (panel_.getWidth() - 22), 1.5f, 1.0f);
        }

        const int footerY = getHeight()
            - (getWidth() < 460 ? 84 : (getHeight() < 390 ? 40 : 44));
        g.setColour (juce::Colour (0xff211d26));
        g.fillRect (0, footerY, getWidth(), getHeight() - footerY);
        g.setColour (juce::Colour (0xff4d424d));
        g.drawHorizontalLine (footerY, 0.0f,
                              static_cast<float> (getWidth()));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pendingCurveCreate_ = false;
        rangeDragBand_ = -1;
        hover_.reset();
        if (! graph_.contains (e.getPosition())
            || (selected_ >= 0 && panel_.contains (e.getPosition())))
            return;
        dragBand_ = hitBand (e.position);
        if (e.mods.isPopupMenu())
        {
            openGraphContext (dragBand_);
            dragBand_ = -1;
            return;
        }
        if (dragBand_ >= 0)
        {
            if (e.mods.isCtrlDown() || e.mods.isCommandDown())
            {
                // Pro-Q desktop workflow: Ctrl/Cmd-click an existing handle
                // toggles membership in the grouped selection, while
                // Ctrl/Cmd-click the background creates a new band.
                toggleSelectedBand (dragBand_);
                dragBand_ = -1;
                return;
            }
            if (e.mods.isAltDown())
            {
                toggleParameter (parameterIndex (dragBand_,
                    BandParameterOffset::Bypass));
                select (dragBand_);
                dragBand_ = -1;
                return;
            }
            const auto ringCentre = nodePoint (dragBand_).toFloat();
            const auto radius = e.position.getDistanceFrom (ringCentre);
            if (radius >= 13.0f && radius <= 24.0f
                && param (dynamicParameterIndex (
                    dragBand_, DynamicBandOffset::Enable))->getBool())
            {
                const int band = dragBand_;
                const bool keepGroup = selectedMask_[
                    static_cast<std::size_t> (band)] && selectedCount() > 1;
                select (band, keepGroup);
                rangeDragBand_ = band;
                rangeDragOriginY_ = e.position.y;
                auto* range = param (dynamicParameterIndex (
                    band, DynamicBandOffset::Range));
                rangeDragStartDb_ = range->getUnitsValue();
                range->beginChangeGesture();
                dragBand_ = -1;
                return;
            }
            const bool keepGroup = selectedMask_[
                static_cast<std::size_t> (dragBand_)] && selectedCount() > 1;
            select (dragBand_, keepGroup);
            beginNodeGesture (dragBand_, e.position);
            return;
        }

        const bool forceCreate = e.mods.isCtrlDown()
                              || e.mods.isCommandDown();
        // Manual p.9: one click deselects while other bands are selected;
        // Ctrl-click adds immediately; dragging the yellow response creates
        // a band regardless of selected state.
        if (selected_ >= 0 && ! forceCreate)
        {
            pendingCurveCreate_ = nearResultCurve (e.position);
            pendingOrigin_ = e.position;
            pendingDynamic_ = e.mods.isAltDown();
            pendingSpectral_ = pendingDynamic_ && e.mods.isShiftDown();
            select (-1);
            return;
        }
        // A normal click on an unselected graph creates a genuine host band.
        createBand (e.position, e.mods.isAltDown(),
                    e.mods.isAltDown() && e.mods.isShiftDown());
        if (selected_ >= 0)
        {
            beginNodeGesture (selected_, e.position);
            createdDuringGesture_ = true;
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (graph_.contains (e.getPosition())
            && (selected_ < 0 || ! panel_.contains (e.getPosition()))
            && hitBand (e.position) < 0)
            hover_ = e.position;
        else
            hover_.reset();
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hover_.reset();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (rangeDragBand_ >= 0)
        {
            // Up/down on the OUTER dynamic ring changes its signed range
            // in decibels with host automation, never frequency or gain.
            const float multiplier = e.mods.isShiftDown() ? 0.035f : 0.15f;
            setUnits (dynamicParameterIndex (
                rangeDragBand_, DynamicBandOffset::Range),
                rangeDragStartDb_
                    + (rangeDragOriginY_ - e.position.y) * multiplier);
            repaint();
            return;
        }
        if (pendingCurveCreate_)
        {
            if (e.position.getDistanceFrom (pendingOrigin_) < 7.0f)
                return;
            pendingCurveCreate_ = false;
            createBand (pendingOrigin_, pendingDynamic_, pendingSpectral_);
            if (selected_ < 0) return;
            beginNodeGesture (selected_, pendingOrigin_);
            createdDuringGesture_ = true;
        }
        if (dragBand_ < 0) return;
        auto pt = e.position;
        if (e.mods.isShiftDown() && ! createdDuringGesture_)
        {
            pt.x = dragOrigin_.x + (pt.x - dragOrigin_.x) * 0.15f;
            pt.y = dragOrigin_.y + (pt.y - dragOrigin_.y) * 0.15f;
        }
        if (e.mods.isAltDown() && ! createdDuringGesture_)
        {
            const float dx = std::abs (pt.x - dragOrigin_.x);
            const float dy = std::abs (pt.y - dragOrigin_.y);
            if (dx > dy) pt.y = dragOrigin_.y;
            else pt.x = dragOrigin_.x;
        }
        const float newHz = static_cast<float> (xToHz (pt.x));
        const float newGain = static_cast<float> (yToDb (pt.y));
        const double frequencyRatio = newHz
            / std::max (20.0f, dragStartHz_);
        const float gainDelta = newGain - dragStartGain_;
        // Use a frequency RATIO, not an additive Hz offset. Selected bands
        // shift by equivalent musical octaves while preserving intervals.
        // Gain moves by equal dB offsets. Each hosted parameter receives an
        // independent, balanced automation gesture.
        for (int band = 0; band < kMaxBands; ++band)
        {
            if (! gestureMask_[static_cast<std::size_t> (band)])
                continue;
            setUnits (parameterIndex (band, BandParameterOffset::Frequency),
                static_cast<float> (std::clamp (
                    groupStartHz_[static_cast<std::size_t> (band)]
                        * frequencyRatio, 20.0, 30000.0)));
            setUnits (parameterIndex (band, BandParameterOffset::Gain),
                groupStartGain_[static_cast<std::size_t> (band)]
                    + gainDelta);
        }
        curveDirty_ = true;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (rangeDragBand_ >= 0)
        {
            param (dynamicParameterIndex (
                rangeDragBand_, DynamicBandOffset::Range))
                ->endChangeGesture();
            rangeDragBand_ = -1;
            commitHistory();
        }
        pendingCurveCreate_ = false;
        if (dragBand_ >= 0) finishGestures();
        dragBand_ = -1;
        createdDuringGesture_ = false;
        sync();
        // Do not shift graph geometry during the pointer drag; only move
        // the contextual HUD AFTER all grouped gestures are balanced.
        resized();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! graph_.contains (e.getPosition())
            || (selected_ >= 0 && panel_.contains (e.getPosition())))
            return;
        pendingCurveCreate_ = false;
        const int band = hitBand (e.position);
        if (band >= 0)
        {
            select (band);
            return;
        }
        createBand (e.position, e.mods.isAltDown(),
                    e.mods.isAltDown() && e.mods.isShiftDown());
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        const int band = hitBand (event.position);
        if (band < 0 || std::abs (wheel.deltaY) < 1.0e-6f) return;
        const bool fine = event.mods.isShiftDown();
        if (event.mods.isAltDown())
        {
            // Official manual: Alt-wheel adjusts the hosted dynamic range;
            // Alt+Ctrl/Cmd reversely links gain and range (musical trade-off).
            // Unlike static wheel/Q, this always operates in actual dB
            // units, independent of log-scaled hosted parameter widths.
            const bool tradeGain = event.mods.isCtrlDown()
                                   || event.mods.isCommandDown();
            const bool grouped = selectedCount() > 1
                && selectedMask_[static_cast<std::size_t> (band)];
            const float amountDb = wheel.deltaY * (fine ? 0.35f : 2.0f);
            for (int b = 0; b < kMaxBands; ++b)
            {
                if (grouped ? ! selectedMask_[static_cast<std::size_t> (b)]
                            : b != band)
                    continue;
                auto* range = param (dynamicParameterIndex (
                    b, DynamicBandOffset::Range));
                auto* enabled = param (dynamicParameterIndex (
                    b, DynamicBandOffset::Enable));
                auto* gain = param (parameterIndex (
                    b, BandParameterOffset::Gain));
                range->beginChangeGesture();
                if (tradeGain) gain->beginChangeGesture();
                const auto targetRange =
                    range->getUnitsValue() + amountDb;
                range->setValueNotifyingHost (
                    range->toNormalised (targetRange));
                // Range defaults to zero (static). Enabling dynamics only
                // after a NONZERO adjustment never changes static bands
                // unexpectedly when moving the cursor over them.
                if (! enabled->getBool()
                    && std::abs (range->getUnitsValue()) > 0.001f)
                {
                    enabled->beginChangeGesture();
                    enabled->setValueNotifyingHost (1.0f);
                    enabled->endChangeGesture();
                }
                if (tradeGain)
                {
                    gain->setValueNotifyingHost (
                        gain->toNormalised (gain->getUnitsValue()
                                            - amountDb));
                    gain->endChangeGesture();
                }
                range->endChangeGesture();
            }
            if (selected_ < 0 || ! grouped)
                select (band);
            commitHistory();
            curveDirty_ = true;
            sync();
            repaint();
            return;
        }
        const int idx = parameterIndex (band,
            (event.mods.isCtrlDown() || event.mods.isCommandDown())
                ? BandParameterOffset::Gain : BandParameterOffset::Q);
        auto* p = param (idx);
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
            p->getValue() + wheel.deltaY
              * (fine ? 0.012f : 0.055f)));
        p->endChangeGesture();
        select (band);
        commitHistory();
        curveDirty_ = true;
    }

    float measuredDynamicDbForTesting (int band) const noexcept
    {
        return band >= 0 && band < kMaxBands && dynamicMeterValid_
            ? dynamicGainDbDisplay_[static_cast<std::size_t> (band)]
            : 0.0f;
    }
    bool hasMeasuredDynamicFrameForTesting (int band) const noexcept
    {
        return dynamicMeterValid_ && band >= 0 && band < kMaxBands
            && (lastDynamicActiveMask_ & (1u << band)) != 0;
    }
    void refreshMeasuredDynamicsForTesting() { updateMeasuredDynamics(); }

    // Native mouse event injection for CI adversarial gesture tests.
    // Production callbacks are invoked, rather than simulating parameters.
    void injectGraphPointerForTesting (juce::Point<float> pt, int action,
                                       juce::ModifierKeys modifiers = {})
    {
        auto* mouse = juce::Desktop::getInstance().getMouseSource (0);
        if (mouse == nullptr)
        {
            addToDesktop (0);
            mouse = juce::Desktop::getInstance().getMouseSource (0);
        }
        if (mouse == nullptr) return;
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent event (
            *mouse, pt, modifiers, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f, this, this,
            now, pt, now, action == 3 ? 2 : 1, action == 1);
        if (action == 0) mouseDown (event);
        if (action == 1) mouseDrag (event);
        if (action == 2) mouseUp (event);
        if (action == 3) mouseDoubleClick (event);
    }

    void injectGraphWheelForTesting (juce::Point<float> pt, float deltaY,
                                      juce::ModifierKeys modifiers)
    {
        auto* mouse = juce::Desktop::getInstance().getMouseSource (0);
        if (mouse == nullptr) return;
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent event (
            *mouse, pt, modifiers, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f, this, this,
            now, pt, now, 1, false);
        juce::MouseWheelDetails wheel;
        wheel.deltaY = deltaY;
        mouseWheelMove (event, wheel);
    }

    // Exact native-offscreen snapshot test (not a generated concept image).
    int selectedBandForTesting() const noexcept { return selected_; }
    int selectedBandCountForTesting() const noexcept { return selectedCount(); }
    bool isBandSelectedForTesting (int band) const noexcept
    {
        return band >= 0 && band < kMaxBands
            && selectedMask_[static_cast<std::size_t> (band)];
    }
    juce::Rectangle<int> graphBoundsForTesting() const noexcept { return graph_; }
    juce::Rectangle<int> panelBoundsForTesting() const noexcept { return panel_; }
    juce::Rectangle<int> dynamicPanelBoundsForTesting() const noexcept
    {
        return dynamicPanel_;
    }
    bool dynamicPanelVisibleForTesting() const noexcept
    {
        return dynamicExpanded_ && selected_ >= 0;
    }
    void expandDynamicForTesting (bool show) { setDynamicExpanded (show); }
    std::array<juce::Slider*, 4> dynamicSlidersForTesting()
    {
        return {{ &dynamicSliders_[0], &dynamicSliders_[1],
                  &dynamicSliders_[2], &dynamicSliders_[3] }};
    }
    void toggleDynamicOnForTesting()
    {
        // JUCE Button::triggerClick() posts a message asynchronously.
        // Unit assertions run synchronously on this test's message thread;
        // invoking the ACTUAL production callback avoids a false negative
        // caused by checking before the queued click was dispatched.
        if (dynToggle_.onClick)
            dynToggle_.onClick();
    }
    juce::Rectangle<int> nodeHitAreaForTesting (int band) const
    {
        if (band < 0 || band >= kMaxBands)
            return {};
        const auto p = nodePoint (band);
        return { p.x - 22, p.y - 22, 44, 44 };
    }
    std::array<juce::Rectangle<int>, 8> contextualHeaderBoundsForTesting() const
    {
        // Hidden mobile controls retain historical desktop bounds in JUCE.
        // The adversarial critic must test ACTUAL interactive hit regions
        // only, never stale rectangles from a prior responsive breakpoint.
        const auto live = [] (const juce::Component& component)
        {
            return component.isVisible() ? component.getBounds()
                                         : juce::Rectangle<int> {};
        };
        return {{ live (bandPower_), live (shape_), live (slope_),
                  live (placement_), live (soloHold_), live (advanced_),
                  live (closeBand_), live (dynamic_) }};
    }
    juce::Rectangle<int> bootEyeBoundsForTesting() const noexcept
    {
        return bootEyeBounds_;
    }
    juce::Rectangle<int> presetBoundsForTesting() const noexcept
    {
        return preset_.getBounds();
    }
    bool hasMeasuredSpectrumForTesting() const noexcept
    {
        return spectrumReady_ && spectrumShown_[0] && spectrumShown_[1];
    }
    // Never seeds artificial analyser data. Only latches a worker FFT frame.
    void refreshMeasuredSpectrumForTesting() { timerCallback(); }
    void setSpectrumFreezeForTesting (bool frozen) { setAnalyzerFrozen (frozen); }
    bool spectrumFrozenForTesting() const noexcept { return analyzerFrozen_; }
    void setPeakHoldForTesting (bool enabled) { setPeakHoldEnabled (enabled); }
    bool hasPeakHoldForTesting (bool post) const noexcept
    {
        return heldPeakReady_[post ? 1 : 0];
    }
    float heldPeakAtHzForTesting (bool post, float frequencyHz) const noexcept
    {
        if (! heldPeakReady_[post ? 1 : 0] || spectrumBinHz_ <= 0.0f)
            return -120.0f;
        const auto bin = std::clamp (static_cast<int> (
            std::round (frequencyHz / spectrumBinHz_)), 0,
            Analysis::SpectrumAnalyzerCore::kSpectrumBins - 1);
        return heldPeaks_[post ? 1 : 0][static_cast<std::size_t> (bin)];
    }
    int spectrumFrameIndexForTesting() const noexcept { return spectrumIndex_; }
    void clearHeldPeaksForTesting() { clearHeldPeaks(); }
    float measuredSpectrumAtHzForTesting (bool post, float frequencyHz) const noexcept
    {
        if (! spectrumReady_ || ! spectrumShown_[post ? 1 : 0]
            || spectrumBinHz_ <= 0.0f)
            return -120.0f;
        const auto bin = std::clamp (static_cast<int> (
            std::round (frequencyHz / spectrumBinHz_)), 0,
            Analysis::SpectrumAnalyzerCore::kSpectrumBins - 1);
        return spectrum_[post ? 1 : 0][static_cast<std::size_t> (bin)];
    }
    void setSpectrumSmoothingForTesting (double octaveWidth)
    {
        spectrumSmoothingOctaves_ = std::clamp (octaveWidth,
                                                1.0 / 72.0, 1.0 / 6.0);
    }
    float displayedSpectrumDbForTesting (bool post, float frequencyHz)
        const noexcept
    {
        return displayedSpectrumDb (post ? 1 : 0, frequencyHz);
    }
    float maximumMeasuredSpectrumDbForTesting (bool post) const noexcept
    {
        if (! spectrumReady_ || ! spectrumShown_[post ? 1 : 0])
            return -120.0f;
        const auto& bins = spectrum_[post ? 1 : 0];
        return *std::max_element (bins.begin() + 2, bins.end());
    }
    int activeBandsForTesting() const noexcept
    {
        int result = 0;
        for (int b = 0; b < kMaxBands; ++b)
            if (param (parameterIndex (b, BandParameterOffset::Enabled))->getBool())
                ++result;
        return result;
    }
    void selectBandForTesting (int band) { select (band); }
    void undoForTesting() { historyStep (-1); }
    void redoForTesting() { historyStep (1); }
    void createBandForTesting (juce::Point<float> pt) { createBand (pt); }
    void refreshForTesting() { curveDirty_ = true; sync(); repaint(); }

private:
    struct EQ4Look final : juce::LookAndFeel_V4
    {
        EQ4Look()
        {
            setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
            setColour (juce::Slider::textBoxBackgroundColourId,
                       juce::Colour (0xff27232c));
            setColour (juce::Slider::textBoxOutlineColourId,
                       juce::Colour (0xff544b58));
            setColour (juce::PopupMenu::backgroundColourId,
                       juce::Colour (0xff252029));
            setColour (juce::PopupMenu::textColourId, juce::Colours::white);
        }
        void drawButtonBackground (juce::Graphics& g, juce::Button&,
                                   const juce::Colour&, bool over, bool down) override
        {
            auto area = g.getClipBounds().toFloat();
            g.setColour (juce::Colour (down ? 0xff504052
                                   : over ? 0xff3c3342 : 0xff2d2733));
            g.fillRoundedRectangle (area.reduced (1), 5.0f);
        }
        void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                             bool, bool) override
        {
            g.setColour (juce::Colour (0xffe8e1ed));
            g.setFont (juce::FontOptions (
                std::clamp (b.getHeight() * 0.33f, 10.0f, 12.0f)));
            g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (2),
                              juce::Justification::centred, 1);
        }
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                               float value, float start, float end,
                               juce::Slider& slider) override
        {
            const float diameter = static_cast<float> (
                std::min (w - 12, h - 39));
            const float radius = std::max (12.0f, diameter * 0.5f);
            const auto cx = static_cast<float> (x) + w * 0.5f;
            const auto cy = static_cast<float> (y) + std::max (17.0f, radius + 2.0f);
            juce::Path track, arc;
            track.addCentredArc (cx, cy, radius, radius, 0,
                                 start, end, true);
            arc.addCentredArc (cx, cy, radius, radius, 0,
                               start, start + value * (end - start), true);
            g.setColour (juce::Colour (0xff56505f));
            g.strokePath (track, juce::PathStrokeType (3.0f));
            g.setColour (juce::Colour (0xffefca65));
            g.strokePath (arc, juce::PathStrokeType (3.1f));
            const float angle = start + value * (end - start);
            g.setColour (juce::Colour (0xff292531));
            g.fillEllipse (cx - radius + 5, cy - radius + 5,
                           (radius - 5) * 2, (radius - 5) * 2);
            g.setColour (juce::Colour (0xfff1eaf2));
            g.drawLine (cx, cy, cx + std::sin (angle) * (radius - 8),
                        cy - std::cos (angle) * (radius - 8), 2.0f);
            g.setFont (juce::FontOptions (10.0f));
            g.drawFittedText (slider.getTitle(), x, y + h - 15, w, 14,
                              juce::Justification::centred, 1);
        }
    };

    static juce::Colour bandColour (int index)
    {
        constexpr std::array<juce::uint32, 10> hex {{
            0xffd87ed5, 0xff59c5e5, 0xffedc24a, 0xff70c883, 0xffe88664,
            0xff8377e3, 0xff79d2a6, 0xffef89aa, 0xffa7d86a, 0xffddaa72
        }};
        return juce::Colour (hex[static_cast<std::size_t> (index % 10)]);
    }
    ParametricEQParameter* param (int index) const
    {
        return processor_.getParametricEQParameter (index);
    }
    void setUnits (int index, float units)
    {
        auto* p = param (index);
        p->setValueNotifyingHost (p->toNormalised (units));
    }
    void toggleParameter (int idx)
    {
        auto* p = param (idx);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getBool() ? 0.0f : 1.0f);
        p->endChangeGesture();
        commitHistory();
        curveDirty_ = true;
    }
    void setupDial (juce::Slider& slider,
                    BandParameterOffset offset, const char* title)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 88, 21);
        slider.setRange (0.0, 1.0);
        slider.setTitle (title);
        slider.onDragStart = [this, offset]
        {
            if (selected_ >= 0)
                param (parameterIndex (selected_, offset))->beginChangeGesture();
        };
        slider.onDragEnd = [this, offset]
        {
            if (selected_ >= 0)
                param (parameterIndex (selected_, offset))->endChangeGesture();
            commitHistory();
        };
        slider.onValueChange = [this, &slider, offset]
        {
            if (selected_ >= 0 && ! syncing_)
            {
                param (parameterIndex (selected_, offset))->setValueNotifyingHost (
                    static_cast<float> (slider.getValue()));
                if (! slider.isMouseButtonDown()) commitHistory();
                curveDirty_ = true; repaint();
            }
        };
        slider.textFromValueFunction = [this, offset] (double value)
        {
            if (selected_ < 0) return juce::String (value, 2);
            return param (parameterIndex (selected_, offset))->getText (
                static_cast<float> (value), 0);
        };
        slider.valueFromTextFunction = [this, offset] (const juce::String& text)
        {
            if (selected_ < 0) return 0.0;
            return static_cast<double> (param (
                parameterIndex (selected_, offset))->getValueForText (text));
        };
        addAndMakeVisible (slider);
    }
    void setupDynamicSlider (juce::Slider& slider,
                             DynamicBandOffset offset)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 83, 26);
        slider.setRange (0.0, 1.0);
        slider.onDragStart = [this, offset]
        {
            if (selected_ >= 0)
                param (dynamicParameterIndex (
                    selected_, offset))->beginChangeGesture();
        };
        slider.onDragEnd = [this, offset]
        {
            if (selected_ >= 0)
                param (dynamicParameterIndex (
                    selected_, offset))->endChangeGesture();
            commitHistory();
        };
        slider.onValueChange = [this, &slider, offset]
        {
            if (selected_ >= 0 && ! syncing_)
            {
                param (dynamicParameterIndex (
                    selected_, offset))->setValueNotifyingHost (
                        static_cast<float> (slider.getValue()));
                if (! slider.isMouseButtonDown())
                    commitHistory();
                repaint();
            }
        };
        slider.textFromValueFunction = [this, offset] (double value)
        {
            if (selected_ < 0) return juce::String (value, 2);
            return param (dynamicParameterIndex (
                selected_, offset))->getText (
                    static_cast<float> (value), 0);
        };
        slider.valueFromTextFunction = [this, offset] (
            const juce::String& text)
        {
            if (selected_ < 0) return 0.0;
            return static_cast<double> (
                param (dynamicParameterIndex (
                    selected_, offset))->getValueForText (text));
        };
        addAndMakeVisible (slider);
    }
    void setDynamicControlsVisible (bool visible)
    {
        const std::array<juce::Component*, 4> buttons {{
            &dynToggle_, &dynSpectral_, &dynFilter_, &dynClose_
        }};
        for (auto* widget : buttons)
            widget->setVisible (visible);
        for (int i = 0; i < 4; ++i)
        {
            dynamicNames_[static_cast<std::size_t> (i)].setVisible (visible);
            dynamicSliders_[static_cast<std::size_t> (i)].setVisible (visible);
        }
    }
    void setDynamicExpanded (bool expanded)
    {
        dynamicExpanded_ = expanded && selected_ >= 0;
        setDynamicControlsVisible (dynamicExpanded_);
        resized();
        repaint();
    }
    void layoutDynamicControls()
    {
        const bool visible = dynamicExpanded_ && selected_ >= 0;
        setDynamicControlsVisible (visible);
        if (! visible) return;
        const int w = getWidth();
        const int panelWidth = std::min (w - 18, w < 460 ? 302 : 346);
        const int panelHeight = std::min (
            272, std::max (180, graph_.getHeight() - 16));
        const int minX = 9;
        const int maxX = std::max (minX, w - panelWidth - 9);
        const int minY = graph_.getY() + 8;
        const int maxY = std::max (minY, graph_.getBottom()
                                          - panelHeight - 10);
        // Select a side of the frequency canvas that keeps the selected
        // filter node unobstructed. On very short windows there may be no
        // such position: panel remains explicitly user-invoked and closable.
        std::array<juce::Rectangle<int>, 4> candidates {{
            { minX, minY, panelWidth, panelHeight },
            { maxX, minY, panelWidth, panelHeight },
            { minX, maxY, panelWidth, panelHeight },
            { maxX, maxY, panelWidth, panelHeight }
        }};
        auto best = candidates[0];
        float score = std::numeric_limits<float>::infinity();
        for (const auto& candidate : candidates)
        {
            // An explicitly opened dynamics card must not cover the main
            // band's knobs and SOLO button. Prefer the opposite quadrant
            // before considering any band-node collision penalties.
            float cost = candidate.intersects (panel_) ? 40000.0f : 0.0f;
            for (int band = 0; band < kMaxBands; ++band)
            {
                if (! param (parameterIndex (band,
                        BandParameterOffset::Enabled))->getBool())
                    continue;
                if (candidate.expanded (18).contains (nodePoint (band)))
                    cost += band == selected_ ? 100000.0f : 20000.0f;
            }
            if (cost < score) { score = cost; best = candidate; }
        }
        dynamicPanel_ = best;
        const int px = best.getX(), py = best.getY(), pw = best.getWidth();
        dynClose_.setBounds (px + pw - 59, py + 5, 51, 31);
        dynToggle_.setBounds (px + 9, py + 38, 72, 35);
        dynSpectral_.setBounds (px + 84, py + 38, 89, 35);
        dynFilter_.setBounds (px + 176, py + 38, pw - 185, 35);
        const int rowTop = py + 81;
        const int rowHeight = std::max (24,
            (best.getHeight() - 88) / 4);
        for (int row = 0; row < 4; ++row)
        {
            const int yy = rowTop + row * rowHeight;
            dynamicNames_[static_cast<std::size_t> (row)].setBounds (
                px + 9, yy, 80, rowHeight - 2);
            dynamicSliders_[static_cast<std::size_t> (row)].setBounds (
                px + 89, yy + 1, pw - 98, rowHeight - 3);
        }
        // These controls must remain accessible when the main band HUD
        // occupies another part of the graph.
        for (auto* widget : { &dynClose_, &dynToggle_,
                              &dynSpectral_, &dynFilter_ })
            widget->toFront (false);
        for (auto& slider : dynamicSliders_)
            slider.toFront (false);
    }
    void setSelectedVisible (bool visible)
    {
        const std::array<juce::Component*, 11> widgets {{
            &bandPower_, &shape_, &slope_, &placement_, &dynamic_,
            &advanced_, &closeBand_, &soloHold_, &freq_, &gain_, &q_
        }};
        for (auto* widget : widgets)
            widget->setVisible (visible);
    }
    int selectedCount() const noexcept
    {
        return static_cast<int> (std::count (
            selectedMask_.begin(), selectedMask_.end(), true));
    }
    void select (int index, bool retainGroup = false)
    {
        if (index < 0 || index >= kMaxBands
            || ! param (parameterIndex (
                index, BandParameterOffset::Enabled))->getBool())
            index = -1;
        if (! retainGroup)
            selectedMask_.fill (false);
        selected_ = index;
        if (index >= 0)
            selectedMask_[static_cast<std::size_t> (index)] = true;
        setSelectedVisible (selected_ >= 0);
        if (selected_ < 0)
            dynamicExpanded_ = false;
        setDynamicControlsVisible (dynamicExpanded_);
        sync(); resized(); repaint();
    }
    void toggleSelectedBand (int band)
    {
        if (band < 0 || band >= kMaxBands
            || ! param (parameterIndex (band,
                BandParameterOffset::Enabled))->getBool())
            return;
        const auto index = static_cast<std::size_t> (band);
        selectedMask_[index] = ! selectedMask_[index];
        if (selectedMask_[index])
            selected_ = band;
        else if (selected_ == band)
        {
            selected_ = -1;
            for (int other = 0; other < kMaxBands; ++other)
                if (selectedMask_[static_cast<std::size_t> (other)])
                    selected_ = other;
        }
        setSelectedVisible (selected_ >= 0);
        if (selected_ < 0)
            dynamicExpanded_ = false;
        setDynamicControlsVisible (dynamicExpanded_);
        sync(); resized(); repaint();
    }
    void finishGestures()
    {
        if (dragBand_ < 0) return;
        for (int band = 0; band < kMaxBands; ++band)
        {
            if (! gestureMask_[static_cast<std::size_t> (band)])
                continue;
            param (parameterIndex (band, BandParameterOffset::Frequency))
                ->endChangeGesture();
            param (parameterIndex (band, BandParameterOffset::Gain))
                ->endChangeGesture();
        }
        gestureMask_.fill (false);
        commitHistory();
    }
    void sync()
    {
        syncing_ = true;
        phase_.setButtonText (param (kPhaseModeParameter)->getBool()
                               ? "LINEAR PHASE" : "ZERO LATENCY");
        globalBypass_.setButtonText (param (kGlobalBypassParameter)->getBool()
                                  ? "BYPASS" : (getWidth() < 670 ? "ON" : "POWER ON"));
        autoGain_.setButtonText (param (kOutputAutoGainParameter)->getBool()
                              ? "AUTO ON" : (getWidth() < 670 ? "AUTO" : "AUTO GAIN"));
        if (selected_ >= 0)
        {
            freq_.setValue (param (parameterIndex (selected_,
                BandParameterOffset::Frequency))->getValue(), juce::dontSendNotification);
            gain_.setValue (param (parameterIndex (selected_,
                BandParameterOffset::Gain))->getValue(), juce::dontSendNotification);
            q_.setValue (param (parameterIndex (selected_,
                BandParameterOffset::Q))->getValue(), juce::dontSendNotification);
            static const char* shapeNames[] = {
                "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
                "Notch", "Band Pass", "Tilt", "Flat Tilt", "All Pass"
            };
            shape_.setButtonText (shapeNames[juce::jlimit (0, 9,
                param (parameterIndex (selected_, BandParameterOffset::Shape))
                    ->getChoiceIndex())]);
            auto* slope = param (parameterIndex (selected_, BandParameterOffset::Slope));
            slope_.setButtonText (slope->getText (slope->getValue(), 0));
            placement_.setButtonText (channelPlacementName (
                static_cast<ChannelPlacement> (juce::jlimit (0, 4,
                    param (placementParameterIndex (selected_))->getChoiceIndex()))));
            const bool by = param (parameterIndex (
                selected_, BandParameterOffset::Bypass))->getBool();
            bandPower_.setButtonText (by ? "OFF" : "ON");
            const bool isDynamic = param (dynamicParameterIndex (
                selected_, DynamicBandOffset::Enable))->getBool();
            dynamic_.setButtonText (isDynamic ? "DYN EDIT" : "DYN");
            dynToggle_.setButtonText (isDynamic ? "DYN ON" : "DYN OFF");
            dynSpectral_.setButtonText (
                param (spectralBandParameterIndex (selected_))->getBool()
                    ? "SPECTRAL ON" : "SPECTRAL");
            dynFilter_.setButtonText (
                param (dynamicFilterParameterIndex (selected_))->getBool()
                    ? "FILTER ON" : "BAND DETECTOR");
            constexpr DynamicBandOffset offsets[] = {
                DynamicBandOffset::Threshold, DynamicBandOffset::Range,
                DynamicBandOffset::Attack, DynamicBandOffset::Release
            };
            for (int i = 0; i < 4; ++i)
                dynamicSliders_[static_cast<std::size_t> (i)].setValue (
                    param (dynamicParameterIndex (
                        selected_, offsets[i]))->getValue(),
                    juce::dontSendNotification);
        }
        syncing_ = false;
    }
    void beginNodeGesture (int band, juce::Point<float> at)
    {
        if (band < 0 || band >= kMaxBands) return;
        dragBand_ = band;
        dragOrigin_ = at;
        createdDuringGesture_ = false;
        auto* hz = param (parameterIndex (band, BandParameterOffset::Frequency));
        auto* gain = param (parameterIndex (band, BandParameterOffset::Gain));
        dragStartHz_ = hz->getUnitsValue();
        dragStartGain_ = gain->getUnitsValue();
        gestureMask_.fill (false);
        for (int other = 0; other < kMaxBands; ++other)
        {
            if (! selectedMask_[static_cast<std::size_t> (other)])
                continue;
            if (! param (parameterIndex (other,
                BandParameterOffset::Enabled))->getBool())
                continue;
            gestureMask_[static_cast<std::size_t> (other)] = true;
            groupStartHz_[static_cast<std::size_t> (other)] =
                param (parameterIndex (other,
                    BandParameterOffset::Frequency))->getUnitsValue();
            groupStartGain_[static_cast<std::size_t> (other)] =
                param (parameterIndex (other,
                    BandParameterOffset::Gain))->getUnitsValue();
            param (parameterIndex (other,
                BandParameterOffset::Frequency))->beginChangeGesture();
            param (parameterIndex (other,
                BandParameterOffset::Gain))->beginChangeGesture();
        }
    }
    bool nearResultCurve (juce::Point<float> at) const
    {
        const auto hz = xToHz (at.x);
        double gainDb = 0.0;
        const auto rate = std::max (8000.0, processor_.getSampleRate());
        const auto mode = param (kDesignModeParameter)->getBool()
            ? DesignMode::AnalogMatched : DesignMode::Realtime;
        for (int band = 0; band < kMaxBands; ++band)
        {
            const auto s = settingsForBand (band);
            if (! s.enabled || s.bypassed) continue;
            const auto response = ResponseCore::bandResponse (s, mode, rate, hz);
            gainDb += 20.0 * std::log10 (std::max (1.0e-8,
                                                   std::abs (response)));
        }
        return std::abs (at.y - dbToY (gainDb)) <= 16.0f;
    }
    void createBand (juce::Point<float> pt, bool dynamic = false,
                     bool spectral = false)
    {
        int slot = -1;
        for (int i = 0; i < kMaxBands; ++i)
            if (! param (parameterIndex (i, BandParameterOffset::Enabled))->getBool())
            {
                slot = i; break;
            }
        if (slot < 0) return;
        // Deleted bands leave frozen host parameter slots; explicitly reset
        // all per-band modes before reuse or stale dynamics could reactivate.
        setUnits (dynamicParameterIndex (
            slot, DynamicBandOffset::Enable), 0.0f);
        setUnits (dynamicParameterIndex (
            slot, DynamicBandOffset::Range), 0.0f);
        setUnits (spectralBandParameterIndex (slot), 0.0f);
        setUnits (placementParameterIndex (slot),
                  static_cast<float> (ChannelPlacement::Stereo));
        setUnits (parameterIndex (slot, BandParameterOffset::Frequency),
                  static_cast<float> (xToHz (pt.x)));
        setUnits (parameterIndex (slot, BandParameterOffset::Gain),
                  static_cast<float> (yToDb (pt.y)));
        setUnits (parameterIndex (slot, BandParameterOffset::Q), 1.0f);
        // Source manual p.9: filter type follows creation location.
        // Extremes become cut filters; outer spectral zones are shelves.
        // The centre remains a Bell until the user chooses another shape.
        const float ratio = std::clamp (pt.x
            / static_cast<float> (std::max (1, getWidth())), 0.0f, 1.0f);
        const auto shape = ratio < 0.045f ? FilterShape::LowCut
                         : ratio > 0.955f ? FilterShape::HighCut
                         : ratio < 0.17f ? FilterShape::LowShelf
                         : ratio > 0.83f ? FilterShape::HighShelf
                         : FilterShape::Bell;
        setUnits (parameterIndex (slot, BandParameterOffset::Shape),
                  static_cast<float> (shape));
        setUnits (parameterIndex (slot, BandParameterOffset::Bypass), 0.0f);
        // Alt creates a real dynamic band; Alt+Shift creates a spectral
        // candidate. Spectral PDC remains deferred safely until prepare.
        if (dynamic && ! spectral)
        {
            setUnits (dynamicParameterIndex (
                slot, DynamicBandOffset::Enable), 1.0f);
            setUnits (dynamicParameterIndex (
                slot, DynamicBandOffset::Range), 6.0f);
        }
        if (spectral)
            setUnits (spectralBandParameterIndex (slot), 1.0f);
        setUnits (parameterIndex (slot, BandParameterOffset::Enabled), 1.0f);
        commitHistory();
        curveDirty_ = true;
        select (slot);
    }
    float hzToX (double hz) const
    {
        const double lo = std::log (20.0), hi = std::log (30000.0);
        return static_cast<float> ((std::log (std::clamp (
            hz, 20.0, 30000.0)) - lo) / (hi - lo) * getWidth());
    }
    double xToHz (float x) const
    {
        return 20.0 * std::exp ((std::clamp (x, 0.0f,
            static_cast<float> (std::max (1, getWidth())))
            / std::max (1, getWidth())) * std::log (1500.0));
    }
    float dbToY (double db) const
    {
        const float usable = static_cast<float> (
            std::max (1, graph_.getHeight() - 56));
        return graph_.getY() + 20.0f + usable * 0.5f
             - static_cast<float> (db / rangeDb_) * usable * 0.5f;
    }
    double yToDb (float y) const
    {
        const float usable = static_cast<float> (
            std::max (1, graph_.getHeight() - 56));
        const double value = rangeDb_
            * (1.0 - 2.0 * (y - graph_.getY() - 20.0f) / usable);
        return std::clamp (value, -static_cast<double> (rangeDb_),
                                   static_cast<double> (rangeDb_));
    }
    juce::Point<int> nodePoint (int band) const
    {
        const auto* h = param (parameterIndex (band, BandParameterOffset::Frequency));
        const auto* g = param (parameterIndex (band, BandParameterOffset::Gain));
        return { juce::roundToInt (hzToX (h->getUnitsValue())),
                 juce::roundToInt (dbToY (g->getUnitsValue())) };
    }
    int hitBand (juce::Point<float> pt) const
    {
        for (int b = kMaxBands - 1; b >= 0; --b)
        {
            if (! param (parameterIndex (b, BandParameterOffset::Enabled))->getBool())
                continue;
            if (pt.getDistanceFrom (nodePoint (b).toFloat()) <= 22.0f)
                return b;
        }
        return -1;
    }
    BandSettings settingsForBand (int band) const
    {
        BandSettings s;
        s.enabled = param (parameterIndex (band, BandParameterOffset::Enabled))->getBool();
        s.bypassed = param (parameterIndex (band, BandParameterOffset::Bypass))->getBool();
        s.shape = static_cast<FilterShape> (
            param (parameterIndex (band, BandParameterOffset::Shape))->getChoiceIndex());
        s.placement = static_cast<ChannelPlacement> (
            param (placementParameterIndex (band))->getChoiceIndex());
        s.frequencyHz = param (parameterIndex (band, BandParameterOffset::Frequency))
            ->getUnitsValue();
        s.gainDb = param (parameterIndex (band, BandParameterOffset::Gain))->getUnitsValue();
        s.q = param (parameterIndex (band, BandParameterOffset::Q))->getUnitsValue();
        s.slopeDbPerOctave = param (parameterIndex (
            band, BandParameterOffset::Slope))->getUnitsValue();
        return s;
    }
    void rebuildResponse()
    {
        response_.clear();
        for (auto& p : bandCurves_) p.clear();
        const int count = juce::jlimit (200, 510, getWidth() / 3);
        const auto rate = std::max (8000.0, processor_.getSampleRate());
        const auto mode = param (kDesignModeParameter)->getBool()
            ? DesignMode::AnalogMatched : DesignMode::Realtime;
        for (int i = 0; i < kMaxBands; ++i)
            cachedBands_[static_cast<std::size_t> (i)] = settingsForBand (i);

        for (int i = 0; i < count; ++i)
        {
            const float x = getWidth() * static_cast<float> (i)
                                      / static_cast<float> (count - 1);
            const double hz = xToHz (x);
            double result = 0.0;
            for (int b = 0; b < kMaxBands; ++b)
            {
                const auto& s = cachedBands_[static_cast<std::size_t> (b)];
                if (! s.enabled || s.bypassed) continue;
                const double amp = std::abs (ResponseCore::bandResponse (
                    s, mode, rate, hz));
                const double db = 20.0 * std::log10 (std::max (1.0e-8, amp));
                result += db;
                auto& path = bandCurves_[static_cast<std::size_t> (b)];
                if (i == 0) path.startNewSubPath (x, dbToY (db));
                else path.lineTo (x, dbToY (db));
            }
            if (i == 0) response_.startNewSubPath (x, dbToY (result));
            else response_.lineTo (x, dbToY (result));
        }
        curveDirty_ = false;
    }
    float displayedSpectrumDb (int mode, double frequencyHz,
                               bool usePeakHold = false) const noexcept
    {
        if (mode < 0 || mode > 1 || spectrumBinHz_ <= 0.0f
            || frequencyHz < 20.0)
            return -120.0f;
        const auto& measured = usePeakHold
            ? heldPeaks_[static_cast<std::size_t> (mode)]
            : spectrum_[static_cast<std::size_t> (mode)];
        const int count = Analysis::SpectrumAnalyzerCore::kSpectrumBins;
        const double fractional = frequencyHz / spectrumBinHz_;
        if (fractional > count - 1.0) return -120.0f;

        // Linear power integration, not a made-up spectrum or synthetic
        // harmonics. Gaussian kernel width 1/18 octave: smooth only the
        // PRESENTATION (spatial frequency axis). Native worker measurements,
        // true harmonic locations and PRE/POST delta remain unchanged.
        const double sigma = spectrumSmoothingOctaves_ * std::log (2.0);
        const double lower = frequencyHz * std::exp (-2.5 * sigma);
        const double upper = frequencyHz * std::exp ( 2.5 * sigma);
        const int first = std::max (1, static_cast<int> (
            std::floor (lower / spectrumBinHz_)));
        const int last = std::min (count - 1, static_cast<int> (
            std::ceil (upper / spectrumBinHz_)));
        double totalPower = 0.0, totalWeight = 0.0;
        for (int bin = first; bin <= last; ++bin)
        {
            const float db = measured[static_cast<std::size_t> (bin)];
            if (! std::isfinite (db)) continue;
            const double hz = bin * static_cast<double> (spectrumBinHz_);
            const double distance = std::log (hz / frequencyHz) / sigma;
            const double weight = std::exp (-0.5 * distance * distance);
            totalPower += weight * std::pow (10.0, db * 0.1);
            totalWeight += weight;
        }
        if (totalWeight > 1.0e-8)
            return static_cast<float> (10.0 * std::log10 (
                std::max (1.0e-13, totalPower / totalWeight)));

        // Below FFT resolution, use its nearest measured bin rather than
        // extrapolating harmonics or hiding all information at 20–50 Hz.
        const int nearest = std::clamp (
            static_cast<int> (std::round (fractional)), 1, count - 1);
        return measured[static_cast<std::size_t> (nearest)];
    }

    void drawSpectrum (juce::Graphics& g)
    {
        if (! spectrumReady_) return;
        const int count = Analysis::SpectrumAnalyzerCore::kSpectrumBins;
        for (int mode = 0; mode < 2; ++mode)
        {
            if (! spectrumShown_[mode]) continue;
            juce::Path spectrum;
            bool begun = false;
            const float bottom = static_cast<float> (graph_.getBottom() - 25);
            const int points = std::clamp (getWidth() / 2, 160, 950);
            for (int sample = 0; sample <= points; ++sample)
            {
                const float x = getWidth() * static_cast<float> (sample)
                                / static_cast<float> (points);
                const double hz = xToHz (x);
                const float maxMeasuredHz = spectrumBinHz_ * (count - 1);
                if (hz > maxMeasuredHz || hz < 20.0) continue;
                // Log-frequency power integration of ACTUAL FFT bins.
                // Raw PRE/POST values remain untouched and testable.
                const float db = displayedSpectrumDb (mode, hz);
                if (! std::isfinite (db)) continue;
                const float y = bottom
                    - (std::clamp (db, -108.0f, -8.0f) + 108.0f)
                      * (graph_.getHeight() - 49) / 100.0f;
                if (! begun) { spectrum.startNewSubPath (x, y); begun = true; }
                else spectrum.lineTo (x, y);
            }
            if (! begun) continue;
            // A translucent filled measured spectrum beneath subtle strokes.
            // PRE stays muted gray, POST warm gold, both always distinguishable.
            juce::Path fill (spectrum);
            fill.lineTo (static_cast<float> (getWidth()), bottom);
            fill.lineTo (0.0f, bottom);
            fill.closeSubPath();
            const auto ink = mode == 0 ? juce::Colour (0xffa6a2af)
                                       : juce::Colour (0xffe5ba65);
            g.setColour (ink.withAlpha (mode == 0 ? 0.07f : 0.12f));
            g.fillPath (fill);
            g.setColour (ink.withAlpha (mode == 0 ? 0.56f : 0.79f));
            g.strokePath (spectrum, juce::PathStrokeType (
                mode == 0 ? 1.0f : 1.45f));
            // Peak hold visualizes bin-wise maxima of REAL worker FFT frames.
            // It never invents sound or infers peaks from the EQ response.
            if (peakHoldEnabled_ && heldPeakReady_[static_cast<std::size_t> (mode)])
            {
                juce::Path peakPath, dashed;
                bool first = true;
                for (int sample = 0; sample <= points; ++sample)
                {
                    const float x = getWidth() * static_cast<float> (sample)
                                    / static_cast<float> (points);
                    const double hz = xToHz (x);
                    if (hz < 20.0 || hz > spectrumBinHz_ * (count - 1))
                        continue;
                    const float db = displayedSpectrumDb (mode, hz, true);
                    if (! std::isfinite (db)) continue;
                    const float y = bottom
                        - (std::clamp (db, -108.0f, -8.0f) + 108.0f)
                          * (graph_.getHeight() - 49) / 100.0f;
                    if (first) { peakPath.startNewSubPath (x, y); first = false; }
                    else peakPath.lineTo (x, y);
                }
                if (! first)
                {
                    constexpr float dash[] = { 3.0f, 4.0f };
                    juce::PathStrokeType (1.0f).createDashedStroke (
                        dashed, peakPath, dash, 2);
                    g.setColour (ink.withAlpha (mode == 0 ? 0.35f : 0.60f));
                    g.strokePath (dashed, juce::PathStrokeType (1.0f));
                }
            }
        }
        if (analyzerFrozen_)
        {
            g.setColour (juce::Colour (0xffe6c474).withAlpha (0.85f));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("FFT FROZEN", graph_.getX() + 16,
                        graph_.getY() + 8, 105, 20,
                        juce::Justification::centredLeft);
        }
    }

    void clearHeldPeaks() noexcept
    {
        for (auto& bins : heldPeaks_) bins.fill (-120.0f);
        heldPeakReady_.fill (false);
    }
    void collectMeasuredPeaks() noexcept
    {
        if (! peakHoldEnabled_ || ! spectrumReady_) return;
        for (int mode = 0; mode < 2; ++mode)
        {
            if (! spectrumShown_[static_cast<std::size_t> (mode)]) continue;
            auto& held = heldPeaks_[static_cast<std::size_t> (mode)];
            const auto& bins = spectrum_[static_cast<std::size_t> (mode)];
            for (std::size_t i = 0; i < bins.size(); ++i)
                if (std::isfinite (bins[i]))
                    held[i] = std::max (held[i], bins[i]);
            heldPeakReady_[static_cast<std::size_t> (mode)] = true;
        }
    }
    void setPeakHoldEnabled (bool enabled)
    {
        peakHoldEnabled_ = enabled;
        clearHeldPeaks();
        // Seed with the currently visible real frame on first activation.
        if (enabled) collectMeasuredPeaks();
        repaint();
    }
    void setAnalyzerFrozen (bool freeze)
    {
        if (freeze && ! spectrumReady_) return;
        analyzerFrozen_ = freeze;
        if (! freeze) spectrumIndex_ = -1; // reacquire latest on resume
        syncAnalyzerText();
        repaint();
    }
    void showAnalyzerMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("REAL FFT ANALYZER");
        static constexpr const char* modes[] = {
            "Off", "Pre-EQ", "Post-EQ", "Pre + Post"
        };
        for (int i = 0; i < 4; ++i)
            menu.addItem (i + 1, modes[i], true,
                static_cast<int> (processor_.getAnalyzerMode()) == i);
        menu.addSeparator();
        menu.addSectionHeader ("FREQUENCY SMOOTHING (DISPLAY ONLY)");
        menu.addItem (11, "Precise - 1/36 octave", true,
                      spectrumSmoothingOctaves_ < 1.0 / 26.0);
        menu.addItem (12, "Natural - 1/18 octave", true,
                      spectrumSmoothingOctaves_ >= 1.0 / 26.0
                      && spectrumSmoothingOctaves_ < 1.0 / 13.0);
        menu.addItem (13, "Smooth - 1/9 octave", true,
                      spectrumSmoothingOctaves_ >= 1.0 / 13.0);
        menu.addSeparator();
        menu.addSectionHeader ("CAPTURE — DISPLAY ONLY");
        menu.addItem (21, "Freeze measured FFT", spectrumReady_,
                      analyzerFrozen_);
        menu.addItem (22, "Peak hold (real FFT maxima)",
                      processor_.getAnalyzerMode() != Analysis::SpectrumTapMode::Closed,
                      peakHoldEnabled_);
        menu.addItem (23, "Clear held peaks", peakHoldEnabled_
                      && (heldPeakReady_[0] || heldPeakReady_[1]));
        juce::Component::SafePointer<ApexQ4Editor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (&analyzer_), [safe] (int id)
        {
            if (auto* editor = safe.getComponent())
            {
                if (id >= 1 && id <= 4)
                {
                    editor->processor_.setAnalyzerMode (
                        static_cast<Analysis::SpectrumTapMode> (id - 1));
                    editor->analyzerAutoStarted_ = false;
                    editor->spectrumReady_ = false;
                    editor->spectrumShown_.fill (false);
                    editor->spectrumIndex_ = -1;
                    editor->analyzerFrozen_ = false;
                    editor->clearHeldPeaks();
                    if (id == 1) editor->peakHoldEnabled_ = false;
                    editor->syncAnalyzerText();
                }
                if (id == 21) editor->setAnalyzerFrozen (
                    ! editor->analyzerFrozen_);
                if (id == 22) editor->setPeakHoldEnabled (
                    ! editor->peakHoldEnabled_);
                if (id == 23) editor->clearHeldPeaks();
                if (id >= 11 && id <= 13)
                    editor->spectrumSmoothingOctaves_ =
                        id == 11 ? 1.0 / 36.0
                          : id == 12 ? 1.0 / 18.0 : 1.0 / 9.0;
                editor->repaint();
            }
        });
    }
    void syncAnalyzerText()
    {
        static const char* labels[] = {
            "ANALYZER OFF", "ANALYZER: PRE", "ANALYZER: POST",
            "ANALYZER: PRE + POST"
        };
        analyzer_.setButtonText (analyzerFrozen_ ? "FFT FROZEN"
            : labels[juce::jlimit (0, 3,
                static_cast<int> (processor_.getAnalyzerMode()))]);
    }
    void showShapeMenu()
    {
        if (selected_ < 0) return;
        juce::PopupMenu menu;
        static const char* names[] = {
            "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
            "Notch", "Band Pass", "Tilt", "Flat Tilt", "All Pass"
        };
        for (int i = 0; i < 10; ++i)
            menu.addItem (i + 1, names[i], true,
                param (parameterIndex (selected_, BandParameterOffset::Shape))
                    ->getChoiceIndex() == i);
        juce::Component::SafePointer<ApexQ4Editor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (&shape_), [safe] (int id)
        {
            if (auto* e = safe.getComponent(); e != nullptr && id > 0)
            {
                e->setUnits (parameterIndex (e->selected_,
                    BandParameterOffset::Shape), static_cast<float> (id - 1));
                e->curveDirty_ = true; e->sync(); e->repaint();
            }
        });
    }
    void showSlopeMenu()
    {
        if (selected_ < 0) return;
        juce::PopupMenu menu;
        static constexpr int slopes[] = { 0, 6, 12, 18, 24, 30, 36, 48, 72, 96 };
        for (int i = 0; i < 10; ++i)
            menu.addItem (i + 1, juce::String (slopes[i]) + " dB/oct");
        juce::Component::SafePointer<ApexQ4Editor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (&slope_), [safe] (int id)
        {
            if (auto* e = safe.getComponent(); e != nullptr && id > 0)
            {
                e->setUnits (parameterIndex (e->selected_,
                    BandParameterOffset::Slope), static_cast<float> (
                        slopes[id - 1]));
                e->curveDirty_ = true; e->sync(); e->repaint();
            }
        });
    }
    void showPlacementMenu()
    {
        if (selected_ < 0) return;
        juce::PopupMenu menu;
        for (int i = 0; i < kChannelPlacementCount; ++i)
            menu.addItem (i + 1, channelPlacementName (
                static_cast<ChannelPlacement> (i)));
        juce::Component::SafePointer<ApexQ4Editor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (&placement_), [safe] (int id)
        {
            if (auto* e = safe.getComponent(); e != nullptr && id > 0)
            {
                e->setUnits (placementParameterIndex (e->selected_),
                              static_cast<float> (id - 1));
                e->curveDirty_ = true; e->sync(); e->repaint();
            }
        });
    }
    void showAdvancedMenu()
    {
        if (selected_ < 0) return;
        juce::PopupMenu menu;
        menu.addItem (1, "Toggle Dynamic EQ");
        menu.addItem (2, "Toggle Spectral Dynamics");
        menu.addItem (3, "Solo: use HOLD control on band strip", false);
        menu.addItem (4, "Reset selected band");
        menu.addItem (5, "Delete band");
        menu.addSeparator();
        menu.addItem (9, "Open dynamic EQ controls...");
        menu.addItem (6, "Filter slope...");
        menu.addItem (7, "Channel placement...");
        menu.addItem (8, "Filter shape...");
        juce::Component::SafePointer<ApexQ4Editor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (&advanced_), [safe] (int id)
        {
            if (auto* e = safe.getComponent(); e != nullptr && e->selected_ >= 0)
            {
                const int band = e->selected_;
                if (id == 1)
                    e->toggleParameter (dynamicParameterIndex (
                        band, DynamicBandOffset::Enable));
                else if (id == 2)
                    e->toggleParameter (spectralBandParameterIndex (band));
                else if (id == 4)
                {
                    e->setUnits (parameterIndex (band,
                        BandParameterOffset::Gain), 0.0f);
                    e->setUnits (parameterIndex (band,
                        BandParameterOffset::Q), 1.0f);
                }
                else if (id == 5)
                {
                    e->setUnits (parameterIndex (band,
                        BandParameterOffset::Enabled), 0.0f);
                    e->select (-1);
                }
                else if (id == 9)
                    e->setDynamicExpanded (true);
                else if (id == 6)
                    e->showSlopeMenu();
                else if (id == 7)
                    e->showPlacementMenu();
                else if (id == 8)
                    e->showShapeMenu();
                e->curveDirty_ = true; e->sync(); e->repaint();
            }
        });
    }
    void openGraphContext (int band)
    {
        juce::PopupMenu menu;
        if (band >= 0)
        {
            menu.addItem (1, "Bypass band");
            menu.addItem (2, "Delete band");
            menu.addItem (3, "Dynamic EQ on/off");
        }
        menu.addItem (4, "Add new EQ band");
        juce::Component::SafePointer<ApexQ4Editor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (this), [safe, band] (int id)
        {
            if (auto* e = safe.getComponent(); e != nullptr)
            {
                if (band >= 0 && id == 1)
                    e->toggleParameter (parameterIndex (
                        band, BandParameterOffset::Bypass));
                if (band >= 0 && id == 2)
                {
                    e->setUnits (parameterIndex (band,
                        BandParameterOffset::Enabled), 0.0f);
                    e->select (-1);
                }
                if (band >= 0 && id == 3)
                    e->toggleParameter (dynamicParameterIndex (
                        band, DynamicBandOffset::Enable));
                if (id == 4)
                    e->createBand ({ static_cast<float> (e->getWidth()) * 0.5f,
                                    static_cast<float> (e->graph_.getCentreY()) });
                e->curveDirty_ = true; e->repaint();
            }
        });
    }
    using Snapshot = std::array<float, kNumParameters>;
    Snapshot capture() const
    {
        Snapshot s {};
        for (int i = 0; i < kNumParameters; ++i)
            s[static_cast<std::size_t> (i)] = param (i)->getValue();
        return s;
    }
    void restore (const Snapshot& s)
    {
        for (int i = 0; i < kNumParameters; ++i)
            if (param (i)->getValue() != s[static_cast<std::size_t> (i)])
                param (i)->setValueNotifyingHost (s[static_cast<std::size_t> (i)]);
        curveDirty_ = true; sync(); repaint();
    }
    void commitHistory()
    {
        const auto state = capture();
        if (historyCount_ > 0 && state == history_[static_cast<std::size_t> (historyPosition_)])
            return;
        if (historyPosition_ + 1 < historyCount_)
            historyCount_ = historyPosition_ + 1;
        if (historyCount_ == static_cast<int> (history_.size()))
        {
            std::move (history_.begin() + 1, history_.end(), history_.begin());
            --historyCount_;
            --historyPosition_;
        }
        history_[static_cast<std::size_t> (historyCount_)] = state;
        historyPosition_ = historyCount_++;
    }
    void historyStep (int offset)
    {
        if (historyCount_ == 0) return;
        const int index = juce::jlimit (0, historyCount_ - 1, historyPosition_ + offset);
        historyPosition_ = index;
        restore (history_[static_cast<std::size_t> (index)]);
    }
    void copySnapshot()
    {
        compareCopy_ = capture();
        hasCopy_ = true;
    }
    void switchCompare()
    {
        if (! hasCopy_) { copySnapshot(); return; }
        auto current = capture();
        restore (compareCopy_);
        compareCopy_ = current;
    }
    void openPresetActions()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Copy current EQ to A/B");
        menu.addItem (2, "Swap A/B", hasCopy_);
        menu.addItem (3, "Reset to flat");
        juce::Component::SafePointer<ApexQ4Editor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (&preset_), [safe] (int id)
        {
            if (auto* e = safe.getComponent())
            {
                if (id == 1) e->copySnapshot();
                if (id == 2) e->switchCompare();
                if (id == 3)
                {
                    for (int b = 0; b < kMaxBands; ++b)
                        e->setUnits (parameterIndex (b,
                            BandParameterOffset::Enabled), 0.0f);
                    e->select (-1);
                }
                e->curveDirty_ = true; e->repaint();
            }
        });
    }
    void updateMeasuredDynamics()
    {
        // The audio thread publishes gain reduction in a coherent fixed-size
        // SPSC ResponseFrame. Never read audio-owned doubles directly!
        ResponseFrame frame;
        if (! processor_.getResponseCore().consumeLatest (frame))
            return;
        lastDynamicActiveMask_ = frame.dynamicActiveMask;
        dynamicMeterValid_ = true;
        for (int band = 0; band < kMaxBands; ++band)
        {
            const auto index = static_cast<std::size_t> (band);
            const double value = (lastDynamicActiveMask_ & (1u << band)) != 0
                && std::isfinite (frame.dynamicGainDb[index])
                ? std::clamp (frame.dynamicGainDb[index], -24.0, 24.0)
                : 0.0;
            // Motion smoothing only applies to the visual meter. The
            // audio/DSP values and original ResponseFrame remain untouched.
            const float incoming = static_cast<float> (value);
            dynamicGainDbDisplay_[index] = 0.35f * incoming
                + 0.65f * dynamicGainDbDisplay_[index];
        }
    }
    void timerCallback() override
    {
        updateMeasuredDynamics();
        // Visual freeze must not block the FFT worker or the DSP meter.
        if (! analyzerFrozen_)
        {
          const auto capture = processor_.getAnalyzer().acquireSnapshot();
          if (capture.isValid() && capture.index() != spectrumIndex_)
          {
            if (capture.discontinuous()
                || std::abs (capture.binHz() - spectrumBinHz_) > 0.01f)
                clearHeldPeaks();
            spectrumIndex_ = capture.index();
            spectrumBinHz_ = std::max (0.1f, capture.binHz());
            spectrumShown_[0] = capture.preActive() && capture.preDb() != nullptr;
            spectrumShown_[1] = capture.postActive() && capture.postDb() != nullptr;
            if (spectrumShown_[0])
                std::copy_n (capture.preDb(),
                    Analysis::SpectrumAnalyzerCore::kSpectrumBins,
                    spectrum_[0].begin());
            if (spectrumShown_[1])
                std::copy_n (capture.postDb(),
                    Analysis::SpectrumAnalyzerCore::kSpectrumBins,
                    spectrum_[1].begin());
            spectrumReady_ = spectrumShown_[0] || spectrumShown_[1];
            collectMeasuredPeaks();
          }
        }
        bool changed = false;
        for (int b = 0; b < kMaxBands; ++b)
        {
            const auto s = settingsForBand (b);
            const auto& old = cachedBands_[static_cast<std::size_t> (b)];
            if (s.enabled != old.enabled || s.bypassed != old.bypassed
                || s.frequencyHz != old.frequencyHz || s.gainDb != old.gainDb
                || s.q != old.q || s.shape != old.shape
                || s.slopeDbPerOctave != old.slopeDbPerOctave)
                changed = true;
        }
        if (changed) curveDirty_ = true;
        if (selected_ >= 0 && dragBand_ < 0) sync();
        syncAnalyzerText();
        repaint();
    }

    Processor& processor_;
    EQ4Look look_;
    juce::Rectangle<int> graph_ {}, panel_ {}, dynamicPanel_ {},
                         bootEyeBounds_ {};
    juce::TextButton undo_, redo_, compare_, copy_, preset_, phase_,
        analyzer_, displayRange_, globalBypass_, autoGain_;
    juce::TextButton bandPower_, shape_, slope_, placement_, dynamic_,
        advanced_, closeBand_;
    ApexQ4SoloHold soloHold_;
    juce::Slider freq_, gain_, q_;
    std::array<juce::Slider, 4> dynamicSliders_ {};
    std::array<juce::Label, 4> dynamicNames_ {};
    juce::TextButton dynToggle_, dynSpectral_, dynFilter_, dynClose_;
    bool dynamicExpanded_ = false;
    std::array<float, kMaxBands> dynamicGainDbDisplay_ {};
    std::uint32_t lastDynamicActiveMask_ = 0;
    bool dynamicMeterValid_ = false;
    int rangeDragBand_ = -1;
    float rangeDragOriginY_ = 0.0f;
    float rangeDragStartDb_ = 0.0f;
    int selected_ = -1, dragBand_ = -1, rangeIndex_ = 2;
    bool pendingCurveCreate_ = false, createdDuringGesture_ = false;
    bool pendingDynamic_ = false, pendingSpectral_ = false;
    juce::Point<float> pendingOrigin_ {};
    std::optional<juce::Point<float>> hover_;
    std::array<bool, kMaxBands> selectedMask_ {};
    std::array<bool, kMaxBands> gestureMask_ {};
    std::array<double, kMaxBands> groupStartHz_ {};
    std::array<float, kMaxBands> groupStartGain_ {};
    float rangeDb_ = 12.0f;
    bool syncing_ = false, curveDirty_ = true;
    juce::Point<float> dragOrigin_;
    float dragStartHz_ = 1000.0f, dragStartGain_ = 0.0f;
    juce::Path response_;
    std::array<juce::Path, kMaxBands> bandCurves_ {};
    std::array<BandSettings, kMaxBands> cachedBands_ {};
    std::array<std::array<float, Analysis::SpectrumAnalyzerCore::kSpectrumBins>, 2>
        spectrum_ {};
    std::array<bool, 2> spectrumShown_ {{ false, false }};
    float spectrumBinHz_ = 11.71875f;
    int spectrumIndex_ = -1;
    bool spectrumReady_ = false;
    bool analyzerFrozen_ = false;
    bool peakHoldEnabled_ = false;
    std::array<std::array<float, Analysis::SpectrumAnalyzerCore::kSpectrumBins>, 2>
        heldPeaks_ {};
    std::array<bool, 2> heldPeakReady_ {{ false, false }};
    double spectrumSmoothingOctaves_ = 1.0 / 18.0;
    Analysis::SpectrumTapMode lastAnalyzerMode_ = Analysis::SpectrumTapMode::Closed;
    bool analyzerAutoStarted_ = false;
    std::uint32_t auditionToken_ = 0;
    bool auditionHolding_ = false;
    std::array<Snapshot, 16> history_ {};
    int historyCount_ = 0, historyPosition_ = 0;
    Snapshot compareCopy_ {};
    bool hasCopy_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApexQ4Editor)
};
} // namespace APEX::ParametricEQ
