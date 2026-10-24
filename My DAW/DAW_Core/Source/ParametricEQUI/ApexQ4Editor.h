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

// APEX-native version of the quick dynamic range ring surrounding Gain.
// Drag the OUTER ring to write the existing hosted per-band Range parameter;
// drag the inner knob normally to change static gain. No audio-thread calls.
class ApexQ4DynamicGainDial final : public juce::Slider
{
public:
    std::function<void()> onRangeBegin, onRangeEnd, onRangeClear;
    std::function<void(float)> onRangeDelta;
    bool ringEditable = false, rangeEnabled = false, meterValid = false;
    float rangeDb = 0.0f, measuredDb = 0.0f;

    bool isOnRangeRing (juce::Point<float> at) const noexcept
    {
        if (! ringEditable) return false;
        const float diameter = static_cast<float> (
            std::min (getWidth() - 12, getHeight() - 39));
        const float radius = std::max (12.0f, diameter * 0.5f);
        const float cx = getWidth() * 0.5f;
        const float cy = std::max (17.0f, radius + 2.0f);
        const float d = at.getDistanceFrom ({ cx, cy });
        // The outermost annulus is reserved for dynamics. The inner arc
        // and center preserve all ordinary static-gain edit gestures.
        return d >= radius + 2.0f && d <= radius + 12.5f;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (isOnRangeRing (e.position))
        {
            rangeDragging_ = true;
            originY_ = e.position.y;
            if (onRangeBegin) onRangeBegin();
            return;
        }
        juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (rangeDragging_)
        {
            if (onRangeDelta)
                onRangeDelta ((originY_ - e.position.y) * 0.22f);
            return;
        }
        juce::Slider::mouseDrag (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (rangeDragging_)
        {
            rangeDragging_ = false;
            if (onRangeEnd) onRangeEnd();
            return;
        }
        juce::Slider::mouseUp (e);
    }
    void mouseWheelMove (const juce::MouseEvent& e,
                         const juce::MouseWheelDetails& wheel) override
    {
        if (isOnRangeRing (e.position)
            && std::abs (wheel.deltaY) > 1.0e-6f)
        {
            if (onRangeBegin) onRangeBegin();
            if (onRangeDelta)
                onRangeDelta (wheel.deltaY * (
                    e.mods.isShiftDown() ? 0.25f : 1.5f));
            if (onRangeEnd) onRangeEnd();
            return;
        }
        juce::Slider::mouseWheelMove (e, wheel);
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (isOnRangeRing (e.position))
        {
            if (rangeDragging_)
            {
                rangeDragging_ = false;
                if (onRangeEnd) onRangeEnd();
            }
            if (onRangeClear) onRangeClear();
            return;
        }
        juce::Slider::mouseDoubleClick (e);
    }
    void cancelRangeDrag() noexcept { rangeDragging_ = false; }
    bool isRangeDragging() const noexcept { return rangeDragging_; }

private:
    bool rangeDragging_ = false;
    float originY_ = 0.0f;
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
        // The editor, not an invisible Windows shortcut proxy, receives
        // keyboard focus after graph gestures for desktop + hardware keys.
        setWantsKeyboardFocus (true);
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
        attach (dynClear_, "RESET");
        dynClear_.setTooltip (
            "Clear the selected band's dynamic range, leaving the static"
            " frequency, gain and Q unchanged");
        dynClear_.onClick = [this] { clearSelectedDynamics(); };
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
        dynFilter_.onClick = [this] { showDynamicsTriggerMenu(); };
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
        gain_.onRangeBegin = [this] { beginGainRingGesture(); };
        gain_.onRangeDelta = [this] (float deltaDb) {
            dragGainRingRange (deltaDb);
        };
        gain_.onRangeEnd = [this] { endGainRingGesture(); };
        gain_.onRangeClear = [this] { clearSelectedDynamics(); };
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
        endGainRingGesture();
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
        // PASS 11 screenshot review: at 480x800 and 1280x720 the old
        // placement scoring forced the band controls to the very BOTTOM
        // of the graph, hundreds of pixels from the selected EQ point.
        // Prioritize a compact contextual panel directly below the band,
        // then above if necessary. Keep the old bottom placement only while
        // the expanded mobile Dynamic EQ panel needs its own free region.
        // All placement decisions stay on the message thread.
        const int minX = 9;
        const int maxX = std::max (minX, w - controlWidth - 9);
        const int minY = graph_.getY() + 8;
        // PASS 11 RED at 640x400: the previous 29px bottom reserve left
        // *no* legal position below a low-frequency-neighbouring tooltip.
        // The selected band's readout collided with a bottom-clamped HUD,
        // while the upper placement covered another active EQ node.
        // Compact-height hosts need an 8px canvas-safe bottom reserve;
        // the footer remains entirely outside graph_.
        const int hudBottomInset = h <= 420 ? 8 : 29;
        const int maxY = std::max (minY, graph_.getBottom()
                                          - controlHeight - hudBottomInset);
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
        const int belowNodeY = clampY (
            juce::roundToInt (selectedPoint.y) + 49);
        const int aboveNodeY = clampY (
            juce::roundToInt (selectedPoint.y) - controlHeight - 49);
        const std::array<int, 9> positionsY {{
            belowNodeY, aboveNodeY,
            clampY (belowNodeY + 28), clampY (belowNodeY - 28),
            clampY (aboveNodeY + 28), clampY (aboveNodeY - 28),
            maxY, clampY (maxY - 48), minY
        }};
        const bool reserveDynamicRoom = dynamicExpanded_ && phone && ! narrow;
        float bestPenalty = std::numeric_limits<float>::infinity();
        juce::Rectangle<int> bestPanel { baseX, belowNodeY,
                                          controlWidth, controlHeight };
        for (const int y : positionsY)
        {
            for (const int x : positionsX)
            {
                const juce::Rectangle<int> candidate { x, y,
                    controlWidth, controlHeight };
                // Contextual placement wins over historical bottom-docking.
                // Only when mobile Dynamics is OPEN, retain the lower band
                // strip so the taller dynamics card can occupy upper space.
                const float distanceBelow = static_cast<float> (
                    std::abs (y - belowNodeY));
                const float distanceAbove = static_cast<float> (
                    std::abs (y - aboveNodeY)) + 37.0f;
                float penalty = reserveDynamicRoom
                    ? static_cast<float> (maxY - y) * 2.5f
                    : std::min (distanceBelow, distanceAbove) * 2.2f;
                penalty += std::abs (candidate.getCentreX()
                    - juce::roundToInt (selectedPoint.x)) * 0.12f;
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
                        const auto valueTag = selectedTagBounds (band);
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
        g.setColour (responseCurveColour());
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
                const float hostedRange = param (
                    dynamicParameterIndex (band, DynamicBandOffset::Range))
                        ->getUnitsValue();
                const float maxRange = std::abs (hostedRange);
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
                    // Shared visual sign semantics: red for downward
                    // compression (positive legacy HOST range), magenta
                    // for upward expansion (negative legacy HOST range).
                    // The user's manual display sign is its inverse.
                    g.setColour ((hostedRange > 0.0f
                        ? juce::Colour (0xffec6f80)
                        : juce::Colour (0xffdb76d1)).withAlpha (0.77f));
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
                // Long signed DYN readouts previously squeezed into 214px,
                // obscuring gain/Q values. Use a responsive measured-data tag
                // without cutting off the audio-derived dynamic gain number.
                const auto tag = selectedTagBounds (band);
                g.setColour (juce::Colour (0xff25212c));
                g.fillRoundedRectangle (tag.toFloat(), 5.0f);
                g.setColour (juce::Colours::white);
                g.drawFittedText (label, tag.reduced (4, 1),
                                  juce::Justification::centred, 1);
            }
        }

        // PASS 13 visual critic: this explains the origin of plotted data.
        // PRE/POST are measured audio, unlike the warm gold DSP transfer
        // function. Rendering it after the curves keeps the caption legible.
        drawSpectrumLegend (g);

        // Touchscreen-friendly group selection: explicitly opt in via MORE.
        // The badge communicates that node taps LINK/UNLINK rather than
        // editing frequency/gain. Linked nodes can still be dragged as a group.
        if (touchMultiSelectMode_)
        {
            const auto badge = juce::Rectangle<int> (
                graph_.getX() + 12, graph_.getY() + 29,
                std::min (graph_.getWidth() - 24, 277), 23);
            g.setColour (juce::Colour (0xff272f31).withAlpha (0.96f));
            g.fillRoundedRectangle (badge.toFloat(), 5.0f);
            g.setColour (juce::Colour (0xff83d9b7));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawFittedText ("TOUCH LINK ON  |  TAP NODES / DRAG GROUP",
                badge.reduced (7, 1), juce::Justification::centredLeft, 1);
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

        // In narrow portraits and low-height landscape host windows there
        // isn't space for BOTH editing cards at once. Show the requested
        // dynamics controls alone and restore FREQ/GAIN/Q when closed.
        if (selected_ >= 0 && ! hideBandHudForDynamics())
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
        touchPendingBand_ = -1;
        rangeDragBand_ = -1;
        hover_.reset();
        if (! graph_.contains (e.getPosition())
            || (selected_ >= 0 && panel_.contains (e.getPosition())))
            return;
        dragBand_ = hitBand (e.position);
        // Keep the keyboard command target on the graph. Do not steal it
        // from active text boxes while they are being typed into.
        grabKeyboardFocus();
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
            if (touchMultiSelectMode_)
            {
                // Defer a touch tap until mouseUp, so a swipe can still begin
                // a proper host automation gesture and move the whole group.
                // This also avoids changing the band selection on pointer-down.
                touchPendingBand_ = dragBand_;
                touchPendingOrigin_ = e.position;
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
        // Blank-canvas touch is intentionally inert in LINK mode. Otherwise
        // an accidental touch while grouping inserts a new EQ band.
        if (touchMultiSelectMode_ && ! forceCreate)
            return;
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
        if (touchPendingBand_ >= 0)
        {
            if (e.position.getDistanceFrom (touchPendingOrigin_) < 7.0f)
                return;
            const int band = touchPendingBand_;
            const auto origin = touchPendingOrigin_;
            touchPendingBand_ = -1;
            // An unlinked band joins before dragging; an already linked
            // band remains linked. Do not toggle membership on swipe.
            select (band, true);
            beginNodeGesture (band, origin);
        }
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
        if (touchPendingBand_ >= 0)
        {
            const int band = touchPendingBand_;
            touchPendingBand_ = -1;
            toggleSelectedBand (band);
            // No DSP parameter gesture or undo entry for selection only.
            return;
        }
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
        if (touchMultiSelectMode_) return; // mode is tap-to-link, not add
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
                if (! supportsDynamicShape (param (parameterIndex (
                        b, BandParameterOffset::Shape))->getChoiceIndex()))
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
                    // A newly created dynamic band uses its own bandpass
                    // detector by default, matching the expected Band key.
                    // Existing saved-session detector modes remain intact.
                    setUnits (dynamicFilterParameterIndex (b), 1.0f);
                    enabled->beginChangeGesture();
                    enabled->setValueNotifyingHost (1.0f);
                    enabled->endChangeGesture();
                }
                else if (enabled->getBool()
                         && std::abs (range->getUnitsValue()) <= 0.001f)
                {
                    enabled->beginChangeGesture();
                    enabled->setValueNotifyingHost (0.0f);
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

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto modifiers = key.getModifiers();
        const bool command = modifiers.isCtrlDown()
                          || modifiers.isCommandDown();
        const int code = key.getKeyCode();
        if (command && (code == 'z' || code == 'Z'))
        {
            historyStep (modifiers.isShiftDown() ? 1 : -1);
            return true;
        }
        if (command && (code == 'y' || code == 'Y'))
        {
            historyStep (1);
            return true;
        }
        if (code == juce::KeyPress::escapeKey)
        {
            // ESC must leave no hidden linked group or sticky DYN controls.
            if (touchMultiSelectMode_)
                setTouchMultiSelectMode (false);
            select (-1);
            return true;
        }
        if (selected_ < 0 || command || modifiers.isAltDown())
            return false;
        if (code == juce::KeyPress::deleteKey
            || code == juce::KeyPress::backspaceKey)
        {
            deleteSelectedBands();
            return true;
        }
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
            || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            nudgeSelectedBands (code, modifiers.isShiftDown());
            return true;
        }
        return false;
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
    juce::Rectangle<int> selectedTagBoundsForTesting() const noexcept
    {
        return selectedTagBounds (selected_);
    }
    bool sendKeyboardShortcutForTesting (const juce::KeyPress& key)
    {
        return keyPressed (key);
    }
    juce::Rectangle<int> dynamicPanelBoundsForTesting() const noexcept
    {
        return dynamicPanel_;
    }
    bool dynamicPanelVisibleForTesting() const noexcept
    {
        return dynamicExpanded_ && selected_ >= 0;
    }
    bool primaryHudVisibleForTesting() const noexcept
    {
        return selected_ >= 0 && bandPower_.isVisible();
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
    juce::Colour spectrumTraceColourForTesting (bool post) const noexcept
    {
        return spectrumTraceColour (post ? 1 : 0);
    }
    juce::Colour eqTransferColourForTesting() const noexcept
    {
        return responseCurveColour();
    }
    juce::Colour bandDialColourForTesting (int dial) const
    {
        const auto* slider = dial == 0 ? &freq_ : dial == 1 ? &gain_ : &q_;
        return slider->findColour (juce::Slider::rotarySliderFillColourId);
    }
    juce::Rectangle<int> spectrumLegendBoundsForTesting() const noexcept
    {
        return spectrumLegendBounds();
    }
    bool gainRingEditableForTesting() const noexcept
    {
        return gain_.ringEditable;
    }
    float gainRingRangeForTesting() const noexcept { return gain_.rangeDb; }
    bool gainRingMeterValidForTesting() const noexcept
    {
        return gain_.meterValid;
    }
    float gainRingMeasuredDbForTesting() const noexcept
    {
        return gain_.measuredDb;
    }
    bool gainRingTouchTargetForTesting (juce::Point<float> p) const noexcept
    {
        return gain_.isOnRangeRing (p);
    }
    juce::Rectangle<int> gainKnobBoundsForTesting() const noexcept
    {
        return gain_.getBounds();
    }
    void dragGainRangeForTesting (float deltaDb)
    {
        beginGainRingGesture();
        dragGainRingRange (deltaDb);
        endGainRingGesture();
    }
    void clearSelectedDynamicsForTesting()
    {
        clearSelectedDynamics();
    }
    juce::Rectangle<int> dynamicResetBoundsForTesting() const noexcept
    {
        return dynClear_.isVisible() ? dynClear_.getBounds()
                                     : juce::Rectangle<int>();
    }
    juce::Range<float> measuredSpectrumDomainXForTesting() const noexcept
    {
        return measuredSpectrumDomainX();
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
    void setTouchMultiSelectForTesting (bool on) { setTouchMultiSelectMode (on); }
    bool touchMultiSelectForTesting() const noexcept { return touchMultiSelectMode_; }
    void undoForTesting() { historyStep (-1); }
    void redoForTesting() { historyStep (1); }
    void createBandForTesting (juce::Point<float> pt) { createBand (pt); }
    void resetAllBandsForTesting() { resetToFlat(); }
    void refreshForTesting() { curveDirty_ = true; sync(); repaint(); }

private:
    struct EQ4Look final : juce::LookAndFeel_V4
    {
        EQ4Look()
        {
            setColour (juce::Slider::rotarySliderFillColourId,
                       juce::Colour (0xffefca65));
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
            // PASS 13: matching the selected band colour across the node
            // and its three interactive controls makes the active filter
            // legible without confusing its controls with the GOLD overall
            // response and the cool-colored measured PRE/POST audio.
            g.setColour (slider.findColour (
                juce::Slider::rotarySliderFillColourId));
            g.strokePath (arc, juce::PathStrokeType (3.1f));
            const float angle = start + value * (end - start);
            g.setColour (juce::Colour (0xff292531));
            g.fillEllipse (cx - radius + 5, cy - radius + 5,
                           (radius - 5) * 2, (radius - 5) * 2);
            g.setColour (juce::Colour (0xfff1eaf2));
            g.drawLine (cx, cy, cx + std::sin (angle) * (radius - 8),
                        cy - std::cos (angle) * (radius - 8), 2.0f);

            // Visible on the selected GAIN knob: editable DYN range ring
            // (red attenuation / magenta expansion) + a yellow *measured*
            // gain-change needle. The yellow meter NEVER runs without a
            // valid coherent audio-thread ResponseFrame.
            if (auto* gainDial = dynamic_cast<ApexQ4DynamicGainDial*> (&slider))
            {
                if (gainDial->ringEditable)
                {
                    const float outer = radius + 7.0f;
                    juce::Path rangeTrack, rangeStroke, measuredStroke;
                    rangeTrack.addCentredArc (cx, cy, outer, outer, 0.0f,
                                             start, end, true);
                    g.setColour (juce::Colour (0xff77717f).withAlpha (0.45f));
                    g.strokePath (rangeTrack, juce::PathStrokeType (3.7f));
                    // Visual UI sign follows the manual; preserved host
                    // engine still clips the original ±24 dB parameter.
                    const float rangeRatio = std::clamp (
                        std::abs (gainDial->rangeDb) / 24.0f, 0.0f, 1.0f);
                    if (gainDial->rangeEnabled && rangeRatio > 0.001f)
                    {
                        rangeStroke.addCentredArc (cx, cy, outer, outer, 0.0f,
                            start, start + rangeRatio * (end - start), true);
                        g.setColour (gainDial->rangeDb < 0.0f
                            ? juce::Colour (0xffec6f80)
                            : juce::Colour (0xffdb76d1));
                        g.strokePath (rangeStroke, juce::PathStrokeType (4.0f));
                        if (gainDial->meterValid
                            && std::abs (gainDial->measuredDb) > 0.05f)
                        {
                            const float meteredRatio = std::clamp (
                                std::abs (gainDial->measuredDb) / 24.0f,
                                0.0f, rangeRatio);
                            measuredStroke.addCentredArc (
                                cx, cy, outer - 3.0f, outer - 3.0f, 0.0f,
                                start, start + meteredRatio * (end - start),
                                true);
                            g.setColour (juce::Colour (0xfff4ce55));
                            g.strokePath (measuredStroke,
                                          juce::PathStrokeType (2.5f));
                        }
                    }
                    if (h >= 65 && gainDial->rangeEnabled
                        && std::abs (gainDial->rangeDb) > 0.01f)
                    {
                        g.setColour (juce::Colour (0xffe7d9e2));
                        g.setFont (juce::FontOptions (9.2f,
                                                     juce::Font::bold));
                        g.drawFittedText (
                            "DYN " + juce::String (
                                gainDial->rangeDb >= 0.0f ? "+" : "")
                                + juce::String (gainDial->rangeDb, 1),
                            x + 10, y + h - 30, w - 20, 14,
                            juce::Justification::centred, 1);
                    }
                }
            }
            g.setFont (juce::FontOptions (10.0f));
            g.drawFittedText (slider.getTitle(), x, y + h - 15, w, 14,
                              juce::Justification::centred, 1);
        }
    };

    // Separate audio (cool slate + teal) from the filter response (gold).
    // PASS 12's real FFT spectrum was rendered in nearly the same yellow
    // as the EQ transfer curve, making two independent signals look like
    // one and obscuring the actual shape while monitoring dense material.
    static juce::Colour spectrumTraceColour (int mode) noexcept
    {
        return mode == 0 ? juce::Colour (0xff9d93b2)
                         : juce::Colour (0xff69c5bf);
    }
    static juce::Colour responseCurveColour() noexcept
    {
        return juce::Colour (0xfff4ce55);
    }
    juce::Rectangle<int> spectrumLegendBounds() const noexcept
    {
        if (! spectrumReady_) return {};
        const int count = static_cast<int> (spectrumShown_[0])
                        + static_cast<int> (spectrumShown_[1]);
        if (count == 0) return {};
        return { graph_.getX() + 12, graph_.getY() + 6,
                 14 + count * 61, 21 };
    }
    void drawSpectrumLegend (juce::Graphics& g) const
    {
        const auto area = spectrumLegendBounds();
        if (area.isEmpty()) return;
        g.setColour (juce::Colour (0xff24202a).withAlpha (0.91f));
        g.fillRoundedRectangle (area.toFloat(), 5.0f);
        g.setColour (juce::Colour (0xff5c535f).withAlpha (0.70f));
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 5.0f, 0.7f);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        int x = area.getX() + 8;
        for (int mode = 0; mode < 2; ++mode)
        {
            if (! spectrumShown_[static_cast<std::size_t> (mode)]) continue;
            g.setColour (spectrumTraceColour (mode));
            g.fillEllipse (static_cast<float> (x),
                           static_cast<float> (area.getCentreY() - 3), 7.0f, 7.0f);
            g.setColour (juce::Colour (0xffe5dfe9));
            g.drawText (mode == 0 ? "PRE" : "POST", x + 11,
                        area.getY() + 2, 43, 17,
                        juce::Justification::centredLeft);
            x += 61;
        }
    }
    static bool supportsDynamicShape (int shapeId) noexcept
    {
        return shapeId == static_cast<int> (FilterShape::Bell)
            || shapeId == static_cast<int> (FilterShape::LowShelf)
            || shapeId == static_cast<int> (FilterShape::HighShelf)
            || shapeId == static_cast<int> (FilterShape::FlatTilt);
    }
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
                        static_cast<float> (
                            offset == DynamicBandOffset::Range
                                ? 1.0 - slider.getValue()
                                : slider.getValue()));
                if (! slider.isMouseButtonDown())
                    commitHistory();
                repaint();
            }
        };
        slider.textFromValueFunction = [this, offset] (double uiValue)
        {
            if (selected_ < 0) return juce::String (uiValue, 2);
            // Signed Pro-Q-style range display is the inverse of the
            // legacy APEX host range. Apply the same map to formatted text.
            const float hostValue = static_cast<float> (
                offset == DynamicBandOffset::Range
                    ? 1.0 - uiValue : uiValue);
            return param (dynamicParameterIndex (
                selected_, offset))->getText (hostValue, 0);
        };
        slider.valueFromTextFunction = [this, offset] (
            const juce::String& text)
        {
            if (selected_ < 0) return 0.0;
            const double hostValue = static_cast<double> (
                param (dynamicParameterIndex (
                    selected_, offset))->getValueForText (text));
            return offset == DynamicBandOffset::Range
                ? 1.0 - hostValue : hostValue;
        };
        addAndMakeVisible (slider);
    }
    void beginGainRingGesture()
    {
        if (selected_ < 0 || ! gain_.ringEditable) return;
        endGainRingGesture();
        gainRingGestureBand_ = selected_;
        auto* range = param (dynamicParameterIndex (
            gainRingGestureBand_, DynamicBandOffset::Range));
        gainRingInitialDb_ = range->getUnitsValue();
        range->beginChangeGesture();
    }
    void dragGainRingRange (float deltaDb)
    {
        if (gainRingGestureBand_ < 0) return;
        auto* range = param (dynamicParameterIndex (
            gainRingGestureBand_, DynamicBandOffset::Range));
        // The original APEX hosted ABI uses POSITIVE range for downward
        // compression. The Pro-Q-style USER ring is negative for downward
        // compression, positive for upward expansion. Reverse only this UI
        // gesture: preserve every stored VST3 automation value/session.
        const float target = std::clamp (gainRingInitialDb_ - deltaDb,
                                         -24.0f, 24.0f);
        range->setValueNotifyingHost (range->toNormalised (target));
        auto* enabled = param (dynamicParameterIndex (
            gainRingGestureBand_, DynamicBandOffset::Enable));
        if (std::abs (range->getUnitsValue()) > 0.001f)
        {
            if (! enabled->getBool())
            {
                // Only newly enabled dynamics receives the band-limited key
                // default; opening an old session never rewrites its state.
                setUnits (dynamicFilterParameterIndex (
                    gainRingGestureBand_), 1.0f);
                enabled->beginChangeGesture();
                enabled->setValueNotifyingHost (1.0f);
                enabled->endChangeGesture();
            }
        }
        else if (enabled->getBool())
        {
            // Manual zero-ring semantics: range 0 restores a static band,
            // not an armed but inaudible dynamic processor.
            enabled->beginChangeGesture();
            enabled->setValueNotifyingHost (0.0f);
            enabled->endChangeGesture();
        }
        sync();
        repaint();
    }
    void endGainRingGesture()
    {
        if (gainRingGestureBand_ < 0) return;
        param (dynamicParameterIndex (
            gainRingGestureBand_, DynamicBandOffset::Range))
            ->endChangeGesture();
        gainRingGestureBand_ = -1;
        commitHistory();
        sync();
        repaint();
    }
    void setDynamicControlsVisible (bool visible)
    {
        const std::array<juce::Component*, 5> buttons {{
            &dynToggle_, &dynSpectral_, &dynFilter_, &dynClose_,
            &dynClear_
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
        // Must run on both OPEN and CLOSE. Otherwise the mobile FREQ/GAIN/Q
        // controls remained hidden after closing the Dynamics overlay.
        const bool compactDynamics = hideBandHudForDynamics();
        setSelectedVisible (selected_ >= 0 && ! compactDynamics);
        if (! visible) return;
        const int w = getWidth();
        // Ultra-wide/short host windows (e.g. 900x300) leave no vertical
        // escape from a band's value tag. Prefer a narrower, still touch-
        // accessible 260px card so it can park to the tag's right or left.
        // Four dynamic sliders still retain >=160px edit targets.
        const bool shortLandscape = w >= 720 && graph_.getHeight() < 260;
        const int panelWidth = std::min (
            w - 18, w < 460 ? 302 : (shortLandscape ? 260 : 346));
        const int panelHeight = std::min (
            272, std::max (180, graph_.getHeight() - 16));
        const int minX = 9;
        const int maxX = std::max (minX, w - panelWidth - 9);
        const int minY = graph_.getY() + 8;
        const int maxY = std::max (minY, graph_.getBottom()
                                          - panelHeight - 10);
        // PASS 10 screenshot critic: the old four-corner solver put the
        // DYNAMIC EQ card ON TOP of the frequency/gain/Q HUD at 480x800.
        // Corners alone cannot find the available middle-height region.
        // Search fixed, allocation-free candidates along both axes and
        // consider *actual intersection area*, not a mere corner penalty.
        //
        // In portrait below 460px both cards cannot fit touch-safely at once.
        // The explicitly opened dynamics editor temporarily replaces the
        // selected-band HUD; closing it restores the ordinary EQ knobs.
        const auto constrainX = [=] (int x) {
            return juce::jlimit (minX, maxX, x);
        };
        const auto constrainY = [=] (int y) {
            return juce::jlimit (minY, maxY, y);
        };
        const int middleX = constrainX ((minX + maxX) / 2);
        const int middleY = constrainY ((minY + maxY) / 2);
        const auto node = nodePoint (selected_);
        // A single contextual stack is easier to scan than two detached
        // overlays. On desktop prefer attaching the dynamics extension
        // directly BELOW (or above) the selected band's GAIN/Q strip,
        // when actual nodes and value tags leave enough free room.
        // The existing collision critic always overrules this preference.
        const bool preferAttached = w >= 900
            && graph_.getHeight() >= 460 && ! compactDynamics;
        const int attachedLeft = constrainX (panel_.getX());
        const int attachedRight = constrainX (
            panel_.getRight() - panelWidth);
        const int attachBelow = constrainY (panel_.getBottom() + 8);
        const int attachAbove = constrainY (
            panel_.getY() - panelHeight - 8);
        const std::array<int, 5> candidateX {{
            minX, maxX, middleX, attachedLeft, attachedRight
        }};
        const std::array<int, 9> candidateY {{
            minY, maxY, middleY,
            constrainY (minY + (maxY - minY) / 4),
            constrainY (minY + (maxY - minY) * 3 / 4),
            attachAbove, attachBelow,
            constrainY (node.y - panelHeight - 32),
            constrainY (node.y + 32)
        }};
        auto best = juce::Rectangle<int> (
            minX, minY, panelWidth, panelHeight);
        auto lowestPenalty = std::numeric_limits<double>::infinity();
        for (const int x : candidateX)
        {
            for (const int y : candidateY)
            {
                const juce::Rectangle<int> candidate {
                    x, y, panelWidth, panelHeight
                };
                const auto collision = [] (juce::Rectangle<int> a,
                                           juce::Rectangle<int> b) {
                    const auto common = a.getIntersection (b);
                    return common.isEmpty() ? 0
                        : common.getWidth() * common.getHeight();
                };
                // Protect EVERY control, including SOLO and numeric boxes.
                // The primary HUD does not paint or receive input in compact
                // dynamics mode, so it no longer counts as a blocker there.
                double cost = compactDynamics ? 0.0
                    : static_cast<double> (collision (
                          candidate.expanded (3), panel_.expanded (3)))
                        * 4000.0;
                // The selected value tag remains visible above its node.
                // Dynamic controls must never hide actual hosted numbers.
                cost += static_cast<double> (collision (
                    candidate.expanded (3), selectedTagBounds (selected_)))
                    * 1200.0;
                for (int band = 0; band < kMaxBands; ++band)
                {
                    if (! param (parameterIndex (band,
                            BandParameterOffset::Enabled))->getBool())
                        continue;
                    const auto point = nodePoint (band);
                    if (candidate.expanded (25).contains (point))
                        cost += band == selected_ ? 3000000.0 : 650000.0;
                }
                if (preferAttached)
                {
                    // Source-manual workflow: dynamic parameters expand
                    // from the same floating band strip, not a remote card.
                    // A small 8px gutter passes the expanded(3px) critic.
                    const double leftDistance =
                        std::abs (x - attachedLeft);
                    const double rightDistance =
                        std::abs (x - attachedRight);
                    const double belowDistance =
                        std::abs (y - attachBelow);
                    const double aboveDistance =
                        std::abs (y - attachAbove) + 35.0;
                    cost += 12.0 * (
                        std::min (leftDistance, rightDistance)
                        + std::min (belowDistance, aboveDistance));
                }
                // On phones and cramped landscapes keep the original
                // non-occluding corner preference.
                cost += (maxY - y) * 0.12 + (x - minX) * 0.05;
                if (cost < lowestPenalty)
                {
                    lowestPenalty = cost;
                    best = candidate;
                }
            }
        }
        dynamicPanel_ = best;
        const int px = best.getX(), py = best.getY(), pw = best.getWidth();
        dynClose_.setBounds (px + pw - 59, py + 5, 51, 31);
        // Compact variant exposes reset inside KEY/MORE menu instead of
        // squeezing a sub-44px button over the live threshold readout.
        dynClear_.setBounds (px + 128, py + 5, 69, 31);
        dynClear_.setVisible (pw >= 330);
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
                              &dynSpectral_, &dynFilter_, &dynClear_ })
            if (widget->isVisible()) widget->toFront (false);
        for (auto& slider : dynamicSliders_)
            slider.toFront (false);
    }
    bool hideBandHudForDynamics() const noexcept
    {
        // The selected strip is 450px wide while the dynamics card is at
        // least 260px wide. At <=300px host height, keeping BOTH overlays
        // visible cannot be reliably collision-free across band positions.
        return dynamicExpanded_ && selected_ >= 0
            && (getWidth() < 460
                || (getWidth() >= 720 && graph_.getHeight() < 260));
    }
    void setSelectedVisible (bool visible)
    {
        // CRITIC FIX: visibility is breakpoint-specific. Calling the old
        // blanket "visible=true" inside layoutDynamicControls() incorrectly
        // re-enabled hidden mobile controls using stale desktop coordinates.
        // That put five hit targets OUTSIDE the real contextual panel.
        const std::array<juce::Component*, 11> widgets {{
            &bandPower_, &shape_, &slope_, &placement_, &dynamic_,
            &advanced_, &closeBand_, &soloHold_, &freq_, &gain_, &q_
        }};
        for (auto* widget : widgets)
            widget->setVisible (visible);
        if (visible && getWidth() < 670)
        {
            placement_.setVisible (false);
            dynamic_.setVisible (false);
            if (getWidth() < 460)
                slope_.setVisible (false);
        }
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
        // Selection changes can occur while touching the ring on mobile.
        // Balance the old hosted gesture before switching its parameter ID.
        if (index != selected_ && gainRingGestureBand_ >= 0)
        {
            gain_.cancelRangeDrag();
            endGainRingGesture();
        }
        selected_ = index;
        if (index >= 0)
            selectedMask_[static_cast<std::size_t> (index)] = true;
        // Keep the three dial rings coherent with the currently selected
        // band node. This is GUI-only: never changes hosted parameter values
        // or the frequency response. Sync runs after the recoloring.
        const auto dialInk = selected_ >= 0
            ? bandColour (selected_) : juce::Colour (0xffefca65);
        // gain_ derives from juce::Slider, unlike freq_ and q_. An
        // explicit common-base array avoids MSVC C3535 in initializer lists.
        for (juce::Slider* dial : std::array<juce::Slider*, 3> {{
                 &freq_, static_cast<juce::Slider*> (&gain_), &q_ }})
            dial->setColour (juce::Slider::rotarySliderFillColourId, dialInk);
        setSelectedVisible (selected_ >= 0);
        if (selected_ < 0)
            dynamicExpanded_ = false;
        setDynamicControlsVisible (dynamicExpanded_);
        sync(); resized(); repaint();
    }
    void resetToFlat()
    {
        // Preset reset previously skipped the editor's undo stack.
        // Treat it as ONE host-automated command across all active bands.
        if (auditionHolding_)
        {
            processor_.endAudition (auditionToken_);
            auditionHolding_ = false;
        }
        for (int band = 0; band < kMaxBands; ++band)
        {
            auto* enabled = param (parameterIndex (
                band, BandParameterOffset::Enabled));
            if (! enabled->getBool()) continue;
            enabled->beginChangeGesture();
            enabled->setValueNotifyingHost (0.0f);
            enabled->endChangeGesture();
        }
        select (-1);
        commitHistory();
        curveDirty_ = true;
    }
    void deleteSelectedBands()
    {
        if (selected_ < 0) return;
        if (auditionHolding_)
        {
            processor_.endAudition (auditionToken_);
            auditionHolding_ = false;
        }
        // One deletion command can disable MANY hosted EQ nodes, yet it
        // creates exactly one history snapshot and balanced host gestures.
        for (int band = 0; band < kMaxBands; ++band)
        {
            if (! selectedMask_[static_cast<std::size_t> (band)])
                continue;
            auto* enabled = param (parameterIndex (
                band, BandParameterOffset::Enabled));
            if (! enabled->getBool()) continue;
            enabled->beginChangeGesture();
            enabled->setValueNotifyingHost (0.0f);
            enabled->endChangeGesture();
        }
        select (-1);
        commitHistory();
        curveDirty_ = true;
    }
    void nudgeSelectedBands (int direction, bool fine)
    {
        const bool frequency = direction == juce::KeyPress::leftKey
                            || direction == juce::KeyPress::rightKey;
        const bool positive = direction == juce::KeyPress::rightKey
                           || direction == juce::KeyPress::upKey;
        // Horizontal keys move a SEMITONE on the logarithmic musical axis,
        // SHIFT fine tune = 0.1 semitone. Vertical keys use 1 / 0.1 dB.
        const double ratio = std::exp2 (
            (positive ? 1.0 : -1.0) * (fine ? 0.1 : 1.0) / 12.0);
        const float gainStep = (positive ? 1.0f : -1.0f)
                             * (fine ? 0.1f : 1.0f);
        for (int band = 0; band < kMaxBands; ++band)
        {
            if (! selectedMask_[static_cast<std::size_t> (band)])
                continue;
            if (! param (parameterIndex (
                band, BandParameterOffset::Enabled))->getBool())
                continue;
            auto* parameter = param (parameterIndex (band,
                frequency ? BandParameterOffset::Frequency
                          : BandParameterOffset::Gain));
            const auto current = parameter->getUnitsValue();
            const auto target = frequency
                ? static_cast<float> (std::clamp (
                    current * ratio, 20.0, 30000.0))
                : current + gainStep;
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->toNormalised (target));
            parameter->endChangeGesture();
        }
        commitHistory();
        curveDirty_ = true;
        sync();
        resized();
        repaint();
    }
    void setTouchMultiSelectMode (bool enabled)
    {
        touchPendingBand_ = -1;
        touchMultiSelectMode_ = enabled;
        // Exiting explicit link mode always leaves a single primary band.
        // Otherwise a future ordinary node drag may unexpectedly move all.
        if (! enabled && selectedCount() > 1)
            select (selected_);
        repaint();
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
            const auto shapeId = param (parameterIndex (
                selected_, BandParameterOffset::Shape))->getChoiceIndex();
            // Pro-Q-style dynamic ring is only meaningful for gain-bearing
            // Bell / shelving / Flat Tilt bands. No nonfunctional rings
            // on cut filters, notch or all-pass.
            gain_.ringEditable = supportsDynamicShape (shapeId);
            gain_.rangeEnabled = isDynamic;
            gain_.rangeDb = -param (dynamicParameterIndex (
                selected_, DynamicBandOffset::Range))->getUnitsValue();
            gain_.meterValid = dynamicMeterValid_
                && (lastDynamicActiveMask_ & (1u << selected_)) != 0;
            gain_.measuredDb = dynamicGainDbDisplay_[
                static_cast<std::size_t> (selected_)];
            dynamic_.setButtonText (isDynamic ? "DYN EDIT" : "DYN");
            dynToggle_.setButtonText (isDynamic ? "DYN ON" : "DYN OFF");
            dynSpectral_.setButtonText (
                param (spectralBandParameterIndex (selected_))->getBool()
                    ? "SPECTRAL ON" : "SPECTRAL");
            const bool bandKey = param (
                dynamicFilterParameterIndex (selected_))->getBool();
            dynFilter_.setButtonText (bandKey ? "KEY: BAND  v"
                                               : "KEY: FULL  v");
            dynFilter_.setTooltip (
                "Select internal band-limited or full-band trigger,"
                " and internal/external source (source is GLOBAL)");
            constexpr DynamicBandOffset offsets[] = {
                DynamicBandOffset::Threshold, DynamicBandOffset::Range,
                DynamicBandOffset::Attack, DynamicBandOffset::Release
            };
            for (int i = 0; i < 4; ++i)
            {
                const auto host = param (dynamicParameterIndex (
                    selected_, offsets[i]))->getValue();
                // UI representation of signed range reverses the old ABI
                // direction; the underlying session/automation is unchanged.
                dynamicSliders_[static_cast<std::size_t> (i)].setValue (
                    offsets[i] == DynamicBandOffset::Range ? 1.0f - host : host,
                    juce::dontSendNotification);
            }
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
            setUnits (dynamicFilterParameterIndex (slot), 1.0f);
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
    juce::Rectangle<int> selectedTagBounds (int band) const noexcept
    {
        if (band < 0 || band >= kMaxBands) return {};
        const auto pt = nodePoint (band);
        const bool hasDynamics = param (dynamicParameterIndex (
            band, DynamicBandOffset::Enable))->getBool();
        const int width = std::min (
            hasDynamics ? 308 : 224, std::max (20, graph_.getWidth() - 10));
        return juce::Rectangle<int> (
            pt.x - width / 2, pt.y - 37, width, 22)
                .constrainedWithin (graph_.reduced (5));
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
            // Range rings visibly extend to ~22.4 px radius. Their 24 px
            // interaction envelope must not be clipped by a 22 px node hit
            // test, or the outermost ring taps become impossible.
            const bool dynamic = param (dynamicParameterIndex (
                b, DynamicBandOffset::Enable))->getBool();
            const float radius = dynamic ? 24.0f : 22.0f;
            if (pt.getDistanceFrom (nodePoint (b).toFloat()) <= radius)
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

    juce::Range<float> measuredSpectrumDomainX() const noexcept
    {
        // The EQ graph always spans 20 Hz–30 kHz, but at 48 kHz a REAL
        // FFT contains audio only to Nyquist (~24 kHz). Never extend its
        // filled shape to the unmeasured 30 kHz side of the canvas.
        const double highestMeasuredHz = std::clamp (
            static_cast<double> (spectrumBinHz_)
                * (Analysis::SpectrumAnalyzerCore::kSpectrumBins - 1),
            20.0, 30000.0);
        return { hzToX (20.0), hzToX (highestMeasuredHz) };
    }
    void drawSpectrum (juce::Graphics& g)
    {
        if (! spectrumReady_) return;
        const auto measuredDomain = measuredSpectrumDomainX();
        const float firstMeasuredX = measuredDomain.getStart();
        const float lastMeasuredX = measuredDomain.getEnd();
        if (lastMeasuredX <= firstMeasuredX) return;
        const int count = Analysis::SpectrumAnalyzerCore::kSpectrumBins;
        for (int mode = 0; mode < 2; ++mode)
        {
            if (! spectrumShown_[mode]) continue;
            juce::Path spectrum;
            bool begun = false;
            const float bottom = static_cast<float> (graph_.getBottom() - 25);
            const int points = std::clamp (getWidth() / 2, 160, 950);
            float firstDrawnX = firstMeasuredX, lastDrawnX = firstMeasuredX;
            for (int sample = 0; sample <= points; ++sample)
            {
                // Integrate PRE and POST only over the ACTUALLY measured
                // frequency domain (and end at precisely the final FFT bin).
                // The previous implementation sampled the full 30 kHz EQ
                // canvas, then closed the spectrum fill at 30 kHz, creating
                // a large fabricated triangular area above Nyquist.
                const float x = firstMeasuredX
                    + (lastMeasuredX - firstMeasuredX)
                       * static_cast<float> (sample)
                       / static_cast<float> (points);
                const double hz = xToHz (x);
                if (hz < 20.0) continue;
                // Log-frequency power integration of ACTUAL FFT bins.
                // Raw PRE/POST values remain untouched and testable.
                const float db = displayedSpectrumDb (mode, hz);
                if (! std::isfinite (db)) continue;
                const float y = bottom
                    - (std::clamp (db, -108.0f, -8.0f) + 108.0f)
                      * (graph_.getHeight() - 49) / 100.0f;
                if (! begun)
                {
                    spectrum.startNewSubPath (x, y);
                    firstDrawnX = x;
                    begun = true;
                }
                else spectrum.lineTo (x, y);
                lastDrawnX = x;
            }
            if (! begun) continue;
            // PASS 13: tinted cool FFT traces distinguish REAL measured
            // audio from the warmer gold EQ transfer curve at a glance.
            // Muted lilac PRE and teal POST remain distinguishable even
            // under dense harmonic audio without claiming synthetic data.
            juce::Path fill (spectrum);
            fill.lineTo (lastDrawnX, bottom);
            fill.lineTo (firstDrawnX, bottom);
            fill.closeSubPath();
            const auto ink = spectrumTraceColour (mode);
            g.setColour (ink.withAlpha (mode == 0 ? 0.045f : 0.085f));
            g.fillPath (fill);
            g.setColour (ink.withAlpha (mode == 0 ? 0.52f : 0.79f));
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
                    const float x = firstMeasuredX
                        + (lastMeasuredX - firstMeasuredX)
                           * static_cast<float> (sample)
                           / static_cast<float> (points);
                    const double hz = xToHz (x);
                    if (hz < 20.0) continue;
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
                    g.setColour (ink.withAlpha (mode == 0 ? 0.33f : 0.60f));
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
                e->commitHistory();
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
                e->commitHistory();
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
                e->commitHistory();
                e->curveDirty_ = true; e->sync(); e->repaint();
            }
        });
    }
    void clearSelectedDynamics()
    {
        if (selected_ < 0) return;
        // The official selected-band workflow clears ALL linked bands in
        // one undo step, while touching no static EQ gain/frequency/Q.
        // A zero-range band remains a valid static EQ node.
        for (int band = 0; band < kMaxBands; ++band)
        {
            if (! selectedMask_[static_cast<std::size_t> (band)]
                || ! param (parameterIndex (
                    band, BandParameterOffset::Enabled))->getBool())
                continue;
            auto* range = param (dynamicParameterIndex (
                band, DynamicBandOffset::Range));
            auto* enabled = param (dynamicParameterIndex (
                band, DynamicBandOffset::Enable));
            range->beginChangeGesture();
            range->setValueNotifyingHost (range->toNormalised (0.0f));
            range->endChangeGesture();
            enabled->beginChangeGesture();
            enabled->setValueNotifyingHost (0.0f);
            enabled->endChangeGesture();
        }
        commitHistory();
        sync();
        repaint();
    }
    void showDynamicsTriggerMenu()
    {
        if (selected_ < 0) return;
        // These options reflect the actual APEX DSP plumbing. The external
        // sidechain is a SHARED hosted parameter, not a per-band source;
        // the detector filter is either linked to this band or wideband.
        // Do not imply that a non-existent 'Free' pair of editable
        // frequency crossover knobs is already implemented.
        const bool filter = param (
            dynamicFilterParameterIndex (selected_))->getBool();
        const bool ext = param (kDynamicSidechainParameter)->getBool();
        juce::PopupMenu menu;
        menu.addSectionHeader ("DYNAMIC TRIGGER FREQUENCY");
        menu.addItem (1, "Band-limited (follow this EQ band)", true, filter);
        menu.addItem (2, "Full-band (wideband key)", true, ! filter);
        menu.addSeparator();
        menu.addSectionHeader ("DETECTOR SOURCE — SHARED ACROSS BANDS");
        menu.addItem (3, "Internal program signal", true, ! ext);
        menu.addItem (4, "External sidechain bus", true, ext);
        menu.addSeparator();
        menu.addItem (5, "Clear selected band's dynamics");
        juce::Component::SafePointer<ApexQ4Editor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (&dynFilter_), [safe] (int id)
        {
            if (auto* e = safe.getComponent(); e && e->selected_ >= 0)
            {
                if (id == 1 || id == 2)
                {
                    e->setUnits (dynamicFilterParameterIndex (
                        e->selected_), id == 1 ? 1.0f : 0.0f);
                    e->commitHistory();
                }
                if (id == 3 || id == 4)
                {
                    e->setUnits (kDynamicSidechainParameter,
                                 id == 4 ? 1.0f : 0.0f);
                    e->commitHistory();
                }
                if (id == 5) e->clearSelectedDynamics();
                e->sync(); e->repaint();
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
        menu.addItem (12, "Dynamic trigger / sidechain...");
        menu.addItem (13, "Clear selected band's dynamics");
        menu.addSectionHeader ("KEYBOARD — WHEN CANVAS HAS FOCUS");
        menu.addItem (31, "Arrow keys: freq (semi) / gain (dB)", false);
        menu.addItem (32, "Shift+arrows: fine adjustments", false);
        menu.addItem (33, "Delete: remove selected / linked bands", false);
        menu.addItem (34, "Ctrl+Z / Ctrl+Y: undo / redo", false);
        menu.addSeparator();
        menu.addItem (10, "Touch multi-select (tap to link bands)", true,
                      touchMultiSelectMode_);
        menu.addItem (11, "Unlink other bands", selectedCount() > 1);
        menu.addSeparator();
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
                    e->commitHistory();
                }
                else if (id == 5)
                {
                    e->setUnits (parameterIndex (band,
                        BandParameterOffset::Enabled), 0.0f);
                    e->select (-1);
                    e->commitHistory();
                }
                else if (id == 9)
                    e->setDynamicExpanded (true);
                else if (id == 12)
                    e->showDynamicsTriggerMenu();
                else if (id == 13)
                    e->clearSelectedDynamics();
                else if (id == 10)
                    e->setTouchMultiSelectMode (! e->touchMultiSelectMode_);
                else if (id == 11)
                    e->select (band);
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
                    e->commitHistory();
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
                    e->resetToFlat();
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
        // Refresh measured response without polling audio-owned state
        // from the GUI paint path. The visuals are never synthetic.
        if (selected_ >= 0)
        {
            gain_.meterValid = dynamicMeterValid_
                && (lastDynamicActiveMask_ & (1u << selected_)) != 0;
            gain_.measuredDb = dynamicGainDbDisplay_[
                static_cast<std::size_t> (selected_)];
            gain_.repaint();
        }
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
    juce::Slider freq_, q_;
    ApexQ4DynamicGainDial gain_;
    std::array<juce::Slider, 4> dynamicSliders_ {};
    std::array<juce::Label, 4> dynamicNames_ {};
    juce::TextButton dynToggle_, dynSpectral_, dynFilter_, dynClose_,
                     dynClear_;
    bool dynamicExpanded_ = false;
    std::array<float, kMaxBands> dynamicGainDbDisplay_ {};
    std::uint32_t lastDynamicActiveMask_ = 0;
    bool dynamicMeterValid_ = false;
    int rangeDragBand_ = -1;
    int gainRingGestureBand_ = -1;
    float gainRingInitialDb_ = 0.0f;
    // Explicit touch link mode; tap toggles membership, swipe starts a host
    // gesture. No allocations/locks or new audio-thread parameters.
    bool touchMultiSelectMode_ = false;
    int touchPendingBand_ = -1;
    juce::Point<float> touchPendingOrigin_ {};
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
