#include "BubblegumAnchorResolver.h"

namespace bubblegum
{
    void BubblegumAnchorResolver::clear()
    {
        trackMap.clear();
    }

    void BubblegumAnchorResolver::setTrackBounds(TrackId trackId, const juce::Rectangle<float>& mixerBounds)
    {
        TrackVisualInfo info;
        info.mixerBounds = mixerBounds;
        info.valid = true;
        trackMap[trackId] = info;
    }

    bool BubblegumAnchorResolver::hasTrack(TrackId trackId) const noexcept
    {
        auto it = trackMap.find(trackId);
        return it != trackMap.end() && it->second.valid;
    }

    juce::Point<float> BubblegumAnchorResolver::getFallbackPoint() const noexcept
    {
        return { -10000.0f, -10000.0f };
    }

    juce::Point<float> BubblegumAnchorResolver::getBubblegumSendAnchor(TrackId trackId) const noexcept
    {
        auto it = trackMap.find(trackId);
        if (it == trackMap.end() || !it->second.valid)
            return getFallbackPoint();

        const auto& r = it->second.mixerBounds;
        return { r.getCentreX(), r.getBottom() };
    }

    juce::Point<float> BubblegumAnchorResolver::getBubblegumReceiveAnchor(TrackId trackId) const noexcept
    {
        auto it = trackMap.find(trackId);
        if (it == trackMap.end() || !it->second.valid)
            return getFallbackPoint();

        const auto& r = it->second.mixerBounds;
        return { r.getCentreX(), r.getBottom() };
    }
}
