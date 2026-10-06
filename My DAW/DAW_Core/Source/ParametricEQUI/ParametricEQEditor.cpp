#include "ParametricEQEditor.h"

#include <array>
#include <cstring>
#include <functional>
#include <optional>

namespace APEX::ParametricEQ
{

namespace
{

// APEX visual identity (mirrors the canonical APEX palette — deep violet
// accent over layered blue-black panels, sky-blue signal curves).
struct Palette
{
    static juce::Colour background() { return juce::Colour (0xff0d0d0f); }
    static juce::Colour surface() { return juce::Colour (0xff1a1a1e); }
    static juce::Colour surfaceHover() { return juce::Colour (0xff212126); }
    static juce::Colour border() { return juce::Colour (0xff252529); }
    static juce::Colour text() { return juce::Colour (0xffe4e4e8); }
    static juce::Colour textSecondary() { return juce::Colour (0xff9a9aa3); }
    static juce::Colour accent() { return juce::Colour (0xff7c3aed); }
    static juce::Colour accentActive() { return juce::Colour (0xff6025cc); }
    static juce::Colour curve() { return juce::Colour (0xff38b2f8); }
    static juce::Colour curveMid() { return juce::Colour (0xfffbbf24); }
    static juce::Colour curveSide() { return juce::Colour (0xff14b87a); }
    static juce::Colour selected() { return juce::Colour (0xfffbbf24); }
    static juce::Colour danger() { return juce::Colour (0xffe83535); }
    static juce::Colour positive() { return juce::Colour (0xff14b87a); }
};

void notifyParameter (Processor& processor, int index, float normalised)
{
    auto* parameter = processor.getParametricEQParameter (index);
    if (parameter == nullptr)
        return;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (normalised);
    parameter->endChangeGesture();
}

double frequencyForX (double x, double width)
{
    const auto ratio = std::clamp (x / std::max (1.0, width), 0.0, 1.0);
    const auto logMin = std::log10 (
        static_cast<double> (ParametricEQEditor::kDisplayMinimumFrequency));
    const auto logMax = std::log10 (
        static_cast<double> (ParametricEQEditor::kDisplayMaximumFrequency));
    return std::pow (10.0, logMin + ratio * (logMax - logMin));
}

double xForFrequency (double frequencyHz, double width)
{
    const auto logMin = std::log10 (
        static_cast<double> (ParametricEQEditor::kDisplayMinimumFrequency));
    const auto logMax = std::log10 (
        static_cast<double> (ParametricEQEditor::kDisplayMaximumFrequency));
    const auto clamped = std::clamp (frequencyHz,
                                     static_cast<double> (
                                         ParametricEQEditor::kDisplayMinimumFrequency),
                                     static_cast<double> (
                                         ParametricEQEditor::kDisplayMaximumFrequency));
    return (std::log10 (clamped) - logMin) / (logMax - logMin) * width;
}

double gainForY (double y, double height)
{
    constexpr double maximumDb = 18.0;
    return maximumDb * (1.0 - 2.0 * std::clamp (y / std::max (1.0, height),
                                                0.0, 1.0));
}

double yForGain (double gainDb, double height)
{
    constexpr double maximumDb = 18.0;
    const auto ratio = 0.5 * (1.0 - std::clamp (gainDb / maximumDb, -1.0, 1.0));
    return ratio * height;
}

const char* shapeName (int shape)
{
    static const char* names[] = {
        "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
        "Notch", "Band Pass", "Tilt", "Flat Tilt", "All Pass"
    };
    return names[std::clamp (shape, 0, 9)];
}

} // namespace

//==============================================================================
class ParametricEQEditor::TopBarComponent final : public juce::Component,
                                                  private juce::Timer
{
public:
    TopBarComponent (ParametricEQEditor& owner, Processor& processor)
        : owner_ (owner), processor_ (processor)
    {
        addAndMakeVisible (designButton_);
        addAndMakeVisible (bypassButton_);
        addAndMakeVisible (phaseButton_);
        addAndMakeVisible (analyzerButton_);
        addAndMakeVisible (detectorButton_);
        addAndMakeVisible (keyButton_);
        addAndMakeVisible (linkButton_);
        addAndMakeVisible (characterButton_);
        addAndMakeVisible (title_);

        designButton_.setTitle ("Switch filter design mode");
        bypassButton_.setTitle ("Toggle global bypass");
        phaseButton_.setTitle ("Switch phase mode");
        analyzerButton_.setTitle ("Toggle spectrum analyzer");
        detectorButton_.setTitle ("Switch dynamic detector mode");
        keyButton_.setTitle ("Switch dynamic sidechain source");
        linkButton_.setTitle ("Toggle dynamic stereo link");
        characterButton_.setTitle ("Cycle APEX character mode");
        // State display is driven by refresh(); onClick must fire on every
        // click, so these buttons do not own click-toggle state.
        designButton_.setClickingTogglesState (false);
        bypassButton_.setClickingTogglesState (false);
        phaseButton_.setClickingTogglesState (false);
        analyzerButton_.setClickingTogglesState (false);
        detectorButton_.setClickingTogglesState (false);
        keyButton_.setClickingTogglesState (false);
        linkButton_.setClickingTogglesState (false);
        characterButton_.setClickingTogglesState (false);
        title_.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        title_.setColour (juce::Label::textColourId, Palette::text());
        title_.setText ("APEX Parametric EQ", juce::dontSendNotification);
        title_.setJustificationType (juce::Justification::centredLeft);
        title_.setInterceptsMouseClicks (false, false);

        designButton_.onClick = [this]
        {
            const auto mode = processor_.getParametricEQParameter (
                kDesignModeParameter);
            notifyParameter (processor_, kDesignModeParameter,
                             mode->getValue() >= 0.5f ? 0.0f : 1.0f);
            refresh();
        };
        bypassButton_.onClick = [this]
        {
            const auto bypass = processor_.getParametricEQParameter (
                kGlobalBypassParameter);
            notifyParameter (processor_, kGlobalBypassParameter,
                             bypass->getValue() >= 0.5f ? 0.0f : 1.0f);
            refresh();
        };
        phaseButton_.onClick = [this]
        {
            const auto phase = processor_.getParametricEQParameter (
                kPhaseModeParameter);
            notifyParameter (processor_, kPhaseModeParameter,
                             phase->getValue() >= 0.5f ? 0.0f : 1.0f);
            refresh();
        };
        analyzerButton_.onClick = [this]
        {
            auto mode = processor_.getAnalyzerMode();
            mode = static_cast<APEX::Analysis::SpectrumTapMode> (
                (static_cast<int> (mode) + 1) % 4);
            processor_.setAnalyzerMode (mode);
            refresh();
        };
        detectorButton_.onClick = [this]
        {
            const auto mode = processor_.getParametricEQParameter (
                kDynamicDetectorParameter);
            notifyParameter (processor_, kDynamicDetectorParameter,
                             mode->getValue() >= 0.5f ? 0.0f : 1.0f);
            refresh();
        };
        keyButton_.onClick = [this]
        {
            const auto source = processor_.getParametricEQParameter (
                kDynamicSidechainParameter);
            notifyParameter (processor_, kDynamicSidechainParameter,
                             source->getValue() >= 0.5f ? 0.0f : 1.0f);
            refresh();
        };
        linkButton_.onClick = [this]
        {
            const auto link = processor_.getParametricEQParameter (
                kDynamicLinkParameter);
            notifyParameter (processor_, kDynamicLinkParameter,
                             link->getValue() >= 0.5f ? 0.0f : 1.0f);
            refresh();
        };
        characterButton_.onClick = [this]
        {
            auto* character = processor_.getParametricEQParameter (
                kCharacterModeParameter);
            const int next = (character->getChoiceIndex() + 1)
                           % kCharacterModeCount;
            notifyParameter (processor_, kCharacterModeParameter,
                             character->toNormalised (static_cast<float> (next)));
            refresh();
        };
        // Host/state changes to peq.phase must reach the top bar even when
        // they were not initiated by this button; a low-rate timer refresh
        // matches the existing inspector/graph synchronization pattern.
        startTimerHz (12);
        refresh();
    }

    juce::TextButton& getPhaseButtonForTesting() noexcept { return phaseButton_; }

    void refresh()
    {
        const bool analog = processor_.getParametricEQParameter (
            kDesignModeParameter)->getValue() >= 0.5f;
        designButton_.setButtonText (analog ? "Analog" : "Realtime");
        designButton_.setColour (juce::TextButton::buttonColourId,
                                 analog ? Palette::accentActive()
                                        : Palette::surfaceHover());
        const bool bypassed = processor_.getParametricEQParameter (
            kGlobalBypassParameter)->getValue() >= 0.5f;
        bypassButton_.setButtonText (bypassed ? "Bypassed" : "Active");
        bypassButton_.setColour (juce::TextButton::buttonColourId,
                                 bypassed ? Palette::danger()
                                          : Palette::surfaceHover());
        const bool linear = processor_.getParametricEQParameter (
            kPhaseModeParameter)->getBool();
        phaseButton_.setButtonText (linear ? "Linear" : "Minimum");
        phaseButton_.setColour (juce::TextButton::buttonColourId,
                                linear ? Palette::accentActive()
                                       : Palette::surfaceHover());
        static const char* modeNames[] = { "Analyzer Off", "Analyzer In",
                                           "Analyzer Out", "Analyzer Both" };
        analyzerButton_.setButtonText (modeNames[static_cast<int> (
            processor_.getAnalyzerMode())]);
        detectorButton_.setButtonText (processor_.getParametricEQParameter (
            kDynamicDetectorParameter)->getBool() ? "Detector RMS" : "Detector Peak");
        keyButton_.setButtonText (processor_.getParametricEQParameter (
            kDynamicSidechainParameter)->getBool() ? "Key External" : "Key Internal");
        linkButton_.setButtonText (processor_.getParametricEQParameter (
            kDynamicLinkParameter)->getBool() ? "Link On" : "Link Off");
        const auto character = static_cast<CharacterMode> (std::clamp (
            processor_.getParametricEQParameter (kCharacterModeParameter)
                ->getChoiceIndex(), 0, kCharacterModeCount - 1));
        characterButton_.setButtonText (
            juce::String ("CHAR ") + characterModeName (character));
        characterButton_.setColour (
            juce::TextButton::buttonColourId,
            character == CharacterMode::Pure ? Palette::surfaceHover()
                                             : Palette::accentActive());
        repaint();
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (4, 4);
        if (getWidth() < 430)
        {
            // Compact-phone contract: two rows of four controls, each row at
            // least 44 logical pixels tall.  The title yields its space to
            // controls instead of shrinking touch targets.
            title_.setVisible (false);
            auto row1 = bounds.removeFromTop (bounds.getHeight() / 2);
            auto row2 = bounds;
            auto placeFour = [] (juce::Rectangle<int> row,
                                 juce::Component& a, juce::Component& b,
                                 juce::Component& d, juce::Component& e)
            {
                const int w = std::max (44, row.getWidth() / 4);
                a.setBounds (row.removeFromLeft (w));
                b.setBounds (row.removeFromLeft (w));
                d.setBounds (row.removeFromLeft (w));
                e.setBounds (row);
            };
            placeFour (row1, designButton_, bypassButton_, phaseButton_,
                       analyzerButton_);
            placeFour (row2, detectorButton_, keyButton_, linkButton_,
                       characterButton_);
            return;
        }

        title_.setVisible (true);
        const int buttonWidth = std::max (44, bounds.getWidth() / 10);
        const int titleWidth = std::max (0, bounds.getWidth() - 8 * buttonWidth);
        title_.setBounds (bounds.removeFromLeft (titleWidth));
        designButton_.setBounds (bounds.removeFromLeft (buttonWidth));
        bypassButton_.setBounds (bounds.removeFromLeft (buttonWidth));
        phaseButton_.setBounds (bounds.removeFromLeft (buttonWidth));
        analyzerButton_.setBounds (bounds.removeFromLeft (buttonWidth));
        detectorButton_.setBounds (bounds.removeFromLeft (buttonWidth));
        keyButton_.setBounds (bounds.removeFromLeft (buttonWidth));
        linkButton_.setBounds (bounds.removeFromLeft (buttonWidth));
        characterButton_.setBounds (bounds);
    }

    void timerCallback() override
    {
        refresh();
    }

private:
    ParametricEQEditor& owner_;
    Processor& processor_;
    juce::Label title_;
    juce::TextButton designButton_ { "Realtime" };
    juce::TextButton bypassButton_ { "Active" };
    juce::TextButton phaseButton_ { "Minimum" };
    juce::TextButton analyzerButton_ { "Analyzer Off" };
    juce::TextButton detectorButton_ { "Detector RMS" };
    juce::TextButton keyButton_ { "Key Internal" };
    juce::TextButton linkButton_ { "Link On" };
    juce::TextButton characterButton_ { "CHAR Pure" };
};

//==============================================================================
class ParametricEQEditor::EqGraphComponent final : public juce::Component,
                                                   private juce::Timer
{
public:
    EqGraphComponent (ParametricEQEditor& owner, Processor& processor)
        : owner_ (owner), processor_ (processor)
    {
        setInterceptsMouseClicks (true, false);
        startTimerHz (25);
    }

    void resized() override { curveDirty_ = true; }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::background());
        g.fillAll();
        g.setColour (Palette::surface());
        g.fillRect (bounds.reduced (1.0f));
        drawGrid (g, bounds);
        if (curveDirty_)
        {
            rebuildCurves (bounds);
            curveDirty_ = false;
        }
        if (spectrumActive_)
            drawSpectrum (g, bounds);
        g.setColour (Palette::curve());
        g.strokePath (mainCurve_, juce::PathStrokeType (1.8f));
        if (mixedPlacements_)
        {
            g.setColour (Palette::curveMid().withAlpha (0.75f));
            g.strokePath (midCurve_, juce::PathStrokeType (1.0f));
            g.setColour (Palette::curveSide().withAlpha (0.75f));
            g.strokePath (sideCurve_, juce::PathStrokeType (1.0f));
        }
        drawNodes (g, bounds);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        dragging_ = false;
        longPressFired_ = false;
        longPressTarget_.reset();
        pointerDown_ = true;
        pressTime_ = juce::Time::getMillisecondCounter();
        lastDragPosition_ = event.position;

