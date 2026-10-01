#include "PianoRollSelectionRenderer.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

void PianoRollSelectionRenderer::paint(juce::Graphics& g, const juce::Rectangle<float>& marquee) const
{
    if (marquee.isEmpty())
        return;

    auto& theme = Theme::getInstance();
    g.setColour(theme.colors.accent.withAlpha(0.16f));
    g.fillRect(marquee);
    g.setColour(theme.colors.pearl.withAlpha(0.85f));
    g.drawRect(marquee, 1.5f);
}

} // namespace DAW
