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
    static juce::Colour spectrum() { return juce::Colour (0xffb46bea); }
    static juce::Colour spectrumTrace() { return juce::Colour (0xffedb9ff); }
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

    void drawComboBox (juce::Graphics& g, int width, int height,
                       bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH,
                       juce::ComboBox& combo) override
    {
        // Rounded glass selector used for Filter Shape and the ONE
        // Stereo/Left/Right/Mid/Side dropdown in the approved design.
        auto box = juce::Rectangle<float> (0.0f, 0.0f,
            static_cast<float> (width), static_cast<float> (height))
            .reduced (1.2f);
        if (box.isEmpty())
            return;
        g.setGradientFill (juce::ColourGradient (
            Palette::surfaceHover().darker (0.12f), box.getTopLeft(),
            Palette::surface(), box.getBottomLeft(), false));
        g.fillRoundedRectangle (box, 7.0f);
        g.setColour ((isButtonDown ? Palette::accent() : Palette::border())
                         .withAlpha (combo.isEnabled() ? 0.89f : 0.38f));
        g.drawRoundedRectangle (box, 7.0f, 1.0f);
        const float cx = static_cast<float> (buttonX)
                       + static_cast<float> (buttonW) * 0.5f;
        const float cy = static_cast<float> (buttonY)
                       + static_cast<float> (buttonH) * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath (cx - 5.0f, cy - 2.0f);
        chevron.lineTo (cx, cy + 3.0f);
        chevron.lineTo (cx + 5.0f, cy - 2.0f);
        g.setColour (Palette::textSecondary());
        g.strokePath (chevron, juce::PathStrokeType (1.7f,
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
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
        addAndMakeVisible (presetButton_);
        addAndMakeVisible (utilityButton_);
        addAndMakeVisible (viewButton_);
        addAndMakeVisible (stereoButton_);
        addAndMakeVisible (title_);
        // Secondary hosted controls remain LIVE but move to one popover.
        // Reduces eight permanently visible bottom buttons to five controls
        // plus a searchable preset entry, without removing any parameter.
        detectorButton_.setVisible (false);
        keyButton_.setVisible (false);
        linkButton_.setVisible (false);
        characterButton_.setVisible (false);
        designButton_.setVisible (false);
        bypassButton_.setVisible (false);
        phaseButton_.setVisible (false);
        title_.setVisible (false); // logo belongs in screenshot's upper ribbon
        viewButton_.setTitle ("Show EQ curve, spectrum or both");
        stereoButton_.setTitle ("Selected band's channel placement");
        presetButton_.setTitle ("APEX Prism curated EQ presets");
        utilityButton_.setTitle (
            "Detector, external sidechain, stereo link and character");

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
        analyzerButton_.onClick = [this] { openAnalyzerMenu(); };
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
        presetButton_.onClick = [this] { openPresetMenu(); };
        utilityButton_.onClick = [this] { openUtilityMenu(); };
        viewButton_.onClick = [this] { openViewMenu(); };
        stereoButton_.onClick = [this] { openStereoMenu(); };
        // Automatically show the real analyzer when the EQ window opens.
        // The audio processor's closed/dormant default is preserved when no
        // UI exists; the scope-owned subscription is released on close unless
        // the user explicitly selected a different tap state.
        if (processor_.getAnalyzerMode() == APEX::Analysis::SpectrumTapMode::Closed)
        {
            processor_.setAnalyzerMode (APEX::Analysis::SpectrumTapMode::Both);
            autoAnalyzerStarted_ = true;
        }
        // Host/state changes to peq.phase must reach the top bar even when
        // they were not initiated by this button; a low-rate timer refresh
        // matches the existing inspector/graph synchronization pattern.
        startTimerHz (12);
        refresh();
    }

    ~TopBarComponent() override
    {
        if (autoAnalyzerStarted_
            && processor_.getAnalyzerMode()
                == APEX::Analysis::SpectrumTapMode::Both)
            processor_.setAnalyzerMode (APEX::Analysis::SpectrumTapMode::Closed);
    }

    juce::TextButton& getPhaseButtonForTesting() noexcept { return phaseButton_; }
    void applyPresetForTesting (int id) { applyPreset (id); }

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
        const bool compact = getWidth() < 700;
        static const char* modesWide[] = {
            "Off", "Pre", "Post", "Pre + Post" };
        static const char* modesCompact[] = {
            "OFF", "PRE", "POST", "BOTH" };
        const int analyzer = std::clamp (
            static_cast<int> (processor_.getAnalyzerMode()), 0, 3);
        analyzerButton_.setButtonText (
            compact ? modesCompact[analyzer] : modesWide[analyzer]);
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
        const int band = owner_.getSelectedBandForTesting();
        const int placement = band >= 0 && band < kMaxBands
            ? processor_.getParametricEQParameter (
                placementParameterIndex (band))->getChoiceIndex() : 0;
        const auto selected = static_cast<ChannelPlacement> (
            std::clamp (placement, 0, kChannelPlacementCount - 1));
        stereoButton_.setButtonText (
            compact ? (placement == 0 ? "ST" : juce::String (
                channelPlacementName (selected)).substring (0, 2))
                : channelPlacementName (selected));
        viewButton_.setButtonText (compact
            ? (viewMode_ == 0 ? "BOTH" : viewMode_ == 1 ? "EQ" : "FFT")
            : (viewMode_ == 0 ? "Analyzer + EQ"
                 : viewMode_ == 1 ? "EQ Only" : "Analyzer Only"));
        // Keep the footer quiet until needed; selected/danger states
        // remain legible, but the row is not a wall of purple buttons.
        analyzerButton_.setColour (juce::TextButton::buttonColourId,
            processor_.getAnalyzerMode() == APEX::Analysis::SpectrumTapMode::Closed
                ? Palette::surfaceHover() : Palette::accent().darker (0.55f));
        utilityButton_.setColour (juce::TextButton::buttonColourId,
                                 Palette::surface());
        presetButton_.setColour (juce::TextButton::buttonColourId,
                                Palette::surface());
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // Five quiet, aligned modules, exactly as in the approved image.
        auto bounds = getLocalBounds().toFloat().reduced (1.0f, 0.5f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff182847), bounds.getTopLeft(),
            Palette::background(), bounds.getBottomLeft(), false));
        g.fillRoundedRectangle (bounds, 11.0f);
        g.setColour (Palette::border().withAlpha (0.75f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 11.0f, 0.9f);
        if (getWidth() < 700 || getHeight() < 66)
            return;
        const std::array<const char*, 5> captions {{
            "VIEW", "ANALYZER", "STEREO", "PRESET", "UTILITY"
        }};
        auto row = getLocalBounds().reduced (7, 0);
        const int cell = row.getWidth() / 5;
        g.setFont (juce::Font (juce::FontOptions (10.5f,
                                                juce::Font::bold)));
        for (int i = 0; i < 5; ++i)
        {
            const auto area = i == 4 ? row : row.removeFromLeft (cell);
            g.setColour (Palette::textSecondary().withAlpha (0.92f));
            g.drawFittedText (captions[static_cast<std::size_t> (i)],
                area.withHeight (20).translated (53, 5),
                juce::Justification::centredLeft, 1);
            const float cx = static_cast<float> (area.getX() + 27);
            const float cy = static_cast<float> (getHeight()) * 0.55f;
            g.setColour (Palette::accent().withAlpha (0.12f));
            g.fillEllipse (cx - 21.0f, cy - 21.0f, 42.0f, 42.0f);
            g.setColour (Palette::border().withAlpha (0.78f));
            g.drawEllipse (cx - 21.0f, cy - 21.0f, 42.0f, 42.0f, 1.0f);
            g.setColour (Palette::accent().brighter (0.37f));
            if (i == 0)
            {
                g.drawEllipse (cx - 11.0f, cy - 6.0f, 22.0f, 12.0f, 1.3f);
                g.fillEllipse (cx - 3.0f, cy - 3.0f, 6.0f, 6.0f);
            }
            else if (i == 1)
            {
                for (int n = 0; n < 4; ++n)
                    g.fillRoundedRectangle (cx - 10.0f + n * 6.0f,
                        cy + 9.0f - 4.0f * n, 3.0f, 4.0f + 4.0f * n, 1.0f);
            }
            else if (i == 2)
            {
                g.drawEllipse (cx - 12.0f, cy - 8.0f, 17.0f, 17.0f, 1.3f);
                g.drawEllipse (cx - 3.0f, cy - 8.0f, 17.0f, 17.0f, 1.3f);
            }
            else if (i == 3)
            {
                g.drawRoundedRectangle (cx - 11.0f, cy - 7.0f,
                    22.0f, 15.0f, 2.0f, 1.4f);
                g.drawLine (cx - 8.0f, cy - 11.0f,
                            cx + 1.0f, cy - 11.0f, 1.4f);
            }
            else
            {
                for (int n = -1; n <= 1; ++n)
                    g.fillEllipse (cx + n * 8.0f - 2.0f,
                                   cy - 2.0f, 4.0f, 4.0f);
            }
            if (i < 4)
            {
                g.setColour (Palette::border().withAlpha (0.42f));
                g.drawVerticalLine (area.getRight() - 1,
                    14.0f, static_cast<float> (getHeight() - 12));
            }
        }
    }

    void resized() override
    {
        title_.setVisible (false);
        auto row = getLocalBounds().reduced (7, 5);
        const bool labels = getWidth() >= 700 && getHeight() >= 66;
        const int captionHeight = labels ? 18 : 0;
        const int cell = row.getWidth() / 5;
        std::array<juce::TextButton*, 5> primary {{
            &viewButton_, &analyzerButton_, &stereoButton_,
            &presetButton_, &utilityButton_
        }};
        for (int i = 0; i < 5; ++i)
        {
            auto area = i == 4 ? row : row.removeFromLeft (cell);
            area = area.reduced (4, 0);
            area.removeFromTop (captionHeight);
            if (labels)
                area.removeFromLeft (46); // vector icon circle
            primary[static_cast<std::size_t> (i)]->setBounds (area);
        }
        // Hidden secondary controls retain real handlers for host/state
        // regression tests and reappear ONLY inside the Utility menu.
        const juce::Rectangle<int> safe (0, 0, 44, 44);
        for (auto* secondary : { &designButton_, &bypassButton_,
                                 &phaseButton_, &detectorButton_, &keyButton_,
                                 &linkButton_, &characterButton_ })
            secondary->setBounds (safe);
    }

    void setBandUnits (int band, BandParameterOffset offset, float units)
    {
        const int index = parameterIndex (band, offset);
        auto* parameter = processor_.getParametricEQParameter (index);
        notifyParameter (processor_, index, parameter->toNormalised (units));
    }

    void applyPreset (int preset)
    {
        if (preset == 7 && undoAvailable_)
        {
            // Snapshot is a single user-initiated preset undo, not a
            // competing automation engine. Only parameters touched by
            // presets are restored; global settings are left intact.
            for (int band = 0; band < kMaxBands; ++band)
            {
                for (int offset = 0; offset < 7; ++offset)
                    notifyParameter (processor_, parameterIndex (band,
                        static_cast<BandParameterOffset> (offset)),
                        oldBands_[static_cast<std::size_t> (band)]
                                 [static_cast<std::size_t> (offset)]);
                notifyParameter (processor_, placementParameterIndex (band),
                    oldPlacements_[static_cast<std::size_t> (band)]);
                notifyParameter (processor_, dynamicParameterIndex (
                    band, DynamicBandOffset::Enable),
                    oldDynamics_[static_cast<std::size_t> (band)]);
                notifyParameter (processor_, dynamicFilterParameterIndex (band),
                    oldDynFilters_[static_cast<std::size_t> (band)]);
                notifyParameter (processor_, spectralBandParameterIndex (band),
                    oldSpectral_[static_cast<std::size_t> (band)]);
            }
            undoAvailable_ = false;
            presetButton_.setButtonText ("PRESETS");
            owner_.resized();
            return;
        }
        if (preset < 1 || preset > 6)
            return;
        // Pre-change hosted state is retained for Restore Previous EQ.
        for (int band = 0; band < kMaxBands; ++band)
        {
            for (int offset = 0; offset < 7; ++offset)
                oldBands_[static_cast<std::size_t> (band)]
                         [static_cast<std::size_t> (offset)] =
                    processor_.getParametricEQParameter (parameterIndex (
                        band, static_cast<BandParameterOffset> (offset)))->getValue();
            oldPlacements_[static_cast<std::size_t> (band)] =
                processor_.getParametricEQParameter (
                    placementParameterIndex (band))->getValue();
            oldDynamics_[static_cast<std::size_t> (band)] =
                processor_.getParametricEQParameter (dynamicParameterIndex (
                    band, DynamicBandOffset::Enable))->getValue();
            oldDynFilters_[static_cast<std::size_t> (band)] =
                processor_.getParametricEQParameter (
                    dynamicFilterParameterIndex (band))->getValue();
            oldSpectral_[static_cast<std::size_t> (band)] =
                processor_.getParametricEQParameter (
                    spectralBandParameterIndex (band))->getValue();
            notifyParameter (processor_, parameterIndex (
                band, BandParameterOffset::Enabled), 0.0f);
        }
        undoAvailable_ = true;

        // These are ORIGINAL APEX starting-point recipes, not third-party
        // factory presets. They remain fully editable 24-band host parameters.
        struct Recipe { FilterShape shape; float hz; float db; float q; };
        std::array<Recipe, 3> recipes {};
        int count = 0;
        juce::String name = "FLAT";
        if (preset == 2) // narrow telephone band, clear voice intelligibility
        {
            name = "PHONE";
            recipes[0] = { FilterShape::LowCut, 340.0f, 0.0f, 0.71f };
            recipes[1] = { FilterShape::HighCut, 3400.0f, 0.0f, 0.71f };
            recipes[2] = { FilterShape::Bell, 1400.0f, 1.8f, 0.95f };
            count = 3;
        }
        else if (preset == 3)
        {
            name = "VOCAL AIR";
            recipes[0] = { FilterShape::LowCut, 85.0f, 0.0f, 0.71f };
            recipes[1] = { FilterShape::Bell, 320.0f, -1.7f, 1.1f };
            recipes[2] = { FilterShape::HighShelf, 10500.0f, 2.0f, 0.65f };
            count = 3;
        }
        else if (preset == 4)
        {
            name = "BASS";
            recipes[0] = { FilterShape::LowCut, 28.0f, 0.0f, 0.71f };
            recipes[1] = { FilterShape::Bell, 78.0f, 2.3f, 0.90f };
            recipes[2] = { FilterShape::Bell, 280.0f, -2.0f, 1.25f };
            count = 3;
        }
        else if (preset == 5)
        {
            name = "MIX CLEAN";
            recipes[0] = { FilterShape::LowCut, 32.0f, 0.0f, 0.71f };
            recipes[1] = { FilterShape::Bell, 310.0f, -1.6f, 1.2f };
            recipes[2] = { FilterShape::Bell, 2800.0f, -0.8f, 1.0f };
            count = 3;
        }
        else if (preset == 6)
        {
            name = "MASTER SHINE";
            recipes[0] = { FilterShape::LowShelf, 105.0f, 1.2f, 0.65f };
            recipes[1] = { FilterShape::Bell, 290.0f, -0.7f, 0.9f };
            recipes[2] = { FilterShape::HighShelf, 12000.0f, 1.6f, 0.65f };
            count = 3;
        }
        for (int band = 0; band < count; ++band)
        {
            const auto& recipe = recipes[static_cast<std::size_t> (band)];
            resetCreatedBand (processor_, band);
            setBandUnits (band, BandParameterOffset::Shape,
                          static_cast<float> (recipe.shape));
            setBandUnits (band, BandParameterOffset::Frequency, recipe.hz);
            setBandUnits (band, BandParameterOffset::Gain, recipe.db);
            setBandUnits (band, BandParameterOffset::Q, recipe.q);
            setBandUnits (band, BandParameterOffset::Enabled, 1.0f);
        }
        presetButton_.setButtonText (name);
        owner_.resized();
    }

    void openPresetMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("APEX PRISM  /  FACTORY STARTING POINTS");
        menu.addItem (1, "Flat / Reset");
        menu.addItem (2, "Phone Effect  /  telephone bandwidth");
        menu.addItem (3, "Vocal Air  /  modern vocals");
        menu.addItem (4, "Bass Tighten  /  low end");
        menu.addItem (5, "Mix Cleanup  /  clarity");
        menu.addItem (6, "Master Shine  /  finishing touch");
        menu.addSeparator();
        menu.addItem (7, "Restore previous EQ", undoAvailable_);
        juce::Component::SafePointer<TopBarComponent> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (&presetButton_),
            [safe] (int id) {
                if (auto* bar = safe.getComponent())
                    bar->applyPreset (id);
            });
    }

    void openViewMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("DISPLAY");
        menu.addItem (1, "Analyzer + EQ", true, viewMode_ == 0);
        menu.addItem (2, "EQ Only", true, viewMode_ == 1);
        menu.addItem (3, "Analyzer Only", true, viewMode_ == 2);
        juce::Component::SafePointer<TopBarComponent> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (&viewButton_), [safe] (int id)
            {
                if (auto* bar = safe.getComponent())
                {
                    if (id < 1 || id > 3)
                        return;
                    bar->viewMode_ = id - 1;
                    bar->owner_.setViewMode (id - 1);
                    bar->refresh();
                }
            });
    }

    void openAnalyzerMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("REAL-TIME FFT");
        const int mode = static_cast<int> (processor_.getAnalyzerMode());
        menu.addItem (1, "Off", true, mode == 0);
        menu.addItem (2, "Pre EQ", true, mode == 1);
        menu.addItem (3, "Post EQ", true, mode == 2);
        menu.addItem (4, "Pre + Post", true, mode == 3);
        juce::Component::SafePointer<TopBarComponent> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (&analyzerButton_), [safe] (int id)
            {
                if (auto* bar = safe.getComponent())
                {
                    if (id < 1 || id > 4)
                        return;
                    bar->autoAnalyzerStarted_ = false;
                    bar->processor_.setAnalyzerMode (
                        static_cast<APEX::Analysis::SpectrumTapMode> (id - 1));
                    bar->refresh();
                }
            });
    }

    void openStereoMenu()
    {
        const int band = owner_.getSelectedBandForTesting();
        if (band < 0 || band >= kMaxBands)
            return; // No selected band: Stereo remains the visual default.
        juce::PopupMenu menu;
        menu.addSectionHeader ("BAND CHANNEL");
        const int current = processor_.getParametricEQParameter (
            placementParameterIndex (band))->getChoiceIndex();
        for (int placement = 0; placement < kChannelPlacementCount; ++placement)
            menu.addItem (placement + 1, channelPlacementName (
                static_cast<ChannelPlacement> (placement)), true,
                current == placement);
        juce::Component::SafePointer<TopBarComponent> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (&stereoButton_), [safe] (int id)
            {
                if (auto* bar = safe.getComponent())
                {
                    const int band = bar->owner_.getSelectedBandForTesting();
                    if (band < 0 || band >= kMaxBands
                        || id < 1 || id > kChannelPlacementCount)
                        return;
                    const int index = placementParameterIndex (band);
                    auto* parameter = bar->processor_.getParametricEQParameter (index);
                    notifyParameter (bar->processor_, index,
                        parameter->toNormalised (static_cast<float> (id - 1)));
                    bar->refresh();
                }
            });
    }

    void openUtilityMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("APEX PRISM  /  UTILITY");
        menu.addItem (1, detectorButton_.getButtonText());
        menu.addItem (2, keyButton_.getButtonText());
        menu.addItem (3, linkButton_.getButtonText());
        menu.addItem (4, characterButton_.getButtonText());
        menu.addSeparator();
        menu.addSectionHeader ("FILTER / MASTER");
        menu.addItem (5, designButton_.getButtonText());
        menu.addItem (6, bypassButton_.getButtonText());
        menu.addItem (7, phaseButton_.getButtonText());
        menu.addSeparator();
        menu.addSectionHeader ("Analyzer capture");
        const int current = static_cast<int> (processor_.getAnalyzerMode());
        menu.addItem (10, "Off", true, current == 0);
        menu.addItem (11, "Pre EQ", true, current == 1);
        menu.addItem (12, "Post EQ", true, current == 2);
        menu.addItem (13, "Pre + Post", true, current == 3);
        juce::Component::SafePointer<TopBarComponent> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (&utilityButton_),
            [safe] (int id) {
                if (auto* bar = safe.getComponent())
                {
                    switch (id)
                    {
                        case 1: bar->detectorButton_.onClick(); break;
                        case 2: bar->keyButton_.onClick(); break;
                        case 3: bar->linkButton_.onClick(); break;
                        case 4: bar->characterButton_.onClick(); break;
                        case 5: bar->designButton_.onClick(); break;
                        case 6: bar->bypassButton_.onClick(); break;
                        case 7: bar->phaseButton_.onClick(); break;
                        default:
                            if (id >= 10 && id <= 13)
                            {
                                bar->autoAnalyzerStarted_ = false;
                                bar->processor_.setAnalyzerMode (
                                    static_cast<APEX::Analysis::SpectrumTapMode>(
                                        id - 10));
                            }
                            break;
                    }
                    bar->refresh();
                }
            });
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
    juce::TextButton presetButton_ { "PRESETS" };
    juce::TextButton utilityButton_ { "UTILITY" };
    juce::TextButton viewButton_ { "Analyzer + EQ" };
    juce::TextButton stereoButton_ { "Stereo" };
    int viewMode_ = 0;
    std::array<std::array<float, 7>, kMaxBands> oldBands_ {};
    std::array<float, kMaxBands> oldPlacements_ {};
    std::array<float, kMaxBands> oldDynamics_ {};
    std::array<float, kMaxBands> oldDynFilters_ {};
    std::array<float, kMaxBands> oldSpectral_ {};
    bool undoAvailable_ = false;
    bool autoAnalyzerStarted_ = false;
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
        addAndMakeVisible (toolsButton_);
        // Advanced toolbar actions remain callable, but the graph no longer
        // has seven chunky always-on buttons obscuring the actual EQ.
        copyButton_.setVisible (false);
        pasteButton_.setVisible (false);
        matchSaveButton_.setVisible (false);
        matchLoadButton_.setVisible (false);
        toolsButton_.setTitle ("Band clipboard and reference utilities");
        toolsButton_.onClick = [this] { openToolsMenu(); };
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
            matchSaveButton_.setVisible (false);
            matchLoadButton_.setVisible (false);
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
        spectrumPeakHold_.fill (-120.0f);
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
        // Four 44px+ touch targets instead of seven graph-obscuring tabs.
        // Clipboard/reference controls are still accessible in TOOLS.
        constexpr int padding = 6;
        const int width = std::max (1, getWidth());
        const int cell = std::clamp (
            (width - 2 * padding) / 4,
            ParametricEQEditor::kTouchTarget, 132);
        std::array<juce::TextButton*, 4> primary {{
            &sketchButton_, &grabButton_, &matchButton_, &toolsButton_
        }};
        if (matchButton_.getButtonText() == "EQ MATCH"
            || matchButton_.getButtonText() == "MATCH")
            matchButton_.setButtonText (width <= 430 ? "MATCH" : "EQ MATCH");
        const bool brandedHeader = width >= 880 && getHeight() >= 235;
        int x = brandedHeader ? 278 : padding;
        for (auto* action : primary)
        {
            action->setBounds (x, 8, cell, ParametricEQEditor::kTouchTarget);
            x += cell;
        }
        // Hidden native buttons remain live for existing action handlers,
        // tests and host-param semantics; they intercept no pointer events.
        for (auto* hidden : { &copyButton_, &pasteButton_,
                              &matchSaveButton_, &matchLoadButton_ })
            hidden->setBounds (0, 0, ParametricEQEditor::kTouchTarget,
                                    ParametricEQEditor::kTouchTarget);
        if (matchSaveButton_.getButtonText() == "REF SAVE"
            || matchSaveButton_.getButtonText() == "SAVE")
            matchSaveButton_.setButtonText (width <= 430 ? "SAVE" : "REF SAVE");
        if (matchLoadButton_.getButtonText() == "REF LOAD"
            || matchLoadButton_.getButtonText() == "LOAD")
            matchLoadButton_.setButtonText (width <= 430 ? "LOAD" : "REF LOAD");
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
        if (spectrumActive_ && viewMode_ != 1)
            drawSpectrum (g, bounds);

        if (viewMode_ != 2)
        {
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
        }
        // APEX's own compact value readout follows the chosen frequency
        // node without additional controls or stealing graph pointer input.
        // The value pill must fit entirely inside the *graph*. At short
        // landscape sizes (notably 900x300) the previous pill was clipped
        // by the below-graph quick-edit sheet and looked like a broken
        // floating inspector. The always-visible quick controls already
        // display the exact values there, so omit this redundant pill.
        if (getWidth() >= 700 && getHeight() >= 220)
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
                // Keep the readout clear of the 52px toolbar and the
                // bottom log-frequency axis. Avoid a clipped rectangle
                // even when the host resizes the editor unusually short.
                const int preferredY = centre.y > 100 ? centre.y - 65
                                                       : centre.y + 36;
                const int y = std::clamp (preferredY, 60,
                                         getHeight() - bubbleHeight - 24);
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

        // The decorative engine caption is not a graph control. Hide it
        // when the response is shallow: at 640x400 and 900x300 it collided
        // with frequency labels and even active EQ nodes.
        // Approved screenshot places PRISM branding and tool ribbon ABOVE
        // the plot, not a stack of anonymous controls on the response curve.
        // Paint the vector header after graph content but before child buttons.
        if (getWidth() >= 880 && getHeight() >= 235)
        {
            const auto bar = juce::Rectangle<float> (
                0.0f, 0.0f, bounds.getWidth(), 66.0f);
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0xff142442), bar.getTopLeft(),
                juce::Colour (0xff0b142a), bar.getBottomLeft(), false));
            g.fillRect (bar);
            g.setColour (Palette::border().withAlpha (0.50f));
            g.drawHorizontalLine (65, 0.0f, bounds.getWidth());
            g.setFont (juce::Font (juce::FontOptions (
                21.0f, juce::Font::bold)));
            g.setColour (Palette::text());
            g.drawFittedText ("APEX /",  juce::Rectangle<int> (22, 12, 100, 39),
                              juce::Justification::centredLeft, 1);
            g.setColour (Palette::accent().brighter (0.22f));
            g.drawFittedText ("PRISM", juce::Rectangle<int> (119, 12, 90, 39),
                              juce::Justification::centredLeft, 1);
            g.setColour (Palette::curve());
            g.drawFittedText ("EQ", juce::Rectangle<int> (207, 12, 47, 39),
                              juce::Justification::centredLeft, 1);
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
    void setViewMode (int mode) noexcept
    {
        viewMode_ = std::clamp (mode, 0, 2);
        repaint();
    }
    int getViewMode() const noexcept { return viewMode_; }
    float analyzerYForTesting (float db) const noexcept
    {
        return spectrumY (db, static_cast<float> (getHeight()));
    }
    juce::TextButton& getSketchButtonForTesting() noexcept { return sketchButton_; }
    juce::TextButton& getGrabButtonForTesting() noexcept { return grabButton_; }
    juce::TextButton& getCopyButtonForTesting() noexcept { return copyButton_; }
    juce::TextButton& getPasteButtonForTesting() noexcept { return pasteButton_; }
    juce::TextButton& getMatchButtonForTesting() noexcept { return matchButton_; }
    juce::TextButton& getToolsButtonForTesting() noexcept { return toolsButton_; }
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
    void openToolsMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("BAND");
        menu.addItem (1, "Copy selected band", owner_.getSelectedBandForTesting() >= 0);
        menu.addItem (2, "Paste as new band", nativeBandClipboard().valid);
        menu.addSeparator();
        menu.addSectionHeader ("EQ MATCH REFERENCE");
        menu.addItem (3, "Save reference", matchReference_.ready() && ! grabArmed_);
        menu.addItem (4, "Load shared reference",
                      sharedMatchReference().valid && ! grabArmed_);
        juce::Component::SafePointer<EqGraphComponent> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (&toolsButton_),
            [safe] (int id)
            {
                if (auto* graph = safe.getComponent())
                {
                    switch (id)
                    {
                        case 1: graph->copyButton_.onClick(); break;
                        case 2: graph->pasteButton_.onClick(); break;
                        case 3: graph->matchSaveButton_.onClick(); break;
                        case 4: graph->matchLoadButton_.onClick(); break;
                        default: break;
                    }
                }
            });
    }

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
                if (source != nullptr && std::isfinite (snapshot.binHz())
                    && snapshot.binHz() > 0.0)
                {
                    std::memcpy (spectrumCopy_.data(), source,
                                 spectrumCopy_.size() * sizeof (float));
                    spectrumBinHz_ = static_cast<float> (snapshot.binHz());
                    // GUI-only hold: real FFT data, finite-sanitized and
                    // decaying by ~18 dB/second at the 25 Hz refresh cadence.
                    // Neither the analysis worker nor audio callback is touched.
                    for (std::size_t bin = 0; bin < spectrumCopy_.size(); ++bin)
                    {
                        const float value = std::isfinite (spectrumCopy_[bin])
                            ? std::clamp (spectrumCopy_[bin], -120.0f, 18.0f)
                            : -120.0f;
                        spectrumCopy_[bin] = value;
                        spectrumPeakHold_[bin] = std::max (value,
                            spectrumPeakHold_[bin] - 0.72f);
                    }
                    spectrumActive_ = true;
                    changed = true;
                }
            }
        }
        else if (spectrumActive_)
        {
            spectrumActive_ = false;
            spectrumPeakHold_.fill (-120.0f);
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
        matchSaveButton_.setVisible (false);
        matchLoadButton_.setVisible (false);
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

    // Decibel spectrum is NOT mapped through the +/-18 dB EQ GAIN axis.
    // Previously it was clamped into the bottom few pixels, invisible for
    // normal -60..-20 dBFS program material. An independent -96..-6 dBFS
    // display range gives a Pro-Q-style full-height live spectrum.
    float spectrumY (float db, float height) const noexcept
    {
        const float top = std::min (64.0f, height * 0.19f);
        const float bottom = std::max (top + 1.0f, height - 23.0f);
        const float fraction = std::clamp ((db + 96.0f) / 90.0f,
                                            0.0f, 1.0f);
        return bottom - fraction * (bottom - top);
    }

    float spectrumAt (const std::array<float,
                      APEX::Analysis::SpectrumAnalyzerCore::kSpectrumBins>& data,
                      double hz) const noexcept
    {
        const float index = static_cast<float> (hz)
                          / std::max (1.0f, spectrumBinHz_);
        const float clamped = std::clamp (index, 0.0f,
            static_cast<float> (data.size() - 1));
        const auto lo = static_cast<std::size_t> (clamped);
        const auto hi = std::min (lo + 1, data.size() - 1);
        return data[lo] + (data[hi] - data[lo])
            * (clamped - static_cast<float> (lo));
    }

    void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        const int width = std::max (1, getWidth());
        const float h = bounds.getHeight();
        const float bottom = std::max (1.0f, h - 23.0f);
        juce::Path live, peakHold, fill;
        // Fixed-size winners: prominent actual FFT peaks, not fabricated
        // resonance data. Spacing keeps labels from colliding.
        std::array<float, 4> loudest {{ -120.0f, -120.0f,
                                         -120.0f, -120.0f }};
        std::array<int, 4> locations {{ -1, -1, -1, -1 }};
        const int separation = std::max (40, width / 10);
        for (int x = 0; x < width; ++x)
        {
            const auto frequency = frequencyForX (x, width);
            const float db = spectrumAt (spectrumCopy_, frequency);
            const float y = spectrumY (db, h);
            const float peakY = spectrumY (
                spectrumAt (spectrumPeakHold_, frequency), h);
            if (x == 0)
            {
                live.startNewSubPath (0.0f, y);
                peakHold.startNewSubPath (0.0f, peakY);
                fill.startNewSubPath (0.0f, bottom);
                fill.lineTo (0.0f, y);
            }
            else
            {
                live.lineTo (static_cast<float> (x), y);
                peakHold.lineTo (static_cast<float> (x), peakY);
                fill.lineTo (static_cast<float> (x), y);
            }
            // Local maximum over adjacent logarithmic-frequency samples.
            // Labels are limited to four loudest peaks above -67 dBFS.
            if (x < 54 || x >= width - 50 || (x % 3) != 0 || db < -67.0f)
                continue;
            const float before = spectrumAt (spectrumCopy_,
                frequencyForX (x - 5, width));
            const float after = spectrumAt (spectrumCopy_,
                frequencyForX (x + 5, width));
            if (db < before || db < after)
                continue;
            int conflicting = -1;
            for (int i = 0; i < 4; ++i)
                if (locations[static_cast<std::size_t> (i)] >= 0
                    && std::abs (x - locations[static_cast<std::size_t> (i)])
                         < separation)
                    conflicting = i;
            if (conflicting >= 0)
            {
                if (db > loudest[static_cast<std::size_t> (conflicting)])
                {
                    loudest[static_cast<std::size_t> (conflicting)] = db;
                    locations[static_cast<std::size_t> (conflicting)] = x;
                }
            }
            else
            {
                int quietest = 0;
                for (int i = 1; i < 4; ++i)
                    if (loudest[static_cast<std::size_t> (i)]
                        < loudest[static_cast<std::size_t> (quietest)])
                        quietest = i;
                if (db > loudest[static_cast<std::size_t> (quietest)])
                {
                    loudest[static_cast<std::size_t> (quietest)] = db;
                    locations[static_cast<std::size_t> (quietest)] = x;
                }
            }
        }
        fill.lineTo (static_cast<float> (width - 1), bottom);
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (
            Palette::spectrum().withAlpha (0.52f), 0.0f,
            h * 0.22f, Palette::spectrum().withAlpha (0.045f),
            0.0f, bottom, false));
        g.fillPath (fill);
        g.setColour (Palette::spectrum().withAlpha (0.19f));
        g.strokePath (live, juce::PathStrokeType (4.0f));
        g.setColour (Palette::spectrumTrace().withAlpha (0.93f));
        g.strokePath (live, juce::PathStrokeType (1.45f));
        g.setColour (Palette::spectrum().withAlpha (0.32f));
        g.strokePath (peakHold, juce::PathStrokeType (0.9f));

        if (width < 440 || h < 250)
            return; // retain touch hit targets and short-landscape plot
        g.setFont (juce::Font (juce::FontOptions (11.0f,
                                                juce::Font::bold)));
        for (int i = 0; i < 4; ++i)
        {
            const int x = locations[static_cast<std::size_t> (i)];
            if (x < 0)
                continue;
            const float y = spectrumY (loudest[static_cast<std::size_t> (i)], h);
            const float bubbleY = y > 102.0f ? y - 30.0f : y + 12.0f;
            const auto hz = frequencyForX (x, width);
            const juce::String caption = hz < 1000.0
                ? juce::String (juce::roundToInt (hz)) + " Hz"
                : juce::String (hz / 1000.0, 1) + " kHz";
            juce::Rectangle<float> box (static_cast<float> (x - 32),
                                         bubbleY, 64.0f, 21.0f);
            g.setColour (Palette::surface().withAlpha (0.88f));
            g.fillRoundedRectangle (box, 6.0f);
            g.setColour (Palette::spectrumTrace().withAlpha (0.72f));
            g.drawRoundedRectangle (box, 6.0f, 0.8f);
            g.fillEllipse (static_cast<float> (x) - 2.5f,
                           y - 2.5f, 5.0f, 5.0f);
            g.setColour (Palette::text());
            g.drawFittedText (caption, box.toNearestInt(),
                              juce::Justification::centred, 1);
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
    int viewMode_ = 0;
    ResponseFrame latestFrame_;
    int lastSpectrumIndex_ = -1;
    bool spectrumActive_ = false;
    float spectrumBinHz_ = 11.71875f;
    std::array<float, APEX::Analysis::SpectrumAnalyzerCore::kSpectrumBins>
        spectrumCopy_ {};
    std::array<float, APEX::Analysis::SpectrumAnalyzerCore::kSpectrumBins>
        spectrumPeakHold_ {};
    juce::TextButton sketchButton_ { "SKETCH" };
    juce::TextButton grabButton_ { "GRAB" };
    juce::TextButton copyButton_ { "COPY" };
    juce::TextButton pasteButton_ { "PASTE" };
    juce::TextButton matchButton_ { "EQ MATCH" };
    juce::TextButton matchSaveButton_ { "REF SAVE" };
    juce::TextButton matchLoadButton_ { "REF LOAD" };
    juce::TextButton toolsButton_ { "TOOLS" };
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
        content_.addAndMakeVisible (placementCombo_);
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
        // Approved GUI: ONE Channel dropdown, Stereo is the canonical default.
        placementCombo_.setTitle ("Channel placement for selected band");
        for (int placement = 0; placement < kChannelPlacementCount; ++placement)
            placementCombo_.addItem (channelPlacementName (
                static_cast<ChannelPlacement> (placement)), placement + 1);
        placementCombo_.setSelectedId (1, juce::dontSendNotification);
        placementCombo_.onChange = [this]
        {
            const int band = owner_.getSelectedBandForTesting();
            if (band < 0 || band >= kMaxBands
                || placementCombo_.getSelectedId() <= 0)
                return;
            const int index = placementParameterIndex (band);
            auto* parameter = processor_.getParametricEQParameter (index);
            notifyParameter (processor_, index, parameter->toNormalised (
                static_cast<float> (placementCombo_.getSelectedId() - 1)));
            refresh();
        };
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
            // Legacy test handles stay callable but cannot visually crowd
            // the floating HUD or intercept clicks underneath a dropdown.
            button->setVisible (false);
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
        constexpr int placementRows = 1;
        // Approved floating inspector design: one header row, three true
        // hosted rotary dials, slope, and a single Channel selector.
        // Dynamic/spectral options are still available by scrolling lower.
        if (floating)
        {
            constexpr int inset = 14;
            content_.setBounds (0, 0, std::max (1, viewport_.getWidth()), 1220);
            auto frame = content_.getLocalBounds().reduced (inset, 12);
            auto header = frame.removeFromTop (48);
            enableButton_.setBounds (header.removeFromLeft (48).reduced (2, 2));
            bandLabel_.setBounds (header.removeFromLeft (108));
            closeButton_.setBounds (header.removeFromRight (44).reduced (2, 2));
            shapeCombo_.setBounds (header.reduced (2, 2));
            frame.removeFromTop (10);
            auto dials = frame.removeFromTop (172);
            const int dialWidth = dials.getWidth() / 3;
            frequencySlider_.setBounds (dials.removeFromLeft (dialWidth).reduced (3, 3));
            gainSlider_.setBounds (dials.removeFromLeft (dialWidth).reduced (3, 3));
            qSlider_.setBounds (dials.reduced (3, 3));
            frame.removeFromTop (6);
            slopeSlider_.setBounds (frame.removeFromTop (50).reduced (2, 2));
            frame.removeFromTop (10);
            placementLabel_.setBounds (frame.removeFromTop (20));
            placementCombo_.setBounds (frame.removeFromTop (45).reduced (2, 0));
            // Secondary editing never disappears; scrolling exposes it.
            frame.removeFromTop (28);
            bypassButton_.setBounds (frame.removeFromTop (48));
            dynLabel_.setBounds (frame.removeFromTop (24));
            dynEnableButton_.setBounds (frame.removeFromTop (48));
            dynFilterButton_.setBounds (frame.removeFromTop (48));
            dynThresholdSlider_.setBounds (frame.removeFromTop (48));
            dynRangeSlider_.setBounds (frame.removeFromTop (48));
            dynAttackSlider_.setBounds (frame.removeFromTop (48));
            dynReleaseSlider_.setBounds (frame.removeFromTop (48));
            spectralLabel_.setBounds (frame.removeFromTop (24));
            spectralArmButton_.setBounds (frame.removeFromTop (48));
            spectralBandButton_.setBounds (frame.removeFromTop (48));
            spectralDensitySlider_.setBounds (frame.removeFromTop (48));
            spectralTiltButton_.setBounds (frame.removeFromTop (48));
            auditionButton_->setBounds (frame.removeFromTop (48));
            for (auto& hidden : placementButtons_)
                hidden->setBounds (0, 0, 44, 44);
            return;
        }
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

        // 900x300 has a 108px sheet: reserve full 88px slider hit targets
        // plus 10px underneath the numeric readouts by trimming 4px of
        // purely decorative top padding, never the control dimensions.
        const bool shortSheetPadding = ! floating && owner_.getHeight() < 500;
        auto bounds = content_.getLocalBounds().reduced (
            8, shortSheetPadding ? 4 : 8);
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
        placementCombo_.setBounds (bounds.removeFromTop (row).reduced (2, 2));
        for (auto& hidden : placementButtons_)
            hidden->setBounds (0, 0, 44, 44);

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
        placementCombo_.setEnabled (valid);

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
            placementCombo_.setSelectedId (placement + 1,
                                           juce::dontSendNotification);
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
    juce::ComboBox& getPlacementComboForTesting() noexcept
    {
        return placementCombo_;
    }

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
    juce::ComboBox placementCombo_;
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
    addAndMakeVisible (eyeStamp_);
    setSize (900, 620);
    applyLayout();
    eyeStamp_.placeInCorner (*this);
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
    eyeStamp_.placeInCorner (*this);
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
    // One quiet, touch-safe command dock at ALL viewports. Old 320px mode
    // burned 104px on an 8-button grid; six grouped controls now fit 44px.
    const int dockHeight = (width < 650 || height < 500)
        ? 52 : std::clamp (height / 8, 72, 110);
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
    const int cardWidth = std::min (std::max (340, graphWidth / 3), 480);
    // At short floating plugin heights, a hard 230px minimum could
    // push the card over the graph and into the bottom dock. Let the
    // internal viewport scroll instead of letting the overlay escape.
    const int cardHeight = std::min (410, std::max (1, graphHeight - 94));
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

void ParametricEQEditor::setViewMode (int mode)
{
    graph_->setViewMode (mode);
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

void ParametricEQEditor::applyPresetForTesting (int presetId)
{
    topBar_->applyPresetForTesting (presetId);
}

float ParametricEQEditor::analyzerYForTesting (float db) const
{
    return graph_->analyzerYForTesting (db);
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

juce::TextButton& ParametricEQEditor::getToolsButtonForTesting()
{
    return graph_->getToolsButtonForTesting();
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

juce::ComboBox& ParametricEQEditor::getPlacementComboForTesting()
{
    return inspector_->getPlacementComboForTesting();
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
