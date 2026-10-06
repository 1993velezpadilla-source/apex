#pragma once
#include <JuceHeader.h>
#include "BubblegumTaskbarChipModel.h"

namespace DAW {

/**
 * BubblegumTaskbarChipLayoutCore
 *
 * Pure layout math: given a list of chips, lay them out left-to-right with
 * configurable padding/gap. Each chip's width is computed from its label
 * (measured against the active-state font), clamped to chipMinWidth.
 *
 * Stateless — config-driven. Mutates the bounds field of each ChipModel.
 */
class BubblegumTaskbarChipLayoutCore
{
public:
    struct Config
    {
        float chipHeight        { 32.0f  };
        float chipMinWidth      { 100.0f };
        float chipPaddingLeft   { 32.0f  };  // dot + spacing on the left
        float chipPaddingRight  { 32.0f  };  // separator + X area on the right
        float chipGap           { 12.0f  };
        float startX            { 20.0f  };
        float topY              { 38.0f  };

        float labelFontSize     { 12.0f };
        bool  labelUsesBold     { true  };  // size against active label font
    };

    BubblegumTaskbarChipLayoutCore() = default;

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    /** Width needed for a chip with the given label. */
    float computeChipWidth(const juce::String& label) const
    {
        const auto font = juce::Font(config_.labelFontSize,
                                     config_.labelUsesBold ? juce::Font::bold
                                                           : juce::Font::plain);
        const float textW = font.getStringWidthFloat(label);
        return juce::jmax(config_.chipMinWidth,
                          textW + config_.chipPaddingLeft + config_.chipPaddingRight);
    }

    /** Lay out the chips in-place, writing to each chip's bounds field. */
    void layout(juce::Array<BubblegumTaskbarChipModel>& chips) const
    {
        float x = config_.startX;
        for (auto& chip : chips)
        {
            const auto label = chip.host ? chip.host->getChipLabel() : juce::String();
            const float w = computeChipWidth(label);
            chip.bounds = juce::Rectangle<float>(x, config_.topY, w, config_.chipHeight);
            x += w + config_.chipGap;
        }
    }

private:
    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumTaskbarChipLayoutCore)
};

} // namespace DAW
