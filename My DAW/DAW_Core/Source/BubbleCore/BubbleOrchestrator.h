#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubbleOrchestrator — snap-through magnet physics for floating bubbles.
 *
 * Two zones around each bubble:
 *
 *   REPULSION WALL  (outerDist … minDist)
 *     Equal-pole magnet effect. The dragged bubble is pushed back to minDist.
 *     Casual drags never accidentally overlap.
 *
 *   SNAP-THROUGH ZONE  (dist < snapThroughDist)
 *     Once the user pushes deliberately past the repulsion wall, the bubble
 *     enters the deep overlap zone. Repulsion stops — the bubble follows the
 *     mouse exactly. The merge preview ghost appears.
 *
 *   ON DROP while in snap-through zone → both bubbles absorbed into MasterBubble.
 */
class BubbleOrchestrator
{
public:
    static constexpr float kMinGap          = 12.f;  // min gap between edges (px)
    static constexpr float kSnapThroughFrac =  0.45f; // fraction of kMinGap to snap through

    struct Registration
    {
        juce::String id;
        float        radius = 28.f;
        std::function<juce::Point<float>()> getCenter;
    };

    void registerBubble(const Registration& r)
    {
        unregisterBubble(r.id);
        entries_.push_back(r);
    }

    void unregisterBubble(const juce::String& id)
    {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
            [&](const Registration& r) { return r.id == id; }), entries_.end());
    }

    /**
     * Snap-through repulsion:
     *  - Outside minDist: free.
     *  - Inside snapThroughDist: free (merge territory, no resistance).
     *  - Between them: pushed back to minDist (the magnetic wall).
     */
    juce::Point<float> resolveRepulsion(const juce::String& movingId,
                                         juce::Point<float>  desired,
                                         float               movingRadius) const
    {
        auto corrected = desired;
        for (auto& r : entries_)
        {
            if (r.id == movingId) continue;
            auto  other          = r.getCenter();
            float minDist        = movingRadius + r.radius + kMinGap;
            float snapThrough    = movingRadius + r.radius + kMinGap * kSnapThroughFrac;
            float dist           = corrected.getDistanceFrom(other);

            if (dist >= minDist)     continue;  // outside — fine
            if (dist < snapThrough)  continue;  // inside snap zone — already through

            // In repulsion wall: push back to outer boundary
            float d   = juce::jmax(dist, 0.001f);
            auto  dir = (corrected - other) / d;
            corrected = other + dir * minDist;
        }
        return corrected;
    }

    /**
     * Returns the ID of any bubble the moving bubble has snapped through into.
     * Empty = not in deep overlap zone.
     */
    juce::String findDeepOverlapTarget(const juce::String& movingId,
                                        juce::Point<float>  center,
                                        float               movingRadius) const
    {
        for (auto& r : entries_)
        {
            if (r.id == movingId) continue;
            float snapThrough = movingRadius + r.radius + kMinGap * kSnapThroughFrac;
            if (center.getDistanceFrom(r.getCenter()) < snapThrough)
                return r.id;
        }
        return {};
    }

    bool isInDeepOverlapZone(const juce::String& movingId,
                              juce::Point<float>  center,
                              float               movingRadius) const
    {
        return findDeepOverlapTarget(movingId, center, movingRadius).isNotEmpty();
    }

    juce::Point<float> getCenterOf(const juce::String& id) const
    {
        for (auto& r : entries_)
            if (r.id == id) return r.getCenter();
        return {};
    }

private:
    std::vector<Registration> entries_;
};

} // namespace DAW