        const auto hit = hitTest (event.position);
        if (hit >= 0)
        {
            owner_.selectBand (hit);
            dragging_ = true;
            dragBand_ = hit;
            auto* frequency = processor_.getParametricEQParameter (parameterIndex (
                hit, BandParameterOffset::Frequency));
            auto* gain = processor_.getParametricEQParameter (parameterIndex (
                hit, BandParameterOffset::Gain));
            frequency->beginChangeGesture();
            gain->beginChangeGesture();
        }
        else
        {
            owner_.selectBand (-1);
            longPressTarget_ = firstDisabledBand();
        }
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        lastDragPosition_ = event.position;
        if (dragging_ && dragBand_ >= 0)
        {
            const auto width = static_cast<double> (std::max (1, getWidth()));
            const auto height = static_cast<double> (std::max (1, getHeight()));
            const auto frequency = static_cast<float> (frequencyForX (
                event.position.x, width));
            const auto gain = static_cast<float> (gainForY (
                event.position.y, height));
            auto* frequencyParameter = processor_.getParametricEQParameter (
                parameterIndex (dragBand_, BandParameterOffset::Frequency));
            auto* gainParameter = processor_.getParametricEQParameter (
                parameterIndex (dragBand_, BandParameterOffset::Gain));
            frequencyParameter->setValueNotifyingHost (
                frequencyParameter->toNormalised (frequency));
            gainParameter->setValueNotifyingHost (
                gainParameter->toNormalised (gain));
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        pointerDown_ = false;
        if (dragging_ && dragBand_ >= 0)
        {
            auto* frequency = processor_.getParametricEQParameter (parameterIndex (
                dragBand_, BandParameterOffset::Frequency));
            auto* gain = processor_.getParametricEQParameter (parameterIndex (
                dragBand_, BandParameterOffset::Gain));
            frequency->endChangeGesture();
            gain->endChangeGesture();
        }
        dragging_ = false;
        dragBand_ = -1;
        owner_.endAuditionGesture();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        owner_.endAuditionGesture();
    }

