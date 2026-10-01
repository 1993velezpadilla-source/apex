// ===========================================================================
// ArrangementToolbarCore.cpp
// ===========================================================================
#include "ArrangementToolbarCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        // APEX signal-core tokens (see ThemeCore::ApexTokens)
        constexpr uint32_t bg = 0xFF070A10;        // deepestB
        constexpr uint32_t text = 0xFFA6ADBC;      // textSecondary
        constexpr uint32_t accent = 0xFFFF1678;    // magenta
        constexpr uint32_t btnNormal = 0xFF111522; // panelC
        constexpr uint32_t btnHover = 0xFF1A2233;  // borderSoftA
        constexpr uint32_t border = 0xFF22283A;    // borderSoftB
        constexpr uint32_t pink = 0xFFFF1678;      // classic eraser pink half
    }

    // Compact button geometry — smaller boxes, icons keep their size.
    static constexpr int kBtnW = 40;
    static constexpr int kBtnH = 32;
    static constexpr int kBtnY = 8;
    static constexpr int kBtnGap = 4;

    ArrangementToolbarCore::ArrangementToolbarCore(EditorToolState& toolState)
        : m_toolState(toolState)
    {
        // Keep consistent with TrackList top header (48px) so the arrangement content
        // doesn't overlap the toolbar when laid out.
        setSize(800, 48);
        buildButtons();

        // Real component for the Quick New Track "+" — exclusive pointer
        // consumption (click-through fix): the button is the deepest
        // hit-test winner inside its bounds, so nothing underneath (lane,
        // clip, Master track) can receive the same pointer interaction.
        m_addTrackBtn.setClickingTogglesState(false);
        addAndMakeVisible(m_addTrackBtn);
        m_addTrackBtn.onClick = [this]
        {
            if (onAddTrackRequested)
                onAddTrackRequested();
        };

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
            { EditorTool::Eraser,    "Eraser",    juce::CharPointer_UTF8("") }, // custom classic eraser
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
            btn.bounds = juce::Rectangle<int>(x, kBtnY, kBtnW, kBtnH);
            m_buttons.push_back(btn);
            x += kBtnW + kBtnGap;
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

        // ── Functional group separators ─────────────────────────────────────
        // Group 1: edit tools (Select..Scrub). Group 2: snap/metro/auto-arm.
        // Group 3: compact "+" add-track. Subtle vertical hairlines separate
        // the groups so the toolbar reads as functional clusters.
        if (!m_buttons.empty())
        {
            const int sepX = m_buttons.back().bounds.getRight() + 8;
            g.setColour(fromU32(Col::border).withAlpha(0.6f));
            g.drawVerticalLine(sepX, 10.f, (float)(b.getHeight() - 10));
        }
        if (!m_autoArmBounds.isEmpty())
        {
            const int sepX = m_autoArmBounds.getRight() + 8;
            g.setColour(fromU32(Col::border).withAlpha(0.6f));
            g.drawVerticalLine(sepX, 10.f, (float)(b.getHeight() - 10));
        }

        // Metronome button (positioned rightmost — painted first so it goes
        // behind more important controls when clipped).
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

        // Snap button
        {
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

        // Tool buttons — icons only (words removed; tooltips carry the names).
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

            // Icon — same size as before (20px), only the button box shrank.
            if (btn.tool == EditorTool::Eraser)
            {
                // Classic half-pink / half-white eraser: a tilted rounded
                // rectangle split horizontally — pink top, white bottom.
                const auto r = btn.bounds.toFloat().reduced(7.f, 6.f);
                const float tilt = 0.18f;
                juce::Path eraser;
                eraser.addRoundedRectangle(r.getX() + r.getHeight() * tilt, r.getY(),
                                           r.getWidth() - r.getHeight() * tilt, r.getHeight(),
                                           3.f, 3.f, true, true, false, false);
                // Top half — pink
                juce::Path topHalf;
                topHalf.addRoundedRectangle(r.getX() + r.getHeight() * tilt, r.getY(),
                                            r.getWidth() - r.getHeight() * tilt, r.getHeight() * 0.5f,
                                            3.f, 3.f, true, true, false, false);
                g.setColour(isActive ? juce::Colours::white : fromU32(Col::pink));
                g.fillPath(topHalf);
                // Bottom half — white
                juce::Path bottomHalf;
                bottomHalf.addRoundedRectangle(r.getX() + r.getHeight() * tilt, r.getY() + r.getHeight() * 0.5f,
                                               r.getWidth() - r.getHeight() * tilt, r.getHeight() * 0.5f,
                                               3.f, 3.f, false, false, true, true);
                g.setColour(isActive ? fromU32(Col::pink) : juce::Colours::white);
                g.fillPath(bottomHalf);
                // Outline
                g.setColour(fromU32(isActive ? 0xFFFFFFFF : Col::border));
                g.strokePath(eraser, juce::PathStrokeType(1.f));
            }
            else
            {
                g.setColour(fromU32(isActive ? 0xFFFFFFFF : Col::text));
                g.setFont(juce::Font("Segoe UI", 20.f, juce::Font::plain));
                g.drawText(btn.icon, btn.bounds, juce::Justification::centred);
            }

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

        // Automation arm button — painted last so it is visible even when the
        // toolbar is narrow enough to clip the rightmost controls.
        if (!m_autoArmBounds.isEmpty())
        {
            const bool active = m_isAutoArmEnabled ? m_isAutoArmEnabled() : false;
            const auto base = active ? juce::Colour(0xFFE74C3C) : fromU32(m_autoArmHovered ? Col::btnHover : Col::btnNormal);
            g.setColour(base);
            g.fillRoundedRectangle(m_autoArmBounds.toFloat(), 4.f);
            g.setColour(fromU32(active ? 0xFFE74C3C : Col::border));
            g.drawRoundedRectangle(m_autoArmBounds.toFloat().reduced(0.5f), 4.f, 1.f);

            g.setColour(fromU32(active ? 0xFFFFFFFF : Col::text));
            g.setFont(juce::Font("Segoe UI", 18.f, juce::Font::plain));
            g.drawText(juce::CharPointer_UTF8("\xe2\x9a\xbf"), m_autoArmBounds.withTrimmedBottom(12), juce::Justification::centred);
            g.setFont(juce::Font("Segoe UI", 10.f, juce::Font::plain));
            g.drawText("Auto", m_autoArmBounds.withTrimmedTop(20), juce::Justification::centred);
        }

        // The "+" Quick New Track button is a real child component
        // (m_addTrackBtn) and paints itself — no painted rect here.
    }

    void ArrangementToolbarCore::resized()
    {
        buildButtons();
        int x = m_buttons.empty() ? 8 : (m_buttons.back().bounds.getRight() + 10);

        // Auto arm button placed immediately after tool buttons so it is
        // visible even on narrower viewports where snap/metro would be clipped.
        m_autoArmBounds = juce::Rectangle<int>(x, kBtnY, 64, kBtnH);

        x = m_autoArmBounds.getRight() + 8;
        m_snapButtonBounds = juce::Rectangle<int>(x, kBtnY, 90, kBtnH);

        x = m_snapButtonBounds.getRight() + 8;
        m_metronomeBounds = juce::Rectangle<int>(x, kBtnY, 64, kBtnH);

        // Compact "+" add-track button at the far right of the toolbar.
        // Touch-safe target (44×40) while staying inside the 48px strip.
        m_addTrackBounds = juce::Rectangle<int>(getWidth() - 54, 4, 44, 40);
        m_addTrackBtn.setBounds(m_addTrackBounds);
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

        const bool wasAutoArmHovered = m_autoArmHovered;
        m_autoArmHovered = m_autoArmBounds.contains(e.getPosition());
        if (wasAutoArmHovered != m_autoArmHovered)
            needsRepaint = true;

        updateTooltip(e.getPosition());

        if (needsRepaint)
            repaint();
    }

    void ArrangementToolbarCore::mouseExit(const juce::MouseEvent&)
    {
        for (auto& btn : m_buttons)
            btn.hovered = false;
        m_snapButtonHovered = false;
        m_metronomeHovered = false;
        m_autoArmHovered = false;
        m_currentTooltip = {};
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

        if (m_autoArmBounds.contains(e.getPosition()))
        {
            if (m_toggleAutoArm)
                m_toggleAutoArm();
            repaint();
            return;
        }

        // The "+" Quick New Track button is a real child component and
        // handles its own pointer events — it can never fall through.

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

    void ArrangementToolbarCore::updateTooltip(juce::Point<int> pos)
    {
        // Tool buttons — detailed descriptions with keyboard shortcuts
        for (auto& btn : m_buttons)
        {
            if (btn.bounds.contains(pos))
            {
                juce::String tip;
                const char key = toolShortcut(btn.tool);
                const juce::String keyStr = (key != ' ')
                    ? (juce::String(" [") + juce::String::charToString(key) + juce::String("]"))
                    : juce::String();

                switch (btn.tool)
                {
                case EditorTool::Select:
                    tip = "Select Tool" + keyStr
                        + "\nClick to select clips. Drag to move selected clips."
                        + "\nHold Shift for multi-select. Hold Ctrl to marquee-select.";
                    break;
                case EditorTool::Split:
                    tip = "Blade Tool" + keyStr
                        + "\nClick on a clip to split it at the cursor position."
                        + "\nHold Shift to snap the split to the grid.";
                    break;
                case EditorTool::RazorEdit:
                    tip = "Razor Edit" + keyStr
                        + "\nClick and drag across tracks to define a time range."
                        + "\nThe selected region can then be deleted or moved.";
                    break;
                case EditorTool::Eraser:
                    tip = "Eraser Tool" + keyStr
                        + "\nClick on a clip to delete it."
                        + "\nDrag across multiple clips to delete them all at once.";
                    break;
                case EditorTool::Draw:
                    tip = "Draw Tool" + keyStr
                        + "\nClick on an empty track lane to create a new clip."
                        + "\nIn automation lanes, drag to draw automation curves.";
                    break;
                case EditorTool::Glue:
                    tip = "Glue Tool" + keyStr
                        + "\nClick on a clip to merge it with its adjacent clip."
                        + "\nAdjacent clips must be on the same track and touching.";
                    break;
                case EditorTool::Mute:
                    tip = "Mute Tool" + keyStr
                        + "\nClick on a clip to toggle mute on or off."
                        + "\nMuted clips are silent during playback but remain in place.";
                    break;
                case EditorTool::Stretch:
                    tip = "Stretch Tool" + keyStr
                        + "\nDrag the edge of a clip to time-stretch or compress it."
                        + "\nHold Alt to preserve pitch during stretch.";
                    break;
                case EditorTool::Zoom:
                    tip = "Zoom Tool" + keyStr
                        + "\nClick to zoom in on the timeline."
                        + "\nAlt+Click to zoom out. Drag to zoom into a region.";
                    break;
                case EditorTool::TimeScrub:
                    tip = "Scrub Tool" + keyStr
                        + "\nDrag across the timeline to scrub through audio."
                        + "\nThe playhead follows your cursor at variable speed.";
                    break;
                default:
                    tip = btn.label + keyStr;
                    break;
                }
                m_currentTooltip = tip;
                return;
            }
        }

        // Automation arm button
        if (m_autoArmBounds.contains(pos))
        {
            const bool armed = m_isAutoArmEnabled ? m_isAutoArmEnabled() : false;
            m_currentTooltip = "Automation Record Arm"
                + juce::String(armed ? " (ON)" : " (OFF)")
                + "\nToggle automation recording for all tracks."
                + "\nWhen enabled, parameter changes during playback"
                + "\nare recorded as automation data in Touch mode."
                + "\nClick to toggle on or off.";
            return;
        }

        // Snap button
        if (m_snapButtonBounds.contains(pos))
        {
            const juce::String mode = snapModeLabelForIndex(m_selectedSnapModeIndex);
            m_currentTooltip = "Snap to Grid — " + mode
                + "\nToggle snap-to-grid for clip movement and editing."
                + "\nClick to open the grid division menu."
                + "\nCurrent grid: " + mode
                + "\nSelect Free, 1/32, 1/16, 1/8, 1/4, 1/2,"
                + "\n1 Bar, 2 Bar, or 4 Bar divisions.";
            return;
        }

        // Metronome button
        if (m_metronomeBounds.contains(pos))
        {
            const bool active = m_isMetronomeEnabled ? m_isMetronomeEnabled() : false;
            m_currentTooltip = "Metronome"
                + juce::String(active ? " (ON)" : " (OFF)")
                + "\nToggle the metronome click track."
                + "\nPlays a click sound on each beat during"
                + "\nplayback and recording to help keep time."
                + "\nClick to toggle on or off.";
            return;
        }

        // Compact "+" add-track button — real child component; hover state
        // comes from the button itself (m_addTrackBtn.isOver()).
        if (m_addTrackBtn.isOver())
        {
            m_currentTooltip = juce::String("Quick Track Builder")
                + "\nOpen the Quick Track Builder to create named, colored"
                + "\ntracks and buses — or tap a role to create one instantly.";
            return;
        }

        // Empty area — no tooltip
        m_currentTooltip = {};
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