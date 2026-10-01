#pragma once
#include "../RoutingCore/RoutingGraphCore.h"

namespace DAW {

// =====================================================================
// CableVisibilityQuery
//
// Single source of truth for whether a cable should be drawn.
// The render layer NEVER decides visibility from UI state alone.
//
// Rules:
//   Master cable  → draw if masterRouteState == ExistsActive
//   Send cable    → draw if edge.state == ExistsActive OR ExistsInactive
//                   (inactive cables are drawn dimmed but not hidden)
//   Non-existent  → do not draw
//
// The BubblegumCableRenderCore passes an alpha multiplier (0..1) to
// the paint call so dimmed/inactive cables render at reduced opacity
// without needing a separate render path.
// =====================================================================

struct CableVisibility
{
    bool  shouldDraw  = false;
    float alphaMult   = 1.0f;  // 1.0 = active, 0.35 = inactive/dimmed
};

class CableVisibilityQuery
{
public:
    CableVisibilityQuery(const MasterRouteStateCore& masterState,
                         const RoutingGraphCore&     graph)
        : master_(masterState), graph_(graph) {}

    // ── Master cable ─────────────────────────────────────────────────

    CableVisibility forMasterCable(const TrackId& trackId) const
    {
        const RouteState s = master_.getMasterRouteState(trackId);
        switch (s)
        {
            case RouteState::ExistsActive:   return { true,  1.00f };
            case RouteState::ExistsInactive: return { true,  0.35f };
            case RouteState::DoesNotExist:   return { false, 0.00f };
        }
        return { false, 0.00f };
    }

    // ── Send / sidechain cable ────────────────────────────────────────

    CableVisibility forEdge(const juce::String& edgeId) const
    {
        const RouteEdge* edge = graph_.getEdge(edgeId);
        if (!edge) return { false, 0.00f };

        switch (edge->state)
        {
            case RouteState::ExistsActive:   return { true,  1.00f };
            case RouteState::ExistsInactive: return { true,  0.35f };
            case RouteState::DoesNotExist:   return { false, 0.00f };
        }
        return { false, 0.00f };
    }

private:
    const MasterRouteStateCore& master_;
    const RoutingGraphCore&     graph_;
};

} // namespace DAW
