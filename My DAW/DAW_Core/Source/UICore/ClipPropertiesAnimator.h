#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * ClipPropertiesAnimator — Adds smooth animations to clip properties window
 * 
 * Features:
 * - Knob highlight pulse when value changes
 * - Button press animations
 * - Waveform shimmer on hover
 * - Smooth color transitions
 */
class ClipPropertiesAnimator : public juce::Component,
                               private juce::Timer
{
public:
    ClipPropertiesAnimator()
    {
        setInterceptsMouseClicks(false, false);
        setAlwaysOnTop(false);
        startTimerHz(60);
    }

    void triggerKnobPulse(juce::Component* knob)
    {
        if (!knob) return;

        KnobPulse pulse;
        pulse.component = knob;
        pulse.phase = 0.0f;
        pulse.strength = 1.0f;
        knobPulses_.push_back(pulse);
    }

    void triggerButtonPress(juce::Component* button)
    {
        if (!button) return;

        ButtonPress press;
        press.component = button;
        press.phase = 0.0f;
        buttonPresses_.push_back(press);
    }

    void setWaveformHovered(bool hovered)
    {
        waveformHovered_ = hovered;
    }

    void paint(juce::Graphics& g) override
    {
        // Draw knob pulses
        for (const auto& pulse : knobPulses_)
        {
            if (!pulse.component || !pulse.component->isVisible()) continue;

            auto bounds = pulse.component->getBounds().toFloat();
            const float radius = std::max(bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const float expandedRadius = radius + 20.f * pulse.phase;
            const float alpha = (1.0f - pulse.phase) * pulse.strength * 0.3f;

            g.setColour(juce::Colour(0xFF5BC8FF).withAlpha(alpha));
            g.drawEllipse(bounds.getCentreX() - expandedRadius,
                         bounds.getCentreY() - expandedRadius,
                         expandedRadius * 2.f,
                         expandedRadius * 2.f,
                         2.f);
        }

        // Waveform shimmer (subtle animated glow)
        if (waveformHovered_ && waveformComponent_)
        {
            auto bounds = waveformComponent_->getBounds().toFloat();
            const float shimmerPhase = waveformShimmerPhase_;
            const float alpha = 0.08f + 0.04f * std::sin(shimmerPhase);

            juce::ColourGradient shimmer(
                juce::Colour(0xFF5BC8FF).withAlpha(alpha), bounds.getX(), bounds.getY(),
                juce::Colours::transparentBlack, bounds.getX(), bounds.getY() + 40.f, false
            );
            g.setGradientFill(shimmer);
            g.fillRoundedRectangle(bounds, 5.f);
        }
    }

    void setWaveformComponent(juce::Component* waveform)
    {
        waveformComponent_ = waveform;
    }

private:
    struct KnobPulse
    {
        juce::Component* component = nullptr;
        float phase = 0.0f;
        float strength = 1.0f;
    };

    struct ButtonPress
    {
        juce::Component* component = nullptr;
        float phase = 0.0f;
    };

    void timerCallback() override
    {
        bool needsRepaint = false;

        // Update knob pulses
        for (auto it = knobPulses_.begin(); it != knobPulses_.end(); )
        {
            it->phase += 0.06f;
            if (it->phase >= 1.0f)
                it = knobPulses_.erase(it);
            else
            {
                ++it;
                needsRepaint = true;
            }
        }

        // Update button presses
        for (auto it = buttonPresses_.begin(); it != buttonPresses_.end(); )
        {
            it->phase += 0.15f;
            if (it->phase >= 1.0f)
                it = buttonPresses_.erase(it);
            else
            {
                ++it;
                needsRepaint = true;
            }
        }

        // Update waveform shimmer
        if (waveformHovered_)
        {
            waveformShimmerPhase_ += 0.08f;
            if (waveformShimmerPhase_ > juce::MathConstants<float>::twoPi)
                waveformShimmerPhase_ -= juce::MathConstants<float>::twoPi;
            needsRepaint = true;
        }

        if (needsRepaint)
            repaint();
    }

    std::vector<KnobPulse> knobPulses_;
    std::vector<ButtonPress> buttonPresses_;
    juce::Component* waveformComponent_ = nullptr;
    bool waveformHovered_ = false;
    float waveformShimmerPhase_ = 0.0f;
};

} // namespace DAW
