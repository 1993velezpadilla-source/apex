// ===========================================================================
// EditorToolButtonCore.cpp
// ===========================================================================
#include "EditorToolButtonCore.h"

namespace ArrangementEditor
{
    // Colour palette (from SampleSettings)
    namespace Col
    {
        constexpr uint32_t bg           = 0xFF1E1E1E;
        constexpr uint32_t panel        = 0xFF252525;
        constexpr uint32_t text         = 0xFFD4D4D4;
        constexpr uint32_t textDim      = 0xFF707070;
        constexpr uint32_t accent       = 0xFFFF1678;
        constexpr uint32_t btnNormal    = 0xFF323232;
        constexpr uint32_t btnHover     = 0xFF3E3E3E;
        constexpr uint32_t btnActive    = 0xFFFF1678;
        constexpr uint32_t border       = 0xFF404040;
    }

    EditorToolButtonCore::EditorToolButtonCore(EditorTool tool, EditorToolState& state)
        : m_tool(tool), m_state(state)
    {
        setSize(56, 48);
        // Tooltip would be set by parent component if needed
    }

    void EditorToolButtonCore::paint(juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat().reduced(2.f);

        // Background
        uint32_t fillCol = isActive() ? Col::btnActive
                         : m_hovered  ? Col::btnHover
                         : Col::btnNormal;

        g.setColour(fromU32(fillCol));
        g.fillRoundedRectangle(b, 4.f);

        if (isActive())
        {
            g.setColour(fromU32(Col::accent));
            g.drawRoundedRectangle(b, 4.f, 1.5f);
        }

        // Icon placeholder (TODO: replace with actual icons)
        float iconY = b.getY() + 8.f;
        g.setColour(fromU32(isActive() ? 0xFFFFFFFF : Col::text));
        g.setFont(juce::Font("Segoe UI", 16.f, juce::Font::bold));
        g.drawText(juce::String::charToString(toolShortcut(m_tool)),
                   (int)b.getX(), (int)iconY, (int)b.getWidth(), 20,
                   juce::Justification::centred);

        // Label
        g.setColour(fromU32(isActive() ? 0xFFFFFFFF : Col::textDim));
        g.setFont(juce::Font("Segoe UI", 7.5f, juce::Font::plain));
        g.drawText(juce::String(toolName(m_tool)),
                   (int)b.getX(), (int)b.getBottom() - 14, (int)b.getWidth(), 12,
                   juce::Justification::centred);
    }

    void EditorToolButtonCore::mouseDown(const juce::MouseEvent&)
    {
        if (onClick) onClick(m_tool);
    }

    void EditorToolButtonCore::mouseEnter(const juce::MouseEvent&)
    {
        m_hovered = true;
        repaint();
    }

    void EditorToolButtonCore::mouseExit(const juce::MouseEvent&)
    {
        m_hovered = false;
        repaint();
    }

} // namespace ArrangementEditor
