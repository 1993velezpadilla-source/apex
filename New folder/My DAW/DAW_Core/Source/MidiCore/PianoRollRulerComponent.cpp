#include "PianoRollRulerComponent.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

void PianoRollRulerComponent::paint(juce::Graphics& g)
{
    auto& theme = Theme::getInstance();
    auto bounds = getLocalBounds();

    g.setColour(theme.colors.panelDepth3);
    g.fillRect(bounds);

    g.setColour(theme.colors.separatorHard.withAlpha(0.75f));
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, (float) bounds.getWidth());

    const auto ticksPerBar = ticksPerQuarterNote_ * 4;
    const float startTick = scrollX_ / pixelsPerTick_;
    const juce::int64 firstGridTick = (juce::int64) std::floor(startTick / (double) ticksPerGridDivision_) * ticksPerGridDivision_;

    for (juce::int64 tick = firstGridTick;; tick += ticksPerGridDivision_)
    {
        const float x = tick * pixelsPerTick_ - scrollX_;
        if (x > bounds.getRight())
            break;
        if (x < -1.0f)
            continue;

        const bool isBar = (tick % ticksPerBar) == 0;
        const bool isBeat = (tick % ticksPerQuarterNote_) == 0;
        const float lineTop = isBar ? 0.0f : isBeat ? 10.0f : 18.0f;

        g.setColour(isBar ? theme.colors.pearl.withAlpha(0.9f)
                          : isBeat ? theme.colors.platinum.withAlpha(0.7f)
                                   : theme.colors.separatorSoft.withAlpha(0.4f));
        g.drawVerticalLine((int) std::round(x), lineTop, (float) bounds.getBottom());

        if (isBar)
        {
            const int barNumber = (int) (tick / ticksPerBar) + 1;
            g.setFont(theme.fonts.small.boldened());
            g.drawText(juce::String(barNumber), (int) x + 4, 2, 40, 14, juce::Justification::centredLeft, false);
        }
    }
}

} // namespace DAW