    void visibilityChanged() override
    {
        if (! isVisible())
            owner_.endAuditionGesture();
    }

    juce::Rectangle<int> nodeBounds (int band) const
    {
        const auto centre = nodeCentre (band);
        const int target = ParametricEQEditor::kTouchTarget;
        return { centre.x - target / 2, centre.y - target / 2, target, target };
    }

    bool isBandDynamicActive (int band) const noexcept
    {
        return band >= 0 && band < kMaxBands
            && (latestFrame_.dynamicActiveMask & (1u << band)) != 0;
    }

    double displayedDynamicGainDb (int band) const noexcept
    {
        return band >= 0 && band < kMaxBands
             ? latestFrame_.dynamicGainDb[static_cast<std::size_t> (band)] : 0.0;
    }

    juce::Point<int> nodeCentre (int band) const
    {
        const auto* frequency = processor_.getParametricEQParameter (
            parameterIndex (band, BandParameterOffset::Frequency));
        const auto* gain = processor_.getParametricEQParameter (
            parameterIndex (band, BandParameterOffset::Gain));
        return { static_cast<int> (xForFrequency (
                     frequency->getUnitsValue(), std::max (1, getWidth()))),
                 static_cast<int> (yForGain (gain->getUnitsValue(),
                                            std::max (1, getHeight()))) };
    }

    bool isMixedPlacementFrame() const noexcept { return mixedPlacements_; }

private:
    void timerCallback() override
    {
        ResponseFrame frame;
        if (processor_.getResponseCore().consumeLatest (frame))
        {
            latestFrame_ = frame;
            curveDirty_ = true;
        }

        bool changed = curveDirty_;
        if (processor_.getAnalyzerMode() != APEX::Analysis::SpectrumTapMode::Closed)
        {
            auto snapshot = processor_.getAnalyzer().acquireSnapshot();
            if (snapshot.isValid() && snapshot.index() != lastSpectrumIndex_)
            {
                lastSpectrumIndex_ = snapshot.index();
                const float* source = snapshot.postActive() ? snapshot.postDb()
                                                            : snapshot.preDb();
                std::memcpy (spectrumCopy_.data(), source,
                             spectrumCopy_.size() * sizeof (float));
                spectrumActive_ = true;
                changed = true;
            }
        }
        else if (spectrumActive_)
        {
            spectrumActive_ = false;
            changed = true;
        }

        // Long-press: audition the held node, or create a band on empty space.
        if (pointerDown_ && ! longPressFired_
            && juce::Time::getMillisecondCounter() - pressTime_ > 450)
        {
            longPressFired_ = true;
            if (dragBand_ >= 0)
            {
                owner_.selectBand (dragBand_);
                owner_.beginAuditionGesture();
            }
            else if (longPressTarget_.has_value())
            {
                const int band = *longPressTarget_;
                const auto frequency = static_cast<float> (frequencyForX (
                    lastDragPosition_.x, std::max (1, getWidth())));
                notifyParameter (processor_, parameterIndex (
                    band, BandParameterOffset::Frequency),
                    processor_.getParametricEQParameter (parameterIndex (
                        band, BandParameterOffset::Frequency))
                            ->toNormalised (frequency));
                notifyParameter (processor_, parameterIndex (
                    band, BandParameterOffset::Enabled), 1.0f);
                owner_.selectBand (band);
            }
            changed = true;
        }

        if (changed)
            repaint();
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        g.setColour (Palette::border().withAlpha (0.5f));
        for (int step = -18; step <= 18; step += 6)
        {
            const auto y = static_cast<float> (yForGain (step, bounds.getHeight()));
            g.drawLine (bounds.getX(), y, bounds.getRight(), y);
        }
        for (const int frequency : { 20, 50, 100, 200, 500, 1000, 2000, 5000,
                                     10000, 20000 })
        {
            const auto x = static_cast<float> (xForFrequency (frequency,
                                                              bounds.getWidth()));
            g.drawLine (x, bounds.getY(), x, bounds.getBottom());
        }
    }

