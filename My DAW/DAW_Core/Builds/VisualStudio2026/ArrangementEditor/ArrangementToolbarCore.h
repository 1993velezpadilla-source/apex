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
                                   public juce::TooltipClient,
                                   private EditorToolState::Listener
    {
    public:
        ArrangementToolbarCore(EditorToolState& toolState);
        void setMetronomeEnabledQuery(std::function<bool()> q) { m_isMetronomeEnabled = std::move(q); repaint(); }
        void setMetronomeToggleAction(std::function<void()> a) { m_toggleMetronome = std::move(a); }
        ~ArrangementToolbarCore() override;

        void paint(juce::Graphics& g) override;
        void resized() override;

        juce::String getTooltip() override { return m_currentTooltip; }

        void setSelectedSnapModeIndex(int index);
        int getSelectedSnapModeIndex() const noexcept { return m_selectedSnapModeIndex; }

        std::function<void(EditorTool)> onToolChanged;
        std::function<void(int)> onSnapModeSelected;

        // Compact "+" add-track button (no permanent "Add New Track" text).
        std::function<void()> onAddTrackRequested;

        /** Real button component for the Quick New Track entry point.
         *  As a real JUCE component it consumes its own mouse/touch events
         *  exclusively — a pointer-down on "+" can never fall through to a
         *  lane, clip, or track scrolled underneath the pinned toolbar. */
        class AddTrackButton final : public juce::Button
        {
        public:
            AddTrackButton() : juce::Button("Quick New Track") {}

            // APEX signal-core tokens (mirrors the toolbar's local Col set —
            // kept private to this component so it never collides with the
            // per-file Col namespaces used across ArrangementEditor).
            static constexpr uint32_t kBtnNormal = 0xFF111522; // panelC
            static constexpr uint32_t kBtnHover  = 0xFF1A2233; // borderSoftA
            static constexpr uint32_t kBorder    = 0xFF22283A; // borderSoftB
            static constexpr uint32_t kAccent    = 0xFFFF1678; // magenta

            void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                             bool shouldDrawButtonAsDown) override
            {
                const auto bounds = getLocalBounds().toFloat();
                const auto base = shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted
                    ? fromU32(kBtnHover)
                    : fromU32(kBtnNormal);
                g.setColour(base);
                g.fillRoundedRectangle(bounds, 4.f);
                g.setColour(fromU32(kBorder));
                g.drawRoundedRectangle(bounds.reduced(0.5f), 4.f, 1.f);
                g.setColour(fromU32(kAccent));
                g.setFont(juce::Font("Segoe UI", 20.f, juce::Font::plain));
                g.drawText("+", getLocalBounds(), juce::Justification::centred);
            }

        private:
            static juce::Colour fromU32(uint32_t c)
            {
                return juce::Colour(
                    (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                    (uint8_t)c,         (uint8_t)(c >> 24));
            }
        };

        /** Screen/anchor bounds of the Quick New Track button (for the Quick
         *  Track Builder popup anchoring and for tests). */
        juce::Rectangle<int> getAddTrackButtonBounds() const noexcept { return m_addTrackBounds; }
        juce::Rectangle<int> getAddTrackButtonScreenBounds() const
        {
            return m_addTrackBounds.isEmpty() ? m_addTrackBounds
                                              : m_addTrackBounds + getScreenPosition();
        }

        // Automation record arm toggle
        void setAutoArmEnabledQuery(std::function<bool()> q) { m_isAutoArmEnabled = std::move(q); repaint(); }
        void setAutoArmToggleAction(std::function<void()> a) { m_toggleAutoArm = std::move(a); }

    private:
        EditorToolState& m_toolState;

        std::function<bool()> m_isMetronomeEnabled;
        std::function<void()> m_toggleMetronome;
        std::function<bool()> m_isAutoArmEnabled;
        std::function<void()> m_toggleAutoArm;

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
        juce::Rectangle<int> m_autoArmBounds;
        juce::Rectangle<int> m_addTrackBounds;
        AddTrackButton m_addTrackBtn;
        bool m_metronomeHovered = false;
        bool m_snapButtonHovered = false;
        bool m_autoArmHovered = false;
        int m_selectedSnapModeIndex = 0;
        juce::String m_currentTooltip;

        void buildButtons();
        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void showSnapMenu();
        void updateTooltip(juce::Point<int> pos);
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
