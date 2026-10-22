#pragma once
// APEX EQ4 — clean-room, standalone native editor.
// UI is reconstructed from interaction principles in the Pro-Q 4 user manual.
// It does NOT import or subclass the retired Prism GUI, its theme, inspectors,
// toolbars or preset widgets. All DSP, IDs and serialized state stay APEX-owned.
#include "../ParametricEQCore/ParametricEQProcessor.h"
#include <array>
#include <cmath>
#include <optional>

namespace APEX::ParametricEQ
{
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
        attach (undo_, "↶"); attach (redo_, "↷");
        attach (compare_, "A/B"); attach (copy_, "COPY");
        attach (preset_, "Default Setting");
        attach (phase_, "Zero Latency");
        attach (analyzer_, "ANALYZER: PRE + POST");
        attach (displayRange_, "12 dB");
        attach (globalBypass_, "POWER");
        attach (autoGain_, "AUTO GAIN");
        attach (bandPower_, "●");
        attach (shape_, "Bell"); attach (slope_, "12 dB/oct");
        attach (placement_, "STEREO");
        attach (dynamic_, "DYN");
        attach (advanced_, "•••");
        attach (closeBand_, "×");

        undo_.onClick = [this] { historyStep (-1); };
        redo_.onClick = [this] { historyStep (1); };
        compare_.onClick = [this] { switchCompare(); };
        copy_.onClick = [this] { copySnapshot(); };
        preset_.onClick = [this] { openPresetActions(); };
        phase_.onClick = [this] { toggleParameter (kPhaseModeParameter); sync(); };
        analyzer_.onClick = [this] { cycleAnalyzer(); };
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
        dynamic_.onClick = [this]
        {
            if (selected_ >= 0)
                toggleParameter (dynamicParameterIndex (
                    selected_, DynamicBandOffset::Enable));
            sync();
        };
        advanced_.onClick = [this] { showAdvancedMenu(); };
        closeBand_.onClick = [this] { select (-1); };

        setupDial (freq_, BandParameterOffset::Frequency, "FREQ");
        setupDial (gain_, BandParameterOffset::Gain, "GAIN");
        setupDial (q_, BandParameterOffset::Q, "Q");
        setSelectedVisible (false);

