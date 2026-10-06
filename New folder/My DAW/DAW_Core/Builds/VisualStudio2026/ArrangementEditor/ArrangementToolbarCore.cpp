// ===========================================================================
// ArrangementToolbarCore.cpp
// ===========================================================================
#include "ArrangementToolbarCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t bg = 0xFF2A2A2A;
        constexpr uint32_t text = 0xFFD4D4D4;
        constexpr uint32_t accent = 0xFFE07B39;
        constexpr uint32_t btnNormal = 0xFF383838;
        constexpr uint32_t btnHover = 0xFF454545;
        constexpr uint32_t border = 0xFF505050;
    }

    ArrangementToolbarCore::ArrangementToolbarCore(EditorToolState& toolState)
        : m_toolState(toolState)
    {
        // Keep consistent with TrackList top header (48px) so the arrangement content
        // doesn't overlap the toolbar when laid out.
        setSize(800, 48);
        buildButtons();

        m_toolState.addListener(this);
    }

    ArrangementToolbarCore::~ArrangementToolbarCore()
    {
        m_toolState.removeListener(this);
    }

    void ArrangementToolbarCore::buildButtons()
    {
        m_buttons.clear();

        const struct { EditorTool tool; juce::String label; juce::String icon; } tools[] = {
            { EditorTool::Select,    "Select",    juce::CharPointer_UTF8("\xe2\x86\x92") },
            { EditorTool::Split,     "Blade",     juce::CharPointer_UTF8("\xe2\x9c\x82") },
            { EditorTool::RazorEdit, "Razor",     juce::CharPointer_UTF8("\xe2\x96\xb0") },
            { EditorTool::Eraser,    "Eraser",    juce::CharPointer_UTF8("\xe2\x8c\xab") },
            { EditorTool::Draw,      "Draw",      juce::CharPointer_UTF8("\xe2\x9c\x8e") },
            { EditorTool::Glue,      "Glue",      juce::CharPointer_UTF8("\xe2\x8a\x95") },
            { EditorTool::Mute,      "Mute",      juce::CharPointer_UTF8("\xf0\x9f\x94\x87") },
            { EditorTool::Stretch,   "Stretch",   juce::CharPointer_UTF8("\xe2\x86\x94") },
            { EditorTool::Zoom,      "Zoom",      juce::CharPointer_UTF8("\xf0\x9f\x94\x8d") },
            { EditorTool::TimeScrub, "Scrub",     juce::CharPointer_UTF8("\xe2\x8f\xa9") }
        };

        int x = 10;
        for (auto& t : tools)
        {
            ToolButton btn;
            btn.tool = t.tool;
            btn.label = t.label;
            btn.icon = t.icon;
            // Keep the toolbar compact vertically (48px tall) while still allowing
            // larger hit targets.
            btn.bounds = juce::Rectangle<int>(x, 6, 82, 36);
            m_buttons.push_back(btn);
            x += 86;
        }
    }

    void ArrangementToolbarCore::paint(juce::Graphics& g)
    {
        auto b = getLocalBounds();

        // Background
        g.setColour(fromU32(Col::bg));
        g.fillRect(b);

        // Bottom border
        g.setColour(fromU32(Col::border));
        g.drawHorizontalLine(b.getBottom() - 1, 0.f, (float)b.getWidth());

        // Metronome button
        if (!m_metronomeBounds.isEmpty())
        {
            const bool active = m_isMetronomeEnabled ? m_isMetronomeEnabled() : false;
            const auto base = active ? fromU32(Col::accent) : fromU32(m_metronomeHovered ? Col::btnHover : Col::btnNormal);
            g.setColour(base);
            g.fillRoundedRectangle(m_metronomeBounds.toFloat(), 4.f);
            g.setColour(fromU32(active ? Col::accent : Col::border));
            g.drawRoundedRectangle(m_metronomeBounds.toFloat().reduced(0.5f), 4.f, 1.f);

            g.setColour(fromU32(active ? 0xFFFFFFFF : Col::text));
            g.setFont(juce::Font("Segoe UI", 18.f, juce::Font::plain));
            g.drawText(juce::String(juce::CharPointer_UTF8("\xF0\x9F\x8E\xB5")), m_metronomeBounds.withTrimmedBottom(12), juce::Justification::centred);
            g.setFont(juce::Font("Segoe UI", 10.f, juce::Font::plain));
            g.drawText("Metro", m_metronomeBounds.withTrimmedTop(20), juce::Justification::centred);
        }

        // Draw buttons
        for (auto& btn : m_buttons)
        {
            bool isActive = (m_toolState.getActiveTool() == btn.tool);

            // Button background
            if (isActive)
            {
                g.setColour(fromU32(Col::accent));
                g.fillRoundedRectangle(btn.bounds.toFloat(), 4.f);
            }
            else if (btn.hovered)
            {
                g.setColour(fromU32(Col::btnHover));
                g.fillRoundedRectangle(btn.bounds.toFloat(), 4.f);
            }
            else
            {
                g.setColour(fromU32(Col::btnNormal));
                g.fillRoundedRectangle(btn.bounds.toFloat(), 4.f);
            }

            // Border
            g.setColour(fromU32(isActive ? Col::accent : Col::border));
            g.drawRoundedRectangle(btn.bounds.toFloat().reduced(0.5f), 4.f, 1.f);

            // Icon
            g.setColour(fromU32(isActive ? 0xFFFFFFFF : Col::text));
            g.setFont(juce::Font("Segoe UI", 20.f, juce::Font::plain));
            g.drawText(btn.icon, btn.bounds.withTrimmedBottom(12), juce::Justification::centred);

            // Label
            g.setFont(juce::Font("Segoe UI", 11.f, juce::Font::plain));
            g.drawText(btn.label, btn.bounds.withTrimmedTop(18), juce::Justification::centred);

            // Keyboard shortcut hint
            char key = toolShortcut(btn.tool);
            if (key != ' ')
            {
                g.setColour(fromU32(Col::text).withAlpha(0.5f));
                g.setFont(juce::Font("Segoe UI", 8.f, juce::Font::plain));
                g.drawText(juce::String::charToString(key),
                          btn.bounds.getX() + 2, btn.bounds.getY() + 2, 12, 12,
                          juce::Justification::centred);
            }
        }

        const bool snapActive = m_selectedSnapModeIndex > 0;
        if (snapActive)
        {
            g.setColour(fromU32(Col::accent));
            g.fillRoundedRectangle(m_snapButtonBounds.toFloat(), 4.f);
        }
        else if (m_snapButtonHovered)
        {
            g.setColour(fromU32(Col::btnHover));
            g.fillRoundedRectangle(m_snapButtonBounds.toFloat(), 4.f);
        }
        else
        {
            g.setColour(fromU32(Col::btnNormal));
            g.fillRoundedRectangle(m_snapButtonBounds.toFloat(), 4.f);
        }

        g.setColour(fromU32(snapActive ? Col::accent : Col::border));
        g.drawRoundedRectangle(m_snapButtonBounds.toFloat().reduced(0.5f), 4.f, 1.f);

        g.setColour(fromU32(snapActive ? 0xFFFFFFFF : Col::text));
        g.setFont(juce::Font("Segoe UI", 10.f, juce::Font::plain));
        g.drawText("Snap", m_snapButtonBounds.getX(), m_snapButtonBounds.getY() + 3,
                   m_snapButtonBounds.getWidth(), 10, juce::Justification::centred);

        g.setFont(juce::Font("Segoe UI", 11.f, juce::Font::bold));
        g.drawText(snapModeLabelForIndex(m_selectedSnapModeIndex),
                   m_snapButtonBounds.getX() + 6, m_snapButtonBounds.getY() + 13,
                   m_snapButtonBounds.getWidth() - 18, 14, juce::Justification::centredLeft, true);

        juce::Path arrow;
        const float arrowCx = (float)m_snapButtonBounds.getRight() - 9.0f;
        const float arrowCy = (float)m_snapButtonBounds.getCentreY() + 3.0f;
        arrow.addTriangle(arrowCx - 3.0f, arrowCy - 1.5f,
                          arrowCx + 3.0f, arrowCy - 1.5f,
                          arrowCx,         arrowCy + 2.5f);
        g.fillPath(arrow);
    }

    void ArrangementToolbarCore::resized()
    {
        buildButtons();
        int x = m_buttons.empty() ? 8 : (m_buttons.back().bounds.getRight() + 12);
        m_snapButtonBounds = juce::Rectangle<int>(x, 6, 104, 36);

        x = m_snapButtonBounds.getRight() + 10;
        m_metronomeBounds = juce::Rectangle<int>(x, 6, 96, 36);
    }

    void ArrangementToolbarCore::setSelectedSnapModeIndex(int index)
    {
        index = juce::jlimit(0, 8, index);
        if (m_selectedSnapModeIndex == index)
            return;

        m_selectedSnapModeIndex = index;
        repaint();
    }

    void ArrangementToolbarCore::mouseMove(const juce::MouseEvent& e)
    {
        bool needsRepaint = false;
        for (auto& btn : m_buttons)
        {
            bool wasHovered = btn.hovered;
            btn.hovered = btn.bounds.contains(e.getPosition());
            if (wasHovered != btn.hovered)
                needsRepaint = true;
        }
        const bool wasSnapHovered = m_snapButtonHovered;
        m_snapButtonHovered = m_snapButtonBounds.contains(e.getPosition());
        if (wasSnapHovered != m_snapButtonHovered)
            needsRepaint = true;

        const bool wasMetroHovered = m_metronomeHovered;
        m_metronomeHovered = m_metronomeBounds.contains(e.getPosition());
        if (wasMetroHovered != m_metronomeHovered)
            needsRepaint = true;
        if (needsRepaint)
            repaint();
    }

    void ArrangementToolbarCore::mouseExit(const juce::MouseEvent&)
    {
        for (auto& btn : m_buttons)
            btn.hovered = false;
        m_snapButtonHovered = false;
        m_metronomeHovered = false;
        repaint();
    }

    void ArrangementToolbarCore::mouseDown(const juce::MouseEvent& e)
    {
        if (m_snapButtonBounds.contains(e.getPosition()))
        {
            showSnapMenu();
            return;
        }

        if (m_metronomeBounds.contains(e.getPosition()))
        {
            if (m_toggleMetronome)
                m_toggleMetronome();
            repaint();
            return;
        }

        for (auto& btn : m_buttons)
        {
            if (btn.bounds.contains(e.getPosition()))
            {
                DBG("[Toolbar] Button clicked: " << toolName(btn.tool));
                m_toolState.setActiveTool(btn.tool);
                if (onToolChanged) onToolChanged(btn.tool);
                repaint();
                return;
            }
        }
    }

    void ArrangementToolbarCore::activeToolChanged(EditorTool newTool)
    {
        DBG("[Toolbar] Listener received active tool: " << toolName(newTool));
        repaint();
    }

    void ArrangementToolbarCore::showSnapMenu()
    {
        juce::PopupMenu menu;
        for (int i = 0; i <= 8; ++i)
            menu.addItem(i + 1, snapModeLabelForIndex(i), true, i == m_selectedSnapModeIndex);

        const auto targetArea = localAreaToGlobal(m_snapButtonBounds);
        auto options = juce::PopupMenu::Options().withTargetComponent(this)
                                                .withTargetScreenArea(targetArea);
        juce::Component::SafePointer<ArrangementToolbarCore> safeThis(this);
        menu.showMenuAsync(options, [safeThis](int result)
        {
            if (safeThis == nullptr || result <= 0)
                return;

            safeThis->setSelectedSnapModeIndex(result - 1);
            if (safeThis->onSnapModeSelected)
                safeThis->onSnapModeSelected(result - 1);
        });
    }

    juce::String ArrangementToolbarCore::snapModeLabelForIndex(int index)
    {
        switch (index)
        {
        case 1:  return "1/32";
        case 2:  return "1/16";
        case 3:  return "1/8";
        case 4:  return "1/4";
        case 5:  return "1/2";
        case 6:  return "1 Bar";
        case 7:  return "2 Bar";
        case 8:  return "4 Bar";
        default: return "Free";
        }
    }

} // namespace ArrangementEditor
