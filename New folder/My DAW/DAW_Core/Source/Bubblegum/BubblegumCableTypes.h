#pragma once
#include <JuceHeader.h>
#include <cstdint>
#include <functional>
#include "../UtilityCore/Types.h"

namespace bubblegum
{
    using TrackId = DAW::TrackID;
    using SendId  = std::int64_t;

    struct TrackIdHash
    {
        std::size_t operator()(const TrackId& id) const noexcept
        {
            return static_cast<std::size_t>(id.hashCode64());
        }
    };

    enum class SendVisualState
    {
        DoesNotExist = 0,
        ExistsInactive,
        ExistsActive
    };

    struct CableKey
    {
        TrackId sourceTrack;
        TrackId destinationTrack;
        SendId sendId = 0;

        bool operator==(const CableKey& other) const noexcept
        {
            return sourceTrack == other.sourceTrack
                && destinationTrack == other.destinationTrack
                && sendId == other.sendId;
        }
    };

    struct CableKeyHash
    {
        std::size_t operator()(const CableKey& k) const noexcept
        {
            const auto h1 = TrackIdHash{}(k.sourceTrack);
            const auto h2 = TrackIdHash{}(k.destinationTrack);
            const auto h3 = std::hash<std::int64_t>{}(k.sendId);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    struct CableEndpoints
    {
        juce::Point<float> source;
        juce::Point<float> destination;
    };

    struct CableWorldSnapshot
    {
        CableKey key;
        SendVisualState state = SendVisualState::DoesNotExist;
        CableEndpoints endpoints;
        bool visible = true;
        float audioEnergy01 = 0.0f;
        float sendLevel01 = 0.5f;
        bool isMasterTarget = false;  // cable routes to the master bus
    };

    struct CableResolvedFrame
    {
        CableKey key;
        SendVisualState state = SendVisualState::DoesNotExist;
        bool visible = true;
        bool isMasterTarget = false;  // cable routes to the master bus

        juce::Point<float> source;
        juce::Point<float> controlA;
        juce::Point<float> controlB;
        juce::Point<float> destination;

        float length = 0.0f;
        float sagAmount = 0.0f;
        float thickness = 14.0f;
        float timeSeconds = 0.0f;

        float audioEnergy01 = 0.0f;
        float sendLevel01 = 0.5f;
        float audioThicknessScale = 1.0f;
        float audioFlowSpeedScale = 1.0f;
        float audioInternalGlowAlpha = 0.20f;
        float audioHighlightBoost = 0.10f;
        float audioDropletChanceScale = 1.0f;

    };

    struct CableStrokeSample
    {
        juce::Point<float> point;
        juce::Point<float> tangent;
        juce::Point<float> normal;
        float t = 0.0f;

        float widthMul = 1.0f;
        float edgeNoise = 0.0f;
        float lowerSplashBias = 0.0f;
        float splashGate = 0.0f;
    };

    struct DropletPoint
    {
        juce::Point<float> position;    // centre of the drip blob
        juce::Point<float> attachPoint; // cable bottom surface — tail starts here
        float radius     = 1.0f;
        float tailLength = 4.0f;  // pixels from attachPoint to blob centre
        float opacity    = 0.5f;
    };
}
