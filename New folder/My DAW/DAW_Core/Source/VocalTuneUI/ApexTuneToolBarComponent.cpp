// =============================================================================
//  ApexTuneToolBarComponent.cpp
//  Full toolbar implementation.
//
//  Drop-in: Source/VocalTuneUI/ApexTuneToolBarComponent.cpp
// =============================================================================

#include "ApexTuneToolBarComponent.h"
#include "ApexTuneColors.h"

namespace apex { namespace vocaltune {

namespace
{
    constexpr int kRotaryWidth   = 80;
    constexpr int kButtonWidth   = 72;
    constexpr int kLabelHeight   = 14;
    constexpr int kEdgePadding   = 8;
    constexpr int kControlGap    = 6;
}

// -----------------------------------------------------------------------------
ApexTuneToolBarComponent::ApexTuneToolBarComponent()
{
    styleRotary (correctionSlider_, 0.0,   1.0,  1.0,   {});
    styleRotary (driftSlider_,      0.0,   1.0,  1.0,   {});
    styleRotary (modulationSlider_, 0.0,   1.0,  1.0,   {});
    styleRotary (formantSlider_,   -12.0, 12.0,  0.0,   " st");
    styleRotary (gainSlider_,      -24.0, 12.0,  0.0,   " dB");

    styleLabel (correctionLabel_, "Correction", correctionSlider_);
    styleLabel (driftLabel_,      "Drift",      driftSlider_);
    styleLabel (modulationLabel_, "Modulation", modulationSlider_);
    styleLabel (formantLabel_,    "Formant",    formantSlider_);
    styleLabel (gainLabel_,       "Gain",       gainSlider_);
    styleLabel (rootLabel_,       "Root",       rootBox_);
    styleLabel (scaleLabel_,      "Scale",      scaleBox_);

    styleCombo (rootBox_);
    styleCombo (scaleBox_);

    static const char* roots[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    for (int i = 0; i < 12; ++i)
        rootBox_.addItem (roots[i], i + 1);

    static const char* scales[] = { "Chromatic", "Major", "Minor", "HarmonicMinor", "MelodicMinor",
                                    "MajorPentatonic", "MinorPentatonic", "Blues", "Dorian",
                                    "Phrygian", "Lydian", "Mixolydian", "Locrian" };
    for (int i = 0; i < 13; ++i)
        scaleBox_.addItem (scales[i], i + 1);

    rootBox_.setText ("C", juce::dontSendNotification);
    scaleBox_.setText ("Chromatic", juce::dontSendNotification);
    rootBox_.addListener (this);
    scaleBox_.addListener (this);

    addAndMakeVisible (snapButton_);
    addAndMakeVisible (renderButton_);
    addAndMakeVisible (bypassButton_);

    snapButton_  .addListener (this);
    renderButton_.addListener (this);
    bypassButton_.addListener (this);

    snapButton_  .setColour (juce::TextButton::buttonColourId,   Col::VocalTune::panelDivider());
    renderButton_.setColour (juce::TextButton::buttonColourId,   Col::VocalTune::panelDivider());
    snapButton_  .setColour (juce::TextButton::textColourOnId,   Col::VocalTune::text());
    renderButton_.setColour (juce::TextButton::textColourOnId,   Col::VocalTune::text());
    snapButton_  .setColour (juce::TextButton::textColourOffId,  Col::VocalTune::text());
    renderButton_.setColour (juce::TextButton::textColourOffId,  Col::VocalTune::text());

    bypassButton_.setColour (juce::ToggleButton::textColourId,         Col::VocalTune::text());
    bypassButton_.setColour (juce::ToggleButton::tickColourId,         Col::VocalTune::noteSlight());
    bypassButton_.setColour (juce::ToggleButton::tickDisabledColourId, Col::VocalTune::textDim());
}

// -----------------------------------------------------------------------------
ApexTuneToolBarComponent::~ApexTuneToolBarComponent() = default;

// -----------------------------------------------------------------------------
void ApexTuneToolBarComponent::styleRotary (juce::Slider& s,
                                            double min, double max, double def,
                                            juce::String suffix)
{
    addAndMakeVisible (s);
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    s.setRange (min, max, 0.0);
    s.setValue (def, juce::dontSendNotification);
    s.setTextValueSuffix (suffix);

    s.setColour (juce::Slider::rotarySliderFillColourId,    Col::VocalTune::noteInTune());
    s.setColour (juce::Slider::rotarySliderOutlineColourId, Col::VocalTune::panelDivider());
    s.setColour (juce::Slider::thumbColourId,               Col::VocalTune::text());
    s.setColour (juce::Slider::textBoxTextColourId,         Col::VocalTune::text());
    s.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);

    s.addListener (this);
}

// -----------------------------------------------------------------------------
void ApexTuneToolBarComponent::styleLabel (juce::Label& l,
                                           const juce::String& text,
                                           juce::Component& /*attachTo*/)
{
    addAndMakeVisible (l);
    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::Font (12.0f, juce::Font::plain));
    l.setColour (juce::Label::textColourId, Col::VocalTune::textDim());
    l.setInterceptsMouseClicks (false, false);
}

void ApexTuneToolBarComponent::styleCombo (juce::ComboBox& box)
{
    addAndMakeVisible (box);
    box.setColour (juce::ComboBox::backgroundColourId, Col::VocalTune::panelDivider());
    box.setColour (juce::ComboBox::textColourId, Col::VocalTune::text());
    box.setColour (juce::ComboBox::outlineColourId, Col::VocalTune::panelDivider());
    box.setColour (juce::ComboBox::arrowColourId, Col::VocalTune::text());
}

// -----------------------------------------------------------------------------
void ApexTuneToolBarComponent::paint (juce::Graphics& g)
{
    g.fillAll (Col::VocalTune::toolbarBg());

    // Bottom hairline divider.
    g.setColour (Col::VocalTune::panelDivider());
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

// -----------------------------------------------------------------------------
void ApexTuneToolBarComponent::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;

    auto area = getLocalBounds().reduced (kEdgePadding, 4);

    // ---- Buttons on the right ----------------------------------------------
    auto right = area.removeFromRight (kButtonWidth * 3 + kControlGap * 2);
    auto bypassArea = right.removeFromRight (kButtonWidth);
    right.removeFromRight (kControlGap);
    auto renderArea = right.removeFromRight (kButtonWidth);
    right.removeFromRight (kControlGap);
    auto snapArea   = right;

    snapButton_  .setBounds (snapArea  .reduced (0, 6));
    renderButton_.setBounds (renderArea.reduced (0, 6));
    bypassButton_.setBounds (bypassArea.reduced (0, 6));

    // ---- Rotaries on the left ----------------------------------------------
    auto layoutRotary = [&] (juce::Slider& s, juce::Label& l)
    {
        if (area.getWidth() < kRotaryWidth) return;

        auto cell = area.removeFromLeft (kRotaryWidth);
        area.removeFromLeft (kControlGap);

        auto labelArea = cell.removeFromTop (kLabelHeight);
        l.setBounds (labelArea);
        s.setBounds (cell);
    };

    layoutRotary (correctionSlider_, correctionLabel_);
    layoutRotary (driftSlider_,      driftLabel_);
    layoutRotary (modulationSlider_, modulationLabel_);
    layoutRotary (formantSlider_,    formantLabel_);
    layoutRotary (gainSlider_,       gainLabel_);

    auto layoutCombo = [&] (juce::ComboBox& box, juce::Label& l, int width)
    {
        if (area.getWidth() < width) return;
        auto cell = area.removeFromLeft (width);
        area.removeFromLeft (kControlGap);
        auto labelArea = cell.removeFromTop (kLabelHeight);
        l.setBounds (labelArea);
        box.setBounds (cell.reduced (0, 8));
    };

    layoutCombo (rootBox_,  rootLabel_,  58);
    layoutCombo (scaleBox_, scaleLabel_, 128);
}

// -----------------------------------------------------------------------------
void ApexTuneToolBarComponent::sliderValueChanged (juce::Slider* s)
{
    if (s == &correctionSlider_  && onCorrectionChanged) onCorrectionChanged ((float) s->getValue());
    else if (s == &driftSlider_      && onDriftChanged)      onDriftChanged      ((float) s->getValue());
    else if (s == &modulationSlider_ && onModulationChanged) onModulationChanged ((float) s->getValue());
    else if (s == &formantSlider_    && onFormantChanged)    onFormantChanged    ((float) s->getValue());
    else if (s == &gainSlider_       && onGainChanged)       onGainChanged       ((float) s->getValue());
}

void ApexTuneToolBarComponent::comboBoxChanged (juce::ComboBox*)
{
    if (onScaleChanged)
        onScaleChanged (rootBox_.getText(), scaleBox_.getText());
}

void ApexTuneToolBarComponent::sliderDragStarted (juce::Slider*)
{
    if (onGestureStarted) onGestureStarted();
}

void ApexTuneToolBarComponent::sliderDragEnded (juce::Slider*)
{
    if (onGestureEnded) onGestureEnded();
}

// -----------------------------------------------------------------------------
void ApexTuneToolBarComponent::buttonClicked (juce::Button* b)
{
    if (b == &snapButton_      && onSnapToScaleClicked) onSnapToScaleClicked();
    else if (b == &renderButton_    && onRenderClicked)      onRenderClicked();
    else if (b == &bypassButton_    && onBypassToggled)      onBypassToggled (bypassButton_.getToggleState());
}

// -----------------------------------------------------------------------------
void ApexTuneToolBarComponent::setCorrectionValue (float v, juce::NotificationType nt)
{
    correctionSlider_.setValue ((double) v, nt);
}
void ApexTuneToolBarComponent::setDriftValue (float v, juce::NotificationType nt)
{
    driftSlider_.setValue ((double) v, nt);
}
void ApexTuneToolBarComponent::setModulationValue (float v, juce::NotificationType nt)
{
    modulationSlider_.setValue ((double) v, nt);
}
void ApexTuneToolBarComponent::setFormantValue (float v, juce::NotificationType nt)
{
    formantSlider_.setValue ((double) v, nt);
}

void ApexTuneToolBarComponent::setGainValue (float v, juce::NotificationType nt)
{
    gainSlider_.setValue ((double) v, nt);
}

void ApexTuneToolBarComponent::setScale (const juce::String& root,
                                         const juce::String& type,
                                         juce::NotificationType nt)
{
    rootBox_.setText (root, nt);
    scaleBox_.setText (type, nt);
}

// -----------------------------------------------------------------------------
void ApexTuneToolBarComponent::setBypassed (bool b)
{
    bypassButton_.setToggleState (b, juce::dontSendNotification);
}

bool ApexTuneToolBarComponent::isBypassed() const
{
    return bypassButton_.getToggleState();
}

}} // namespace apex::vocaltune
