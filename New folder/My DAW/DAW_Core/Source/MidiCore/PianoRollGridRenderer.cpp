#include "PianoRollGridRenderer.h"
#include "PianoRollScaleHelper.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

namespace
{
    bool isBlackKey(int midiNote)
    {
        switch (midiNote % 12)
        {
            case 1: case 3: case 6: case 8: case 10: return true;
            default: return false;
        }
    }
}

void PianoRollGridRenderer::paint(juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    auto& theme = Theme::getInstance();
    g.setColour(theme.colors.backgroundDark);
    g.fillRect(bounds);

    for (int row = 0; row * keyRowHeight_ < bounds.getHeight(); ++row)
    {
        const int midiNote = juce::jlimit(0, 127, firstVisibleNote_ - row);
        auto rowBounds = juce::Rectangle<float>((float) bounds.getX(),
                                                (float) bounds.getY() + row * keyRowHeight_,
                                                (float) bounds.getWidth(),
                                                keyRowHeight_);

        auto rowColour = isBlackKey(midiNote)
            ? theme.colors.panelDepth2.brighter(0.02f)
            : theme.colors.panelDepth2.brighter(0.08f);

        if (PianoRollScaleHelper::isNoteInScale(midiNote, scaleRoot_, scaleName_))
            rowColour = rowColour.brighter(0.08f);
        else
            rowColour = rowColour.darker(0.12f);

        g.setColour(rowColour);
        g.fillRect(rowBounds);

        g.setColour(theme.colors.separatorSoft.withAlpha(0.45f));
        g.drawHorizontalLine((int) rowBounds.getBottom(), rowBounds.getX(), rowBounds.getRight());
    }

    const float startTick = scrollX_ / pixelsPerTick_;
    const juce::int64 firstGridTick = (juce::int64) std::floor(startTick / (double) ticksPerGridDivision_) * ticksPerGridDivision_;
    const float width = (float) bounds.getWidth();

    for (juce::int64 tick = firstGridTick;; tick += ticksPerGridDivision_)
    {
        const float x = (float) bounds.getX() + tick * pixelsPerTick_ - scrollX_;
        if (x > bounds.getRight())
            break;
        if (x < bounds.getX() - 1.0f)
            continue;

        const bool isBar = (tick % (ticksPerQuarterNote_ * 4)) == 0;
        const bool isBeat = (tick % ticksPerQuarterNote_) == 0;

        g.setColour(isBar ? theme.colors.separatorHard.withAlpha(0.85f)
                          : isBeat ? theme.colors.separatorSoft.withAlpha(0.65f)
                                   : theme.colors.separatorSoft.withAlpha(0.28f));
        g.drawVerticalLine((int) std::round(x), (float) bounds.getY(), (float) bounds.getBottom());

        if (x - (float) bounds.getX() > width)
            break;
    }
}

} // namespace DAW
