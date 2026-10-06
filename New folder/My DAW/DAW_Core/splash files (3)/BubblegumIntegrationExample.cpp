// =============================================================================
//  BubblegumIntegrationExample.cpp
//
//  Copy this into your component that owns the routing zone paint call.
//  Replace every [YOUR_...] placeholder with your real types/fields.
//
//  Pipeline order (must be preserved):
//    1. anchorResolver.setTrackBounds()    — layout pass
//    2. routingAdapter.getSendRecords()    — routing adapter pass
//    3. snapshotBuilder.build()            — snapshot build pass
//    4. renderCore.paintAll()              — render pass
// =============================================================================

#include "Source/Bubblegum/BubblegumCableTypes.h"
#include "Source/Bubblegum/BubblegumAnchorResolver.h"
#include "Source/Bubblegum/BubblegumRoutingAdapter_SourceSyncExample.h"
#include "Source/Bubblegum/BubblegumCableSnapshotBuilder.h"
#include "Source/Bubblegum/BubblegumCableRenderCore.h"

// ─── Member fields (add to your component class) ─────────────────────────────

bubblegum::BubblegumAnchorResolver                   anchorResolver;
bubblegum::BubblegumRoutingAdapterSourceSyncExample  routingAdapter;
bubblegum::BubblegumCableSnapshotBuilder             snapshotBuilder;
bubblegum::BubblegumCableRenderCore                  renderCore;

float lastFrameTimeSeconds = 0.0f;  // track elapsed time for animation

// ─── Call once per layout change (mixer scroll, resize, track reorder) ───────

void updateCableAnchors()
{
    anchorResolver.clear();

    for (auto* trackUi : [YOUR_MIXER_TRACK_UI_LIST])
    {
        // getBounds() must return bounds in the same coordinate space
        // that your routing zone paint() uses.
        anchorResolver.setTrackBounds(
            trackUi->getTrackId(),
            trackUi->getBounds().toFloat());
    }

    // Also call resetMotion() if tracks were added, removed, or reordered,
    // so the stability smoother doesn't drag from stale positions.
    // renderCore.resetMotion();
}

// ─── Call from your routing zone paint() method ──────────────────────────────

void paintBubblegumCables(juce::Graphics& g, float currentTimeSeconds)
{
    const float dt = currentTimeSeconds - lastFrameTimeSeconds;
    lastFrameTimeSeconds = currentTimeSeconds;

    // ── Step 1: resolve source track from bgV2.sourceSync ────────────────────
    // BubblegumRoutingAdapterSourceSyncExample handles the resolution law:
    //   resolvedSource = bgV2.sourceSync.getSourceTrackId()
    //   if (resolvedSource < 0) resolvedSource = selectedTrack
    auto sendRecords = routingAdapter.getSendRecords(
        [YOUR_ROUTING_GRAPH],        // routingGraph.getOutgoingSendsForTrack(...)
        bgV2.sourceSync,             // bgV2.sourceSync.getSourceTrackId()
        [YOUR_SELECTED_TRACK_ID]);   // fallback if sourceSync returns invalid

    // ── Step 2: build snapshots from routing truth + anchors + meters ─────────
    // Pass nullptr as the last argument if you have no meter bridge yet.
    auto snapshots = snapshotBuilder.build(
        sendRecords,
        [YOUR_SEND_STATE],           // sendState.exists() + sendState.isActive()
        anchorResolver,
        &[YOUR_METER_BRIDGE]);       // or: nullptr

    // ── Step 3: assemble visibility inputs ────────────────────────────────────
    // Visibility law: cablesVisible = bubblegumPanelOpen || cableForceVisible
    // NOTHING ELSE drives this. Routing truth is NOT linked to visibility.
    bubblegum::BubblegumCableRenderCore::VisibilityInputs vis;
    vis.bubblegumPanelOpen = [YOUR_BUBBLEGUM_PANEL_OPEN_FLAG];
    vis.cableForceVisible  = [YOUR_CABLE_FORCE_VISIBLE_FLAG];

    // ── Step 4: paint ─────────────────────────────────────────────────────────
    bubblegum::BubblegumCableRenderCore::RenderStats stats;
    renderCore.paintAll(g, snapshots, vis, currentTimeSeconds, dt, &stats);

    // stats.submitted — total snapshots received
    // stats.visible   — passed visibility filter
    // stats.rendered  — actually painted
    //
    // Use stats for debugging. Remove in release.
    // DBG("cables submitted=" << stats.submitted
    //     << " visible=" << stats.visible
    //     << " rendered=" << stats.rendered);
}