    void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        const float binHz = spectrumCopy_.size() > 0
                          ? static_cast<float> (latestFrame_.sampleRate)
                                / static_cast<float> (2 * (spectrumCopy_.size() - 1))
                          : 11.72f;
        g.setColour (Palette::curveSide().withAlpha (0.30f));
        const int width = std::max (1, getWidth());
        const auto baseline = static_cast<float> (yForGain (-60.0,
                                                            bounds.getHeight()));
        for (int x = 0; x < width; ++x)
        {
            const auto frequency = frequencyForX (x, width);
            const int bin = std::clamp (static_cast<int> (
                std::lround (frequency / std::max (1.0f, binHz))),
                0, static_cast<int> (spectrumCopy_.size()) - 1);
            const auto db = spectrumCopy_[static_cast<std::size_t> (bin)];
            const auto y = static_cast<float> (yForGain (
                std::clamp (static_cast<double> (db), -60.0, 18.0),
                bounds.getHeight()));
            g.drawVerticalLine (x, std::min (y, baseline),
                                std::max (y, baseline));
        }
    }

    void rebuildCurves (juce::Rectangle<float> bounds)
    {
        juce::ignoreUnused (bounds);
        mainCurve_.clear();
        midCurve_.clear();
        sideCurve_.clear();
        mixedPlacements_ = false;

        for (const auto& band : latestFrame_.currentBands)
            if (band.enabled && ! band.bypassed
                && band.placement != ChannelPlacement::Stereo)
                mixedPlacements_ = true;

        const int width = std::max (1, getWidth());
        const int height = std::max (1, getHeight());
        constexpr int basePoints = 320;
        constexpr int criticalInsertions = 32;
        const int totalPoints = basePoints + kMaxBands * criticalInsertions;
        bool haveMain = false, haveMid = false, haveSide = false;

        for (int point = 0; point < totalPoints; ++point)
        {
            double frequency = 0.0;
            if (point < basePoints)
            {
                frequency = frequencyForX (static_cast<double> (point)
                                             / (basePoints - 1) * width, width);
            }
            else
            {
                const int extra = point - basePoints;
                const int band = std::clamp (extra / criticalInsertions, 0,
                                             kMaxBands - 1);
                const int step = extra % criticalInsertions;
                const auto centre = static_cast<double> (
                    latestFrame_.currentBands[static_cast<std::size_t> (band)]
                        .frequencyHz);
                frequency = centre * std::pow (2.0,
                    (step - criticalInsertions / 2) / 8.0);
            }
            if (! std::isfinite (frequency) || frequency <= 0.0)
                continue;

            const auto matrix = ResponseCore::responseMatrix (latestFrame_,
                                                              frequency);
            const auto x = static_cast<float> (xForFrequency (frequency, width));
            const auto y = static_cast<float> (yForGain (
                20.0 * std::log10 (std::max (1.0e-12,
                                             std::abs (matrix.ll))), height));
            if (haveMain)
                mainCurve_.lineTo (x, y);
            else
                mainCurve_.startNewSubPath (x, y);
            haveMain = true;

            if (mixedPlacements_)
            {
                const auto midY = static_cast<float> (yForGain (
                    20.0 * std::log10 (std::max (1.0e-12,
                                                 std::abs (matrix.midProjection()))),
                    height));
                if (haveMid)
                    midCurve_.lineTo (x, midY);
                else
                    midCurve_.startNewSubPath (x, midY);
                haveMid = true;

                const auto sideY = static_cast<float> (yForGain (
                    20.0 * std::log10 (std::max (1.0e-12,
                                                 std::abs (matrix.sideProjection()))),
                    height));
                if (haveSide)
                    sideCurve_.lineTo (x, sideY);
                else
                    sideCurve_.startNewSubPath (x, sideY);
                haveSide = true;
            }
        }
    }

    void drawNodes (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        juce::ignoreUnused (bounds);
        for (int band = 0; band < kMaxBands; ++band)
        {
            const auto* enabled = processor_.getParametricEQParameter (
                parameterIndex (band, BandParameterOffset::Enabled));
            const auto* bypassed = processor_.getParametricEQParameter (
                parameterIndex (band, BandParameterOffset::Bypass));
            if (! enabled->getBool())
                continue;
            const auto centre = nodeCentre (band).toFloat();
            const float visual = 11.0f;
            juce::Colour colour = Palette::accent();
            if (bypassed->getBool())
                colour = Palette::textSecondary();
            else if (band == owner_.getSelectedBandForTesting())
                colour = Palette::selected();
            g.setColour (colour.withAlpha (bypassed->getBool() ? 0.6f : 1.0f));
            juce::Path diamond;
            diamond.addPolygon (centre, 4, visual,
                                juce::MathConstants<float>::pi / 4.0f);
            g.fillPath (diamond);
            g.setColour (Palette::background());
            g.strokePath (diamond, juce::PathStrokeType (1.5f));
        }
        if (latestFrame_.auditionActive && latestFrame_.auditionBand >= 0
            && latestFrame_.auditionBand < kMaxBands)
        {
            const auto centre = nodeCentre (latestFrame_.auditionBand).toFloat();
            g.setColour (Palette::positive().withAlpha (0.9f));
            g.drawEllipse (centre.x - 14.0f, centre.y - 14.0f,
                           28.0f, 28.0f, 2.0f);
        }

        // Live dynamic activity from the published processor state. Cuts
        // (negative gain) draw an amber arc below the node; boosts (positive
        // gain) draw a green arc above it; length encodes the magnitude.
        for (int band = 0; band < kMaxBands; ++band)
        {
            if ((latestFrame_.dynamicActiveMask & (1u << band)) == 0)
                continue;
            const auto gainDb = latestFrame_.dynamicGainDb[
                static_cast<std::size_t> (band)];
            const auto centre = nodeCentre (band).toFloat();
            const float magnitude = static_cast<float> (
                std::clamp (std::abs (gainDb) / 24.0, 0.05, 1.0));
            const float radius = 16.0f + 10.0f * magnitude;
            const auto colour = gainDb < 0.0 ? Palette::curveMid()
                                             : Palette::curveSide();
            const float startAngle = gainDb < 0.0
                ? juce::MathConstants<float>::pi * 0.25f
                : -juce::MathConstants<float>::pi * 0.75f;
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                               startAngle, startAngle + 0.5f
                                   * juce::MathConstants<float>::pi, true);
            g.setColour (colour.withAlpha (0.85f));
            g.strokePath (arc, juce::PathStrokeType (2.5f));
        }
    }

    int hitTest (juce::Point<float> position) const
    {
        for (int band = kMaxBands - 1; band >= 0; --band)
        {
            const auto* enabled = processor_.getParametricEQParameter (
                parameterIndex (band, BandParameterOffset::Enabled));
            if (! enabled->getBool())
                continue;
            if (nodeBounds (band).toFloat().contains (position))
                return band;
        }
        return -1;
    }

    std::optional<int> firstDisabledBand() const
    {
        for (int band = 0; band < kMaxBands; ++band)
        {
            const auto* enabled = processor_.getParametricEQParameter (
                parameterIndex (band, BandParameterOffset::Enabled));
            if (! enabled->getBool())
                return band;
        }
        return std::nullopt;
    }

    ParametricEQEditor& owner_;
    Processor& processor_;
    juce::Path mainCurve_, midCurve_, sideCurve_;
    bool curveDirty_ = true;
    bool mixedPlacements_ = false;
    ResponseFrame latestFrame_;
    int lastSpectrumIndex_ = -1;
    bool spectrumActive_ = false;
    std::array<float, APEX::Analysis::SpectrumAnalyzerCore::kSpectrumBins>
        spectrumCopy_ {};
    bool dragging_ = false;
    int dragBand_ = -1;
    juce::Point<float> lastDragPosition_;
    bool pointerDown_ = false;
    bool longPressFired_ = false;
    std::optional<int> longPressTarget_;
    std::uint32_t pressTime_ = 0;
};

