// ===========================================================================
// ClipEdgeHandleCore.cpp
// ===========================================================================
#include "ClipEdgeHandleCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t accent = 0xFFE07B39;
        constexpr uint32_t border = 0xFF404040;
    }

    ClipEdgeHandleCore::ClipEdgeHandleCore(ArrangementClipModel& model,
                                            ArrangementZoomCore& zoom,
                                            EdgeHandleType type)
        : m_model(model), m_zoom(zoom), m_type(type)
    {
        setSize(8, 80);
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }

    void ClipEdgeHandleCore::paint(juce::Graphics& g)
    {
        if (!m_hovered && !m_dragging)
            return;

        auto b = getLocalBounds().toFloat();
        g.setColour(fromU32(m_dragging ? Col::accent : Col::border).withAlpha(0.6f));
        g.fillRect(b);
    }

    void ClipEdgeHandleCore::mouseDown(const juce::MouseEvent& e)
    {
        m_dragStartX = (float)e.getPosition().x;
        m_dragStartTime = m_model.startTime;
        m_dragStartOffset = m_model.sourceOffset;
        m_dragStartLength = m_model.length;
        m_dragging = true;
        repaint();
    }

    void ClipEdgeHandleCore::mouseDrag(const juce::MouseEvent& e)
    {
        if (!m_dragging) return;

        float dx = (float)e.getPosition().x - m_dragStartX;
        double deltaSec = dx / m_zoom.getPixelsPerSecond();

        if (m_type == EdgeHandleType::Left)
        {
            // LEFT RESIZE — trims the start of the clip.
            //
            // CORRECT BEHAVIOR (matches right-resize and all pro DAWs):
            //   startTime  moves forward/back
            //   sourceOffset moves by the same delta
            //   length adjusts to compensate
            //   → The audio content anchored at the RIGHT edge never moves.
            //   → The left edge reveals or hides source material.
            //
            // WRONG (old) BEHAVIOR:
            //   sourceOffset stayed fixed → left-trim moved audio content inside clip
            //   → This caused slip/drift on left resize (the reported bug).

            double newStart  = m_dragStartTime   + deltaSec;
            double newOffset = m_dragStartOffset + deltaSec;
            double newLength = m_dragStartLength - deltaSec;

            // Keep minimum clip length and prevent negative source offset
            if (newLength > 0.01 && newOffset >= 0.0)
            {
                m_model.startTime    = newStart;
                m_model.sourceOffset = newOffset;
                m_model.length       = newLength;
            }
        }
        else
        {
            // RIGHT RESIZE — extends/trims the end.
            // startTime and sourceOffset stay fixed; only length changes.
            double newLength = m_dragStartLength + deltaSec;
            if (newLength > 0.01)
            {
                m_model.length = newLength;
            }
        }

        if (onEdgeChanged)
            onEdgeChanged();

        repaint();
    }

    void ClipEdgeHandleCore::mouseUp(const juce::MouseEvent&)
    {
        m_dragging = false;
        repaint();
    }

    void ClipEdgeHandleCore::mouseEnter(const juce::MouseEvent&)
    {
        m_hovered = true;
        repaint();
    }

    void ClipEdgeHandleCore::mouseExit(const juce::MouseEvent&)
    {
        m_hovered = false;
        repaint();
    }

} // namespace ArrangementEditor