// ─── Optional: custom style override ─────────────────────────────────────────
//
// Call once at startup (or from a settings panel) to override any defaults.
// All defaults are already Bubblegum pink — only override if you want to tune.

void applyCustomStyle()
{
    auto style = renderCore.getStyle();

    // Example: push more visual chaos for a heavier stream feel
    style.lowerBreakupBias = 1.40f;
    style.topBreakupBias   = 0.18f;
    style.widthMulMax      = 2.80f;
    style.dropletDensity   = 0.28f;

    // Example: tighter sag for horizontal mixer layouts
    style.baseSag        = 10.0f;
    style.maxSag         = 55.0f;

    renderCore.setStyle(style);
}

// ─── Quality presets ─────────────────────────────────────────────────────────
//
// Two modes live inside the SAME renderer. Swap freely at runtime.
//
//   Performance  — lower cost. Use while user is dragging mixer, resizing,
//                  scrolling fast, or during dense sessions.
//   Cinematic    — maximum beauty. Use when stable/idle or explicitly enabled.
//
// Pick a default based on machine capability:
//   Strong systems    → Cinematic
//   Fallback systems  → Performance

void applyPerformanceStyle()
{
    using Style = bubblegum::BubblegumCableStyleSettingsCore::Style;
    auto style = renderCore.getStyle();

    style.qualityMode      = Style::QualityMode::Performance;
    style.performanceSteps = 48;

    // Shared toggles — keep the stream readable, drop expensive polish
    style.enableMistPass             = false;   // biggest pixel cost, first to go
    style.enableSpecularFlares       = false;   // many ellipses — skip on perf
    style.enableInnerFlowDensityLine = true;    // cheap, keeps flow read
    style.enableUndersideShadowLine  = true;    // cheap, keeps weight feel
    style.enableDroplets             = false;   // particles drop first
    style.enableSplashDrips          = true;    // few drips are cheap enough
    style.enableImpactSplash         = true;    // endpoint bursts — identity, keep on
    style.enableOutline              = true;

    // Cinematic extras — all off in Performance regardless
    style.enableCinematicScatterGlow = false;
    style.enableCinematicDepthAura   = false;
    style.enableCinematicMicroSheen  = false;

    renderCore.setStyle(style);
}

void applyCinematicStyle()
{
    using Style = bubblegum::BubblegumCableStyleSettingsCore::Style;
    auto style = renderCore.getStyle();

    style.qualityMode    = Style::QualityMode::Cinematic;
    style.cinematicSteps = 96;

    // Everything on — maximum polish
    style.enableMistPass             = true;
    style.enableSpecularFlares       = true;
    style.enableInnerFlowDensityLine = true;
    style.enableUndersideShadowLine  = true;
    style.enableDroplets             = true;
    style.enableSplashDrips          = true;
    style.enableImpactSplash         = true;
    style.enableOutline              = true;

    style.enableCinematicScatterGlow = true;
    style.enableCinematicDepthAura   = true;
    style.enableCinematicMicroSheen  = true;

    renderCore.setStyle(style);
}

// ─── Optional auto-switch ─────────────────────────────────────────────────────
//
// Call onUserInteractionStarted() from your mixer's mouseDown / drag / wheel
// handlers. Call onUserIdleTick() from a 100ms timer. After ~300ms of no
// interaction, the system returns to Cinematic.
//
// This gives you premium visuals without paying the cost during active UI work.

namespace bubblegum_autoswitch
{
    bool  interactionActive  = false;
    double lastInteractionMs = 0.0;
    constexpr double idleReturnMs = 300.0;
}

void onUserInteractionStarted()
{
    if (!bubblegum_autoswitch::interactionActive)
    {
        bubblegum_autoswitch::interactionActive = true;
        applyPerformanceStyle();
    }
    bubblegum_autoswitch::lastInteractionMs = juce::Time::getMillisecondCounterHiRes();
}

void onUserIdleTick()
{
    if (!bubblegum_autoswitch::interactionActive) return;

    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - bubblegum_autoswitch::lastInteractionMs >= bubblegum_autoswitch::idleReturnMs)
    {
        bubblegum_autoswitch::interactionActive = false;
        applyCinematicStyle();
    }
}

// ─── Call when routing resets ─────────────────────────────────────────────────
//
// Clears the motion smoother's state map so stale positions don't linger.

void onRoutingReset()
{
    renderCore.resetMotion();
    renderCore.resetAudio();
}