//==============================================================================
namespace
{
class HoldButton final : public juce::TextButton
{
public:
    HoldButton (ParametricEQEditor& owner)
        : juce::TextButton ("Hold to Audition Band"), owner_ (owner) {}

    void mouseDown (const juce::MouseEvent&) override
    {
        owner_.beginAuditionGesture();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        owner_.endAuditionGesture();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        owner_.endAuditionGesture();
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! getLocalBounds().toFloat().contains (event.position))
            owner_.endAuditionGesture();
    }

private:
    ParametricEQEditor& owner_;
};
} // namespace

//==============================================================================
class ParametricEQEditor::InspectorComponent final : public juce::Component,
                                                     private juce::Timer
{
public:
    InspectorComponent (ParametricEQEditor& owner, Processor& processor)
        : owner_ (owner), processor_ (processor)
    {
        addAndMakeVisible (viewport_);
        viewport_.setViewedComponent (&content_, false);
        content_.addAndMakeVisible (bandLabel_);
        content_.addAndMakeVisible (enableButton_);
        content_.addAndMakeVisible (bypassButton_);
        content_.addAndMakeVisible (closeButton_);
        content_.addAndMakeVisible (shapeCombo_);
        content_.addAndMakeVisible (frequencySlider_);
        content_.addAndMakeVisible (gainSlider_);
        content_.addAndMakeVisible (qSlider_);
        content_.addAndMakeVisible (slopeSlider_);
        content_.addAndMakeVisible (placementLabel_);
        auditionButton_ = std::make_unique<HoldButton> (owner_);
        content_.addAndMakeVisible (auditionButton_.get());

        bandLabel_.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        bandLabel_.setColour (juce::Label::textColourId, Palette::text());
        closeButton_.setButtonText ("X");
        closeButton_.setTitle ("Close band inspector");
        closeButton_.onClick = [this] { owner_.selectBand (-1); };
        enableButton_.onClick = [this] { toggle (BandParameterOffset::Enabled); };
        bypassButton_.onClick = [this] { toggle (BandParameterOffset::Bypass); };

        for (int shape = 0; shape < 10; ++shape)
            shapeCombo_.addItem (shapeName (shape), shape + 1);
        shapeCombo_.setTitle ("Band filter shape");
        shapeCombo_.onChange = [this]
        {
            if (owner_.getSelectedBandForTesting() < 0)
                return;
            auto* parameter = processor_.getParametricEQParameter (parameterIndex (
                owner_.getSelectedBandForTesting(), BandParameterOffset::Shape));
            notifyParameter (processor_, parameterIndex (
                owner_.getSelectedBandForTesting(), BandParameterOffset::Shape),
                parameter->toNormalised (static_cast<float> (
                    shapeCombo_.getSelectedId() - 1)));
        };

        configureSlider (frequencySlider_, "Frequency");
        configureSlider (gainSlider_, "Gain");
        configureSlider (qSlider_, "Q");
        configureSlider (slopeSlider_, "Slope");

        placementLabel_.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        placementLabel_.setColour (juce::Label::textColourId,
                                   Palette::textSecondary());
        placementLabel_.setText ("Placement", juce::dontSendNotification);
        placementLabel_.setInterceptsMouseClicks (false, false);
        for (int placement = 0; placement < kChannelPlacementCount; ++placement)
        {
            auto button = std::make_unique<juce::TextButton> (
                channelPlacementName (static_cast<ChannelPlacement> (placement)));
            button->setComponentID ("placement." + juce::String (placement));
            button->setTitle (juce::String ("Set placement to ")
                              + channelPlacementName (
                                  static_cast<ChannelPlacement> (placement)));
            // Toggle visuals are owned by refresh(); onClick must fire on
            // every press (radio grouping would swallow clicks).
            button->setClickingTogglesState (false);
            button->onClick = [this, placement]
            {
                if (owner_.getSelectedBandForTesting() < 0)
                    return;
                auto* parameter = processor_.getParametricEQParameter (
                    placementParameterIndex (owner_.getSelectedBandForTesting()));
                notifyParameter (processor_, placementParameterIndex (
                    owner_.getSelectedBandForTesting()),
                    parameter->toNormalised (static_cast<float> (placement)));
                refresh();
            };
            addAndMakeVisible (button.get());
            placementButtons_.push_back (std::move (button));
        }

        // Contextual Dynamic EQ section (Phase 5). Shown only for the
        // selected band; every control drives the canonical hosted
        // parameters with balanced gestures.
        dynLabel_.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        dynLabel_.setColour (juce::Label::textColourId, Palette::textSecondary());
        dynLabel_.setText ("Dynamic EQ", juce::dontSendNotification);
        dynLabel_.setInterceptsMouseClicks (false, false);
        dynEnableButton_.setClickingTogglesState (false);
        dynEnableButton_.setTitle ("Enable per-band Dynamic EQ");
        dynEnableButton_.onClick = [this]
        {
            toggleDynamic (DynamicBandOffset::Enable);
        };
        configureSlider (dynThresholdSlider_, "Dynamic Threshold");
        configureSlider (dynRangeSlider_, "Dynamic Range");
        configureSlider (dynAttackSlider_, "Dynamic Attack");
        configureSlider (dynReleaseSlider_, "Dynamic Release");

        startTimerHz (12);
        refresh();
    }

    void resized() override
    {
        viewport_.setBounds (getLocalBounds());

        // The inspector content uses touch-sized fixed rows. When the editor
        // is shorter than the full control stack, the viewport scrolls the
        // contextual panel instead of shrinking controls into unusability.
        constexpr int row = 56;
        const int placementColumns = viewport_.getWidth() >= 340 ? 5 : 3;
        const int placementRows = (kChannelPlacementCount + placementColumns - 1)
                                / placementColumns;
        constexpr int labelHeight = 18;
        constexpr int auditionHeight = 48;
        constexpr int contentMargin = 16; // reduced (8, 8)
        // The audition row must retain its full touch-target height: content
        // includes the margin plus every fixed row so no row is ever clamped
        // short by vertical exhaustion. 12 static rows + 1 label +
        // 1 dynamic label + 5 dynamic rows.
        const int contentHeight = contentMargin + 12 * row + 2 * labelHeight
                                + placementRows * row + auditionHeight;
        content_.setBounds (0, 0, std::max (1, viewport_.getWidth()),
                            std::max (1, contentHeight));

        auto bounds = content_.getLocalBounds().reduced (8, 8);
        bandLabel_.setBounds (bounds.removeFromTop (row));
        auto buttonRow = bounds.removeFromTop (row);
        const int third = buttonRow.getWidth() / 3;
        enableButton_.setBounds (buttonRow.removeFromLeft (third).reduced (2, 2));
        bypassButton_.setBounds (buttonRow.removeFromLeft (third).reduced (2, 2));
        closeButton_.setBounds (buttonRow.reduced (2, 2));

        shapeCombo_.setBounds (bounds.removeFromTop (row));
        frequencySlider_.setBounds (bounds.removeFromTop (row));
        gainSlider_.setBounds (bounds.removeFromTop (row));
        qSlider_.setBounds (bounds.removeFromTop (row));
        slopeSlider_.setBounds (bounds.removeFromTop (row));

        placementLabel_.setBounds (bounds.removeFromTop (labelHeight));
        for (int rowIndex = 0; rowIndex < placementRows; ++rowIndex)
        {
            auto rowBounds = bounds.removeFromTop (row);
            const int count = std::min (placementColumns,
                kChannelPlacementCount - rowIndex * placementColumns);
            const int columnWidth = rowBounds.getWidth() / count;
            for (int column = 0; column < count; ++column)
            {
                const int index = rowIndex * placementColumns + column;
                placementButtons_[static_cast<std::size_t> (index)]->setBounds (
                    rowBounds.removeFromLeft (columnWidth).reduced (2, 2));
            }
        }

        dynLabel_.setBounds (bounds.removeFromTop (labelHeight));
        dynEnableButton_.setBounds (bounds.removeFromTop (row));
        dynThresholdSlider_.setBounds (bounds.removeFromTop (row));
        dynRangeSlider_.setBounds (bounds.removeFromTop (row));
        dynAttackSlider_.setBounds (bounds.removeFromTop (row));
        dynReleaseSlider_.setBounds (bounds.removeFromTop (row));

        auditionButton_->setBounds (bounds.removeFromTop (auditionHeight));
    }

    void refresh()
    {
        const int band = owner_.getSelectedBandForTesting();
        const bool valid = band >= 0 && band < kMaxBands;
        bandLabel_.setText (valid ? "Band " + juce::String (band + 1)
                                  : "No band selected — tap a node",
                            juce::dontSendNotification);
        enableButton_.setEnabled (valid);
        bypassButton_.setEnabled (valid);
        closeButton_.setEnabled (valid);
        shapeCombo_.setEnabled (valid);
        frequencySlider_.setEnabled (valid);
        gainSlider_.setEnabled (valid);
        qSlider_.setEnabled (valid);
        slopeSlider_.setEnabled (valid);
        auditionButton_->setEnabled (valid);
        for (auto& button : placementButtons_)
            button->setEnabled (valid);

        if (valid)
        {
            const auto* enabled = processor_.getParametricEQParameter (
                parameterIndex (band, BandParameterOffset::Enabled));
            const auto* bypassed = processor_.getParametricEQParameter (
                parameterIndex (band, BandParameterOffset::Bypass));
            enableButton_.setButtonText (enabled->getBool() ? "On" : "Off");
            enableButton_.setColour (juce::TextButton::buttonColourId,
                                     enabled->getBool() ? Palette::positive()
                                                        : Palette::surfaceHover());
            bypassButton_.setButtonText (bypassed->getBool() ? "Bypassed" : "In Path");
            bypassButton_.setColour (juce::TextButton::buttonColourId,
                                     bypassed->getBool() ? Palette::danger()
                                                         : Palette::surfaceHover());
            syncSlider (frequencySlider_, parameterIndex (
                band, BandParameterOffset::Frequency));
            syncSlider (gainSlider_, parameterIndex (band, BandParameterOffset::Gain));
            syncSlider (qSlider_, parameterIndex (band, BandParameterOffset::Q));
            syncSlider (slopeSlider_, parameterIndex (
                band, BandParameterOffset::Slope));
            shapeCombo_.setSelectedId (
                1 + processor_.getParametricEQParameter (parameterIndex (
                    band, BandParameterOffset::Shape))->getChoiceIndex(),
                juce::dontSendNotification);
            const int placement = processor_.getParametricEQParameter (
                placementParameterIndex (band))->getChoiceIndex();
            for (int index = 0; index < kChannelPlacementCount; ++index)
                placementButtons_[static_cast<std::size_t> (index)]->setToggleState (
                    index == placement, juce::dontSendNotification);

            // Dynamic EQ section: only gain-compatible shapes offer dynamics.
            const auto shape = static_cast<FilterShape> (
                processor_.getParametricEQParameter (parameterIndex (
                    band, BandParameterOffset::Shape))->getChoiceIndex());
            const bool dynamicCompatible = isDynamicCompatibleShape (shape);
            dynEnableButton_.setEnabled (dynamicCompatible);
            dynThresholdSlider_.setEnabled (dynamicCompatible);
            dynRangeSlider_.setEnabled (dynamicCompatible);
            dynAttackSlider_.setEnabled (dynamicCompatible);
            dynReleaseSlider_.setEnabled (dynamicCompatible);
            if (dynamicCompatible)
            {
                const auto dynamicOn = processor_.getParametricEQParameter (
                    dynamicParameterIndex (band, DynamicBandOffset::Enable))->getBool();
                dynEnableButton_.setButtonText (dynamicOn ? "Dynamic On" : "Dynamic Off");
                dynEnableButton_.setColour (juce::TextButton::buttonColourId,
                                            dynamicOn ? Palette::accentActive()
                                                      : Palette::surfaceHover());
                syncSlider (dynThresholdSlider_, dynamicParameterIndex (
                    band, DynamicBandOffset::Threshold));
                syncSlider (dynRangeSlider_, dynamicParameterIndex (
                    band, DynamicBandOffset::Range));
                syncSlider (dynAttackSlider_, dynamicParameterIndex (
                    band, DynamicBandOffset::Attack));
                syncSlider (dynReleaseSlider_, dynamicParameterIndex (
                    band, DynamicBandOffset::Release));
            }
        }
        else
        {
            dynEnableButton_.setEnabled (false);
            dynThresholdSlider_.setEnabled (false);
            dynRangeSlider_.setEnabled (false);
            dynAttackSlider_.setEnabled (false);
            dynReleaseSlider_.setEnabled (false);
        }
        repaint();
    }

    juce::TextButton& getDynEnableButtonForTesting() { return dynEnableButton_; }
    juce::Slider& getDynRangeSliderForTesting() { return dynRangeSlider_; }
    juce::TextButton& getAuditionButtonForTesting() { return *auditionButton_; }
    const std::vector<std::unique_ptr<juce::TextButton>>&
        getPlacementButtonsForTesting() const { return placementButtons_; }

private:
    void configureSlider (juce::Slider& slider, const juce::String& title)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        slider.setRange (0.0, 1.0, 0.0);
        slider.setTitle (title);
        slider.onDragStart = [this, &slider]
        {
            const int index = parameterForSlider (slider);
            if (index >= 0)
                processor_.getParametricEQParameter (index)->beginChangeGesture();
        };
        slider.onDragEnd = [this, &slider]
        {
            const int index = parameterForSlider (slider);
            if (index >= 0)
                processor_.getParametricEQParameter (index)->endChangeGesture();
        };
        slider.onValueChange = [this, &slider]
        {
            const int index = parameterForSlider (slider);
            if (index >= 0)
                processor_.getParametricEQParameter (index)->setValueNotifyingHost (
                    static_cast<float> (slider.getValue()));
        };
        // Exact readouts through the canonical parameter text formatter.
        slider.textFromValueFunction = [this, &slider] (double value)
        {
            const int index = parameterForSlider (slider);
            if (index >= 0)
                return processor_.getParametricEQParameter (index)->getText (
                    static_cast<float> (value), 0);
            return juce::String (value, 3);
        };
    }

    int parameterForSlider (const juce::Slider& slider) const
    {
        const int band = owner_.getSelectedBandForTesting();
        if (band < 0)
            return -1;
        if (&slider == &frequencySlider_)
            return parameterIndex (band, BandParameterOffset::Frequency);
        if (&slider == &gainSlider_)
            return parameterIndex (band, BandParameterOffset::Gain);
        if (&slider == &qSlider_)
            return parameterIndex (band, BandParameterOffset::Q);
        if (&slider == &slopeSlider_)
            return parameterIndex (band, BandParameterOffset::Slope);
        if (&slider == &dynThresholdSlider_)
            return dynamicParameterIndex (band, DynamicBandOffset::Threshold);
        if (&slider == &dynRangeSlider_)
            return dynamicParameterIndex (band, DynamicBandOffset::Range);
        if (&slider == &dynAttackSlider_)
            return dynamicParameterIndex (band, DynamicBandOffset::Attack);
        if (&slider == &dynReleaseSlider_)
            return dynamicParameterIndex (band, DynamicBandOffset::Release);
        return -1;
    }

    void toggleDynamic (DynamicBandOffset offset)
    {
        const int band = owner_.getSelectedBandForTesting();
        if (band < 0)
            return;
        auto* parameter = processor_.getParametricEQParameter (dynamicParameterIndex (
            band, offset));
        notifyParameter (processor_, dynamicParameterIndex (band, offset),
                         parameter->getValue() >= 0.5f ? 0.0f : 1.0f);
        refresh();
    }

    void syncSlider (juce::Slider& slider, int index)
    {
        slider.setValue (processor_.getParametricEQParameter (index)->getValue(),
                         juce::dontSendNotification);
    }

    void toggle (BandParameterOffset offset)
    {
        const int band = owner_.getSelectedBandForTesting();
        if (band < 0)
            return;
        auto* parameter = processor_.getParametricEQParameter (parameterIndex (
            band, offset));
        notifyParameter (processor_, parameterIndex (band, offset),
                         parameter->getValue() >= 0.5f ? 0.0f : 1.0f);
        refresh();
    }

    void timerCallback() override
    {
        refresh();
    }

    ParametricEQEditor& owner_;
    Processor& processor_;
    juce::Viewport viewport_;
    juce::Component content_;
    juce::Label bandLabel_;
    juce::TextButton enableButton_ { "On" };
    juce::TextButton bypassButton_ { "In Path" };
    juce::TextButton closeButton_ { "X" };
    juce::ComboBox shapeCombo_;
    juce::Slider frequencySlider_;
    juce::Slider gainSlider_;
    juce::Slider qSlider_;
    juce::Slider slopeSlider_;
    juce::Label placementLabel_;
    std::unique_ptr<HoldButton> auditionButton_;
    std::vector<std::unique_ptr<juce::TextButton>> placementButtons_;

    // Phase 5 contextual Dynamic EQ controls.
    juce::Label dynLabel_;
    juce::TextButton dynEnableButton_ { "Dynamic Off" };
    juce::Slider dynThresholdSlider_;
    juce::Slider dynRangeSlider_;
    juce::Slider dynAttackSlider_;
    juce::Slider dynReleaseSlider_;
};

