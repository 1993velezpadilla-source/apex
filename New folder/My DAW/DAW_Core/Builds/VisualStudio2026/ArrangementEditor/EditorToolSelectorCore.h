// ===========================================================================
// EditorToolSelectorCore.h
// Toolbar UI component showing all tool buttons in a horizontal strip.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "EditorToolCore.h"
#include "EditorToolButtonCore.h"
#include <vector>
#include <memory>

namespace ArrangementEditor
{
    class EditorToolSelectorCore : public juce::Component
    {
    public:
        EditorToolSelectorCore(EditorToolState& state);

        void paint(juce::Graphics& g) override;
        void resized() override;

        std::function<void(EditorTool)> onToolChanged;

    private:
        EditorToolState& m_state;
        std::vector<std::unique_ptr<EditorToolButtonCore>> m_buttons;

        void buildButtons();

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
