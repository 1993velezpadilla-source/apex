// ===========================================================================
// ClipPitchBadgeCore.h
// Small badge showing pitch offset ("+3st") when non-zero.
// Rendered top-right of clip.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "ArrangementClipModel.h"

namespace ArrangementEditor
{
    class ClipPitchBadgeCore : public juce::Component
    {
    public:
        explicit ClipPitchBadgeCore(const ArrangementClipModel& model);

        void setModel(const ArrangementClipModel& model);

        void paint(juce::Graphics& g) override;
        void resized() override {}

    private:
        const ArrangementClipModel* m_model = nullptr;

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