//==============================================================================
ParametricEQEditor::ParametricEQEditor (Processor& processor)
    : juce::AudioProcessorEditor (processor),
      processor_ (processor)
{
    setOpaque (true);
    setResizable (true, true);
    topBar_ = std::make_unique<TopBarComponent> (*this, processor_);
    graph_ = std::make_unique<EqGraphComponent> (*this, processor_);
    inspector_ = std::make_unique<InspectorComponent> (*this, processor_);
    addAndMakeVisible (topBar_.get());
    addAndMakeVisible (graph_.get());
    addAndMakeVisible (inspector_.get());
    setSize (900, 620);
    applyLayout();
}

ParametricEQEditor::~ParametricEQEditor()
{
    if (auditionArmed_)
    {
        processor_.endAudition (auditionToken_);
        auditionArmed_ = false;
    }
    processor_.cancelAudition();
}

void ParametricEQEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background());
}

void ParametricEQEditor::resized()
{
    applyLayout();
}

void ParametricEQEditor::applyLayout()
{
    const int width = std::max (1, getWidth());
    const int height = std::max (1, getHeight());

    if (width >= 1400 && height >= 700)
        layoutMode_ = LayoutMode::LargeDesktop;
    else if (width >= 900 && height >= 600)
        layoutMode_ = LayoutMode::Desktop;
    else if (width >= 600 && height >= 500)
        layoutMode_ = LayoutMode::Tablet;
    else if (width >= 430)
        layoutMode_ = LayoutMode::LargePhone;
    else
        layoutMode_ = LayoutMode::CompactPhone;

    auto bounds = getLocalBounds();
    const int topBarHeight = width < 430
                           ? std::min (104, std::max (88, height / 6))
                           : std::min (56, std::max (44, height / 12));
    topBar_->setBounds (bounds.removeFromTop (topBarHeight));

    // Portrait-capable, compact devices use a bottom contextual sheet; the
    // graph stays the dominant surface. Wide layouts use a side inspector.
    const bool portraitCompact = width < 900
        && height >= static_cast<int> (width * 1.05);
    if (portraitCompact)
    {
        const bool hasSelection = selectedBand_ >= 0;
        const int sheetHeight = hasSelection
                              ? std::min (height - topBarHeight - 140,
                                          std::max (240, height * 3 / 5))
                              : std::max (56, height / 12);
        const int graphHeight = std::max (140, height - topBarHeight - sheetHeight);
        graph_->setBounds (bounds.removeFromTop (graphHeight));
        inspector_->setBounds (bounds);
    }
    else
    {
        const int inspectorWidth = std::min (std::max (240, width / 3),
                                             width >= 1400 ? 380 : 320);
        inspector_->setBounds (bounds.removeFromRight (inspectorWidth));
        graph_->setBounds (bounds);
    }
}

