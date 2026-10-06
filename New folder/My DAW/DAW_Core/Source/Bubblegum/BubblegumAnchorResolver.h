#pragma once
#include <JuceHeader.h>
#include <unordered_map>
#include "BubblegumCableTypes.h"

namespace bubblegum
{
    class BubblegumAnchorResolver
    {
    public:
        struct TrackVisualInfo
        {
            juce::Rectangle<float> mixerBounds;
            bool valid = false;
        };

        void clear();
        void setTrackBounds(TrackId trackId, const juce::Rectangle<float>& mixerBounds);

        bool hasTrack(TrackId trackId) const noexcept;

        juce::Point<float> getBubblegumSendAnchor(TrackId trackId) const noexcept;
        juce::Point<float> getBubblegumReceiveAnchor(TrackId trackId) const noexcept;

    private:
        std::unordered_map<TrackId, TrackVisualInfo, TrackIdHash> trackMap;

        juce::Point<float> getFallbackPoint() const noexcept;
    };
}
