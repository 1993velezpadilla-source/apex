// ===========================================================================
// EditorToolButtonCore.h
// Single tool button for the toolbar.
// Shows icon + label + keyboard shortcut tooltip.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "EditorToolCore.h"

namespace ArrangementEditor
{
    class EditorToolButtonCore : public juce::Component
    {
    public:
        EditorToolButtonCore(EditorTool tool, EditorToolState& state);

        void paint(juce::Graphics& g) override;
        void resized() override {}

        void mouseDown(const juce::MouseEvent& e) override;
        void mouseEnter(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;

        std::function<void(EditorTool)> onClick;

    private:
        EditorTool       m_tool;
        EditorToolState& m_state;
        bool             m_hovered = false;

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }

        bool isActive() const { return m_state.getActiveTool() == m_tool; }
    };

} // namespace ArrangementEditor