        lastAnalyzerMode_ = processor_.getAnalyzerMode();
        if (lastAnalyzerMode_ == Analysis::SpectrumTapMode::Closed)
        {
            processor_.setAnalyzerMode (Analysis::SpectrumTapMode::Both);
            analyzerAutoStarted_ = true;
        }
        setSize (1000, 640);
        startTimerHz (25);
    }

    ~ApexQ4Editor() override
    {
        stopTimer();
        if (dragBand_ >= 0)
            finishGestures();
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
        const int foot = h < 390 ? 40 : 44;
        graph_ = { 0, 42, w, std::max (140, h - 42 - foot) };
        const int b = 32;
        undo_.setBounds (w > 620 ? 222 : 137, 5, b, b);
        redo_.setBounds (w > 620 ? 256 : 169, 5, b, b);
        compare_.setBounds (w > 620 ? 301 : 209, 5, 42, b);
        copy_.setBounds (w > 620 ? 350 : 254, 5, 50, b);
        preset_.setBounds (w > 620 ? std::max (405, w / 2 - 110) : w - 155,
                           5, w > 620 ? 220 : 135, b);
        phase_.setBounds (6, h - foot + 5, phone ? 118 : 152, 33);
        analyzer_.setBounds (phone ? 124 : 160, h - foot + 5,
                             phone ? 158 : 216, 33);
        displayRange_.setBounds (phone ? 284 : 380, h - foot + 5,
                                 phone ? 63 : 70, 33);
        autoGain_.setBounds (phone ? 352 : std::max (465, w - 255),
                             h - foot + 5, phone ? 75 : 104, 33);
        globalBypass_.setBounds (w - (phone ? 51 : 129), h - foot + 5,
                                 phone ? 49 : 117, 33);

        // Context controls are genuinely floating OVER the frequency display,
        // directly beneath the selected EQ nodes: NO permanent bottom sheet.
        // The canvas does not shrink when switching bands.
        const int controlWidth = std::min (phone ? 456 : 548, w - 18);
        const int controlHeight = h < 400 ? 112 : 136;
        const int baseX = selected_ < 0
            ? (w - controlWidth) / 2
            : juce::jlimit (9, std::max (9, w - controlWidth - 9),
                nodePoint (selected_).x - controlWidth / 2);
        const int baseY = graph_.getBottom() - controlHeight - (h < 400 ? 3 : 19);
        panel_ = { baseX, std::max (graph_.getY() + 6, baseY),
                   controlWidth, controlHeight };
        const int px = panel_.getX(), py = panel_.getY();
        bandPower_.setBounds (px + 8, py + 9, 31, 30);
        shape_.setBounds (px + 44, py + 9,
                          phone ? 83 : 98, 30);
        slope_.setBounds (px + (phone ? 131 : 147), py + 9,
                          phone ? 91 : 110, 30);
        placement_.setBounds (px + controlWidth - (phone ? 146 : 160),
                              py + 9, phone ? 68 : 80, 30);
        advanced_.setBounds (px + controlWidth - 79, py + 9, 33, 30);
        closeBand_.setBounds (px + controlWidth - 41, py + 9, 32, 30);
        const int sliderY = py + 43;
        const int sliderSpace = controlWidth - 26;
        const int cw = sliderSpace / 3;
        freq_.setBounds (px + 13, sliderY, cw, controlHeight - 49);
        gain_.setBounds (px + 13 + cw, sliderY, cw, controlHeight - 49);
        q_.setBounds (px + 13 + 2 * cw, sliderY,
                      sliderSpace - 2 * cw, controlHeight - 49);
        dynamic_.setBounds (px + controlWidth / 2 - 32,
                            py + 8, 64, 30);
        dynamic_.toFront (false);
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
            if (band == selected_)
            {
                g.setColour (juce::Colours::white.withAlpha (0.86f));
                g.drawEllipse (pt.x - 10.0f, pt.y - 10.0f, 20.0f, 20.0f, 1.4f);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                const auto label = juce::String (s.frequencyHz, 0) + " Hz   "
                    + juce::String (s.gainDb, 2) + " dB   Q "
                    + juce::String (s.q, 2);
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
        g.drawFittedText ("EQ 4", 94, 9, 56, 25,
                          juce::Justification::centredLeft, 1);

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

        const int footerY = getHeight() - (getHeight() < 390 ? 40 : 44);
        g.setColour (juce::Colour (0xff211d26));
        g.fillRect (0, footerY, getWidth(), getHeight() - footerY);
        g.setColour (juce::Colour (0xff4d424d));
        g.drawHorizontalLine (footerY, 0.0f,
                              static_cast<float> (getWidth()));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! graph_.contains (e.getPosition()) || panel_.contains (e.getPosition()))
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
            if (e.mods.isAltDown())
            {
                toggleParameter (parameterIndex (dragBand_, BandParameterOffset::Bypass));
                select (dragBand_);
                dragBand_ = -1;
                return;
            }
            select (dragBand_);
            auto* hz = param (parameterIndex (dragBand_,
                                              BandParameterOffset::Frequency));
            auto* gain = param (parameterIndex (dragBand_,
                                                BandParameterOffset::Gain));
            hz->beginChangeGesture(); gain->beginChangeGesture();
            dragOrigin_ = e.position;
            dragStartHz_ = hz->getUnitsValue();
            dragStartGain_ = gain->getUnitsValue();
            return;
        }
        if (selected_ >= 0)
        {
            select (-1);
            return;
        }
        createBand (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBand_ < 0) return;
        auto pt = e.position;
        if (e.mods.isShiftDown())
        {
            pt.x = dragOrigin_.x + (pt.x - dragOrigin_.x) * 0.15f;
            pt.y = dragOrigin_.y + (pt.y - dragOrigin_.y) * 0.15f;
        }
        if (e.mods.isAltDown())
        {
            const float dx = std::abs (pt.x - dragOrigin_.x);
            const float dy = std::abs (pt.y - dragOrigin_.y);
            if (dx > dy) pt.y = dragOrigin_.y;
            else pt.x = dragOrigin_.x;
        }
        setUnits (parameterIndex (dragBand_, BandParameterOffset::Frequency),
                  static_cast<float> (xToHz (pt.x)));
        setUnits (parameterIndex (dragBand_, BandParameterOffset::Gain),
                  static_cast<float> (yToDb (pt.y)));
        curveDirty_ = true;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragBand_ >= 0) finishGestures();
        dragBand_ = -1;
        sync();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! graph_.contains (e.getPosition()) || panel_.contains (e.getPosition()))
            return;
        const int band = hitBand (e.position);
        if (band >= 0)
        {
            select (band);
            return;
        }
        createBand (e.position);
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        const int band = hitBand (event.position);
        if (band < 0 || std::abs (wheel.deltaY) < 1.0e-6f) return;
        const int idx = parameterIndex (band,
                            event.mods.isCtrlDown()
                              ? BandParameterOffset::Gain
                              : BandParameterOffset::Q);
        auto* p = param (idx);
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
            p->getValue() + wheel.deltaY
              * (event.mods.isShiftDown() ? 0.012f : 0.055f)));
        p->endChangeGesture();
        select (band);
        curveDirty_ = true;
    }

    // Exact native-offscreen snapshot test (not a generated concept image).
    int selectedBandForTesting() const noexcept { return selected_; }
    juce::Rectangle<int> graphBoundsForTesting() const noexcept { return graph_; }
    juce::Rectangle<int> panelBoundsForTesting() const noexcept { return panel_; }
    int activeBandsForTesting() const noexcept
    {
        int result = 0;
        for (int b = 0; b < kMaxBands; ++b)
            if (param (parameterIndex (b, BandParameterOffset::Enabled))->getBool())
                ++result;
        return result;
    }
    void selectBandForTesting (int band) { select (band); }
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
        };
        slider.onValueChange = [this, &slider, offset]
        {
            if (selected_ >= 0 && ! syncing_)
            {
                param (parameterIndex (selected_, offset))->setValueNotifyingHost (
                    static_cast<float> (slider.getValue()));
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
    void setSelectedVisible (bool visible)
    {
        for (juce::Component* c : { static_cast<juce::Component*> (&bandPower_),
                                   &shape_, &slope_, &placement_, &dynamic_,
                                   &advanced_, &closeBand_, &freq_, &gain_, &q_ })
            c->setVisible (visible);
    }
    void select (int index)
    {
        selected_ = index >= 0 && index < kMaxBands ? index : -1;
        setSelectedVisible (selected_ >= 0);
        sync(); resized(); repaint();
    }
    void finishGestures()
    {
        if (dragBand_ < 0) return;
        param (parameterIndex (dragBand_, BandParameterOffset::Frequency))
            ->endChangeGesture();
        param (parameterIndex (dragBand_, BandParameterOffset::Gain))
            ->endChangeGesture();
    }
    void sync()
    {
        syncing_ = true;
        phase_.setButtonText (param (kPhaseModeParameter)->getBool()
                               ? "LINEAR PHASE" : "ZERO LATENCY");
        globalBypass_.setButtonText (param (kGlobalBypassParameter)->getBool()
                                  ? "BYPASSED" : "POWER ON");
        autoGain_.setButtonText (param (kOutputAutoGainParameter)->getBool()
                              ? "AUTO ON" : "AUTO GAIN");
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
            bandPower_.setButtonText (by ? "○" : "●");
            dynamic_.setButtonText (param (dynamicParameterIndex (
                selected_, DynamicBandOffset::Enable))->getBool()
                  ? "DYN ON" : "DYN");
        }
        syncing_ = false;
    }
    void createBand (juce::Point<float> pt)
    {
        int slot = -1;
        for (int i = 0; i < kMaxBands; ++i)
            if (! param (parameterIndex (i, BandParameterOffset::Enabled))->getBool())
            {
                slot = i; break;
            }
        if (slot < 0) return;
        setUnits (parameterIndex (slot, BandParameterOffset::Frequency),
                  static_cast<float> (xToHz (pt.x)));
        setUnits (parameterIndex (slot, BandParameterOffset::Gain),
                  static_cast<float> (yToDb (pt.y)));
        setUnits (parameterIndex (slot, BandParameterOffset::Q), 1.0f);
        setUnits (parameterIndex (slot, BandParameterOffset::Shape),
                  static_cast<float> (FilterShape::Bell));
        setUnits (parameterIndex (slot, BandParameterOffset::Bypass), 0.0f);
        setUnits (parameterIndex (slot, BandParameterOffset::Enabled), 1.0f);
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
    void drawSpectrum (juce::Graphics& g)
    {
        if (! spectrumReady_) return;
        const int count = Analysis::SpectrumAnalyzerCore::kSpectrumBins;
        for (int mode = 0; mode < 2; ++mode)
        {
            if (! spectrumShown_[mode]) continue;
            juce::Path spectrum;
            bool begun = false;
            for (int i = 2; i < count; i += 3)
            {
                const double hz = i * spectrumBinHz_;
                if (hz < 20 || hz > 30000) continue;
                const float db = spectrum_[mode][static_cast<std::size_t> (i)];
                if (! std::isfinite (db)) continue;
                const float x = hzToX (hz);
                const float y = graph_.getY() + graph_.getHeight() - 24.0f
                    - (std::clamp (db, -110.0f, -10.0f) + 110.0f)
                      * (graph_.getHeight() - 48) / 100.0f;
                if (! begun) { spectrum.startNewSubPath (x, y); begun = true; }
                else spectrum.lineTo (x, y);
            }
            g.setColour (mode == 0 ? juce::Colour (0xff77747b).withAlpha (0.68f)
                                    : juce::Colour (0xffd6a75d).withAlpha (0.68f));
            g.strokePath (spectrum, juce::PathStrokeType (1.2f));
        }
    }
    void cycleAnalyzer()
    {
        const auto previous = processor_.getAnalyzerMode();
        const int next = (static_cast<int> (previous) + 1) % 4;
        processor_.setAnalyzerMode (
            static_cast<Analysis::SpectrumTapMode> (next));
        analyzerAutoStarted_ = false;
        syncAnalyzerText();
    }
    void syncAnalyzerText()
    {
        static const char* labels[] = {
            "ANALYZER OFF", "ANALYZER: PRE", "ANALYZER: POST",
            "ANALYZER: PRE + POST"
        };
        analyzer_.setButtonText (labels[juce::jlimit (0, 3,
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
        constexpr int slopes[] = { 0, 6, 12, 18, 24, 30, 36, 48, 72, 96 };
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
        menu.addItem (3, "Band audition (hold to listen)");
        menu.addItem (4, "Reset selected band");
        menu.addItem (5, "Delete band");
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
                else if (id == 3)
                {
                    e->processor_.beginAudition (band, ++e->auditionToken_);
                    // A menu action cannot safely implement hold-to-audition:
                    // immediately stop rather than risk latching solo.
                    e->processor_.endAudition (e->auditionToken_);
                }
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
    void timerCallback() override
    {
        const auto capture = processor_.getAnalyzer().acquireSnapshot();
        if (capture.isValid() && capture.index() != spectrumIndex_)
        {
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
    juce::Rectangle<int> graph_ {}, panel_ {};
    juce::TextButton undo_, redo_, compare_, copy_, preset_, phase_,
        analyzer_, displayRange_, globalBypass_, autoGain_;
    juce::TextButton bandPower_, shape_, slope_, placement_, dynamic_,
        advanced_, closeBand_;
    juce::Slider freq_, gain_, q_;
    int selected_ = -1, dragBand_ = -1, rangeIndex_ = 2;
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
    Analysis::SpectrumTapMode lastAnalyzerMode_ = Analysis::SpectrumTapMode::Closed;
    bool analyzerAutoStarted_ = false;
    std::uint32_t auditionToken_ = 0;
    std::array<Snapshot, 16> history_ {};
    int historyCount_ = 0, historyPosition_ = 0;
    Snapshot compareCopy_ {};
    bool hasCopy_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApexQ4Editor)
};
} // namespace APEX::ParametricEQ
