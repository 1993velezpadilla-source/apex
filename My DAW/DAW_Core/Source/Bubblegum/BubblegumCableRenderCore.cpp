#include "BubblegumCableRenderCore.h"

namespace bubblegum
{
    void BubblegumCableRenderCore::resetMotion()
    {
        motionStabilityCore.reset();
        cableCache_.clear();
    }

    void BubblegumCableRenderCore::resetAudio()
    {
        audioReactiveCore.reset();
    }

    void BubblegumCableRenderCore::setStyle(const BubblegumCableStyleSettingsCore::Style& s)
    {
        styleCore.setStyle(s);
        ++styleVersion_;
        cableCache_.clear();
    }

    const BubblegumCableStyleSettingsCore::Style& BubblegumCableRenderCore::getStyle() const noexcept
    {
        return styleCore.getStyle();
    }

    //=========================================================================
    // makeResolvedFrame
    // Assembles the full per-frame data needed by the surface shader.
    // Audio reactive pass and hanging pass both happen here.
    //=========================================================================
    CableResolvedFrame BubblegumCableRenderCore::makeResolvedFrame(
        const CableWorldSnapshot& snapshot,
        float timeSeconds,
        float dt) noexcept
    {
        CableResolvedFrame frame;
        frame.key         = snapshot.key;
        frame.state       = snapshot.state;
        frame.visible     = snapshot.visible;
        frame.isMasterTarget = snapshot.isMasterTarget;
        frame.cableType   = snapshot.cableType;
        frame.source      = snapshot.endpoints.source;
        frame.destination = snapshot.endpoints.destination;
        frame.timeSeconds = 0.0f;
        frame.sendLevel01 = snapshot.sendLevel01;

        // ── Bypassed ghost ───────────────────────────────────────────────────
        // A bypassed cable (any of the 3 types) renders dim, thin and almost
        // transparent — a ghost that still shows the route exists.
        if (snapshot.bypassed)
        {
            frame.state       = SendVisualState::ExistsInactive;
            frame.sendLevel01 *= 0.12f;
            frame.audioEnergy01 = 0.0f;
        }

        const auto& st = styleCore.getStyle();
        frame.audioEnergy01           = 0.0f;
        frame.audioThicknessScale     = 1.0f;
        frame.audioFlowSpeedScale     = 1.0f;
        frame.audioInternalGlowAlpha  = st.audioInternalGlowBase;
        frame.audioHighlightBoost     = st.audioHighlightBase;
        frame.audioDropletChanceScale = 1.0f;

        // ── Hanging pass ──────────────────────────────────────────────────────
        const auto hang = hangingCore.solve(frame.source, frame.destination, st);
        frame.controlA  = hang.controlA;
        frame.controlB  = hang.controlB;
        frame.sagAmount = hang.sagAmount;
        frame.length    = frame.source.getDistanceFrom(frame.destination);

        // ── Thickness ────────────────────────────────────────────────────────
        float thickness = st.baseThickness;

        // Inactive sends are thinner — visual cue without routing truth change
        if (snapshot.state == SendVisualState::ExistsInactive)
            thickness *= st.inactiveThicknessScale;

        // Audio can push thickness slightly
        thickness *= frame.audioThicknessScale;

        frame.thickness = juce::jlimit(st.minThickness, st.maxThickness, thickness);

        return frame;
    }

    BubblegumCableRenderCore::CableCacheKey BubblegumCableRenderCore::makeCacheKey(
        const CableResolvedFrame& frame,
        int steps) const noexcept
    {
        auto q = [](float v) noexcept { return (int)std::round(v); };

        CableCacheKey key;
        key.edgeId = q(frame.key.sendId);
        key.qSrcX = q(frame.source.x);
        key.qSrcY = q(frame.source.y);
        key.qCtrlAX = q(frame.controlA.x);
        key.qCtrlAY = q(frame.controlA.y);
        key.qCtrlBX = q(frame.controlB.x);
        key.qCtrlBY = q(frame.controlB.y);
        key.qDstX = q(frame.destination.x);
        key.qDstY = q(frame.destination.y);
        key.qThickness = q(frame.thickness);
        key.qSendLevel = q(juce::jlimit(0.0f, 1.0f, frame.sendLevel01) * 1000.0f);
        key.styleVersion = styleVersion_;
        key.stateId = (int)frame.state;
        key.isMasterTarget = frame.isMasterTarget ? 1 : 0;
        key.cableType = (int)frame.cableType;
        key.qualityMode = (int)styleCore.getStyle().qualityMode;
        key.steps = steps;
        return key;
    }

    juce::Rectangle<int> BubblegumCableRenderCore::makeCacheBounds(
        const CableResolvedFrame& frame) const noexcept
    {
        const float minX = juce::jmin(frame.source.x, frame.controlA.x, frame.controlB.x, frame.destination.x);
        const float maxX = juce::jmax(frame.source.x, frame.controlA.x, frame.controlB.x, frame.destination.x);
        const float minY = juce::jmin(frame.source.y, frame.controlA.y, frame.controlB.y, frame.destination.y);
        const float maxY = juce::jmax(frame.source.y, frame.controlA.y, frame.controlB.y, frame.destination.y);
        const int pad = juce::jmax(48, (int)std::ceil(frame.thickness * 10.0f));

        return juce::Rectangle<int>::leftTopRightBottom(
            (int)std::floor(minX) - pad,
            (int)std::floor(minY) - pad,
            (int)std::ceil(maxX) + pad,
            (int)std::ceil(maxY) + pad);
    }

