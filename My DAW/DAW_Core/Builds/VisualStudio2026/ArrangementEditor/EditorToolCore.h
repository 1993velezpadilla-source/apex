// ===========================================================================
// EditorToolCore.h
// Global tool state for the arrangement editor.
// One tool is active at a time. All mouse events route through the active tool.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <functional>
#include <string>

namespace ArrangementEditor
{
    enum class EditorTool
    {
        Select,       // click to select, drag to move
        Split,        // click on clip = cut at cursor position
        RazorEdit,    // drag time/track area = REAPER-style area edit
        Eraser,       // click on clip = delete it
        Draw,         // click empty lane = create new empty clip
        Glue,         // click adjacent clips = merge them
        Mute,         // click clip = toggle mute
        Stretch,      // drag edge of clip = time-stretch (default is trim)
        Zoom,         // drag to zoom into region
        TimeScrub     // drag timeline = scrub playhead
    };

    inline std::string toolName(EditorTool t)
    {
        switch (t)
        {
        case EditorTool::Select:    return "Select";
        case EditorTool::Split:     return "Split";
        case EditorTool::RazorEdit: return "Razor Edit";
        case EditorTool::Eraser:    return "Eraser";
        case EditorTool::Draw:      return "Draw";
        case EditorTool::Glue:      return "Glue";
        case EditorTool::Mute:      return "Mute";
        case EditorTool::Stretch:   return "Stretch";
        case EditorTool::Zoom:      return "Zoom";
        case EditorTool::TimeScrub: return "Time Scrub";
        default:                    return "Unknown";
        }
    }

    inline char toolShortcut(EditorTool t)
    {
        switch (t)
        {
        case EditorTool::Select:    return 'S';
        case EditorTool::Split:     return 'B';
        case EditorTool::RazorEdit: return 'R';
        case EditorTool::Eraser:    return 'E';
        case EditorTool::Draw:      return 'D';
        case EditorTool::Glue:      return 'G';
        case EditorTool::Mute:      return 'M';
        case EditorTool::Stretch:   return 'X';
        case EditorTool::Zoom:      return 'Z';
        case EditorTool::TimeScrub: return 'T';
        default:                    return ' ';
        }
    }

    // -----------------------------------------------------------------------
    // Global tool state
    // -----------------------------------------------------------------------
    class EditorToolState
    {
    public:
        // -----------------------------------------------------------------------
        // Listener interface — supports multiple observers
        // -----------------------------------------------------------------------
        struct Listener
        {
            virtual ~Listener() = default;
            virtual void activeToolChanged(EditorTool newTool) = 0;
        };

        void addListener(Listener* l)    { m_listeners.add(l); }
        void removeListener(Listener* l) { m_listeners.remove(l); }

        EditorTool getActiveTool() const { return m_active; }

        void setActiveTool(EditorTool t)
        {
            if (m_active == t)
            {
                DBG("[ToolState] Tool already active: " << toolName(t));
                return;
            }

            m_active = t;

            DBG("[ToolState] Active tool changed to: " << toolName(t));

            // Notify all registered listeners
            m_listeners.call([t](Listener& l) { l.activeToolChanged(t); });

            // Keep legacy single-callback support
            if (onToolChanged) onToolChanged(t);
        }

        // Legacy single callback — kept for backward compatibility
        std::function<void(EditorTool)> onToolChanged;

    private:
        EditorTool m_active = EditorTool::Select;
        juce::ListenerList<Listener> m_listeners;
    };

} // namespace ArrangementEditor
