#pragma once

#include <JuceHeader.h>

namespace ArrangementEditor
{
    class ArrangementDebugLogCore
    {
    public:
        explicit ArrangementDebugLogCore(int maxLines = 400)
            : m_maxLines(juce::jmax(1, maxLines))
        {
        }

        void addLine(const juce::String& line)
        {
            if (line.isEmpty())
                return;

            const auto stamped = juce::Time::getCurrentTime().formatted("%H:%M:%S") + "  " + line;
            m_lines.add(stamped);

            while (m_lines.size() > m_maxLines)
                m_lines.remove(0);
        }

        void clear()
        {
            m_lines.clear();
        }

        juce::String getText() const
        {
            return m_lines.joinIntoString("\n");
        }

        void copyToClipboard() const
        {
            juce::SystemClipboard::copyTextToClipboard(getText());
        }

    private:
        juce::StringArray m_lines;
        int m_maxLines = 400;
    };
}
