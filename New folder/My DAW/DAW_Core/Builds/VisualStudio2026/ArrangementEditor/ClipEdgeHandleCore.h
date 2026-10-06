// ===========================================================================
// ClipEdgeHandleCore.h
// Left and right resize handles for clips.
// Drag left = trim start, drag right = trim end / extend.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "ArrangementClipModel.h"
#include "ArrangementZoomCore.h"
#include <functional>

namespace ArrangementEditor
{
    enum class EdgeHandleType { Left, Right };

    class ClipEdgeHandleCore : public juce::Component
    {
    public:
        ClipEdgeHandleCore(ArrangementClipModel& model,
                           ArrangementZoomCore& zoom,
                           EdgeHandleType type);

        void paint(juce::Graphics& g) override;
        void resized() override {}

        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseEnter(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;

        std::function<void()> onEdgeChanged;

    private:
        ArrangementClipModel& m_model;
        ArrangementZoomCore&  m_zoom;
        EdgeHandleType        m_type;

        float  m_dragStartX = 0.f;
        double m_dragStartTime = 0.0;
        double m_dragStartOffset = 0.0;
        double m_dragStartLength = 0.0;
        bool   m_dragging = false;
        bool   m_hovered = false;

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
