#include "PianoRollKeyboardComponent.h"
#include "PianoRollScaleHelper.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

bool PianoRollKeyboardComponent::isBlackKey(int midiNote)
{
    switch (midiNote % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

void PianoRollKeyboardComponent::paint(juce::Graphics& g)
{
    auto& theme = Theme::getInstance();
    auto bounds = getLocalBounds();

    g.setColour(theme.colors.graphite);
    g.fillRect(bounds);

    for (int row = 0; row * keyRowHeight_ < bounds.getHeight(); ++row)
    {
        const int midiNote = juce::jlimit(0, 127, firstVisibleMidiNote_ - row);
        auto rowBounds = juce::Rectangle<float>(0.0f,
                                                row * keyRowHeight_,
                                                (float) bounds.getWidth(),
                                                keyRowHeight_);

        const bool black = isBlackKey(midiNote);
        const bool inScale = PianoRollScaleHelper::isNoteInScale(midiNote, scaleRoot_, scaleName_);
        auto keyColour = black ? theme.colors.obsidian.brighter(0.18f)
                               : theme.colors.smoke.brighter(0.20f);
        keyColour = inScale ? keyColour.brighter(0.10f) : keyColour.darker(0.18f);
        g.setColour(keyColour);
        g.fillRect(rowBounds);

        g.setColour(theme.colors.separatorSoft.withAlpha(0.55f));
        g.drawHorizontalLine((int) rowBounds.getBottom(), 0.0f, (float) bounds.getWidth());

        g.setColour(black ? theme.colors.platinum : theme.colors.pearl);
        g.setFont(theme.fonts.small);
        g.drawText(PianoRollScaleHelper::getDisplayNoteName(midiNote, scaleRoot_), rowBounds.toNearestInt().reduced(6, 0), juce::Justification::centredLeft, false);
    }

    g.setColour(theme.colors.separatorHard.withAlpha(0.85f));
    g.drawVerticalLine(bounds.getWidth() - 1, 0.0f, (float) bounds.getHeight());
}

} // namespace DAW
