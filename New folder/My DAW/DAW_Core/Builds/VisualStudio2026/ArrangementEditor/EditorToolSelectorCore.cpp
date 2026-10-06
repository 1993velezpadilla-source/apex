// ===========================================================================
// EditorToolSelectorCore.cpp
// ===========================================================================
#include "EditorToolSelectorCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t panel = 0xFF252525;
        constexpr uint32_t border = 0xFF404040;
    }

    EditorToolSelectorCore::EditorToolSelectorCore(EditorToolState& state)
        : m_state(state)
    {
        setSize(480, 56);
        buildButtons();

        m_state.onToolChanged = [this](EditorTool t) {
            for (auto& btn : m_buttons)
                btn->repaint();
            if (onToolChanged) onToolChanged(t);
        };
    }

    void EditorToolSelectorCore::buildButtons()
    {
        const EditorTool tools[] = {
            EditorTool::Select,
            EditorTool::Split,
            EditorTool::RazorEdit,
            EditorTool::Eraser,
            EditorTool::Draw,
            EditorTool::Glue,
            EditorTool::Mute,
            EditorTool::Zoom,
            EditorTool::TimeScrub
        };

        for (auto tool : tools)
        {
            auto btn = std::make_unique<EditorToolButtonCore>(tool, m_state);
            btn->onClick = [this](EditorTool t) {
                m_state.setActiveTool(t);
            };
            addAndMakeVisible(*btn);
            m_buttons.push_back(std::move(btn));
        }
    }

    void EditorToolSelectorCore::paint(juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(fromU32(Col::panel));
        g.fillRoundedRectangle(b, 4.f);
        g.setColour(fromU32(Col::border));
        g.drawRoundedRectangle(b.reduced(0.5f), 4.f, 1.f);
    }

    void EditorToolSelectorCore::resized()
    {
        int x = 4;
        for (auto& btn : m_buttons)
        {
            btn->setBounds(x, 4, 56, 48);
            x += 60;
        }
    }

} // namespace ArrangementEditor
