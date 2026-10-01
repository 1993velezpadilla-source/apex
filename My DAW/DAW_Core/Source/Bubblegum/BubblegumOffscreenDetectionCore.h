#pragma once
#include <JuceHeader.h>
#include <set>
#include "../UtilityCore/Types.h"
#include "../RoutingCore/RoutingConnection.h"

namespace DAW {

/**
 * BubblegumOffscreenDetectionCore
 * 
 * Detects when Bubblegum send targets are offscreen (scrolled out of view)
 * and groups them by direction (left/right).
 * 
 * Used by the endpoint overlay to show multi-target picker popups.
 */
class BubblegumOffscreenDetectionCore
{
public:
    BubblegumOffscreenDetectionCore() = default;

    enum class Direction { Left, Right };

    struct OffscreenTarget
    {
        TrackID trackId;
        juce::String trackName;
        int trackNumber = 0;
        Direction direction = Direction::Right;
        float sendLevel = 0.0f;
        bool hasSend = false;
        bool sendActive = true;
        bool hasSidechain = false;
        bool sidechainActive = true;
        TapPoint tapPoint = TapPoint::PreFX;
    };

    struct OffscreenGroup
    {
        Direction direction = Direction::Right;
        std::vector<OffscreenTarget> targets;
    };

    /**
     * Analyze which targets are offscreen given the viewport bounds.
     * 
     * @param viewportBounds    The visible area in parent coordinates
     * @param targetPositions   Map of trackId -> horizontal center position (in parent coords)
     * @param targetNames       Map of trackId -> display name
     * @param targetNumbers     Map of trackId -> track number
     * @param sendLevels        Map of trackId -> send level
     * @return                  Offscreen groups by direction
     */
    std::vector<OffscreenGroup> detectOffscreenTargets(
        juce::Rectangle<float> viewportBounds,
        const std::map<TrackID, float>& targetPositions,
        const std::map<TrackID, juce::String>& targetNames,
        const std::map<TrackID, int>& targetNumbers,
        const std::map<TrackID, float>& sendLevels,
        const std::map<TrackID, bool>& sendActiveStates = {},
        const std::set<TrackID>& sendIds = {},
        const std::set<TrackID>& sidechainIds = {},
        const std::map<TrackID, bool>& sidechainActiveStates = {}) const
    {
        std::vector<OffscreenGroup> groups;
        OffscreenGroup leftGroup;
        OffscreenGroup rightGroup;
        leftGroup.direction = Direction::Left;
        rightGroup.direction = Direction::Right;

        for (const auto& [trackId, xPos] : targetPositions)
        {
            // Check if target is offscreen
            bool offscreenLeft = xPos < viewportBounds.getX();
            bool offscreenRight = xPos > viewportBounds.getRight();

            if (offscreenLeft || offscreenRight)
            {
                OffscreenTarget target;
                target.trackId = trackId;
                target.trackName = targetNames.count(trackId) ? targetNames.at(trackId) : trackId;
                target.trackNumber = targetNumbers.count(trackId) ? targetNumbers.at(trackId) : 0;
                target.sendLevel = sendLevels.count(trackId) ? sendLevels.at(trackId) : 0.0f;
                target.hasSend = sendIds.count(trackId) > 0;
                target.sendActive = target.hasSend
                    ? (sendActiveStates.count(trackId) ? sendActiveStates.at(trackId) : true)
                    : false;
                target.hasSidechain = sidechainIds.count(trackId) > 0;
                target.sidechainActive = target.hasSidechain
                    ? (sidechainActiveStates.count(trackId) ? sidechainActiveStates.at(trackId) : true)
                    : false;
                target.direction = offscreenLeft ? Direction::Left : Direction::Right;

                if (offscreenLeft)
                    leftGroup.targets.push_back(target);
                else
                    rightGroup.targets.push_back(target);
            }
        }

        // Sort targets by track number
        auto sortByTrackNumber = [](const OffscreenTarget& a, const OffscreenTarget& b)
        {
            return a.trackNumber < b.trackNumber;
        };

        std::sort(leftGroup.targets.begin(), leftGroup.targets.end(), sortByTrackNumber);
        std::sort(rightGroup.targets.begin(), rightGroup.targets.end(), sortByTrackNumber);

        if (!leftGroup.targets.empty())
            groups.push_back(leftGroup);
        if (!rightGroup.targets.empty())
            groups.push_back(rightGroup);

        return groups;
    }

    /**
     * Get a preview string for hovering over an endpoint.
     * Example: "3 sends ->" or "<- 2 sends"
     */
    static juce::String getPreviewText(const OffscreenGroup& group)
    {
        int count = (int)group.targets.size();
        if (count == 0) return {};
        if (count == 1)
            return group.targets[0].trackName;

        // Count sends and sidechains separately
        int scCount   = 0;
        int sndCount  = 0;
        for (const auto& t : group.targets)
        {
            if (t.hasSend) ++sndCount;
            if (t.hasSidechain) ++scCount;
        }

        if (scCount == 0)
            return juce::String(sndCount) + " sends";
        if (sndCount == 0)
            return juce::String(scCount) + " SC";
        return juce::String(sndCount) + " sends, " + juce::String(scCount) + " SC";
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumOffscreenDetectionCore)
};

} // namespace DAW
