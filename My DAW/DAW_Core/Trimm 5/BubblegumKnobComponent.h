#pragma once
#include <JuceHeader.h>
#include "BubblegumKnobCore.h"

namespace DAW {

/**
 * BubblegumKnobComponent
 *
 * JUCE Component that assembles the Bubblegum knob from the seven Batch 2
 * nucleos. The knob is intentionally value-source agnostic: callers supply
 * a BubblegumKnobValueBinding whose lambdas read/write the actual parameter
 * (per-track input trim, pan, send level, plugin parameter, anything).
 *
 * Per-track uniqueness: the binding's get/set lambdas close over a specific
 * track's parameter. Each panel constructs a fresh binding pointing at its
 * own track. Two panels for two tracks have independent bindings — the knob
 * never sees or caches a shared value.
 *
 * Interaction:
 *   - vertical drag         : change value (sensitivity from interaction config)
 *   - Shift + drag          : fine mode (10x slower)
 *   - Ctrl + drag           : snap to integers / unity
 *   - double-click          : reset to binding.defaultValue
 *
 * Painting order: halo -> track (incl. active arc) -> body -> indicator.
 * Body never rotates; only the indicator. Highlight directions stay
 * consistent regardless of value.
 */
class BubblegumKnobComponent : public juce::Component
{
public:
    BubblegumKnobComponent() = default;

    explicit BubblegumKnobComponent(BubblegumKnobValueBinding binding)
    {
        setBinding(std::move(binding));
    }

    /** Replace the value binding. Reconfigures the scale's unity value. */
    void setBinding(BubblegumKnobValueBinding b)
    {
        binding_ = std::move(b);

        BubblegumKnobScaleCore::Config sc;
        sc.mode        = BubblegumKnobScaleCore::Mode::BipolarUnity;
        sc.unityValue  = binding_.defaultValue;
        sc.minAngleDeg = -135.0f;
        sc.maxAngleDeg =  135.0f;
        scale_.setConfig(sc);

        repaint();
    }

    BubblegumKnobValueBinding&       getBinding()       { return binding_; }
    const BubblegumKnobValueBinding& getBinding() const { return binding_; }

    /** Optional callback fired whenever the user changes the value. */
    std::function<void()> onValueChanged;

    void paint(juce::Graphics& g) override
    {
        if (! binding_.isValid()) return;

        const auto centre = getLocalBounds().toFloat().getCentre();
        const float bodyR = juce::jmin(getWidth(), getHeight()) * 0.5f - 12.0f;
        if (bodyR <= 0.0f) return;

        const float currentValue = binding_.read();
        const float currentAngle = scale_.valueToAngleDeg(currentValue,
                                                          binding_.minValue,
                                                          binding_.maxValue);
        const float unityAngle   = scale_.unityAngleDeg();

        halo_.paint(g, centre, bodyR);
        track_.paint(g, centre, bodyR,
                     scale_.minAngleDeg(), scale_.maxAngleDeg(),
                     currentAngle, unityAngle);
        body_.paint(g, centre, bodyR);
        indicator_.paint(g, centre, currentAngle);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (! binding_.isValid()) return;
        interaction_.beginDrag(binding_.read(), e.getScreenY());
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (! binding_.isValid() || ! interaction_.isDragging()) return;
        const float v = interaction_.computeDragValue(
            e.getScreenY(),
            binding_.minValue, binding_.maxValue, binding_.defaultValue,
            e.mods.isShiftDown(), e.mods.isCtrlDown());
        binding_.write(v);
        if (onValueChanged) onValueChanged();
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        interaction_.endDrag();
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (! binding_.isValid()) return;
        binding_.write(binding_.defaultValue);
        if (onValueChanged) onValueChanged();
        repaint();
    }

private:
    BubblegumKnobValueBinding       binding_;
    BubblegumKnobInteractionCore    interaction_;
    BubblegumKnobScaleCore          scale_;
    BubblegumKnobHaloRenderer       halo_;
    BubblegumKnobTrackRenderer      track_;
    BubblegumKnobBodyRenderer       body_;
    BubblegumKnobIndicatorRenderer  indicator_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumKnobComponent)
};

} // namespace DAW
