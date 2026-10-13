#include "ParametricEQEditor.h"
#include "../ParametricEQCore/ParametricEQSketchCore.h"
#include "../ParametricEQCore/ParametricEQSpectrumGrabCore.h"
#include "../ParametricEQCore/ParametricEQMatchCore.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>

namespace APEX::ParametricEQ
{

namespace
{

// APEX visual identity (mirrors the canonical APEX palette — deep violet
// accent over layered blue-black panels, sky-blue signal curves).
// APEX Prism: graphite/navy depth, cyan response, violet controls and amber
// selection. No bitmap assets or fonts: pixel-perfect native JUCE on HiDPI.
struct Palette
{
    static juce::Colour background() { return juce::Colour (0xff090e19); }
    static juce::Colour surface() { return juce::Colour (0xff121a2b); }
    static juce::Colour surfaceHover() { return juce::Colour (0xff27334d); }
    static juce::Colour border() { return juce::Colour (0xff34415d); }
    static juce::Colour text() { return juce::Colour (0xffeff5ff); }
    static juce::Colour textSecondary() { return juce::Colour (0xff9aabc9); }
    static juce::Colour accent() { return juce::Colour (0xff9b71f7); }
    static juce::Colour accentActive() { return juce::Colour (0xff7544db); }
    static juce::Colour curve() { return juce::Colour (0xff55dff4); }
    static juce::Colour curveMid() { return juce::Colour (0xffffc777); }
    static juce::Colour curveSide() { return juce::Colour (0xff48e1b4); }
    static juce::Colour selected() { return juce::Colour (0xffffd68e); }
    static juce::Colour danger() { return juce::Colour (0xfff16c85); }
    static juce::Colour positive() { return juce::Colour (0xff48e1b4); }
};

// Scoped native design system: buttons, control typography and slider rails.
// Paint occurs exclusively on the JUCE message thread. The singleton is
// immutable after construction and lives longer than any open plugin editor.
class PrismLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PrismLookAndFeel()
    {
        setColour (juce::Slider::textBoxBackgroundColourId, Palette::surface());
        setColour (juce::Slider::textBoxTextColourId, Palette::text());
        setColour (juce::Slider::textBoxOutlineColourId, Palette::border());
        setColour (juce::ComboBox::backgroundColourId, Palette::surfaceHover());
        setColour (juce::ComboBox::textColourId, Palette::text());
        setColour (juce::ComboBox::outlineColourId, Palette::border());
        setColour (juce::PopupMenu::backgroundColourId, Palette::surface());
        setColour (juce::PopupMenu::textColourId, Palette::text());
        setColour (juce::ScrollBar::thumbColourId, Palette::accent().withAlpha (0.75f));
        setColour (juce::TextButton::buttonColourId, Palette::surfaceHover());
        setColour (juce::TextButton::buttonOnColourId, Palette::accentActive());
        setColour (juce::TextButton::textColourOffId, Palette::text());
        setColour (juce::TextButton::textColourOnId, Palette::text());
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& colour, bool hovered,
                               bool down) override
    {
        auto area = button.getLocalBounds().toFloat().reduced (1.0f);
        if (area.isEmpty())
            return;
        const float radius = std::min (9.0f, area.getHeight() * 0.24f);
        const auto base = button.isEnabled() ? colour : Palette::surface();
        const auto top = base.brighter (hovered ? 0.18f : 0.07f);
        const auto bottom = base.darker (down ? 0.22f : 0.08f);
        g.setGradientFill (juce::ColourGradient (
            top, area.getX(), area.getY(), bottom,
            area.getX(), area.getBottom(), false));
        g.fillRoundedRectangle (area, radius);
        g.setColour ((hovered ? Palette::accent() : Palette::border())
                         .withAlpha (button.isEnabled() ? 0.76f : 0.35f));
        g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
        if (down)
        {
            g.setColour (Palette::accent().withAlpha (0.35f));
            g.drawRoundedRectangle (area.reduced (2.0f),
                                    std::max (2.0f, radius - 2.0f), 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool, bool) override
    {
        const float size = std::clamp (button.getHeight() * 0.26f,
                                       10.0f, 12.5f);
        g.setFont (juce::Font (juce::FontOptions (size, juce::Font::bold)));
        g.setColour (Palette::text().withAlpha (
            button.isEnabled() ? 0.97f : 0.43f));
        g.drawFittedText (button.getButtonText(),
                          button.getLocalBounds().reduced (4, 2),
                          juce::Justification::centred, 1, 0.65f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y,
                           int width, int height, float proportion,
                           float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        if (width <= 0 || height <= 0)
            return;
        // Distinct APEX Prism dials for the three primary EQ dimensions.
        // Uses JUCE's canonical rotary hit-testing/parameter machinery.
        const float cx = x + width * 0.5f;
        const float cy = y + std::min (height * 0.49f, 49.0f);
        const float radius = std::max (13.0f,
            std::min (width * 0.32f, height * 0.31f));
        const float angle = startAngle
                          + std::clamp (proportion, 0.0f, 1.0f)
                            * (endAngle - startAngle);
        juce::Path track, value;
        track.addCentredArc (cx, cy, radius, radius, 0.0f,
                             startAngle, endAngle, true);
        value.addCentredArc (cx, cy, radius, radius, 0.0f,
                             startAngle, angle, true);
        g.setColour (Palette::border());
        g.strokePath (track, juce::PathStrokeType (4.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto active = slider.isEnabled()
            ? Palette::accent() : Palette::textSecondary();
        g.setColour (active.withAlpha (0.23f));
        g.strokePath (value, juce::PathStrokeType (8.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (active);
        g.strokePath (value, juce::PathStrokeType (3.2f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto dial = juce::Rectangle<float> (
            cx - radius + 5.0f, cy - radius + 5.0f,
            2.0f * (radius - 5.0f), 2.0f * (radius - 5.0f));
        g.setGradientFill (juce::ColourGradient (
            Palette::surfaceHover().brighter (0.14f), dial.getTopLeft(),
            Palette::background(), dial.getBottomLeft(), false));
        g.fillEllipse (dial);
        g.setColour (Palette::border());
        g.drawEllipse (dial, 1.0f);
        const auto needle = juce::Point<float> (
            cx + (radius - 10.0f) * std::sin (angle),
            cy - (radius - 10.0f) * std::cos (angle));
        g.setColour (Palette::text());
        g.drawLine (cx, cy, needle.x, needle.y, 2.1f);
        g.setColour (Palette::textSecondary());
        g.setFont (juce::Font (juce::FontOptions (
            10.5f, juce::Font::bold)));
        g.drawFittedText (slider.getTitle(),
                          juce::Rectangle<int> (x, y + height - 19,
                                                width, 16),
                          juce::Justification::centred, 1);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width,
                           int height, float sliderPos, float, float,
                           juce::Slider::SliderStyle,
                           juce::Slider& slider) override
    {
        if (width <= 0 || height <= 0)
            return;
        auto label = juce::Rectangle<int> (x + 3, y + 1,
                                           std::max (1, width - 6), 19);
        g.setColour (Palette::textSecondary().withAlpha (
            slider.isEnabled() ? 0.95f : 0.43f));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawFittedText (slider.getTitle(), label,
                          juce::Justification::centredLeft, 1);

        const float left = static_cast<float> (x + 8);
        const float right = static_cast<float> (x + std::max (9, width - 8));
        const float barY = static_cast<float> (y + height) - 13.0f;
        const float valueX = std::clamp (sliderPos, left, right);
        const juce::Rectangle<float> rail (left, barY, right - left, 4.0f);
        g.setColour (Palette::border().withAlpha (0.82f));
        g.fillRoundedRectangle (rail, 2.0f);
        g.setGradientFill (juce::ColourGradient (
            Palette::accent(), left, barY,
            Palette::curve(), right, barY, false));
        g.fillRoundedRectangle (juce::Rectangle<float> (
            left, barY, std::max (1.0f, valueX - left), 4.0f), 2.0f);
        g.setColour (Palette::accent().withAlpha (0.23f));
        g.fillEllipse (valueX - 9.0f, barY - 7.0f, 18.0f, 18.0f);
        g.setColour (slider.isEnabled() ? Palette::text()
                                        : Palette::textSecondary());
        g.fillEllipse (valueX - 5.0f, barY - 3.0f, 10.0f, 10.0f);
    }
};

PrismLookAndFeel& prismLookAndFeel()
{
    static PrismLookAndFeel look;
    return look;
}

// Local APEX clipboard; valid across native EQ editor instances on the
// JUCE message thread. This copies hosted parameter VALUES only, never
// processor/audio objects. The first seven IDs of each band are frozen.
struct NativeBandClipboard
{
    static constexpr int kSlots = 15;
    std::array<float, kSlots> normalised {};
    bool valid = false;
};

struct SharedMatchReference
{
    MatchCapture reference;
    bool valid = false;
};

SharedMatchReference& sharedMatchReference()
{
    // In-process and GUI-message-thread only; a copied capture owns all of
    // its data, not the analyzer snapshot or a processor pointer.
    static SharedMatchReference shared;
    return shared;
}

NativeBandClipboard& nativeBandClipboard()
{
    static NativeBandClipboard clipboard;
    return clipboard;
}

int clipboardParameterIndex (int band, int slot) noexcept
{
    if (slot < 7)
        return parameterIndex (band, static_cast<BandParameterOffset> (slot));
    if (slot == 7)
        return placementParameterIndex (band);
    if (slot < 13)
        return dynamicParameterIndex (
            band, static_cast<DynamicBandOffset> (slot - 8));
    if (slot == 13)
        return dynamicFilterParameterIndex (band);
    return spectralBandParameterIndex (band);
}

void notifyParameter (Processor& processor, int index, float normalised)
{
    auto* parameter = processor.getParametricEQParameter (index);
    if (parameter == nullptr)
        return;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (normalised);
    parameter->endChangeGesture();
}

// Creation paths (Sketch/Grab/Match) never inherit latent bypass, placement,
// broadband dynamics or spectral enables from a previously disabled slot.
// Clipboard Paste is different: it explicitly restores all copied controls.
void resetCreatedBand (Processor& processor, int band)
{
    notifyParameter (processor,
        parameterIndex (band, BandParameterOffset::Bypass), 0.0f);
    notifyParameter (processor, placementParameterIndex (band), 0.0f);
    notifyParameter (processor,
        dynamicParameterIndex (band, DynamicBandOffset::Enable), 0.0f);
    notifyParameter (processor, dynamicFilterParameterIndex (band), 0.0f);
    notifyParameter (processor, spectralBandParameterIndex (band), 0.0f);
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

// On unusually shallow landscape canvases the seven >=44px graph tools
// occupy the top row. Without reserving a plot-safe inset, the actual band
// handle at +8dB falls BEHIND the MATCH button (verified in native 640x400
// Windows screenshot). Keep the inverse pointer transform exactly paired
// with the display transform; this changes no EQ DSP or hosted parameter.
juce::Range<double> gainPlotYRange (double height)
{
    const double h = std::max (1.0, height);
    return h <= 200.0 ? juce::Range<double> (68.0, std::max (69.0, h - 20.0))
                      : juce::Range<double> (0.0, h);
}

double gainForY (double y, double height)
{
    constexpr double maximumDb = 18.0;
    const auto plot = gainPlotYRange (height);
    const auto ratio = std::clamp ((y - plot.getStart()) / plot.getLength(),
                                   0.0, 1.0);
    return maximumDb * (1.0 - 2.0 * ratio);
}

double yForGain (double gainDb, double height)
{
    constexpr double maximumDb = 18.0;
    const auto plot = gainPlotYRange (height);
    const auto ratio = 0.5 * (1.0 - std::clamp (gainDb / maximumDb, -1.0, 1.0));
    return plot.getStart() + ratio * plot.getLength();
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
        title_.setText ("APEX / EQ", juce::dontSendNotification);
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

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (
            Palette::surfaceHover().darker (0.28f), area.getTopLeft(),
            Palette::background(), area.getBottomLeft(), false));
        g.fillRect (area);
        g.setColour (Palette::accent().withAlpha (0.9f));
        g.fillRect (0.0f, 0.0f, std::min (4.0f, area.getWidth()),
                    area.getHeight());
        g.setColour (Palette::border().withAlpha (0.70f));
        g.drawLine (0.0f, area.getBottom() - 0.5f,
                    area.getRight(), area.getBottom() - 0.5f);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (4, 4);
        if (getWidth() < 650)
        {
            // Phone/small-tablet contract: two rows of four controls, each at
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
        title_.setText (titleWidth >= 210 ? "APEX / PRISM EQ"
                                          : "APEX / EQ",
                        juce::dontSendNotification);
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
        setInterceptsMouseClicks (true, true);
        addAndMakeVisible (sketchButton_);
        addAndMakeVisible (grabButton_);
        addAndMakeVisible (copyButton_);
        addAndMakeVisible (pasteButton_);
        addAndMakeVisible (matchButton_);
        addAndMakeVisible (matchSaveButton_);
        addAndMakeVisible (matchLoadButton_);
        sketchButton_.setTitle ("Draw an EQ curve and convert it to APEX bands");
        sketchButton_.setClickingTogglesState (true);
        sketchButton_.setColour (juce::TextButton::buttonColourId,
                                 Palette::surfaceHover());
        sketchButton_.onClick = [this]
        {
            sketchArmed_ = sketchButton_.getToggleState();
            if (sketchArmed_)
            {
                clearGrabPreview();
                resetMatchCapture();
            }
            if (! sketchArmed_)
            {
                sketching_ = false;
                sketchPointCount_ = 0;
            }
            sketchButton_.setButtonText (sketchArmed_ ? "SKETCH ON" : "SKETCH");
            sketchButton_.setColour (
                juce::TextButton::buttonColourId,
                sketchArmed_ ? Palette::accentActive() : Palette::surfaceHover());
            repaint();
        };

        grabButton_.setTitle ("Freeze PRE-EQ resonances; tap a marker to cut");
        grabButton_.setColour (juce::TextButton::buttonColourId,
                               Palette::surfaceHover());
        grabButton_.onClick = [this]
        {
            if (grabArmed_)
            {
                clearGrabPreview();
                repaint();
                return;
            }
            if (processor_.getAnalyzerMode()
                == APEX::Analysis::SpectrumTapMode::Closed)
                return;
            auto snapshot = processor_.getAnalyzer().acquireSnapshot();
            // Require the actual pre-EQ feed; post-EQ resonance suggestions
            // would be biased by the very cuts the producer is making.
            if (! snapshot.isValid() || ! snapshot.preActive()
                || snapshot.preDb() == nullptr)
                return;
            grabPlan_ = SpectrumGrab::analyze (snapshot.preDb(),
                APEX::Analysis::SpectrumAnalyzerCore::kSpectrumBins,
                snapshot.binHz());
            grabArmed_ = grabPlan_.count > 0;
            if (grabArmed_)
                resetMatchCapture();
            matchButton_.setVisible (! grabArmed_);
            matchSaveButton_.setVisible (! grabArmed_);
            matchLoadButton_.setVisible (! grabArmed_);
            grabButton_.setButtonText (grabArmed_ ? "GRAB ON" : "NO PEAKS");
            grabButton_.setColour (juce::TextButton::buttonColourId,
                grabArmed_ ? Palette::accentActive() : Palette::surfaceHover());
            if (grabArmed_ && sketchArmed_)
            {
                sketchArmed_ = false;
                sketching_ = false;
                sketchPointCount_ = 0;
                sketchButton_.setToggleState (false, juce::dontSendNotification);
                sketchButton_.setButtonText ("SKETCH");
                sketchButton_.setColour (juce::TextButton::buttonColourId,
                                        Palette::surfaceHover());
            }
            repaint();
        };
        copyButton_.setTitle ("Copy all selected band parameters");
        pasteButton_.setTitle ("Paste as a new band without overwriting");
        copyButton_.setColour (juce::TextButton::buttonColourId,
                              Palette::surfaceHover());
        pasteButton_.setColour (juce::TextButton::buttonColourId,
                               Palette::surfaceHover());
        copyButton_.onClick = [this] { copySelectedBand(); };
        pasteButton_.onClick = [this] { pasteNewBand(); };
        matchButton_.setTitle (
            "EQ Match: collect PRE spectrum of reference, then current input; "
            "apply native fitted APEX bands after both are captured");
        matchButton_.setColour (juce::TextButton::buttonColourId,
                                Palette::surfaceHover());
        matchButton_.onClick = [this] { advanceMatch(); };
        matchSaveButton_.setTitle (
            "Save the measured PRE-EQ reference for other APEX EQ instances "
            "in this running application");
        matchLoadButton_.setTitle (
            "Use the reference previously shared by another APEX EQ instance");
        matchSaveButton_.onClick = [this]
        {
            if (! matchReference_.ready())
                return;
            auto& shared = sharedMatchReference();
            shared.reference = matchReference_;
            shared.valid = true;
            matchSaveButton_.setButtonText ("SAVED");
        };
        matchLoadButton_.onClick = [this]
        {
            const auto& shared = sharedMatchReference();
            if (! shared.valid || ! shared.reference.ready())
                return;
            matchReference_ = shared.reference;
            matchInput_.reset();
            matchStage_ = MatchStage::ReferenceReady;
            lastMatchSpectrumIndex_ = lastSpectrumIndex_;
            matchButton_.setButtonText ("GET INPUT");
            matchButton_.setColour (juce::TextButton::buttonColourId,
                                    Palette::positive());
            repaint();
        };
        startTimerHz (25);
    }

    // The offscreen CI screenshot test pulls an already-published response
    // synchronously on the message thread. Production still uses the timer.
    void ingestPublishedResponseForTesting()
    {
        ResponseFrame frame;
        if (processor_.getResponseCore().consumeLatest (frame))
        {
            latestFrame_ = frame;
            curveDirty_ = true;
        }
    }

    void resized() override
    {
        curveDirty_ = true;
        // A single touch-safe tool ribbon replaces the previous two rows
        // that covered ~100 px of a 142 px graph on 320px phones. Every
        // action remains accessible; 7 * 44 + 2 * 6 == 320 exactly.
        constexpr int padding = 6;
        const int width = std::max (1, getWidth());
        const int cell = std::clamp (
            (width - 2 * padding) / 7,
            ParametricEQEditor::kTouchTarget, 110);
        std::array<juce::TextButton*, 7> actions {{
            &sketchButton_, &grabButton_, &copyButton_, &pasteButton_,
            &matchButton_, &matchSaveButton_, &matchLoadButton_
        }};
        // Seven controls fit exactly at 320px, but long captions become
        // unreadable when JUCE shrinks the font. Shorten only the idle tool
        // captions on compact widths; active EQ Match / Save state labels
        // are preserved so resize cannot erase the current workflow.
        const bool compact = width <= 430;
        if (matchButton_.getButtonText() == "EQ MATCH"
            || matchButton_.getButtonText() == "MATCH")
            matchButton_.setButtonText (compact ? "MATCH" : "EQ MATCH");
        if (matchSaveButton_.getButtonText() == "REF SAVE"
            || matchSaveButton_.getButtonText() == "SAVE")
            matchSaveButton_.setButtonText (compact ? "SAVE" : "REF SAVE");
        if (matchLoadButton_.getButtonText() == "REF LOAD"
            || matchLoadButton_.getButtonText() == "LOAD")
            matchLoadButton_.setButtonText (compact ? "LOAD" : "REF LOAD");
        int x = padding;
        for (auto* action : actions)
        {
            action->setBounds (x, 8, cell, ParametricEQEditor::kTouchTarget);
            x += cell;
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff19243b), bounds.getTopLeft(),
            juce::Colour (0xff090f20), bounds.getBottomLeft(), false));
        g.fillRect (bounds);

        // Frequency-domain graph: quiet grid, strong zero-dB reference,
        // precision analyser, soft coloured response area and lit nodes.
        drawGrid (g, bounds);
        if (curveDirty_)
        {
            rebuildCurves (bounds);
            curveDirty_ = false;
        }
        if (spectrumActive_)
            drawSpectrum (g, bounds);

        if (! mainCurve_.isEmpty())
        {
            juce::Path fill (mainCurve_);
            fill.lineTo (bounds.getRight(), bounds.getBottom());
            fill.lineTo (bounds.getX(), bounds.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (
                Palette::curve().withAlpha (0.22f), bounds.getTopLeft(),
                Palette::curve().withAlpha (0.01f), bounds.getBottomLeft(),
                false));
            g.fillPath (fill);
        }
        g.setColour (Palette::curve().withAlpha (0.16f));
        g.strokePath (mainCurve_, juce::PathStrokeType (
            8.0f, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
        g.setColour (Palette::curve());
        g.strokePath (mainCurve_, juce::PathStrokeType (
            2.4f, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
        if (mixedPlacements_)
        {
            g.setColour (Palette::curveMid().withAlpha (0.85f));
            g.strokePath (midCurve_, juce::PathStrokeType (1.6f));
            g.setColour (Palette::curveSide().withAlpha (0.88f));
            g.strokePath (sideCurve_, juce::PathStrokeType (1.6f));
        }

        if (sketching_ && sketchPointCount_ > 1)
        {
            juce::Path sketch;
            for (int i = 0; i < sketchPointCount_; ++i)
            {
                const auto& point = sketchPoints_[static_cast<std::size_t> (i)];
                const auto x = static_cast<float> (point.x * bounds.getWidth());
                const auto y = static_cast<float> (
                    yForGain (point.gainDb, bounds.getHeight()));
                if (i == 0)
                    sketch.startNewSubPath (x, y);
                else
                    sketch.lineTo (x, y);
            }
            g.setColour (Palette::selected().withAlpha (0.95f));
            g.strokePath (sketch, juce::PathStrokeType (
                3.0f, juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }
        drawNodes (g, bounds);
        // APEX's own compact value readout follows the chosen frequency
        // node without additional controls or stealing graph pointer input.
        if (getWidth() >= 700)
        {
            const int selectedBand = owner_.getSelectedBandForTesting();
            if (selectedBand >= 0 && selectedBand < kMaxBands)
            {
                const auto centre = nodeCentre (selectedBand);
                const auto* frequency = processor_.getParametricEQParameter (
                    parameterIndex (selectedBand, BandParameterOffset::Frequency));
                const auto* gain = processor_.getParametricEQParameter (
                    parameterIndex (selectedBand, BandParameterOffset::Gain));
                const auto* q = processor_.getParametricEQParameter (
                    parameterIndex (selectedBand, BandParameterOffset::Q));
                const juce::String readout = frequency->getText (
                    frequency->getValue(), 0) + "     "
                    + gain->getText (gain->getValue(), 0) + "     "
                    + q->getText (q->getValue(), 0);
                constexpr int bubbleWidth = 288;
                constexpr int bubbleHeight = 32;
                const int x = std::clamp (
                    centre.x - bubbleWidth / 2, 8,
                    std::max (8, getWidth() - bubbleWidth - 8));
                const int y = centre.y > 100 ? centre.y - 65
                                             : centre.y + 36;
                auto bubble = juce::Rectangle<float> (
                    static_cast<float> (x), static_cast<float> (y),
                    static_cast<float> (bubbleWidth),
                    static_cast<float> (bubbleHeight));
                g.setColour (Palette::background().withAlpha (0.88f));
                g.fillRoundedRectangle (bubble, 9.0f);
                g.setColour (Palette::accent().withAlpha (0.66f));
                g.drawRoundedRectangle (bubble, 9.0f, 1.0f);
                g.setColour (Palette::text());
                g.setFont (juce::Font (juce::FontOptions (
                    12.0f, juce::Font::bold)));
                g.drawFittedText (readout, bubble.toNearestInt().reduced (9, 3),
                                  juce::Justification::centred, 1);
            }
        }
        if (grabArmed_)
            drawGrabPeaks (g);

        // Never steal input events: this is purely painted status text.
        if (getWidth() >= 560)
        {
            g.setFont (juce::Font (juce::FontOptions (
                10.5f, juce::Font::bold)));
            g.setColour (Palette::textSecondary().withAlpha (0.88f));
            g.drawFittedText ("APEX  /  24-BAND PRECISION ENGINE",
                juce::Rectangle<int> (std::max (330, getWidth() - 264),
                                       std::max (12, getHeight() - 54),
                                       std::min (250, getWidth() - 340), 24),
                juce::Justification::centredRight, 1);
        }
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
        if (hit < 0 && grabArmed_ && useGrabPeak (event.position))
        {
            longPressFired_ = true;
            return;
        }
        if (sketchArmed_ && hit < 0)
        {
            sketching_ = true;
            longPressFired_ = true; // sketch owns this pointer lease
            sketchPointCount_ = 0;
            appendSketchPoint (event.position);
            owner_.selectBand (-1);
            repaint();
            return;
        }
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

    // Direct, canvas-first creation: double-click any empty frequency/gain
    // position to create a new hosted APEX band with the real target values.
    // This complements long-press (touch) and the existing Sketch/Grab tools.
    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (sketchArmed_ || grabArmed_)
            return;
        pointerDown_ = false;
        longPressTarget_.reset();
        const int existing = hitTest (event.position);
        if (existing >= 0)
        {
            owner_.selectBand (existing);
            return;
        }
        const auto slot = firstDisabledBand();
        if (! slot.has_value())
            return;
        const int band = *slot;
        resetCreatedBand (processor_, band);
        const auto applyBand = [this, band] (BandParameterOffset offset,
                                            float value)
        {
            const int index = parameterIndex (band, offset);
            auto* parameter = processor_.getParametricEQParameter (index);
            notifyParameter (processor_, index, parameter->toNormalised (value));
        };
        applyBand (BandParameterOffset::Shape,
                   static_cast<float> (FilterShape::Bell));
        applyBand (BandParameterOffset::Frequency,
                   static_cast<float> (frequencyForX (
                       event.position.x, std::max (1, getWidth()))));
        applyBand (BandParameterOffset::Gain,
                   static_cast<float> (gainForY (
                       event.position.y, std::max (1, getHeight()))));
        applyBand (BandParameterOffset::Q, 1.0f);
        if (event.mods.isAltDown())
        {
            const int index = dynamicParameterIndex (
                band, DynamicBandOffset::Enable);
            notifyParameter (processor_, index, 1.0f);
        }
        applyBand (BandParameterOffset::Enabled, 1.0f);
        owner_.selectBand (band);
        curveDirty_ = true;
        repaint();
    }

    // Mouse wheel over a node adjusts its hosted Q with balanced gestures.
    // Shift provides fine resolution. No change to graph pan/zoom behaviour
    // outside nodes, and the same Q slider remains accessible to touch.
    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        const int band = hitTest (event.position);
        if (band < 0 || std::abs (wheel.deltaY) <= 1.0e-6f)
            return;
        const int index = parameterIndex (band, BandParameterOffset::Q);
        auto* parameter = processor_.getParametricEQParameter (index);
        const float precision = event.mods.isShiftDown() ? 0.012f : 0.065f;
        const float next = std::clamp (
            parameter->getValue() + wheel.deltaY * precision, 0.0f, 1.0f);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (next);
        parameter->endChangeGesture();
        curveDirty_ = true;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        lastDragPosition_ = event.position;
        if (sketching_)
        {
            appendSketchPoint (event.position);
            repaint();
            return;
        }
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
            // All layout computations are GUI-thread-only. Relocate the
            // floating HUD at most 30Hz while a node moves so its old
            // position cannot swallow the next pointer gesture.
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastHudRelayoutMs_ >= 33)
            {
                owner_.resized();
                lastHudRelayoutMs_ = now;
            }
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        pointerDown_ = false;
        if (sketching_)
        {
            commitSketch();
            return;
        }
        if (dragging_ && dragBand_ >= 0)
        {
            auto* frequency = processor_.getParametricEQParameter (parameterIndex (
                dragBand_, BandParameterOffset::Frequency));
            auto* gain = processor_.getParametricEQParameter (parameterIndex (
                dragBand_, BandParameterOffset::Gain));
            frequency->endChangeGesture();
            gain->endChangeGesture();
            owner_.resized(); // final HUD anchor at the committed node
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
    juce::TextButton& getSketchButtonForTesting() noexcept { return sketchButton_; }
    juce::TextButton& getGrabButtonForTesting() noexcept { return grabButton_; }
    juce::TextButton& getCopyButtonForTesting() noexcept { return copyButton_; }
    juce::TextButton& getPasteButtonForTesting() noexcept { return pasteButton_; }
    juce::TextButton& getMatchButtonForTesting() noexcept { return matchButton_; }
    juce::TextButton& getMatchSaveButtonForTesting() noexcept
    {
        return matchSaveButton_;
    }
    juce::TextButton& getMatchLoadButtonForTesting() noexcept
    {
        return matchLoadButton_;
    }
    void feedMatchSpectrumForTesting (const float* db, int bins, double binHz)
    {
        captureMatchFrame (db, bins, binHz);
    }
    void setGrabSpectrumForTesting (const float* db, int bins, double binHz)
    {
        grabPlan_ = SpectrumGrab::analyze (db, bins, binHz);
        grabArmed_ = grabPlan_.count > 0;
        grabButton_.setButtonText (grabArmed_ ? "GRAB ON" : "NO PEAKS");
        grabButton_.setColour (juce::TextButton::buttonColourId,
            grabArmed_ ? Palette::accentActive() : Palette::surfaceHover());
        repaint();
    }
    juce::Rectangle<int> getGrabPeakBoundsForTesting (int index) const
    {
        return ! grabArmed_ || index < 0 || index >= grabPlan_.count ? juce::Rectangle<int>()
            : grabPeakBounds (grabPlan_.peaks[static_cast<std::size_t> (index)]);
    }

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
                if (matchStage_ == MatchStage::CapturingReference
                    || matchStage_ == MatchStage::CapturingInput)
                {
                    if (snapshot.discontinuous())
                    {
                        // Seeking or queue loss breaks measurement continuity.
                        if (matchStage_ == MatchStage::CapturingReference)
                            matchReference_.reset();
                        else
                            matchInput_.reset();
                    }
                    if (snapshot.preActive() && snapshot.preDb() != nullptr
                        && snapshot.index() != lastMatchSpectrumIndex_)
                    {
                        lastMatchSpectrumIndex_ = snapshot.index();
                        captureMatchFrame (snapshot.preDb(),
                            APEX::Analysis::SpectrumAnalyzerCore::kSpectrumBins,
                            snapshot.binHz());
                    }
                }
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


    enum class MatchStage
    {
        Idle, CapturingReference, ReferenceReady,
        CapturingInput, InputReady
    };

    void resetMatchCapture()
    {
        matchReference_.reset();
        matchInput_.reset();
        matchStage_ = MatchStage::Idle;
        lastMatchSpectrumIndex_ = -1;
        // Match the idle captions to the actual graph width. A phone EQ
        // reset must not restore desktop-length labels on 44px buttons.
        const bool compact = getWidth() <= 430;
        matchButton_.setButtonText (compact ? "MATCH" : "EQ MATCH");
        matchSaveButton_.setButtonText (compact ? "SAVE" : "REF SAVE");
        matchButton_.setColour (juce::TextButton::buttonColourId,
                                Palette::surfaceHover());
    }

    void captureMatchFrame (const float* db, int bins, double binHz)
    {
        MatchCapture* capture = nullptr;
        if (matchStage_ == MatchStage::CapturingReference)
            capture = &matchReference_;
        else if (matchStage_ == MatchStage::CapturingInput)
            capture = &matchInput_;
        if (capture == nullptr || ! capture->append (db, bins, binHz))
            return;

        if (capture->ready())
        {
            if (matchStage_ == MatchStage::CapturingReference)
            {
                matchStage_ = MatchStage::ReferenceReady;
                matchButton_.setButtonText ("GET INPUT");
            }
            else
            {
                matchStage_ = MatchStage::InputReady;
                matchButton_.setButtonText ("APPLY EQ");
            }
            matchButton_.setColour (juce::TextButton::buttonColourId,
                                    Palette::positive());
        }
        repaint();
    }

    void advanceMatch()
    {
        if (matchStage_ == MatchStage::CapturingReference
            || matchStage_ == MatchStage::CapturingInput)
        {
            // A second press cancels instead of accidentally advancing.
            resetMatchCapture();
            return;
        }

        if (matchStage_ == MatchStage::InputReady)
        {
            const auto plan = MatchEngine::fit (matchReference_, matchInput_);
            int lastBand = -1;
            for (int proposal = 0;
                 proposal < std::min (plan.count, 8); ++proposal)
            {
                const auto slot = firstDisabledBand();
                if (! slot.has_value())
                    break;
                const int band = *slot;
                resetCreatedBand (processor_, band);
                const auto& suggestion =
                    plan.bands[static_cast<std::size_t> (proposal)];
                auto setUnits = [this, band] (BandParameterOffset offset,
                                              float value)
                {
                    const int id = parameterIndex (band, offset);
                    auto* parameter = processor_.getParametricEQParameter (id);
                    notifyParameter (processor_, id,
                                     parameter->toNormalised (value));
                };
                setUnits (BandParameterOffset::Shape,
                    static_cast<float> (suggestion.shape));
                setUnits (BandParameterOffset::Frequency,
                    static_cast<float> (suggestion.frequencyHz));
                setUnits (BandParameterOffset::Gain,
                    static_cast<float> (std::clamp (
                        suggestion.gainDb, -9.0, 9.0)));
                setUnits (BandParameterOffset::Q,
                    static_cast<float> (suggestion.q));
                notifyParameter (processor_, parameterIndex (
                    band, BandParameterOffset::Enabled), 1.0f);
                lastBand = band;
            }
            resetMatchCapture();
            curveDirty_ = true;
            if (lastBand >= 0)
                owner_.selectBand (lastBand);
            repaint();
            return;
        }

        // Never assume the analyzer was active or generate synthetic data.
        if ((static_cast<int> (processor_.getAnalyzerMode()) & 1) == 0)
        {
            matchButton_.setButtonText ("PRE OFF");
            return;
        }

        if (matchStage_ == MatchStage::ReferenceReady)
        {
            matchInput_.reset();
            matchStage_ = MatchStage::CapturingInput;
            matchButton_.setButtonText ("INPUT...");
        }
        else
        {
            matchReference_.reset();
            matchInput_.reset();
            matchStage_ = MatchStage::CapturingReference;
            matchButton_.setButtonText ("REF...");
        }
        lastMatchSpectrumIndex_ = lastSpectrumIndex_;
        if (grabArmed_)
            clearGrabPreview();
        if (sketchArmed_)
        {
            sketchArmed_ = false;
            sketching_ = false;
            sketchPointCount_ = 0;
            sketchButton_.setToggleState (false, juce::dontSendNotification);
            sketchButton_.setButtonText ("SKETCH");
            sketchButton_.setColour (juce::TextButton::buttonColourId,
                                    Palette::surfaceHover());
        }
        matchButton_.setColour (juce::TextButton::buttonColourId,
                                Palette::accentActive());
        repaint();
    }

    void copySelectedBand()
    {
        const int source = owner_.getSelectedBandForTesting();
        if (source < 0 || source >= kMaxBands)
            return;
        auto* enabled = processor_.getParametricEQParameter (
            parameterIndex (source, BandParameterOffset::Enabled));
        if (enabled == nullptr || ! enabled->getBool())
            return;

        auto& clipboard = nativeBandClipboard();
        clipboard.valid = false;
        for (int slot = 0; slot < NativeBandClipboard::kSlots; ++slot)
        {
            const auto* parameter = processor_.getParametricEQParameter (
                clipboardParameterIndex (source, slot));
            if (parameter == nullptr)
                return;
            clipboard.normalised[static_cast<std::size_t> (slot)]
                = parameter->getValue();
        }
        clipboard.valid = true;
    }

    void pasteNewBand()
    {
        const auto& clipboard = nativeBandClipboard();
        if (! clipboard.valid)
            return;
        const auto free = firstDisabledBand();
        if (! free.has_value())
            return;
        const int band = *free;
        // The Enabled flag is LAST so no intermediate half-configured
        // band gets switched on. Every edit goes through the APEX host
        // automation gesture bridge rather than manipulating DSP state.
        for (int slot = 1; slot < NativeBandClipboard::kSlots; ++slot)
            notifyParameter (processor_, clipboardParameterIndex (band, slot),
                clipboard.normalised[static_cast<std::size_t> (slot)]);
        notifyParameter (processor_, parameterIndex (
            band, BandParameterOffset::Enabled), 1.0f);
        clearGrabPreview();
        owner_.selectBand (band);
        curveDirty_ = true;
        repaint();
    }

    juce::Rectangle<int> grabPeakBounds (const GrabPeak& peak) const
    {
        const int x = static_cast<int> (std::lround (
            xForFrequency (peak.frequencyHz, std::max (1, getWidth()))));
        return { x - ParametricEQEditor::kTouchTarget / 2, 58,
                 ParametricEQEditor::kTouchTarget,
                 ParametricEQEditor::kTouchTarget };
    }

    void clearGrabPreview()
    {
        grabArmed_ = false;
        matchButton_.setVisible (true);
        matchSaveButton_.setVisible (true);
        matchLoadButton_.setVisible (true);
        grabPlan_ = {};
        grabButton_.setButtonText ("GRAB");
        grabButton_.setColour (juce::TextButton::buttonColourId,
                              Palette::surfaceHover());
    }

    void drawGrabPeaks (juce::Graphics& g)
    {
        for (int i = 0; i < grabPlan_.count; ++i)
        {
            const auto& peak = grabPlan_.peaks[static_cast<std::size_t> (i)];
            const auto marker = grabPeakBounds (peak);
            const auto centre = marker.getCentre().toFloat();
            g.setColour (Palette::selected().withAlpha (0.85f));
            g.fillEllipse (centre.x - 7.0f, centre.y - 7.0f, 14.0f, 14.0f);
            g.setColour (Palette::background());
            g.drawEllipse (centre.x - 7.0f, centre.y - 7.0f, 14.0f,
                           14.0f, 1.5f);
            g.setColour (Palette::text());
            const juce::String label = peak.frequencyHz < 1000.0
                ? juce::String (static_cast<int> (std::lround (peak.frequencyHz))) + " Hz"
                : juce::String (peak.frequencyHz / 1000.0, 1) + "k";
            g.drawFittedText (label, marker.withY (102).withHeight (20),
                              juce::Justification::centred, 1);
        }
    }

    bool useGrabPeak (juce::Point<float> position)
    {
        for (int i = 0; i < grabPlan_.count; ++i)
        {
            const auto& peak = grabPlan_.peaks[static_cast<std::size_t> (i)];
            if (! grabPeakBounds (peak).toFloat().contains (position))
                continue;
            const auto slot = firstDisabledBand();
            if (! slot.has_value())
                return false;
            const int band = *slot;
            resetCreatedBand (processor_, band);
            auto setBandUnits = [this, band] (BandParameterOffset offset,
                                             float units)
            {
                const auto id = parameterIndex (band, offset);
                auto* parameter = processor_.getParametricEQParameter (id);
                notifyParameter (processor_, id,
                                 parameter->toNormalised (units));
            };
            setBandUnits (BandParameterOffset::Shape,
                          static_cast<float> (FilterShape::Bell));
            setBandUnits (BandParameterOffset::Frequency,
                          static_cast<float> (peak.frequencyHz));
            setBandUnits (BandParameterOffset::Gain, -3.0f);
            setBandUnits (BandParameterOffset::Q,
                          static_cast<float> (peak.suggestedQ));
            notifyParameter (processor_, parameterIndex (
                band, BandParameterOffset::Enabled), 1.0f);
            clearGrabPreview();  // one-shot; never mutates existing bands
            owner_.selectBand (band);
            curveDirty_ = true;
            repaint();
            return true;
        }
        return false;
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (int step = -18; step <= 18; step += 6)
        {
            const float y = static_cast<float> (
                yForGain (step, bounds.getHeight()));
            const bool reference = step == 0;
            g.setColour ((reference ? Palette::curve() : Palette::border())
                             .withAlpha (reference ? 0.35f : 0.38f));
            g.drawHorizontalLine (static_cast<int> (y),
                                  bounds.getX(), bounds.getRight());
            g.setColour (Palette::textSecondary().withAlpha (
                reference ? 0.89f : 0.58f));
            const auto label = step > 0 ? "+" + juce::String (step)
                                      : juce::String (step);
            g.drawFittedText (label + " dB",
                              juce::Rectangle<int> (6,
                                  std::max (0, static_cast<int> (y) - 14),
                                  48, 13),
                              juce::Justification::centredLeft, 1);
        }
        for (const int frequency : { 20, 50, 100, 200, 500, 1000, 2000, 5000,
                                     10000, 20000 })
        {
            const float x = static_cast<float> (
                xForFrequency (frequency, bounds.getWidth()));
            g.setColour (Palette::border().withAlpha (0.35f));
            g.drawVerticalLine (static_cast<int> (x), bounds.getY(),
                                bounds.getBottom());
            g.setColour (Palette::textSecondary().withAlpha (0.64f));
            const auto frequencyText = frequency >= 1000
                ? juce::String (frequency / 1000) + "k"
                : juce::String (frequency);
            const int labelX = std::clamp (static_cast<int> (x) - 21,
                                           0, std::max (0, getWidth() - 43));
            g.drawFittedText (frequencyText,
                juce::Rectangle<int> (labelX,
                    std::max (0, getHeight() - 20), 43, 15),
                juce::Justification::centred, 1);
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
        constexpr int capacity = basePoints + kMaxBands * criticalInsertions;
        std::array<double, capacity> frequencies {};
        int count = 0;
        for (int point = 0; point < basePoints; ++point)
            frequencies[static_cast<std::size_t> (count++)] = frequencyForX (
                static_cast<double> (point) / (basePoints - 1) * width, width);

        // All path vertices must be in logarithmic X order. The old code
        // appended band-centred detail AFTER the 20-20k sweep, drawing long
        // zigzagging connections across the response graph. Inserting the
        // detail into a fixed array and sorting keeps sharp Q peaks precise
        // without allocations, and makes the cyan fill a valid polygon.
        for (int band = 0; band < kMaxBands; ++band)
        {
            const auto& settings = latestFrame_.currentBands[
                static_cast<std::size_t> (band)];
            if (! settings.enabled || settings.bypassed)
                continue;
            for (int step = 0; step < criticalInsertions; ++step)
            {
                const double value = settings.frequencyHz * std::pow (
                    2.0, (step - criticalInsertions / 2) / 8.0);
                if (std::isfinite (value)
                    && value >= kDisplayMinimumFrequency
                    && value <= kDisplayMaximumFrequency)
                    frequencies[static_cast<std::size_t> (count++)] = value;
            }
        }
        std::sort (frequencies.begin(), frequencies.begin() + count);
        bool haveMain = false, haveMid = false, haveSide = false;

        for (int point = 0; point < count; ++point)
        {
            const double frequency = frequencies[
                static_cast<std::size_t> (point)];
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
            const bool selected = band == owner_.getSelectedBandForTesting();
            const auto placement = static_cast<ChannelPlacement> (
                processor_.getParametricEQParameter (
                    placementParameterIndex (band))->getChoiceIndex());
            auto bandColour = Palette::accent();
            if (placement == ChannelPlacement::Left)
                bandColour = Palette::curve();
            else if (placement == ChannelPlacement::Right)
                bandColour = juce::Colour (0xffff8eb4);
            else if (placement == ChannelPlacement::Mid)
                bandColour = Palette::curveMid();
            else if (placement == ChannelPlacement::Side)
                bandColour = Palette::curveSide();
            const auto colour = bypassed->getBool()
                ? Palette::textSecondary()
                : selected ? Palette::selected() : bandColour;
            const float radius = selected ? 12.0f : 10.0f;
            g.setColour (colour.withAlpha (selected ? 0.21f : 0.12f));
            g.fillEllipse (centre.x - radius - 6.0f,
                           centre.y - radius - 6.0f,
                           2.0f * (radius + 6.0f),
                           2.0f * (radius + 6.0f));
            g.setColour (Palette::background());
            g.fillEllipse (centre.x - radius, centre.y - radius,
                           radius * 2.0f, radius * 2.0f);
            g.setColour (colour.withAlpha (bypassed->getBool() ? 0.52f : 1.0f));
            g.drawEllipse (centre.x - radius, centre.y - radius,
                           radius * 2.0f, radius * 2.0f, selected ? 2.3f : 1.8f);
            g.setFont (juce::Font (juce::FontOptions (
                10.0f, juce::Font::bold)));
            g.setColour (Palette::text());
            g.drawFittedText (juce::String (band + 1),
                juce::Rectangle<int> (
                    static_cast<int> (centre.x - radius),
                    static_cast<int> (centre.y - radius),
                    static_cast<int> (radius * 2.0f),
                    static_cast<int> (radius * 2.0f)),
                juce::Justification::centred, 1);
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

    void appendSketchPoint (juce::Point<float> position)
    {
        const double width = std::max (1, getWidth());
        const double height = std::max (1, getHeight());
        SketchPoint point {
            std::clamp (static_cast<double> (position.x) / width, 0.0, 1.0),
            std::clamp (gainForY (position.y, height), -18.0, 18.0)
        };

        if (sketchPointCount_ > 0)
        {
            const auto& previous = sketchPoints_[
                static_cast<std::size_t> (sketchPointCount_ - 1)];
            if (std::abs (point.x - previous.x) < 0.002
                && std::abs (point.gainDb - previous.gainDb) < 0.15)
                return;
        }

        if (sketchPointCount_ < SketchPlanner::kMaximumInputPoints)
            sketchPoints_[static_cast<std::size_t> (sketchPointCount_++)] = point;
        else
            sketchPoints_.back() = point;
    }

    void commitSketch()
    {
        const auto plan = SketchPlanner::plan (
            sketchPoints_.data(), sketchPointCount_,
            ParametricEQEditor::kDisplayMinimumFrequency,
            ParametricEQEditor::kDisplayMaximumFrequency);

        int lastBand = -1;
        for (int proposal = 0; proposal < plan.count; ++proposal)
        {
            const auto freeBand = firstDisabledBand();
            if (! freeBand.has_value())
                break;
            const int band = *freeBand;
            resetCreatedBand (processor_, band);
            const auto& proposed = plan.bands[static_cast<std::size_t> (proposal)];

            auto publishUnits = [this, band] (BandParameterOffset offset,
                                              float value)
            {
                const int index = parameterIndex (band, offset);
                auto* parameter = processor_.getParametricEQParameter (index);
                notifyParameter (processor_, index, parameter->toNormalised (value));
            };

            publishUnits (BandParameterOffset::Shape,
                          static_cast<float> (proposed.shape));
            publishUnits (BandParameterOffset::Frequency,
                          static_cast<float> (proposed.frequencyHz));
            publishUnits (BandParameterOffset::Gain,
                          static_cast<float> (proposed.gainDb));
            publishUnits (BandParameterOffset::Q,
                          static_cast<float> (proposed.q));
            notifyParameter (processor_, parameterIndex (
                band, BandParameterOffset::Enabled), 1.0f);
            lastBand = band;
        }

        sketching_ = false;
        sketchArmed_ = false;
        sketchPointCount_ = 0;
        sketchButton_.setToggleState (false, juce::dontSendNotification);
        sketchButton_.setButtonText ("SKETCH");
        sketchButton_.setColour (juce::TextButton::buttonColourId,
                                 Palette::surfaceHover());
        curveDirty_ = true;
        if (lastBand >= 0)
            owner_.selectBand (lastBand);
        repaint();
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
    juce::TextButton sketchButton_ { "SKETCH" };
    juce::TextButton grabButton_ { "GRAB" };
    juce::TextButton copyButton_ { "COPY" };
    juce::TextButton pasteButton_ { "PASTE" };
    juce::TextButton matchButton_ { "EQ MATCH" };
    juce::TextButton matchSaveButton_ { "REF SAVE" };
    juce::TextButton matchLoadButton_ { "REF LOAD" };
    MatchCapture matchReference_;
    MatchCapture matchInput_;
    MatchStage matchStage_ = MatchStage::Idle;
    int lastMatchSpectrumIndex_ = -1;
    GrabPlan grabPlan_ {};
    bool grabArmed_ = false;
    std::array<SketchPoint, SketchPlanner::kMaximumInputPoints> sketchPoints_ {};
    int sketchPointCount_ = 0;
    bool sketchArmed_ = false;
    bool sketching_ = false;
    bool dragging_ = false;
    int dragBand_ = -1;
    juce::Point<float> lastDragPosition_;
    bool pointerDown_ = false;
    bool longPressFired_ = false;
    std::optional<int> longPressTarget_;
    std::uint32_t pressTime_ = 0;
    std::uint32_t lastHudRelayoutMs_ = 0;
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
        viewport_.setScrollBarThickness (7);
        viewport_.setOpaque (false);
        content_.setOpaque (false);
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
        content_.addAndMakeVisible (dynLabel_);
        content_.addAndMakeVisible (dynEnableButton_);
        content_.addAndMakeVisible (dynFilterButton_);
        content_.addAndMakeVisible (dynThresholdSlider_);
        content_.addAndMakeVisible (dynRangeSlider_);
        content_.addAndMakeVisible (dynAttackSlider_);
        content_.addAndMakeVisible (dynReleaseSlider_);
        content_.addAndMakeVisible (spectralLabel_);
        content_.addAndMakeVisible (spectralArmButton_);
        content_.addAndMakeVisible (spectralBandButton_);
        content_.addAndMakeVisible (spectralDensitySlider_);
        content_.addAndMakeVisible (spectralTiltButton_);
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
            content_.addAndMakeVisible (button.get());
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
        dynFilterButton_.setClickingTogglesState (false);
        dynFilterButton_.setTitle (
            "Band-limit the Dynamic EQ detector around this band's frequency and Q");
        dynFilterButton_.onClick = [this]
        {
            const int band = owner_.getSelectedBandForTesting();
            if (band < 0)
                return;
            const int index = dynamicFilterParameterIndex (band);
            auto* parameter = processor_.getParametricEQParameter (index);
            notifyParameter (processor_, index,
                             parameter->getValue() >= 0.5f ? 0.0f : 1.0f);
            refresh();
        };
        configureSlider (dynThresholdSlider_, "Dynamic Threshold");
        configureSlider (dynRangeSlider_, "Dynamic Range");
        configureSlider (dynAttackSlider_, "Dynamic Attack");
        configureSlider (dynReleaseSlider_, "Dynamic Release");

        spectralLabel_.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        spectralLabel_.setColour (juce::Label::textColourId,
                                 Palette::textSecondary());
        spectralLabel_.setText ("Spectral Dynamics — 1024 samples latency",
                                juce::dontSendNotification);
        spectralLabel_.setInterceptsMouseClicks (false, false);
        spectralArmButton_.setTitle (
            "Arm the FFT engine. The DAW must stop/reprepare the audio device "
            "before the new 1024-sample plugin latency becomes active.");
        spectralArmButton_.onClick = [this]
        {
            const auto index = kSpectralArmParameter;
            auto* parameter = processor_.getParametricEQParameter (index);
            notifyParameter (processor_, index,
                parameter->getBool() ? 0.0f : 1.0f);
            refresh();
        };
        spectralBandButton_.setTitle (
            "Apply frequency-selective dynamics to this band using its "
            "ordinary Dynamic EQ threshold/range/attack/release parameters");
        spectralBandButton_.onClick = [this]
        {
            const int band = owner_.getSelectedBandForTesting();
            if (band < 0)
                return;
            const auto index = spectralBandParameterIndex (band);
            auto* parameter = processor_.getParametricEQParameter (index);
            notifyParameter (processor_, index,
                parameter->getBool() ? 0.0f : 1.0f);
            refresh();
        };
        configureSlider (spectralDensitySlider_, "Spectral Density");
        spectralTiltButton_.setTitle (
            "Apply optional +3dB/oct spectral detector tilt");
        spectralTiltButton_.onClick = [this]
        {
            const auto index = kSpectralTiltParameter;
            auto* parameter = processor_.getParametricEQParameter (index);
            notifyParameter (processor_, index,
                parameter->getBool() ? 0.0f : 1.0f);
            refresh();
        };

        startTimerHz (12);
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        // Floating rotary HUD exists ONLY on >=900px desktop canvases.
        // 640x400 and 700x500 landscape touch devices use the below-graph
        // sheet, just like narrow portrait phones. Match applyLayout().
        const bool floating = owner_.getWidth() >= 900 && owner_.getHeight() >= 500;
        if (floating)
        {
            // Self-contained APEX glass HUD; the controls are all real JUCE
            // components and still scroll (no render-only dummy controls).
            g.setColour (juce::Colours::black.withAlpha (0.31f));
            g.fillRoundedRectangle (area.translated (0.0f, 2.0f)
                                        .reduced (1.0f, 1.0f), 13.0f);
        }
        auto body = area.reduced (floating ? 2.0f : 0.0f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff243451), body.getTopLeft(),
            Palette::surface(), body.getBottomLeft(), false));
        if (floating)
            g.fillRoundedRectangle (body, 12.0f);
        else
            g.fillRect (body);
        g.setColour (Palette::accent().withAlpha (floating ? 0.84f : 0.45f));
        if (floating)
            g.drawRoundedRectangle (body.reduced (0.5f), 12.0f, 1.3f);
        else
            g.fillRect (0.0f, 0.0f, std::min (2.0f, area.getWidth()),
                        area.getHeight());
    }

    void resized() override
    {
        viewport_.setBounds (getLocalBounds());

        // The inspector content uses touch-sized fixed rows. When the editor
        // is shorter than the full control stack, the viewport scrolls the
        // contextual panel instead of shrinking controls into unusability.
        // Floating inspector uses dense but still >=44px touch targets.
        // Portrait sheet keeps comfortable full-size 56px control rows.
        // Floating rotary HUD exists ONLY on >=900px desktop canvases.
        // 640x400 and 700x500 landscape touch devices use the below-graph
        // sheet, just like narrow portrait phones. Match applyLayout().
        const bool floating = owner_.getWidth() >= 900 && owner_.getHeight() >= 500;
        const int row = floating ? 48 : 56;
        for (auto* slider : { &frequencySlider_, &gainSlider_, &qSlider_ })
        {
            slider->setSliderStyle (floating
                ? juce::Slider::RotaryHorizontalVerticalDrag
                : juce::Slider::LinearHorizontal);
            // On phones, put the editable numeric field UNDER the slider:
            // all three primary controls can share a full-width quick-edit
            // strip without squeezing a 64px value box beside a 90px track.
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow,
                false, floating ? 96 : 86, 22);
        }
        const int placementColumns = viewport_.getWidth() >= 340 ? 5 : 3;
        const int placementRows = (kChannelPlacementCount + placementColumns - 1)
                                / placementColumns;
        constexpr int labelHeight = 18;
        constexpr int auditionHeight = 48;
        constexpr int contentMargin = 16; // reduced (8, 8)
        // The audition row must retain its full touch-target height: content
        // includes the margin plus every fixed row so no row is ever clamped
        // short by vertical exhaustion. 12 static rows + 1 label +
        // 1 dynamic label + 6 dynamic rows.
        const bool shortSheet = ! floating && owner_.getHeight() < 500;
        // The actual viewport for short landscapes is only ~116 px tall:
        // 112px + content padding clipped the numeric JUCE text boxes
        // behind the bottom command dock, even though geometry tests passed.
        // Shrink only the quick strip (never the 44px action touch targets).
        const int quickHeight = shortSheet ? 96 : 112;
        const int contentHeight = contentMargin
            + (floating ? 14 * row + 134 : 13 * row + 44 + quickHeight)
            + 3 * labelHeight + placementRows * row + auditionHeight;
        content_.setBounds (0, 0, std::max (1, viewport_.getWidth()),
                            std::max (1, contentHeight));

        auto bounds = content_.getLocalBounds().reduced (8, 8);
        // In very short landscape windows there is room for the 112px
        // precision strip OR a heading above it, not both. Give the
        // actual FREQ / GAIN / Q controls first-viewport priority; the
        // band label and secondary actions remain accessible by scrolling.
        if (! shortSheet)
            bandLabel_.setBounds (bounds.removeFromTop (floating ? row : 44));
        if (! floating)
        {
            // Compact-first quick controls: selecting a node immediately
            // exposes frequency, gain and Q without scrolling past On,
            // Bypass and shape. Each slider retains a real hosted parameter
            // with an editable numeric JUCE readout, even at 320px.
            auto quick = bounds.removeFromTop (quickHeight);
            const int column = quick.getWidth() / 3;
            frequencySlider_.setBounds (
                quick.removeFromLeft (column).reduced (2, 4));
            gainSlider_.setBounds (
                quick.removeFromLeft (column).reduced (2, 4));
            qSlider_.setBounds (quick.reduced (2, 4));
        }
        if (shortSheet)
            bandLabel_.setBounds (bounds.removeFromTop (44));
        auto buttonRow = bounds.removeFromTop (row);
        const int third = buttonRow.getWidth() / 3;
        enableButton_.setBounds (buttonRow.removeFromLeft (third).reduced (2, 2));
        bypassButton_.setBounds (buttonRow.removeFromLeft (third).reduced (2, 2));
        closeButton_.setBounds (buttonRow.reduced (2, 2));

        shapeCombo_.setBounds (bounds.removeFromTop (row));
        if (floating)
        {
            // Frequency/Gain/Q are the everyday EQ controls, placed together
            // in one true JUCE rotary strip; secondary settings scroll below.
            auto dials = bounds.removeFromTop (134);
            const int columnWidth = dials.getWidth() / 3;
            frequencySlider_.setBounds (
                dials.removeFromLeft (columnWidth).reduced (2, 1));
            gainSlider_.setBounds (
                dials.removeFromLeft (columnWidth).reduced (2, 1));
            qSlider_.setBounds (dials.reduced (2, 1));
        }
        // In compact mode the real FREQ/GAIN/Q sliders already occupy the
        // pinned first-visible quick strip. Secondary controls still scroll.
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
        dynFilterButton_.setBounds (bounds.removeFromTop (row));
        dynThresholdSlider_.setBounds (bounds.removeFromTop (row));
        dynRangeSlider_.setBounds (bounds.removeFromTop (row));
        dynAttackSlider_.setBounds (bounds.removeFromTop (row));
        dynReleaseSlider_.setBounds (bounds.removeFromTop (row));

        spectralLabel_.setBounds (bounds.removeFromTop (labelHeight));
        spectralArmButton_.setBounds (bounds.removeFromTop (row));
        spectralBandButton_.setBounds (bounds.removeFromTop (row));
        spectralDensitySlider_.setBounds (bounds.removeFromTop (row));
        spectralTiltButton_.setBounds (bounds.removeFromTop (row));

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
        spectralArmButton_.setEnabled (true);
        spectralTiltButton_.setEnabled (true);
        spectralDensitySlider_.setEnabled (true);
        spectralBandButton_.setEnabled (valid);
        const bool requestedSpectral = processor_.getParametricEQParameter (
            kSpectralArmParameter)->getBool();
        const bool armedSpectral = processor_.isSpectralModeArmedForTesting();
        spectralArmButton_.setButtonText (
            requestedSpectral != armedSpectral
                ? (requestedSpectral ? "SPEC ON: RESTART AUDIO"
                                     : "SPEC OFF: RESTART AUDIO")
                : (armedSpectral ? "Spectral Engine ON (+1024)"
                                 : "Spectral Engine OFF (0ms)"));
        spectralArmButton_.setColour (juce::TextButton::buttonColourId,
            requestedSpectral != armedSpectral
                ? Palette::curveMid()
                : armedSpectral ? Palette::accentActive()
                                 : Palette::surfaceHover());
        const bool tiltOn = processor_.getParametricEQParameter (
            kSpectralTiltParameter)->getBool();
        spectralTiltButton_.setButtonText (
            tiltOn ? "Detector Tilt ON (+3dB/oct)" : "Detector Tilt OFF");
        spectralTiltButton_.setColour (juce::TextButton::buttonColourId,
            tiltOn ? Palette::accentActive() : Palette::surfaceHover());
        syncSlider (spectralDensitySlider_, kSpectralDensityParameter);
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
            const bool spectralCompatible = shape == FilterShape::Bell
                || shape == FilterShape::LowShelf
                || shape == FilterShape::HighShelf;
            spectralBandButton_.setEnabled (spectralCompatible);
            const bool perBandSpectral = processor_.getParametricEQParameter (
                spectralBandParameterIndex (band))->getBool();
            spectralBandButton_.setButtonText (
                perBandSpectral ? "This Band: Spectral ON"
                                : "This Band: Spectral OFF");
            spectralBandButton_.setColour (juce::TextButton::buttonColourId,
                perBandSpectral ? Palette::accentActive()
                                : Palette::surfaceHover());
            dynEnableButton_.setEnabled (dynamicCompatible);
            dynFilterButton_.setEnabled (dynamicCompatible);
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
                const auto detectorFiltered = processor_.getParametricEQParameter (
                    dynamicFilterParameterIndex (band))->getBool();
                dynFilterButton_.setButtonText (
                    detectorFiltered ? "SC Filter On" : "SC Filter Off");
                dynFilterButton_.setColour (
                    juce::TextButton::buttonColourId,
                    detectorFiltered ? Palette::accentActive()
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
            spectralBandButton_.setEnabled (false);
            dynEnableButton_.setEnabled (false);
            dynFilterButton_.setEnabled (false);
            dynThresholdSlider_.setEnabled (false);
            dynRangeSlider_.setEnabled (false);
            dynAttackSlider_.setEnabled (false);
            dynReleaseSlider_.setEnabled (false);
        }
        repaint();
    }

    juce::TextButton& getDynEnableButtonForTesting() { return dynEnableButton_; }
    juce::TextButton& getDynFilterButtonForTesting() { return dynFilterButton_; }
    juce::TextButton& getSpectralArmButtonForTesting()
    {
        return spectralArmButton_;
    }
    juce::TextButton& getSpectralBandButtonForTesting()
    {
        return spectralBandButton_;
    }
    juce::Slider& getDynRangeSliderForTesting() { return dynRangeSlider_; }
    std::array<juce::Slider*, 3> getPrimaryDialsForTesting() noexcept
    {
        return {{ &frequencySlider_, &gainSlider_, &qSlider_ }};
    }
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
        if (&slider == &spectralDensitySlider_)
            return kSpectralDensityParameter;
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
    juce::TextButton dynFilterButton_ { "SC Filter Off" };
    juce::Slider dynThresholdSlider_;
    juce::Slider dynRangeSlider_;
    juce::Slider dynAttackSlider_;
    juce::Slider dynReleaseSlider_;

    juce::Label spectralLabel_;
    juce::TextButton spectralArmButton_ { "Spectral Engine OFF" };
    juce::TextButton spectralBandButton_ { "This Band: Spectral OFF" };
    juce::Slider spectralDensitySlider_;
    juce::TextButton spectralTiltButton_ { "Detector Tilt ON" };
};

//==============================================================================
ParametricEQEditor::ParametricEQEditor (Processor& processor)
    : juce::AudioProcessorEditor (processor),
      processor_ (processor)
{
    setOpaque (true);
    setResizable (true, true);
    setLookAndFeel (&prismLookAndFeel());
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
    setLookAndFeel (nullptr);
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

    // Immersive, graph-first APEX layout. Pro-Q-style interaction philosophy
    // but entirely native APEX visuals. A bottom command dock never steals
    // horizontal space from the response display.
    auto workspace = getLocalBounds();
    const int dockHeight = width < 650 ? 104
                          : std::min (56, std::max (44, height / 12));
    topBar_->setBounds (workspace.removeFromBottom (dockHeight));

    // Narrow devices AND short plugin windows use a scrollable touch sheet
    // BELOW the graph. At 900x300 the floating 340px inspector physically
    // cannot avoid covering one of two nearby EQ nodes; a touch sheet
    // preserves editability instead of scoring an impossible HUD position.
    // The main response remains >=45% of the whole editor.
    const bool touchSheet = width < 900 || height < 500;
    if (touchSheet)
    {
        const int available = workspace.getHeight();
        const int minGraph = std::max (140, (height * 45 + 99) / 100);
        const int desiredSheet = selectedBand_ >= 0
            ? std::max (120, available * 38 / 100)
            : std::max (56, height / 12);
        const int sheet = std::min (desiredSheet,
                                    std::max (0, available - minGraph));
        graph_->setBounds (workspace.removeFromTop (available - sheet));
        inspector_->setBounds (workspace);
        inspector_->setVisible (true);
        return;
    }

    // Landscape / desktop: NO permanently docked side inspector. The graph
    // receives the entire canvas, and a small scrolling control HUD appears
    // beside the selected node only when required.
    graph_->setBounds (workspace);
    if (selectedBand_ < 0 || selectedBand_ >= kMaxBands)
    {
        inspector_->setVisible (false);
        return;
    }

    const int graphWidth = graph_->getWidth();
    const int graphHeight = graph_->getHeight();
    const int cardWidth = std::min (std::max (340, graphWidth / 3), 414);
    // At short floating plugin heights, a hard 230px minimum could
    // push the card over the graph and into the bottom dock. Let the
    // internal viewport scroll instead of letting the overlay escape.
    const int cardHeight = std::min (398, std::max (1, graphHeight - 94));
    const auto node = graph_->nodeBounds (selectedBand_).getCentre();

    // Choose a nearby HUD position which obscures as few OTHER EQ nodes as
    // possible. A fixed right-side panel hid high-frequency nodes behind
    // the inspector whenever a low-frequency band was selected. Comparing
    // six legal card positions is cheap, GUI-only, and allocation-free.
    const int maxX = std::max (12, graphWidth - cardWidth - 12);
    const int maxY = std::max (0, graphHeight - cardHeight - 12);
    const int minY = std::min (64, maxY);
    const std::array<int, 2> xCandidates {{
        std::clamp (node.x + 42, 12, maxX),
        std::clamp (node.x - cardWidth - 42, 12, maxX)
    }};
    const std::array<int, 3> yCandidates {{
        std::clamp (node.y + 32, minY, maxY),
        std::clamp (node.y - cardHeight - 32, minY, maxY),
        std::clamp (node.y - cardHeight / 2, minY, maxY)
    }};
    juce::Rectangle<int> best;
    int bestScore = std::numeric_limits<int>::max();
    for (const int x : xCandidates)
        for (const int y : yCandidates)
        {
            const juce::Rectangle<int> candidate (x, y,
                                                   cardWidth, cardHeight);
            int score = std::abs (y - (node.y + 32)) / 12;
            for (int band = 0; band < kMaxBands; ++band)
            {
                const auto* enabled = processor_.getParametricEQParameter (
                    parameterIndex (band, BandParameterOffset::Enabled));
                if (! enabled->getBool())
                    continue;
                if (candidate.intersects (
                    graph_->nodeBounds (band).expanded (8)))
                    score += band == selectedBand_ ? 10000 : 100;
            }
            if (score < bestScore)
            {
                best = candidate;
                bestScore = score;
            }
        }

    inspector_->setBounds (best);
    inspector_->setVisible (true);
    inspector_->toFront (false); // overlay stays interactive, graph stays huge
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

juce::TextButton& ParametricEQEditor::getSketchButtonForTesting()
{
    return graph_->getSketchButtonForTesting();
}

juce::TextButton& ParametricEQEditor::getGrabButtonForTesting()
{
    return graph_->getGrabButtonForTesting();
}

juce::TextButton& ParametricEQEditor::getCopyButtonForTesting()
{
    return graph_->getCopyButtonForTesting();
}

juce::TextButton& ParametricEQEditor::getPasteButtonForTesting()
{
    return graph_->getPasteButtonForTesting();
}

juce::TextButton& ParametricEQEditor::getMatchButtonForTesting()
{
    return graph_->getMatchButtonForTesting();
}

juce::TextButton& ParametricEQEditor::getMatchSaveButtonForTesting()
{
    return graph_->getMatchSaveButtonForTesting();
}

juce::TextButton& ParametricEQEditor::getMatchLoadButtonForTesting()
{
    return graph_->getMatchLoadButtonForTesting();
}

void ParametricEQEditor::feedMatchSpectrumForTesting (
    const float* db, int bins, double binHz)
{
    graph_->feedMatchSpectrumForTesting (db, bins, binHz);
}

void ParametricEQEditor::setGrabSpectrumForTesting (
    const float* db, int bins, double binHz)
{
    graph_->setGrabSpectrumForTesting (db, bins, binHz);
}

juce::Rectangle<int> ParametricEQEditor::getGrabPeakBoundsForTesting (
    int index) const
{
    return graph_->getGrabPeakBoundsForTesting (index);
}

juce::TextButton& ParametricEQEditor::getDynEnableButtonForTesting()
{
    return inspector_->getDynEnableButtonForTesting();
}

juce::TextButton& ParametricEQEditor::getSpectralArmButtonForTesting()
{
    return inspector_->getSpectralArmButtonForTesting();
}

juce::TextButton& ParametricEQEditor::getSpectralBandButtonForTesting()
{
    return inspector_->getSpectralBandButtonForTesting();
}

juce::TextButton& ParametricEQEditor::getDynFilterButtonForTesting()
{
    return inspector_->getDynFilterButtonForTesting();
}

juce::Slider& ParametricEQEditor::getDynRangeSliderForTesting()
{
    return inspector_->getDynRangeSliderForTesting();
}

std::array<juce::Slider*, 3> ParametricEQEditor::getPrimaryDialsForTesting()
{
    return inspector_->getPrimaryDialsForTesting();
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

void ParametricEQEditor::refreshResponseForTesting()
{
    graph_->ingestPublishedResponseForTesting();
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

void ParametricEQEditor::injectGraphDoubleClickForTesting (
    juce::Point<float> position)
{
    auto* source = testMouseSource (this);
    if (source == nullptr)
        return;
    const juce::MouseEvent event (*source, position, juce::ModifierKeys(),
                                  1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  graph_.get(), graph_.get(),
                                  juce::Time::getCurrentTime(), position,
                                  juce::Time::getCurrentTime(), 2, false);
    graph_->mouseDoubleClick (event);
}

void ParametricEQEditor::injectGraphWheelForTesting (
    juce::Point<float> position, float deltaY)
{
    auto* source = testMouseSource (this);
    if (source == nullptr)
        return;
    const juce::MouseEvent event (*source, position, juce::ModifierKeys(),
                                  1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  graph_.get(), graph_.get(),
                                  juce::Time::getCurrentTime(), position,
                                  juce::Time::getCurrentTime(), 1, false);
    juce::MouseWheelDetails details;
    details.deltaY = deltaY;
    graph_->mouseWheelMove (event, details);
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
