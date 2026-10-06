#pragma once

#include "AutomationParameterCore.h"
#include <JuceHeader.h>

namespace apex::automation
{
    namespace detail
    {
        struct ReentryGuard
        {
            explicit ReentryGuard (bool& f) : flag (f) { flag = true; }
            ~ReentryGuard()                            { flag = false; }
            bool& flag;
        };
    }

    // -----------------------------------------------------------------------
    //  Slider attachment -- for continuous controls (faders, knobs, rotaries)
    // -----------------------------------------------------------------------

    class SliderAutomationAttachment
        : public AutomationParameter::Listener
        , private juce::Slider::Listener
    {
    public:
        SliderAutomationAttachment (AutomationParameter& param,
                                    juce::Slider&        slider)
            : parameter (param)
            , widget    (slider)
        {
            const auto& range = parameter.getRange();
            widget.setRange (range.minValue, range.maxValue,
                             range.isStepped
                                ? (range.maxValue - range.minValue)
                                  / juce::jmax (1, range.numSteps - 1)
                                : 0.0);
            if (range.skew != 1.0f)
                widget.setSkewFactor (range.skew);

            widget.setValue (parameter.getDenormalizedValue(),
                             juce::dontSendNotification);

            widget.addListener (this);
            parameter.addListener (this);
        }

        ~SliderAutomationAttachment() override
        {
            widget.removeListener (this);
            parameter.removeListener (this);
        }

        // ----- AutomationParameter::Listener (widget moves by itself) ----

        void parameterValueChanged (AutomationParameter& p,
                                    float                newNormalized,
                                    ChangeSource         source) override
        {
            if (source == ChangeSource::User && updatingWidget)
                return;

            detail::ReentryGuard g (updatingWidget);
            widget.setValue (p.getRange().denormalize (newNormalized),
                             juce::dontSendNotification);
        }

        void parameterGestureBegan (AutomationParameter&) override {}
        void parameterGestureEnded (AutomationParameter&) override {}

        // ----- juce::Slider::Listener (user drags -> gesture + value) -----

        void sliderValueChanged (juce::Slider* s) override
        {
            if (updatingWidget || s != &widget) return;
            const float normalized = parameter.getRange().normalize ((float) s->getValue());
            detail::ReentryGuard g (updatingWidget);
            parameter.setValueFromUser (normalized);
        }

        void sliderDragStarted (juce::Slider* s) override
        {
            if (s != &widget) return;
            parameter.beginGesture();
        }

        void sliderDragEnded (juce::Slider* s) override
        {
            if (s != &widget) return;
            parameter.endGesture();
        }

    private:
        AutomationParameter& parameter;
        juce::Slider&        widget;
        bool                 updatingWidget = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliderAutomationAttachment)
    };

    // -----------------------------------------------------------------------
    //  Button attachment -- for toggles, mutes, solos, bypass switches
    // -----------------------------------------------------------------------

    class ButtonAutomationAttachment
        : public AutomationParameter::Listener
        , private juce::Button::Listener
    {
    public:
        ButtonAutomationAttachment (AutomationParameter& param,
                                    juce::Button&        button)
            : parameter (param)
            , widget    (button)
        {
            widget.setToggleState (parameter.getNormalizedValue() >= 0.5f,
                                   juce::dontSendNotification);
            widget.addListener (this);
            parameter.addListener (this);
        }

        ~ButtonAutomationAttachment() override
        {
            widget.removeListener (this);
            parameter.removeListener (this);
        }

        void parameterValueChanged (AutomationParameter&,
                                    float        newNormalized,
                                    ChangeSource source) override
        {
            if (source == ChangeSource::User && updatingWidget)
                return;

            detail::ReentryGuard g (updatingWidget);
            widget.setToggleState (newNormalized >= 0.5f,
                                   juce::dontSendNotification);
        }

        void buttonClicked (juce::Button* b) override
        {
            if (updatingWidget || b != &widget) return;

            const float v = widget.getToggleState() ? 1.0f : 0.0f;
            detail::ReentryGuard g (updatingWidget);

            parameter.beginGesture();
            parameter.setValueFromUser (v);
            parameter.endGesture();
        }

        void buttonStateChanged (juce::Button*) override {}

    private:
        AutomationParameter& parameter;
        juce::Button&        widget;
        bool                 updatingWidget = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ButtonAutomationAttachment)
    };

    // -----------------------------------------------------------------------
    //  ComboBox attachment -- for stepped/discrete selectors
    // -----------------------------------------------------------------------

    class ComboBoxAutomationAttachment
        : public AutomationParameter::Listener
        , private juce::ComboBox::Listener
    {
    public:
        ComboBoxAutomationAttachment (AutomationParameter& param,
                                      juce::ComboBox&      box)
            : parameter (param)
            , widget    (box)
        {
            const auto& range = parameter.getRange();
            jassert (range.isStepped && range.numSteps > 0);

            const int initial = juce::jlimit (0, range.numSteps - 1,
                int (parameter.getNormalizedValue() * (range.numSteps - 1) + 0.5f));
            widget.setSelectedItemIndex (initial, juce::dontSendNotification);

            widget.addListener (this);
            parameter.addListener (this);
        }

        ~ComboBoxAutomationAttachment() override
        {
            widget.removeListener (this);
            parameter.removeListener (this);
        }

        void parameterValueChanged (AutomationParameter& p,
                                    float        newNormalized,
                                    ChangeSource source) override
        {
            if (source == ChangeSource::User && updatingWidget)
                return;

            const auto& range = p.getRange();
            const int idx = juce::jlimit (0, juce::jmax (0, range.numSteps - 1),
                int (newNormalized * juce::jmax (1, range.numSteps - 1) + 0.5f));

            detail::ReentryGuard g (updatingWidget);
            widget.setSelectedItemIndex (idx, juce::dontSendNotification);
        }

        void comboBoxChanged (juce::ComboBox* cb) override
        {
            if (updatingWidget || cb != &widget) return;

            const auto& range = parameter.getRange();
            const int idx     = widget.getSelectedItemIndex();
            const int steps   = juce::jmax (1, range.numSteps - 1);
            const float v     = juce::jlimit (0.0f, 1.0f, float (idx) / float (steps));

            detail::ReentryGuard g (updatingWidget);
            parameter.beginGesture();
            parameter.setValueFromUser (v);
            parameter.endGesture();
        }

    private:
        AutomationParameter& parameter;
        juce::ComboBox&      widget;
        bool                 updatingWidget = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComboBoxAutomationAttachment)
    };
}
