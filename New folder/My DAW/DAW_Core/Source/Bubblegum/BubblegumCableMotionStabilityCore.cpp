#include "BubblegumCableMotionStabilityCore.h"

namespace bubblegum
{
    void BubblegumCableMotionStabilityCore::reset()
    {
        stableMap.clear();
    }

    juce::Point<float> BubblegumCableMotionStabilityCore::smoothPoint(
        const juce::Point<float>& current,
        const juce::Point<float>& target,
        float dt,
        float smoothingHz,
        float snapDistance,
        float maxVelocityPxPerSec) const noexcept
    {
        const auto delta = target - current;
        const float dist = delta.getDistanceFromOrigin();

        if (dist <= snapDistance)
            return target;

        const float alpha = 1.0f - std::exp(-smoothingHz * dt);
        auto step = delta * alpha;

        const float maxStep = maxVelocityPxPerSec * dt;
        const float stepLen = step.getDistanceFromOrigin();

        if (stepLen > maxStep && stepLen > 0.0001f)
            step *= (maxStep / stepLen);

        return current + step;
    }

    std::vector<CableWorldSnapshot> BubblegumCableMotionStabilityCore::resolveStableSnapshots(
        const std::vector<CableWorldSnapshot>& snapshots,
        float dt,
        const BubblegumCableStyleSettingsCore::Style& style,
        bool snapToTargets)
    {
        std::vector<CableWorldSnapshot> out;
        out.reserve(snapshots.size());

        std::unordered_map<CableKey, StableEndpoints, CableKeyHash> nextMap;

        if (snapToTargets)
        {
            for (const auto& s : snapshots)
            {
                StableEndpoints st;
                st.source      = s.endpoints.source;
                st.destination = s.endpoints.destination;
                st.initialized = true;
                st.framesAlive = 1;
                nextMap[s.key] = st;
                out.push_back(s);
            }

            stableMap = std::move(nextMap);
            return out;
        }

        for (const auto& s : snapshots)
        {
            auto it = stableMap.find(s.key);
            StableEndpoints st;

            if (it == stableMap.end())
            {
                // First time this cable is seen: record position but do NOT render yet.
                // This gives the coordinate system one frame to fully settle
                // (layout, viewport transform, overlay position) so the cable
                // appears at the correct, stable position on its very first visible frame.
                st.source      = s.endpoints.source;
                st.destination = s.endpoints.destination;
                st.initialized = true;
                st.framesAlive = 0;
            }
            else
            {
                st = it->second;
                st.framesAlive++;
                st.source = smoothPoint(
                    st.source,
                    s.endpoints.source,
                    dt,
                    style.smoothingHz,
                    style.snapDistance,
                    style.maxVelocityPxPerSec);

                st.destination = smoothPoint(
                    st.destination,
                    s.endpoints.destination,
                    dt,
                    style.smoothingHz,
                    style.snapDistance,
                    style.maxVelocityPxPerSec);
            }

            nextMap[s.key] = st;

            // Skip rendering on the very first frame (framesAlive == 0).
            // The cable renders correctly from frame 1 onward.
            if (st.framesAlive == 0)
                continue;

            auto stableSnapshot = s;
            stableSnapshot.endpoints.source      = st.source;
            stableSnapshot.endpoints.destination = st.destination;
            out.push_back(stableSnapshot);
        }

        stableMap = std::move(nextMap);
        return out;
    }
}
