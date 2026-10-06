// =============================================================================
//  ApexTuneToolBarComponent.h
//  Top toolbar: macro sliders + action buttons + bypass toggle.
//
//  Drop-in: Source/VocalTuneUI/ApexTuneToolBarComponent.h
//  Depends on: JUCE.
//  Touches:    nothing else in the APEX codebase.
//
//  Layout (left to right):
//   [Correction]  [Drift]  [Modulation]  [Formant]   ...   [Snap] [Render] [Bypass]
//   rotary x 4 with labels                                  text buttons + toggle
//
//  The toolbar emits std::function callbacks; the ApexTuneEditorComponent
//  binds them to clipState mutations + repaint requests.
// =============================================================================

#pragma once

#include <JuceHeader.h>

namespace apex { namespace vocaltune {

class ApexTuneToolBarComponent
    : public juce::Component
    , private juce::Slider::Listener
    , private juce::Button::Listener
    , private juce::ComboBox::Listener
{
public:
    ApexTuneToolBarComponent();
    ~ApexTuneToolBarComponent() override;

    // ---- Callbacks (caller binds these) ------------------------------------
    std::function<void (float)> onCorrectionChanged;     // 0..1
    std::function<void (float)> onDriftChanged;          // 0..1
    std::function<void (float)> onModulationChanged;     // 0..1
    std::function<void (float)> onFormantChanged;        // -12..+12 semitones
    std::function<void (float)> onGainChanged;           // -24..+12 dB
    std::function<void ()>      onSnapToScaleClicked;
    std::function<void ()>      onRenderClicked;
    std::function<void (bool)>  onBypassToggled;
    std::function<void ()>      onGestureStarted;
    std::function<void ()>      onGestureEnded;
    std::function<void (juce::String, juce::String)> onScaleChanged;

    // ---- Programmatic setters (do not fire callbacks by default) -----------
    void setCorrectionValue (float v, juce::NotificationType nt = juce::dontSendNotification);
    void setDriftValue      (float v, juce::NotificationType nt = juce::dontSendNotification);
    void setModulationValue (float v, juce::NotificationType nt = juce::dontSendNotification);
    void setFormantValue    (float v, juce::NotificationType nt = juce::dontSendNotification);
    void setGainValue       (float v, juce::NotificationType nt = juce::dontSendNotification);
    void setScale (const juce::String& root, const juce::String& type,
                   juce::NotificationType nt = juce::dontSendNotification);

    void setBypassed (bool b);
    bool isBypassed() const;

    // ---- juce::Component ---------------------------------------------------
    void paint   (juce::Graphics&) override;
    void resized() override;

private:
    void sliderValueChanged (juce::Slider*) override;
    void sliderDragStarted (juce::Slider*) override;
    void sliderDragEnded   (juce::Slider*) override;
    void buttonClicked      (juce::Button*) override;
    void comboBoxChanged    (juce::ComboBox*) override;

    juce::Slider correctionSlider_;
    juce::Slider driftSlider_;
    juce::Slider modulationSlider_;
    juce::Slider formantSlider_;
    juce::Slider gainSlider_;

    juce::Label  correctionLabel_;
    juce::Label  driftLabel_;
    juce::Label  modulationLabel_;
    juce::Label  formantLabel_;
    juce::Label  gainLabel_;
    juce::Label  rootLabel_;
    juce::Label  scaleLabel_;

    juce::ComboBox rootBox_;
    juce::ComboBox scaleBox_;

    juce::TextButton   snapButton_   { "Snap" };
    juce::TextButton   renderButton_ { "Render" };
    juce::ToggleButton bypassButton_ { "Bypass" };

    void styleRotary (juce::Slider& s, double min, double max, double def, juce::String suffix);
    void styleLabel  (juce::Label& l, const juce::String& text, juce::Component& attachTo);
    void styleCombo  (juce::ComboBox& box);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApexTuneToolBarComponent)
};

}} // namespace apex::vocaltune
