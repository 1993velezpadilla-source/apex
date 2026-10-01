// ===========================================================================
// ClipFadeHandleCore.cpp
// ===========================================================================
#include "ClipFadeHandleCore.h"
#include "../Source/UICore/CursorThemeCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t accent = 0xFFFF1678;
    }

    ClipFadeHandleCore::ClipFadeHandleCore(ArrangementClipModel& model,
                                            ArrangementZoomCore& zoom,
                                            FadeHandleType type)
        : m_model(model), m_zoom(zoom), m_type(type)
    {
        setSize(20, 20);
        setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::LeftRightResizeCursor));
    }

    void ClipFadeHandleCore::paint(juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();

        g.setColour(fromU32(Col::accent).withAlpha(m_dragging ? 0.8f : 0.5f));

        juce::Path triangle;
        if (m_type == FadeHandleType::In)
        {
            triangle.addTriangle(b.getX(), b.getBottom(),
                                 b.getRight(), b.getBottom(),
                                 b.getRight(), b.getY());
        }
        else
        {
            triangle.addTriangle(b.getX(), b.getY(),
                                 b.getX(), b.getBottom(),
                                 b.getRight(), b.getBottom());
        }

        g.fillPath(triangle);
    }

    void ClipFadeHandleCore::mouseDown(const juce::MouseEvent& e)
    {
        m_dragStartX = (float)e.getPosition().x;
        m_dragStartFade = (m_type == FadeHandleType::In) ? m_model.fadeInLength
                                                          : m_model.fadeOutLength;
        m_dragging = true;
        repaint();
    }

    void ClipFadeHandleCore::mouseDrag(const juce::MouseEvent& e)
    {
        if (!m_dragging) return;

        float dx = (float)e.getPosition().x - m_dragStartX;
        double deltaSec = dx / m_zoom.getPixelsPerSecond();

        double newFade = juce::jlimit(0.0, m_model.length,
                                      m_dragStartFade + deltaSec);

        if (m_type == FadeHandleType::In)
            m_model.fadeInLength = newFade;
        else
            m_model.fadeOutLength = newFade;

        if (onFadeChanged)
            onFadeChanged(newFade);

        repaint();
    }

    void ClipFadeHandleCore::mouseUp(const juce::MouseEvent&)
    {
        m_dragging = false;
        repaint();
    }

} // namespace ArrangementEditor