    void BubblegumCableRenderCore::evictCacheEntries(
        const std::unordered_set<juce::int64>& touchedThisFrame) const
    {
        for (auto it = cableCache_.begin(); it != cableCache_.end();)
        {
            if (touchedThisFrame.find(it->first) == touchedThisFrame.end())
                it = cableCache_.erase(it);
            else
                ++it;
        }

        while (cableCache_.size() > kMaxCachedCables)
        {
            auto oldest = cableCache_.end();
            for (auto it = cableCache_.begin(); it != cableCache_.end(); ++it)
                if (oldest == cableCache_.end() || it->second.lastUsedFrame < oldest->second.lastUsedFrame)
                    oldest = it;

            if (oldest == cableCache_.end())
                break;

            cableCache_.erase(oldest);
        }
    }

    //=========================================================================
    // paintAll — main frame entry point
    //=========================================================================
    void BubblegumCableRenderCore::paintAll(
        juce::Graphics& g,
        const std::vector<CableWorldSnapshot>& snapshots,
        const VisibilityInputs& visibilityInputs,
        float timeSeconds,
        float dt,
        RenderStats* outStats,
        bool snapMotionNow)
    {
        RenderStats stats;
        stats.submitted = (int)snapshots.size();
        ++frameCounter_;

        // Global visibility gate — skip all rendering if cables aren't visible
        if (!visibilityCore.cablesVisible(visibilityInputs))
        {
            if (outStats) *outStats = stats;
            return;
        }

        // ── Visibility filter ─────────────────────────────────────────────────
        std::vector<CableWorldSnapshot> visible;
        visible.reserve(snapshots.size());

        for (const auto& s : snapshots)
        {
            if (!visibilityCore.shouldRenderSnapshot(s, visibilityInputs))
                continue;
            ++stats.visible;
            visible.push_back(s);
        }

        // ── Motion stability pass ──────────────────────────────────────────────
        // Returns same list with endpoints replaced by smoothed values.
        auto stable = motionStabilityCore.resolveStableSnapshots(
            visible, dt, styleCore.getStyle(), snapMotionNow);

        // ── Per-cable render ───────────────────────────────────────────────────
        const int steps = styleCore.getRecommendedSteps();
        std::unordered_set<juce::int64> touchedThisFrame;
        touchedThisFrame.reserve(stable.size());

for (const auto& snapshot : stable)
        {
            auto frame = makeResolvedFrame(snapshot, 0.0f, 0.0f);

            const auto& baseStyle = styleCore.getStyle();

            // ── Cable-type style dispatch ───────────────────────────────────
            // ARC STREAM (NormalSend): violet electrical plasma for normal Send/Aux
            // Master (unchanged): bright bubblegum-pink
            // Sidechain (unchanged): separate cyan render pipeline, not here
            const auto& activeStyle = frame.isMasterTarget
                ? BubblegumCableStyleSettingsCore::makeMasterGoldStyle(baseStyle)
                : (frame.cableType == CableType::NormalSend
                    ? BubblegumCableStyleSettingsCore::makeArcStreamStyle(baseStyle)
                    : baseStyle);

            const auto key = makeCacheKey(frame, steps);
            touchedThisFrame.insert(key.edgeId);

            auto it = cableCache_.find(key.edgeId);
            if (it != cableCache_.end()
                && it->second.image.isValid()
                && it->second.key == key)
            {
                it->second.lastUsedFrame = frameCounter_;
                g.drawImageAt(it->second.image, it->second.bounds.getX(), it->second.bounds.getY());
                ++stats.cacheHits;
                ++stats.rendered;
                continue;
            }

            auto bounds = makeCacheBounds(frame);
            bounds = bounds.withSize(juce::jmax(1, bounds.getWidth()),
                                            juce::jmax(1, bounds.getHeight()));

            juce::Image image(juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);
            {
                juce::Graphics imageG(image);
                imageG.addTransform(juce::AffineTransform::translation((float)-bounds.getX(),
                                                                        (float)-bounds.getY()));

                surfaceShaderCore.paintLiquidStream(
                    imageG,
                    frame,
                    geometryCore,
                    dropletCore,
                    impactSplashCore,
                    activeStyle,
                    steps);
            }

            CableCacheEntry entry;
            entry.image = image;
            entry.key = key;
            entry.bounds = bounds;
            entry.lastUsedFrame = frameCounter_;
            cableCache_[key.edgeId] = entry;

            g.drawImageAt(image, bounds.getX(), bounds.getY());
            ++stats.cacheMisses;

            ++stats.rendered;
        }

        evictCacheEntries(touchedThisFrame);

        if (outStats) *outStats = stats;
    }

} // namespace bubblegum
