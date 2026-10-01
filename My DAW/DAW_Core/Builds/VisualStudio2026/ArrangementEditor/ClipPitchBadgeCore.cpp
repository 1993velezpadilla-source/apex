// ===========================================================================
// ClipPitchBadgeCore.cpp
// ===========================================================================
#include "ClipPitchBadgeCore.h"
#include "ClipPitchCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t accent = 0xFFFF1678;
        constexpr uint32_t text = 0xFFFFFFFF;
    }

    ClipPitchBadgeCore::ClipPitchBadgeCore(const ArrangementClipModel& model)
        : m_model(&model)
    {
        setSize(40, 14);
        setInterceptsMouseClicks(false, false);
    }

    void ClipPitchBadgeCore::setModel(const ArrangementClipModel& model)
    {
        m_model = &model;
        repaint();
    }

    void ClipPitchBadgeCore::paint(juce::Graphics& g)
    {
        if (!m_model || !ClipPitchCore::isPitchShifted(m_model->pitch))
        {
            setVisible(false);
            return;
        }

        setVisible(true);

        char buf[16];
        snprintf(buf, sizeof(buf), "%+.0fst", m_model->pitch);

        g.setColour(fromU32(Col::accent).withAlpha(0.8f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.f);

        g.setColour(fromU32(Col::text));
        g.setFont(juce::Font("Segoe UI", 8.f, juce::Font::bold));
        g.drawText(juce::String(buf), getLocalBounds(),
                   juce::Justification::centred);
    }

} // namespace ArrangementEditor
