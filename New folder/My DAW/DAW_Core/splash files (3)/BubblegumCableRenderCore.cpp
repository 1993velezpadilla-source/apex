#include "BubblegumCableRenderCore.h"

namespace bubblegum
{
    void BubblegumCableRenderCore::resetMotion()
    {
        motionStabilityCore.reset();
    }

    void BubblegumCableRenderCore::resetAudio()
    {
        audioReactiveCore.reset();
    }

    void BubblegumCableRenderCore::setStyle(const BubblegumCableStyleSettingsCore::Style& s)
    {
        styleCore.setStyle(s);
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
        frame.source      = snapshot.endpoints.source;
        frame.destination = snapshot.endpoints.destination;
        frame.timeSeconds = timeSeconds;

        // ── Audio reactive pass ───────────────────────────────────────────────
        const auto& st = styleCore.getStyle();
        const auto reactive = audioReactiveCore.updateForSend(
            snapshot.key.sendId,
            snapshot.audioEnergy01,
            dt,
            st.audioThicknessScaleMax,
            st.audioFlowSpeedScaleMax,
            st.audioInternalGlowBase,
            st.audioInternalGlowMaxAdd,
            st.audioHighlightBase,
            st.audioHighlightMaxAdd,
            st.audioDropletChanceMaxAdd);

        frame.audioEnergy01           = reactive.energy01;
        frame.audioThicknessScale     = reactive.thicknessScale;
        frame.audioFlowSpeedScale     = reactive.flowSpeedScale;
        frame.audioInternalGlowAlpha  = reactive.internalGlowAlpha;
        frame.audioHighlightBoost     = reactive.highlightBoost;
        frame.audioDropletChanceScale = reactive.dropletChanceScale;

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

    //=========================================================================
    // paintAll — main frame entry point
    //=========================================================================
    void BubblegumCableRenderCore::paintAll(
        juce::Graphics& g,
        const std::vector<CableWorldSnapshot>& snapshots,
        const VisibilityInputs& visibilityInputs,
        float timeSeconds,
        float dt,
        RenderStats* outStats)
    {
        RenderStats stats;
        stats.submitted = (int)snapshots.size();

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
            visible, dt, styleCore.getStyle());

        // ── Per-cable render ───────────────────────────────────────────────────
        const int steps = styleCore.getRecommendedSteps();

        for (const auto& snapshot : stable)
        {
            const auto frame = makeResolvedFrame(snapshot, timeSeconds, dt);

            surfaceShaderCore.paintLiquidStream(
                g,
                frame,
                geometryCore,
                dropletCore,
                impactSplashCore,
                styleCore.getStyle(),
                steps);

            ++stats.rendered;
        }

        if (outStats) *outStats = stats;
    }

} // namespace bubblegum
