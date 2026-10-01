#pragma once
#include <JuceHeader.h>
#include <vector>
#include "BubblegumCableTypes.h"
#include "BubblegumCableStyleSettingsCore.h"

namespace bubblegum
{
    class BubblegumDropletCore
    {
    public:
        void paintSplashPaths(
            juce::Graphics& g,
            const std::vector<juce::Path>& splashes,
            const BubblegumCableStyleSettingsCore::Style& style,
            float centerX,
            float centerY,
            float thickness) const;

        void paintDropletPoints(
            juce::Graphics& g,
            const std::vector<DropletPoint>& droplets,
            const BubblegumCableStyleSettingsCore::Style& style,
            float thickness) const;
    };
}
