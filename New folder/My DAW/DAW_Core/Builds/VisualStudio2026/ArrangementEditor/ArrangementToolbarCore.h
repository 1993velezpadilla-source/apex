// ===========================================================================
// ArrangementToolbarCore.h
// Visible toolbar with tool buttons (Select, Split/Blade, Eraser, etc.)
// Shows at top of arrangement view with clear icons
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "EditorToolCore.h"
#include <functional>

namespace ArrangementEditor
{
    class ArrangementToolbarCore : public juce::Component,
                                   private EditorToolState::Listener
    {
    public:
        ArrangementToolbarCore(EditorToolState& toolState);
        void setMetronomeEnabledQuery(std::function<bool()> q) { m_isMetronomeEnabled = std::move(q); repaint(); }
        void setMetronomeToggleAction(std::function<void()> a) { m_toggleMetronome = std::move(a); }
        ~ArrangementToolbarCore() override;

        void paint(juce::Graphics& g) override;
        void resized() override;

        void setSelectedSnapModeIndex(int index);
        int getSelectedSnapModeIndex() const noexcept { return m_selectedSnapModeIndex; }

        std::function<void(EditorTool)> onToolChanged;
        std::function<void(int)> onSnapModeSelected;

    private:
        EditorToolState& m_toolState;

        std::function<bool()> m_isMetronomeEnabled;
        std::function<void()> m_toggleMetronome;

        struct ToolButton
        {
            EditorTool tool;
            juce::Rectangle<int> bounds;
            juce::String label;
            juce::String icon;
            bool hovered = false;
        };

        std::vector<ToolButton> m_buttons;
        juce::Rectangle<int> m_snapButtonBounds;
        juce::Rectangle<int> m_metronomeBounds;
        bool m_metronomeHovered = false;
        bool m_snapButtonHovered = false;
        int m_selectedSnapModeIndex = 0;

        void buildButtons();
        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void showSnapMenu();
        static juce::String snapModeLabelForIndex(int index);

        // EditorToolState::Listener
        void activeToolChanged(EditorTool newTool) override;

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
