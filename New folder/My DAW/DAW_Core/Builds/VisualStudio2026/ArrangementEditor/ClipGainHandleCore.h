// ===========================================================================
// ClipGainHandleCore.h
// Horizontal gain drag line inside clips.
// Dragging up/down changes clip gain, shows dB tooltip.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "ArrangementClipModel.h"
#include <functional>

namespace ArrangementEditor
{
    class ClipGainHandleCore : public juce::Component
    {
    public:
        explicit ClipGainHandleCore(ArrangementClipModel& model);

        void paint(juce::Graphics& g) override;
        void resized() override {}

        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;

        std::function<void(float)> onGainChanged;

    private:
        ArrangementClipModel& m_model;

        float m_dragStartY = 0.f;
        float m_dragStartGain = 1.f;
        bool  m_dragging = false;

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
