// ===========================================================================
// ClipFadeHandleCore.h
// Draggable fade triangles at clip edges.
// Mouse drag changes fadeInLength / fadeOutLength.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "ArrangementClipModel.h"
#include "ArrangementZoomCore.h"
#include <functional>

namespace ArrangementEditor
{
    enum class FadeHandleType { In, Out };

    class ClipFadeHandleCore : public juce::Component
    {
    public:
        ClipFadeHandleCore(ArrangementClipModel& model,
                           ArrangementZoomCore& zoom,
                           FadeHandleType type);

        void paint(juce::Graphics& g) override;
        void resized() override {}

        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;

        std::function<void(double)> onFadeChanged;

    private:
        ArrangementClipModel& m_model;
        ArrangementZoomCore&  m_zoom;
        FadeHandleType        m_type;

        float m_dragStartX = 0.f;
        double m_dragStartFade = 0.0;
        bool m_dragging = false;

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