void ParametricEQEditor::selectBand (int band)
{
    if (band == selectedBand_)
    {
        inspector_->refresh();
        return;
    }
    if (auditionArmed_ && selectedBand_ != band)
        endAuditionGesture();
    selectedBand_ = band;
    inspector_->refresh();
    applyLayout();
    graph_->repaint();
}

void ParametricEQEditor::setSelectedBandForTesting (int band)
{
    selectBand (band);
}

juce::Rectangle<int> ParametricEQEditor::getNodeBoundsForTesting (int band) const
{
    return graph_->nodeBounds (band);
}

juce::TextButton& ParametricEQEditor::getAuditionButtonForTesting()
{
    return inspector_->getAuditionButtonForTesting();
}

juce::TextButton& ParametricEQEditor::getPhaseButtonForTesting()
{
    return topBar_->getPhaseButtonForTesting();
}

juce::TextButton& ParametricEQEditor::getDynEnableButtonForTesting()
{
    return inspector_->getDynEnableButtonForTesting();
}

juce::Slider& ParametricEQEditor::getDynRangeSliderForTesting()
{
    return inspector_->getDynRangeSliderForTesting();
}

bool ParametricEQEditor::isBandDynamicActiveForTesting (int band) const
{
    return graph_->isBandDynamicActive (band);
}

