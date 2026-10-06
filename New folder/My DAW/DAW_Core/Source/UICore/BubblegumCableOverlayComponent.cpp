#include "BubblegumCableOverlayComponent.h"
#include "MixerPanel.h"

#include <unordered_map>

namespace
{
    template <typename T>
    static void hashCombine(std::size_t& seed, const T& value) noexcept
    {
        seed ^= std::hash<T>{}(value) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    }

    bubblegum::SendId routeIdToSendId(const DAW::RouteID& routeId) noexcept
    {
        return static_cast<bubblegum::SendId>(routeId.hashCode64());
    }

    struct RoutingGraphSendStateAdapter
    {
        explicit RoutingGraphSendStateAdapter(const DAW::RoutingGraph& graphIn) : graph(graphIn) {}

        bool exists(bubblegum::SendId sendId) const noexcept
        {
            return findConnection(sendId) != nullptr;
        }

        bool isActive(bubblegum::SendId sendId) const noexcept
        {
            if (auto* connection = findConnection(sendId))
                return connection->active.load(std::memory_order_relaxed);

            return false;
        }

    private:
        const DAW::RoutingGraph& graph;

        const DAW::RoutingConnection* findConnection(bubblegum::SendId sendId) const noexcept
        {
            for (auto* connection : graph.getAllConnections())
            {
                if (connection == nullptr)
                    continue;
                // Match Send connections and Direct→master connections.
                const bool isSend   = (connection->type == DAW::ConnectionType::Send);
                const bool isDirect = (connection->type == DAW::ConnectionType::Direct);
                if (!isSend && !isDirect)
                    continue;
                if (routeIdToSendId(connection->id) == sendId)
                    return connection;
            }
            return nullptr;
        }
    };

}

