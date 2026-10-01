// ===========================================================================
// ClipGainHandleCore.cpp
// ===========================================================================
#include "ClipGainHandleCore.h"
#include "ClipGainCore.h"
#include "../Source/UICore/CursorThemeCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t accent = 0xFFFF1678;
    }

    ClipGainHandleCore::ClipGainHandleCore(ArrangementClipModel& model)
        : m_model(model)
    {
        setSize(100, 80);
        setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::UpDownResizeCursor));
    }

    void ClipGainHandleCore::paint(juce::Graphics& g)
    {
        // Line is drawn by ClipRenderCore, this is just the interactive overlay
    }

    void ClipGainHandleCore::mouseDown(const juce::MouseEvent& e)
    {
        m_dragStartY = (float)e.getPosition().y;
        m_dragStartGain = m_model.gain;
        m_dragging = true;

        // Tooltip will be shown by parent component
    }

    void ClipGainHandleCore::mouseDrag(const juce::MouseEvent& e)
    {
        if (!m_dragging) return;

        float dy = m_dragStartY - (float)e.getPosition().y;
        float sensitivity = e.mods.isCtrlDown() ? 0.002f : 0.01f;

        float range = ClipGainCore::kMaxLinear - ClipGainCore::kMinLinear;
        m_model.gain = juce::jlimit(ClipGainCore::kMinLinear, ClipGainCore::kMaxLinear,
                                    m_dragStartGain + dy * sensitivity * range);

        if (onGainChanged)
            onGainChanged(m_model.gain);

        repaint();
    }

    void ClipGainHandleCore::mouseUp(const juce::MouseEvent&)
    {
        m_dragging = false;
    }

} // namespace ArrangementEditor
