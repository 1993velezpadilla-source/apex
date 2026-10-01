// ===========================================================================
// ClipMuteOverlayCore.h
// Grey wash overlay for muted clips (60% opacity).
// ===========================================================================
#pragma once
#include <JuceHeader.h>

namespace ArrangementEditor
{
    class ClipMuteOverlayCore : public juce::Component
    {
    public:
        ClipMuteOverlayCore();

        void paint(juce::Graphics& g) override;
        void resized() override {}

    private:
        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