namespace DAW {

void BubblegumCableOverlayComponent::setCableAccentColour(juce::Colour colour)
{
    if (cableAccentColour_ == colour)
        return;

    cableAccentColour_ = colour;

    auto style = DAW::BubblegumAppearanceSettings::makeCableStyleForAccent(colour);
    const auto current = renderCore_.getStyle();
    style.qualityMode                 = current.qualityMode;
    style.performanceSteps            = current.performanceSteps;
    style.cinematicSteps              = current.cinematicSteps;
    style.enableMistPass              = current.enableMistPass;
    style.enableSpecularFlares        = current.enableSpecularFlares;
    style.enableInnerFlowDensityLine  = current.enableInnerFlowDensityLine;
    style.enableUndersideShadowLine   = current.enableUndersideShadowLine;
    style.enableDroplets              = current.enableDroplets;
    style.enableSplashDrips           = current.enableSplashDrips;
    style.enableImpactSplash          = current.enableImpactSplash;
    style.enableOutline               = current.enableOutline;
    style.enableCinematicScatterGlow  = current.enableCinematicScatterGlow;
    style.enableCinematicDepthAura    = current.enableCinematicDepthAura;
    style.enableCinematicMicroSheen   = current.enableCinematicMicroSheen;
    renderCore_.setStyle(style);
    repaint();
}

BubblegumCableOverlayComponent::~BubblegumCableOverlayComponent()
{
    attachRoutingGraphListener(nullptr);
    detachMovementListeners();
    stopTimer();
}

void BubblegumCableOverlayComponent::detachMovementListeners()
{
    if (mixerPanel_)
        mixerPanel_->removeComponentListener(this);

    if (listenedTopLevel_ != nullptr && listenedTopLevel_ != mixerPanel_)
        listenedTopLevel_->removeComponentListener(this);

    listenedTopLevel_ = nullptr;
}

void BubblegumCableOverlayComponent::refreshMovementListeners()
{
    juce::Component* newTopLevel = nullptr;

    if (mixerPanel_)
    {
        mixerPanel_->addComponentListener(this);
        newTopLevel = mixerPanel_->getTopLevelComponent();
        if (newTopLevel == mixerPanel_)
            newTopLevel = nullptr;
    }

    if (newTopLevel == listenedTopLevel_)
        return;

    if (listenedTopLevel_ != nullptr && listenedTopLevel_ != mixerPanel_)
        listenedTopLevel_->removeComponentListener(this);

    listenedTopLevel_ = newTopLevel;

    if (listenedTopLevel_ != nullptr)
        listenedTopLevel_->addComponentListener(this);
}

void BubblegumCableOverlayComponent::bind(BubblegumV2System* bgV2, MixerPanel* mixer, juce::Viewport* viewport)
{
    detachMovementListeners();

    bgV2_        = bgV2;
    mixerPanel_  = mixer;
    viewport_    = viewport;
    lastTimerSec_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
    snapshotCacheValid_ = false;
    cachedSendRecords_.clear();
    cachedSnapshots_.clear();

    // Push reduced-motion preference to the sidechain cable animator.
    // Default is false (full animation). To honour the Windows "Show animations
    // in Windows" accessibility toggle, wire setReducedMotion() from your
    // application settings once a DAW-level accessibility preference is added.
    if (bgV2_)
        bgV2_->setReducedMotion(false);
    cachedSourceId_.clear();

    // Listen to topology changes so cables appear immediately on send/sidechain creation.
    attachRoutingGraphListener(bgV2_ ? bgV2_->routingGraph : nullptr);

    refreshMovementListeners();

    // Wire mixer FPS callback so the HUD can display it.
    mixerFpsFn_ = mixer ? [mixer]() { return mixer->getMixerPaintFps(); }
                        : std::function<double()>{};
}

void BubblegumCableOverlayComponent::componentMovedOrResized(juce::Component& component,
                                                             bool wasMoved,
                                                             bool /*wasResized*/)
{
    if (!wasMoved || !mixerPanel_)
        return;

    if (&component != mixerPanel_ && &component != listenedTopLevel_)
        return;

    const auto mixerScreen = mixerPanel_->localPointToGlobal(juce::Point<int>());
    lastMixerScreenX_ = mixerScreen.x;
    lastMixerScreenY_ = mixerScreen.y;
    notifyMotion();

    // Cables stay fully visible during mixer motion.
    // core smooths anchor positions so there's no jump to hide, and the user
    // wants the stream to remain live while scrolling/dragging/resizing.
}

void BubblegumCableOverlayComponent::componentParentHierarchyChanged(juce::Component& component)
{
    if (&component == mixerPanel_)
        refreshMovementListeners();
}

bool BubblegumCableOverlayComponent::shouldRender() const
{
    if (!bgV2_ || !bgV2_->routingGraph || !mixerPanel_)
        return false;

    // Bubblegum overlays belong to the mixer. If the mixer is not actually
    // visible (window closed, not showing on screen), the cables must hide.
    if (!mixerPanel_->isShowing())
        return false;

    return mixerPanel_->areCablesVisible();
}

void BubblegumCableOverlayComponent::timerCallback()
{
    const double timerStart = juce::Time::getMillisecondCounterHiRes();
    const double nowSec = timerStart * 0.001;
    if (lastTimerSec_ <= 0.0)
        lastTimerSec_ = nowSec;

    if (viewport_ && bgV2_)
    {
        const int scrollX = viewport_->getViewPositionX();
        const int scrollY = viewport_->getViewPositionY();
        if (scrollX != lastViewportScrollX_ || scrollY != lastViewportScrollY_)
        {
            lastViewportScrollX_ = scrollX;
            lastViewportScrollY_ = scrollY;
            snapshotCacheValid_  = false;
            compositeFrameDirty_ = true;
            snapMotionUntilSec_  = nowSec + 0.12;
        }
    }

    if (mixerPanel_)
    {
        const auto mixerScreen = mixerPanel_->localPointToGlobal(juce::Point<int>());
        if (mixerScreen.x != lastMixerScreenX_ || mixerScreen.y != lastMixerScreenY_)
        {
            lastMixerScreenX_ = mixerScreen.x;
            lastMixerScreenY_ = mixerScreen.y;
            snapshotCacheValid_  = false;
            compositeFrameDirty_ = true;
            snapMotionUntilSec_  = nowSec + 0.12;
        }
    }

    const double dt = juce::jlimit(0.0, 1.0 / 60.0, nowSec - lastTimerSec_);
    lastTimerSec_ = nowSec;
    timerDt_ = (float)dt;

    ++timerTickCount_;

    osDragFade_.tick(dt);
    osDragFade_.triggerFadeIn();  // always allow fade-in; envelope handles timing

    visualTime_ += dt;

    // Keep the sidechain cable animator in sync with cable visibility so it
    // skips tick() when cables are hidden, honoring the offscreen-stop rule.
    if (bgV2_)
        bgV2_->setSidechainCablesVisible(shouldRender());

    timingHUD_.recordTimer(juce::Time::getMillisecondCounterHiRes() - timerStart);

    if (onTick)
        onTick();

    const bool nowRendering = shouldRender();

    // ── Repaint gate ────────────────────────────────────────────────────────
    // Only call repaint() when there is something visible to paint AND at
    // least one reason to believe the frame will be different from the last.
    //
    // Condition A: overlay is visible and has non-zero size
    const bool visibleAndSized = isVisible() && getWidth() > 0 && getHeight() > 0;

    // Condition B: real work to do
    //   B1. snapshot is stale (topology/layout changed) → must re-bake composite
    //   B2. sidechain animation is ticking (bgV2 tick advances cable state)
    //   B3. one final repaint when transitioning from rendering→hidden to clear the frame
    const bool snapshotStale         = nowRendering && !snapshotCacheValid_;
    const bool sidechainAnimating    = nowRendering && bgV2_ && bgV2_->hasSidechainCablesActive();
    const bool clearFrame            = (!nowRendering && wasRendering_);
    const bool forceRefresh          = nowRendering && forceTopologyRefreshFrames_ > 0;
    const bool motionSmoothingActive = nowRendering && nowSec < snapMotionUntilSec_;

    // Mark composite dirty whenever snapshot changes or sidechain is animating
    if (snapshotStale || sidechainAnimating || forceRefresh || motionSmoothingActive)
    {
        if (forceRefresh)
        {
            snapshotCacheValid_ = false;
            --forceTopologyRefreshFrames_;
        }
        compositeFrameDirty_ = true;
    }

    if (visibleAndSized && (compositeFrameDirty_ || clearFrame))
    {
        ++paintedTickCount_;
        repaint();
    }
    else
    {
        ++gatedTickCount_;
    }

    wasRendering_ = nowRendering;
}

void BubblegumCableOverlayComponent::paint(juce::Graphics& g)
{
    paintRateMeter_.tick();
    const double paintStart = juce::Time::getMillisecondCounterHiRes();

    if (osDragFade_.isFullyHidden())
        return;

    if (!shouldRender())
        return;

    // Apply OS-drag fade opacity envelope
    const float dragOpacity = osDragFade_.getOpacity();
    if (dragOpacity < 1.f)
        g.setOpacity(dragOpacity);

    // Detect mixer viewport scroll \u2014 when the horizontal scroll position changes,
    // Sync cached scroll coords for motion freeze detection.
    if (viewport_)
    {
        const int scrollX = viewport_->getViewPositionX();
        const int scrollY = viewport_->getViewPositionY();
        if (scrollX != lastViewportScrollX_ || scrollY != lastViewportScrollY_)
        {
            lastViewportScrollX_ = scrollX;
            lastViewportScrollY_ = scrollY;
        }
    }

    auto sourceId = bgV2_->sourceSync.getSourceTrackId();
    if (sourceId.isEmpty() && mixerPanel_)
        sourceId = mixerPanel_->getSelectedTrackId();

    // ── Compute routing node Y ──────────────────────────────────────────
    // Routing lane lives BELOW the mixer horizontal scrollbar, in a true
    // external routing zone. X comes from each track's center; Y is anchored
    // to the scrollbar bottom + a very small offset so the attachment feels
    // born from the mixer architecture rather than floating below it.
    constexpr float kBelowScrollbarGap = 3.f;
    float routingNodeY = 0.f;
    float vpLeftX = 0.f, vpRightX = (float)getWidth();

    if (viewport_ && viewport_->isShowing())
    {
        // viewport->getHeight() is the outer viewport height which INCLUDES
        // the horizontal scrollbar. Anchoring routingNodeY there (then adding
        // a gap) places the cable lane cleanly below the scrollbar.
        auto vpBelowScrollbarGlobal = viewport_->localPointToGlobal(
            juce::Point<int>(0, viewport_->getHeight()));
        routingNodeY = (float)getLocalPoint(nullptr, vpBelowScrollbarGlobal).y
                       + kBelowScrollbarGap;

        // Viewport visible X range (for offscreen cable clamping)
        auto vpLeftGlobal  = viewport_->localPointToGlobal(juce::Point<int>(0, 0));
        auto vpRightGlobal = viewport_->localPointToGlobal(
            juce::Point<int>(viewport_->getMaximumVisibleWidth(), 0));
        vpLeftX  = (float)getLocalPoint(nullptr, vpLeftGlobal).x;
        vpRightX = (float)getLocalPoint(nullptr, vpRightGlobal).x;
    }
    else
    {
        // Fallback: use mixer panel bottom + gap
        auto mixerBotGlobal = mixerPanel_->localPointToGlobal(
            juce::Point<int>(0, mixerPanel_->getHeight()));
        routingNodeY = (float)getLocalPoint(nullptr, mixerBotGlobal).y
                       + kBelowScrollbarGap;
    }

    const float minRoutingNodeY = 8.0f;
    const float maxRoutingNodeY = (float)getHeight() - (float)kCableZoneH + 18.0f;
    routingNodeY = juce::jlimit(minRoutingNodeY, juce::jmax(minRoutingNodeY, maxRoutingNodeY), routingNodeY);

    // laneBottomY: deepest sag point for cable bezier curves
    float laneBottomY = routingNodeY + (float)kCableZoneH - 20.f;

    // Convert a strip's center X into overlay coords, paired with routingNodeY.
    // Both source and target anchors live in the lower external routing zone.
    auto toRoutingNode = [&](MixerStrip* strip) -> juce::Point<float>
    {
        auto sb = strip->getBounds();
        auto anchorGlobal = mixerPanel_->localPointToGlobal(
            juce::Point<int>(sb.getCentreX(), 0));
        float x = (float)getLocalPoint(nullptr, anchorGlobal).x;
        return { x, routingNodeY };
    };

    if (sourceId.isEmpty())
        return;

    const auto& strips = mixerPanel_->getStrips();
    const auto* folderCore = mixerPanel_->getFolderBusCore();
    const auto& collapsed  = mixerPanel_->getCollapsedFolderBuses();

    std::size_t layoutHash = 0;
    hashCombine(layoutHash, std::hash<int>{}(juce::roundToInt(routingNodeY * 8.0f)));
    hashCombine(layoutHash, std::hash<int>{}(juce::roundToInt(vpLeftX * 8.0f)));
    hashCombine(layoutHash, std::hash<int>{}(juce::roundToInt(vpRightX * 8.0f)));
    for (auto* strip : strips)
    {
        if (strip == nullptr || !strip->isVisible())
            continue;

        const auto bounds = strip->getBounds();
        hashCombine(layoutHash, std::hash<std::int64_t>{}(strip->getTrack().getID().hashCode64()));
        hashCombine(layoutHash, std::hash<int>{}(bounds.getX()));
        hashCombine(layoutHash, std::hash<int>{}(bounds.getY()));
        hashCombine(layoutHash, std::hash<int>{}(bounds.getWidth()));
        hashCombine(layoutHash, std::hash<int>{}(bounds.getHeight()));
    }
    for (const auto& folderId : collapsed)
        hashCombine(layoutHash, std::hash<std::int64_t>{}(folderId.hashCode64()));

    std::size_t topologyHash = 0;
    if (bgV2_->routingGraph)
    {
        const auto& connections = bgV2_->routingGraph->getAllConnections();
        hashCombine(topologyHash, std::hash<int>{}(connections.size()));
        for (auto* connection : connections)
        {
            if (connection == nullptr)
                continue;

            hashCombine(topologyHash, std::hash<std::int64_t>{}(connection->id.hashCode64()));
            hashCombine(topologyHash, std::hash<int>{}(static_cast<int>(connection->type)));
            hashCombine(topologyHash, std::hash<bool>{}(connection->active.load(std::memory_order_relaxed)));
            hashCombine(topologyHash, std::hash<int>{}(juce::roundToInt(connection->gain.load(std::memory_order_relaxed) * 10000.0f)));
        }
    }

    const bool needsSnapshotRebuild = !snapshotCacheValid_
        || cachedSourceId_ != sourceId
        || cachedLayoutHash_ != layoutHash
        || cachedTopologyHash_ != topologyHash;

    if (needsSnapshotRebuild)
    {
        anchorResolver_.clear();

        // Build a map: trackId → strip pointer (for fast folder-bus lookup)
        auto& stripByTrack = stripByTrackCache_;
        stripByTrack.clear();
        stripByTrack.reserve((size_t)strips.size());
        for (auto* s : strips)
            if (s) stripByTrack[s->getTrack().getID()] = s;

        auto resolveDestStrip = [&](const TrackID& destId) -> MixerStrip*
        {
            if (!folderCore)
            {
                auto it = stripByTrack.find(destId);
                return it != stripByTrack.end() ? it->second : nullptr;
            }

            auto ancestors = folderCore->getAncestorChain(destId);
            for (int i = (int)ancestors.size() - 1; i >= 0; --i)
            {
                if (collapsed.count(ancestors[(size_t)i]))
                {
                    auto it = stripByTrack.find(ancestors[(size_t)i]);
                    return it != stripByTrack.end() ? it->second : nullptr;
                }
            }

            auto it = stripByTrack.find(destId);
            return it != stripByTrack.end() ? it->second : nullptr;
        };

        for (auto* strip : strips)
        {
            if (strip == nullptr)
                continue;

            auto anchor = toRoutingNode(strip);
            anchor.x = juce::jlimit(vpLeftX + 24.0f, vpRightX - 24.0f, anchor.x);

            anchorResolver_.setTrackBounds(
                strip->getTrack().getID(),
                juce::Rectangle<float>(anchor.x - 0.5f, anchor.y - 1.0f, 1.0f, 1.0f));
        }

        cachedSendRecords_ = routingAdapter_.getSendRecords(
            *bgV2_->routingGraph,
            bgV2_->sourceSync,
            mixerPanel_->getSelectedTrackId());

        FORENSIC_LOG("[CABLE OVERLAY REBUILD] source=" << sourceId
            << " graphVersion=" << bgV2_->routingGraph->getGraphVersion()
            << " connections=" << bgV2_->routingGraph->getConnectionCount()
            << " sendRecords=" << (int)cachedSendRecords_.size());

        auto& folderSendCount      = folderSendCountCache_;
        auto& folderSidechainCount = folderSidechainCountCache_;
        auto& folderSendNames      = folderSendNamesCache_;
        auto& folderSidechainNames = folderSidechainNamesCache_;
        folderSendCount.clear();
        folderSidechainCount.clear();
        folderSendNames.clear();
        folderSidechainNames.clear();

        for (auto& record : cachedSendRecords_)
        {
            auto* destStrip = resolveDestStrip(record.destinationTrackId);
            if (destStrip == nullptr) continue;

            const TrackID redirectedId = destStrip->getTrack().getID();
            if (redirectedId != record.destinationTrackId)
            {
                folderSendCount[redirectedId]++;
                if (auto* origTrack = bgV2_->trackManager ? bgV2_->trackManager->getTrack(record.destinationTrackId) : nullptr)
                    folderSendNames[redirectedId].add(origTrack->getName());

                anchorResolver_.setTrackBounds(
                    record.destinationTrackId,
                    anchorResolver_.hasTrack(redirectedId)
                        ? [&]{ auto a = anchorResolver_.getBubblegumReceiveAnchor(redirectedId);
                               return juce::Rectangle<float>(a.x - 0.5f, a.y - 1.0f, 1.0f, 1.0f); }()
                        : juce::Rectangle<float>(-10000.f, -10000.f, 1.f, 1.f));
            }
        }

        // Count collapsed-folder sidechain targets from the currently selected source.
        if (bgV2_->routingGraph)
        {
            const auto& allConns = bgV2_->routingGraph->getAllConnections();
            for (auto* conn : allConns)
            {
                if (!conn || conn->type != ConnectionType::Sidechain) continue;
                auto* srcNode  = bgV2_->routingGraph->getNode(conn->sourceNodeId);
                auto* destNode = bgV2_->routingGraph->getNode(conn->destNodeId);
                if (!srcNode || !destNode) continue;
                if (srcNode->trackId != sourceId) continue;

                auto* destStrip = resolveDestStrip(destNode->trackId);
                if (destStrip == nullptr) continue;

                const TrackID redirectedId = destStrip->getTrack().getID();
                if (redirectedId != destNode->trackId)
                {
                    folderSidechainCount[redirectedId]++;
                    if (auto* origTrack = bgV2_->trackManager ? bgV2_->trackManager->getTrack(destNode->trackId) : nullptr)
                        folderSidechainNames[redirectedId].add(origTrack->getName());
                }
            }
        }

        const RoutingGraphSendStateAdapter sendState(*bgV2_->routingGraph);
        cachedSnapshots_ = snapshotBuilder_.build(
            cachedSendRecords_,
            sendState,
            anchorResolver_);

        DAW::TrackID masterId;
        for (auto* strip : strips)
            if (strip && strip->getTrack().isMaster())
                { masterId = strip->getTrack().getID(); break; }

        if (masterId.isNotEmpty())
        {
            for (auto& s : cachedSnapshots_)
                s.isMasterTarget = (s.key.destinationTrack == masterId);
        }

        cachedSourceId_ = sourceId;
        cachedLayoutHash_ = layoutHash;
        cachedTopologyHash_ = topologyHash;
        snapshotCacheValid_ = true;

        // Prune animator states for sidechain connections that no longer exist
        // in the routing graph, so the state vector doesn't grow indefinitely.
        if (bgV2_->routingGraph)
        {
            juce::StringArray liveIds;
            for (auto* conn : bgV2_->routingGraph->getAllConnections())
                if (conn && conn->type == ConnectionType::Sidechain)
                    liveIds.add(conn->id);
            bgV2_->pruneSidechainCableStates(liveIds);
        }
    }

    // Feed sidechain trigger levels from source track peak meters so the
    // chain-ellipse pulse fires in sync with actual signal activity.
    if (bgV2_->routingGraph && bgV2_->trackManager)
    {
        for (auto* conn : bgV2_->routingGraph->getAllConnections())
        {
            if (!conn || conn->type != ConnectionType::Sidechain) continue;
            auto* srcNode = bgV2_->routingGraph->getNode(conn->sourceNodeId);
            if (!srcNode) continue;
            auto* srcTrack = bgV2_->trackManager->getTrack(srcNode->trackId);
            if (!srcTrack) continue;
            // Use the peak level as the trigger signal (0..1 linear)
            const float peakLin = juce::jmax(srcTrack->getPeakLevelLeft(),
                                              srcTrack->getPeakLevelRight());
            bgV2_->feedSidechainTriggerLevel(conn->id, peakLin);
        }
    }

    // ── Apply per-frame quality mode from the mixer's HD toggle ──────────
    {
        using Style = bubblegum::BubblegumCableStyleSettingsCore::Style;
        const auto& currentStyle = renderCore_.getStyle();
        const auto desired = mixerPanel_->getCableQuality();
        if (currentStyle.qualityMode != desired)
        {
            auto next = currentStyle;
            next.qualityMode = desired;
            renderCore_.setStyle(next);
        }

        // Apply sidechain cable settings from the mixer's settings menu.
        bgV2_->setSidechainCableQuality(mixerPanel_->getSidechainCableQuality());
    }

    bubblegum::BubblegumCableRenderCore::VisibilityInputs vis;
    vis.bubblegumPanelOpen = mixerPanel_->isBubblegumModeActive();
    vis.cableForceVisible  = mixerPanel_->areCablesVisible() && !vis.bubblegumPanelOpen;

    const double setupDone = juce::Time::getMillisecondCounterHiRes();
    double renderDoneMs = setupDone;
    bubblegum::BubblegumCableRenderCore::RenderStats cableRenderStats;

    // ── Composite frame cache ──────────────────────────────────────────────
    // Only re-bake cachedFrame_ when topology/layout changed or sidechain is
    // animating. Static cables with all cache hits blit the previous frame.
    const int w = getWidth(), h = getHeight();
    const bool frameSizeChanged = !cachedFrame_.isValid()
                                  || cachedFrame_.getWidth() != w
                                  || cachedFrame_.getHeight() != h;
    if (frameSizeChanged)
    {
        cachedFrame_ = juce::Image(juce::Image::ARGB, juce::jmax(1, w), juce::jmax(1, h), true);
        compositeFrameDirty_ = true;
    }

    if (compositeFrameDirty_)
    {
        cachedFrame_.clear(cachedFrame_.getBounds(), juce::Colours::transparentBlack);
        juce::Graphics offG(cachedFrame_);

        const bool snapSendCableMotion = juce::Time::getMillisecondCounterHiRes() * 0.001 < snapMotionUntilSec_;
        renderCore_.paintAll(offG, cachedSnapshots_, vis, (float)visualTime_, timerDt_, &cableRenderStats, snapSendCableMotion);

        // ── Sidechain cables ──────────────────────────────────────────────────
        if (bgV2_->routingGraph)
        {
            auto resolveSidechainDestStrip = [&](const TrackID& destId) -> MixerStrip*
            {
                if (!folderCore)
                {
                    auto it = stripByTrackCache_.find(destId);
                    return it != stripByTrackCache_.end() ? it->second : nullptr;
                }

                auto ancestors = folderCore->getAncestorChain(destId);
                for (int i = (int)ancestors.size() - 1; i >= 0; --i)
                {
                    if (collapsed.count(ancestors[(size_t)i]))
                    {
                        auto it = stripByTrackCache_.find(ancestors[(size_t)i]);
                        return it != stripByTrackCache_.end() ? it->second : nullptr;
                    }
                }

                auto it = stripByTrackCache_.find(destId);
                return it != stripByTrackCache_.end() ? it->second : nullptr;
            };

            std::vector<BubblegumCableInput> scCables;
            for (auto* conn : bgV2_->routingGraph->getAllConnections())
            {
                if (!conn || conn->type != ConnectionType::Sidechain) continue;

                auto* srcNode  = bgV2_->routingGraph->getNode(conn->sourceNodeId);
                auto* destNode = bgV2_->routingGraph->getNode(conn->destNodeId);
                if (!srcNode || !destNode) continue;

                // Only draw this cable when its source track is the active/selected source.
                if (srcNode->trackId != sourceId) continue;

                // O(1) lookups via the per-frame strip map built during snapshot rebuild
                auto srcIt  = stripByTrackCache_.find(srcNode->trackId);
                auto* destStrip = resolveSidechainDestStrip(destNode->trackId);
                if (srcIt == stripByTrackCache_.end() || destStrip == nullptr) continue;

                const bool isActive = conn->active.load(std::memory_order_relaxed)
                                   && !conn->bypassed.load(std::memory_order_relaxed);

                // Clamp anchor X to the visible viewport range — same rule as normal send cables
                // (anchorResolver_ applies [vpLeftX+24, vpRightX-24] to every strip).
                auto clampAnchorX = [&](juce::Point<float> p) -> BubblegumCableEndpoint {
                    BubblegumCableEndpoint ep;
                    ep.x = juce::jlimit(vpLeftX + 24.0f, vpRightX - 24.0f, p.x);
                    ep.y = p.y;
                    return ep;
                };

                BubblegumCableInput scCable;
                scCable.source    = clampAnchorX(toRoutingNode(srcIt->second));
                scCable.target    = clampAnchorX(toRoutingNode(destStrip));
                scCable.edgeId    = conn->id;
                scCable.alphaMult = isActive ? 1.0f : 0.45f;
                scCables.push_back(scCable);
            }

            if (!scCables.empty())
            {
                bgV2_->sidechainCable.thicknessMultiplier = sidechainCableThickness_;
                bgV2_->paintSidechainCables(offG, scCables, laneBottomY);
            }
        } // if (bgV2_->routingGraph)

        // ── Folder badges ─────────────────────────────────────────────────────
        folderBadges_.clear();
        auto& folderSendCount      = folderSendCountCache_;
        auto& folderSidechainCount = folderSidechainCountCache_;
        auto& folderSendNames      = folderSendNamesCache_;
        auto& folderSidechainNames = folderSidechainNamesCache_;

        std::unordered_set<TrackID, bubblegum::TrackIdHash> folderBadgeIds;
        for (const auto& kv : folderSendCount)      folderBadgeIds.insert(kv.first);
        for (const auto& kv : folderSidechainCount) folderBadgeIds.insert(kv.first);

        for (const auto& folderId : folderBadgeIds)
        {
            const int sendCount  = folderSendCount.count(folderId)      ? folderSendCount[folderId]      : 0;
            const int scCount    = folderSidechainCount.count(folderId) ? folderSidechainCount[folderId] : 0;
            const int badgeCount = sendCount + scCount;
            if (badgeCount <= 0) continue;

            auto stripIt = stripByTrackCache_.find(folderId);
            if (stripIt == stripByTrackCache_.end() || stripIt->second == nullptr) continue;

            auto* folderStrip = stripIt->second;
            auto sb = folderStrip->getBounds();
            auto anchorGlobal = mixerPanel_->localPointToGlobal(juce::Point<int>(sb.getCentreX(), 0));
            juce::Point<float> anchorLocal = getLocalPoint(nullptr, anchorGlobal).toFloat();

            constexpr float kBadgeR       = 9.f;
            constexpr float kBadgeXOffset = 26.f;
            juce::Point<float> sendBadgeCentre { anchorLocal.x + kBadgeXOffset, routingNodeY };
            juce::Point<float> scBadgeCentre   { anchorLocal.x - kBadgeXOffset, routingNodeY };
            juce::Point<float> badgeCentre = (sendCount > 0 && scCount > 0)
                ? juce::Point<float>(anchorLocal.x, routingNodeY)
                : (sendCount > 0 ? sendBadgeCentre : scBadgeCentre);

            if (sendCount > 0 && showSendBadges_)
            {
                offG.setColour(juce::Colour(0xFFFF4FA3).withAlpha(0.18f));
                offG.fillEllipse(sendBadgeCentre.x - kBadgeR * 1.6f, sendBadgeCentre.y - kBadgeR * 1.6f, kBadgeR * 3.2f, kBadgeR * 3.2f);
                juce::ColourGradient bg2(juce::Colour(0xFFFF4FA3), sendBadgeCentre.x, sendBadgeCentre.y - kBadgeR,
                                         juce::Colour(0xFFC44E88), sendBadgeCentre.x, sendBadgeCentre.y + kBadgeR, false);
                offG.setGradientFill(bg2);
                offG.fillEllipse(sendBadgeCentre.x - kBadgeR, sendBadgeCentre.y - kBadgeR, kBadgeR * 2.f, kBadgeR * 2.f);
                offG.setColour(juce::Colours::white);
                offG.setFont(juce::Font(9.f, juce::Font::bold));
                offG.drawText(juce::String(sendCount),
                    juce::Rectangle<float>(sendBadgeCentre.x - kBadgeR, sendBadgeCentre.y - kBadgeR, kBadgeR * 2.f, kBadgeR * 2.f),
                    juce::Justification::centred, false);
            }

            if (scCount > 0 && showSidechainBadges_)
            {
                juce::Path diamond;
                const float r = kBadgeR;
                diamond.startNewSubPath(scBadgeCentre.x,     scBadgeCentre.y - r);
                diamond.lineTo         (scBadgeCentre.x + r, scBadgeCentre.y);
                diamond.lineTo         (scBadgeCentre.x,     scBadgeCentre.y + r);
                diamond.lineTo         (scBadgeCentre.x - r, scBadgeCentre.y);
                diamond.closeSubPath();
                offG.setColour(juce::Colour(0xFF3A7BD5).withAlpha(0.20f));
                offG.fillPath(diamond);
                offG.setColour(juce::Colour(0xFF3A7BD5));
                offG.fillPath(diamond);
                offG.setColour(juce::Colours::white);
                offG.setFont(juce::Font(9.f, juce::Font::bold));
                offG.drawText(juce::String(scCount),
                    juce::Rectangle<float>(scBadgeCentre.x - kBadgeR, scBadgeCentre.y - kBadgeR, kBadgeR * 2.f, kBadgeR * 2.f),
                    juce::Justification::centred, false);
            }

            juce::String folderName;
            if (bgV2_->trackManager != nullptr)
                if (auto* ft = bgV2_->trackManager->getTrack(folderId))
                    folderName = ft->getName();

            juce::String tip = folderName.isNotEmpty() ? folderName : juce::String("Folder");
            tip += "\n" + juce::String(sendCount) + (sendCount == 1 ? " send" : " sends")
                 + ", " + juce::String(scCount)   + (scCount   == 1 ? " sidechain" : " sidechains");

            auto appendNames = [&](const juce::StringArray& names, const char* header)
            {
                if (names.size() > 0)
                {
                    tip += juce::String("\n\n") + header;
                    for (int i = 0; i < names.size() && i < 5; ++i)
                        tip += "\n  \xE2\x80\xA2 " + names[i];
                    if (names.size() > 5) tip += "\n  ...";
                }
            };
            appendNames(folderSendNames[folderId],      "Sends:");
            appendNames(folderSidechainNames[folderId], "Sidechains:");

            FolderBadge badge;
            badge.centre        = badgeCentre;
            badge.label         = juce::String(badgeCount);
            badge.tooltip       = tip;
            badge.radius        = (sendCount > 0 && scCount > 0) ? (kBadgeR + 10.f) : kBadgeR;
            badge.sendCount     = sendCount;
            badge.sidechainCount = scCount;
            folderBadges_.push_back(badge);
        }

        renderDoneMs = juce::Time::getMillisecondCounterHiRes();
        compositeFrameDirty_ = false;
    } // compositeFrameDirty_ block ends — offG is destroyed here

    // Blit the cached frame to the live context.
    g.drawImageAt(cachedFrame_, 0, 0);


    // ── Tooltip overlay ───────────────────────────────────────────────────
    if (hoveredTooltip_.isNotEmpty())
    {
        const int pad = 6;
        juce::Font tipFont(11.f);
        auto lines = juce::StringArray::fromLines(hoveredTooltip_);
        int maxW = 0;
        for (auto& l : lines)
            maxW = juce::jmax(maxW, (int)std::ceil(tipFont.getStringWidthFloat(l)));
        int tipW = maxW + pad * 2;
        int tipH = lines.size() * 14 + pad * 2;

        int tx = juce::jlimit(2, getWidth()  - tipW - 2, tooltipPos_.x + 14);
        int ty = juce::jlimit(2, getHeight() - tipH - 2, tooltipPos_.y - tipH - 6);

        auto tipR = juce::Rectangle<float>((float)tx, (float)ty, (float)tipW, (float)tipH);
        g.setColour(juce::Colour(0xF0101018));
        g.fillRoundedRectangle(tipR, 5.f);
        g.setColour(juce::Colour(0x60FF4FA3));
        g.drawRoundedRectangle(tipR, 5.f, 0.8f);
        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.setFont(tipFont);
        for (int i = 0; i < lines.size(); ++i)
            g.drawText(lines[i], tx + pad, ty + pad + i * 14, tipW - pad * 2, 14,
                       juce::Justification::centredLeft, false);
    }

   #if JUCE_DEBUG
    // ── Timing profiler HUD (Debug only) ──────────────────────────────────
    {
        const double paintEnd = juce::Time::getMillisecondCounterHiRes();
        timingHUD_.record(
            setupDone  - paintStart,   // setup ms
            renderDoneMs - setupDone,  // paintAll ms
            paintEnd   - paintStart,   // total paint ms
            (int)cachedSnapshots_.size(),
            cableRenderStats.cacheHits,
            cableRenderStats.cacheMisses);
        const double mixerFps = mixerFpsFn_ ? mixerFpsFn_() : -1.0;
        timingHUD_.draw(g, getLocalBounds(), paintRateMeter_.getFps(), mixerFps);
    }
   #endif
}

void BubblegumCableOverlayComponent::mouseMove(const juce::MouseEvent& e)
{
    juce::String newTip;
    juce::Point<int> newPos = e.getPosition();

    for (const auto& badge : folderBadges_)
    {
        if (badge.centre.getDistanceFrom(e.position.toFloat()) <= badge.radius + 4.f)
        {
            newTip = badge.tooltip;
            break;
        }
    }

    if (newTip != hoveredTooltip_)
    {
        hoveredTooltip_ = newTip;
        tooltipPos_     = newPos;
        repaint();
    }
    else if (hoveredTooltip_.isNotEmpty() && newPos != tooltipPos_)
    {
        tooltipPos_ = newPos;
        repaint();
    }
}

void BubblegumCableOverlayComponent::mouseExit(const juce::MouseEvent&)
{
    if (hoveredTooltip_.isNotEmpty())
    {
        hoveredTooltip_.clear();
        repaint();
    }
}

} // namespace DAW
