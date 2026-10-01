// ===========================================================================
// ClipColourPickerCore.h
// Row of colour swatches for clip tinting.
// Clicking a swatch sets the clip's colour.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "ArrangementClipModel.h"
#include <vector>
#include <functional>

namespace ArrangementEditor
{
    class ClipColourPickerCore : public juce::Component
    {
    public:
        ClipColourPickerCore();

        void setClip(ArrangementClipModel* clip);

        void paint(juce::Graphics& g) override;
        void resized() override;

        void mouseDown(const juce::MouseEvent& e) override;

        std::function<void(juce::Colour)> onColourChanged;

    private:
        ArrangementClipModel* m_clip = nullptr;

        struct Swatch
        {
            juce::Colour colour;
            juce::Rectangle<int> bounds;
        };

        std::vector<Swatch> m_swatches;

        void buildSwatches();

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
