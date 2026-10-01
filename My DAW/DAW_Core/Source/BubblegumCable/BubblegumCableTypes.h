#pragma once
#include <JuceHeader.h>
#include <vector>

namespace DAW {

// =====================================================================
// BubblegumCableTypes.h — shared data types only. NO logic here.
// Every core in Source/BubblegumCable/ depends on this file.
// =====================================================================

struct BubblegumCableEndpoint
{
    float x = 0.f;
    float y = 0.f;
};

/** Input description of a single cable. Everything else is derived. */
struct BubblegumCableInput
{
    BubblegumCableEndpoint source;
    BubblegumCableEndpoint target;

    float sendLevel  = 0.f;
    float sendEnergy = 0.f;

    // Graph-driven visibility — set by CableVisibilityQuery, not by UI.
    // alphaMult == 0 means do not paint; 0..1 = inactive-to-active.
    float alphaMult = 1.0f;

    // Routing graph identifiers.  One of these must be set by the caller.
    // edgeId is non-empty for send/sidechain cables;
    // masterTrackId is non-empty for master cables.
    juce::String edgeId;
    juce::String masterTrackId;

    // Transient visual-only reaction state.
    float activationPulse = 0.f;
    float deletePulse     = 0.f;
    float dragTension     = 0.f;
    float interactionGlow = 0.f;

    // Identifier used by effects to track per-cable state (droplets etc).
    // Defaults to source.x which is unique per routing source column.
    float id() const noexcept { return source.x; }

    bool shouldDraw() const noexcept { return alphaMult > 0.f; }
};

/** Per-sample point on the cable. Asymmetric: topW = upper lip, bottomW = heavy underside + drips. */
struct BubblegumCablePoint
{
    float t     = 0.f;
    float x     = 0.f, y  = 0.f;   // centerline
    float nx    = 0.f, ny = -1.f;  // unit normal
    float halfW   = 0.f;           // compat / helpers (max of topW, bottomW)
    float topW    = 0.f;           // upper lip
    float bottomW = 0.f;           // heavy underside + integrated drips
};

/** Geometry result — pure data, no paint. */
struct BubblegumCableGeometry
{
    static constexpr int kSeg = 128;
    std::array<BubblegumCablePoint, kSeg + 1> points;

    // Endpoints retained for hub/overlay reference.
    float sx = 0.f, sy = 0.f, tx = 0.f, ty = 0.f;

    // Base thickness used by the renderer.
    float thickness = 12.f;

    // Closed ribbon path, prebuilt for fast repeat fill/stroke.
    juce::Path ribbon;
};

/** Optional per-track slime merge hub (not drawn itself by default). */
struct BubblegumTrackHubInfo
{
    float x = 0.f;
    float y = 0.f;
    int   cableCount = 1;
    float energy = 0.f;
    float phase  = 0.f;

    float activationPulse = 0.f;
    float deletePulse     = 0.f;
    float dragTension     = 0.f;
    float interactionGlow = 0.f;
};

/** Debug overlay modes. Full = production (no overlay). */
enum class BubblegumCableDebugMode
{
    Full = 0,        // default — real rendering, no helper overlays
    Centerline,
    TopContour,
    BottomContour,
    Outline,
    FilledRibbon
};

} // namespace DAW
