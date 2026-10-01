#pragma once

#include <JuceHeader.h>

namespace DAW {

/** Message-thread-only child-stack repair for a transparent Mixer overlay.
 *  It never changes component ownership or visibility, and does no polling. */
class MixerOverlayStackingCore final
{
public:
    static bool ensureAbove(juce::Component& parent,
                            juce::Component& overlay,
                            const juce::Component& anchor)
    {
        if (&overlay == &anchor
            || overlay.getParentComponent() != &parent
            || anchor.getParentComponent() != &parent)
            return false;

        const int anchorIndex = parent.getIndexOfChildComponent(&anchor);
        const int overlayIndex = parent.getIndexOfChildComponent(&overlay);
        if (anchorIndex < 0 || overlayIndex < 0 || overlayIndex > anchorIndex)
            return false;

        // JUCE's addChildComponent() deliberately does not reorder a child
        // that already belongs to this parent.  Use JUCE's explicit sibling
        // reordering API instead, preserving every sibling above the anchor.
        if (auto* nextAboveAnchor = parent.getChildComponent(anchorIndex + 1))
            overlay.toBehind(nextAboveAnchor);
        else
            overlay.toFront(false);

        return parent.getIndexOfChildComponent(&overlay)
            > parent.getIndexOfChildComponent(&anchor);
    }
};

} // namespace DAW