double ParametricEQEditor::getDisplayedDynamicGainDbForTesting (int band) const
{
    return graph_->displayedDynamicGainDb (band);
}

const std::vector<std::unique_ptr<juce::TextButton>>&
    ParametricEQEditor::getPlacementButtonsForTesting() const
{
    return inspector_->getPlacementButtonsForTesting();
}

juce::Rectangle<int> ParametricEQEditor::getTopBarBoundsForTesting() const
{
    return topBar_->getBounds();
}

juce::Rectangle<int> ParametricEQEditor::getGraphBoundsForTesting() const
{
    return graph_->getBounds();
}

juce::Rectangle<int> ParametricEQEditor::getInspectorBoundsForTesting() const
{
    return inspector_->getBounds();
}

namespace
{
juce::MouseInputSource* testMouseSource (juce::Component* peer)
{
    auto* source = juce::Desktop::getInstance().getMouseSource (0);
    if (source == nullptr && peer != nullptr)
    {
        peer->addToDesktop (0);
        source = juce::Desktop::getInstance().getMouseSource (0);
    }
    return source;
}
} // namespace

void ParametricEQEditor::injectGraphMouseDownForTesting (juce::Point<float> position)
{
    auto* source = testMouseSource (this);
    if (source == nullptr)
        return;
    const juce::MouseEvent event (*source, position, juce::ModifierKeys(),
                                  1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  graph_.get(), graph_.get(),
                                  juce::Time::getCurrentTime(), position,
                                  juce::Time::getCurrentTime(), 1, false);
    graph_->mouseDown (event);
}

void ParametricEQEditor::injectGraphMouseDragForTesting (juce::Point<float> position)
{
    auto* source = testMouseSource (this);
    if (source == nullptr)
        return;
    const juce::MouseEvent event (*source, position, juce::ModifierKeys(),
                                  1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  graph_.get(), graph_.get(),
                                  juce::Time::getCurrentTime(), position,
                                  juce::Time::getCurrentTime(), 1, true);
    graph_->mouseDrag (event);
}

void ParametricEQEditor::injectGraphMouseUpForTesting (juce::Point<float> position)
{
    auto* source = testMouseSource (this);
    if (source == nullptr)
        return;
    const juce::MouseEvent event (*source, position, juce::ModifierKeys(),
                                  1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  graph_.get(), graph_.get(),
                                  juce::Time::getCurrentTime(), position,
                                  juce::Time::getCurrentTime(), 1, false);
    graph_->mouseUp (event);
}

void ParametricEQEditor::beginAuditionGesture()
{
    const int band = selectedBand_;
    if (band < 0 || band >= kMaxBands || auditionArmed_)
        return;
    auditionToken_ = auditionToken_ == 0 ? 1 : auditionToken_ + 1;
    if (processor_.beginAudition (band, auditionToken_))
        auditionArmed_ = true;
}

void ParametricEQEditor::endAuditionGesture()
{
    if (! auditionArmed_)
        return;
    processor_.endAudition (auditionToken_);
    auditionArmed_ = false;
}

} // namespace APEX::ParametricEQ
