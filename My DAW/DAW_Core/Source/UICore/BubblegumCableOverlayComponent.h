#pragma once
#include <JuceHeader.h>
#include "../Bubblegum/BubblegumV2System.h"
#include "../Bubblegum/BubblegumAppearanceSettings.h"
#include "../Bubblegum/BubblegumAnchorResolver.h"
#include "../Bubblegum/BubblegumRoutingAdapter_SourceSyncExample.h"
#include "../Bubblegum/BubblegumCableSnapshotBuilder.h"
#include "../BubblegumCable/BubblegumCableOsDragFadeCore.h"
#include "../Bubblegum/BubblegumCableRenderCore.h"

namespace DAW {

class MixerPanel;
class MixerStrip;

/**
 * BubblegumCableOverlayComponent
 *
 * Full-screen transparent overlay that renders Bubblegum liquid routing cables
 * in a zone BELOW the mixer, so cables hang naturally outside the mixer bounds.
 *
 * Lives as a sibling of the mixer viewport in MainComponent — not clipped to
 * the mixer interior. Coordinate conversion uses localPointToGlobal so it works
 * correctly for both docked and floating mixer layouts.
 */
class BubblegumCableOverlayComponent : public juce::Component,
                                       public RoutingGraph::Listener,
                                       private juce::Timer
{
public:
    static constexpr int kCableZoneH = 120;
    static constexpr int kLegacyTimerHz = 60;

    BubblegumCableOverlayComponent()
    {
        setOpaque(false);
        setInterceptsMouseClicks(true, false);  // receive mouseMove for tooltips; hitTest=false blocks no clicks
        startTimerHz(kLegacyTimerHz);   // 60 Hz — smooth cable animation
    }

    ~BubblegumCableOverlayComponent() override;

    /** Cable-anchor click target: the small endpoint balls where cables
     *  connect. Clicking one toggles that connection Active/Bypassed.
     *  The DESTINATION anchor doubles as the wet/dry knob: left-drag adjusts
     *  the send level, right-click toggles Active/Bypassed. */
    struct CableAnchorHit
    {
        TrackID            sourceTrack;
        TrackID            destTrack;
        bool               isSidechain = false;
        bool               isDestination = false;
        juce::Point<float> point;
    };

    bool hitTest(int x, int y) override
    {
        // Only intercept clicks that land on a cable endpoint anchor; every
        // other pixel passes through to the mixer below.
        const float px = (float) x, py = (float) y;
        for (const auto& a : cableAnchors_)
            if (a.point.getDistanceFrom(juce::Point<float>(px, py)) <= 12.f)
                return true;
        return false;
    }

    /** Called by MainComponent on scroll/mixer-move to synchronise cable anchors
     *  with the latest mixer geometry before the next paint. */
    void notifyMotion()
    {
        requestTopologyRefresh();
    }

    void notifyMotionFinished()
    {
        snapMotionUntilSec_ = 0.0;
        forceTopologyRefreshFrames_ = 0;
        snapshotCacheValid_ = false;
        compositeFrameDirty_ = true;
        repaint();
    }

    void requestTopologyRefresh()
    {
        lastMixerScreenX_ = std::numeric_limits<int>::min();
        lastMixerScreenY_ = std::numeric_limits<int>::min();
        snapshotCacheValid_  = false;
        compositeFrameDirty_ = true;
        cachedSourceId_.clear();
        forceTopologyRefreshFrames_ = juce::jmax(forceTopologyRefreshFrames_, 8);
        repaint();
    }

    void timerCallback() override;

    void resized() override
    {
        // No detached droplet state in the new pipeline — nothing to purge.
    }

    void bind(BubblegumV2System* bgV2, MixerPanel* mixer, juce::Viewport* viewport = nullptr);

    /** Detach all non-owning UI/model references before their owners shut down. */
    void unbind() noexcept;

    /** Check if cables should be rendered (panel open OR force-visible). */
    bool shouldRender() const;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent&) override;
    void setCableAccentColour(juce::Colour colour);
    juce::Colour getCableAccentColour() const noexcept { return cableAccentColour_; }
    bubblegum::BubblegumCableStyleSettingsCore::Style getRenderStyle() const { return renderCore_.getStyle(); }
    void setRenderStyle(const bubblegum::BubblegumCableStyleSettingsCore::Style& style) { renderCore_.setStyle(style); repaint(); }

    /** Event-driven Mixer/offscreen state refresh callback. */
    std::function<void()> onTick;

    void setShowSendBadges(bool show) { showSendBadges_ = show; repaint(); }
    void setShowSidechainBadges(bool show) { showSidechainBadges_ = show; repaint(); }
    void setSidechainCableThickness(float thickness) { sidechainCableThickness_ = thickness; repaint(); }

private:
    BubblegumV2System* bgV2_       = nullptr;
    MixerPanel*        mixerPanel_ = nullptr;
    juce::Viewport*    viewport_   = nullptr;
    bool               wasRendering_ = false;

    // Last known viewport scroll and Mixer screen position.
    mutable int lastViewportScrollX_ = -1;
    mutable int lastViewportScrollY_ = -1;
    mutable int lastMixerScreenX_    = std::numeric_limits<int>::min();
    mutable int lastMixerScreenY_    = std::numeric_limits<int>::min();

    double visualTime_      = 0.0;
    double lastTimerSec_    = 0.0;
    double freezeUntilSec_  = 0.0;
    double snapMotionUntilSec_ = 0.0;
    BubblegumCableOsDragFadeCore osDragFade_;

    // Liquid stream pipeline
    bubblegum::BubblegumAnchorResolver anchorResolver_;
    bubblegum::BubblegumRoutingAdapterSourceSyncExample routingAdapter_;
    bubblegum::BubblegumCableSnapshotBuilder snapshotBuilder_;
    bubblegum::BubblegumCableRenderCore renderCore_;
    float timerDt_ = 1.0f / 60.0f;
    juce::Colour cableAccentColour_ = BubblegumAppearanceSettings::getDefaultCableAccent();

    int timerTickCount_ = 0;

    // ── Repaint gate debug counters ────────────────────────────────────────
    mutable int gatedTickCount_  = 0;   // ticks where repaint() was skipped
    mutable int paintedTickCount_ = 0;  // ticks where repaint() was called

public:
    int getGatedTickCount()  const noexcept { return gatedTickCount_;  }
    int getPaintedTickCount() const noexcept { return paintedTickCount_; }

private:

    juce::Image cachedFrame_;  // offscreen render target; allocated once, reused each frame

    std::vector<bubblegum::BubblegumSendRecord> cachedSendRecords_;
    std::vector<bubblegum::CableWorldSnapshot>  cachedSnapshots_;
    std::vector<CableAnchorHit>                 cableAnchors_; // rebuilt each frame bake

    std::size_t cachedLayoutHash_    = 0;
    std::size_t cachedTopologyHash_  = 0;
    bool        snapshotCacheValid_  = false;
    bool        compositeFrameDirty_ = true;  // true = must re-bake cachedFrame_ this paint
    int         forceTopologyRefreshFrames_ = 0;
    TrackID     cachedSourceId_;

    // Reused per-frame map to avoid heap alloc/dealloc on every paint
    std::unordered_map<DAW::TrackID, MixerStrip*, bubblegum::TrackIdHash> stripByTrackCache_;
    std::unordered_map<DAW::TrackID, int, bubblegum::TrackIdHash>          folderSendCountCache_;
    std::unordered_map<DAW::TrackID, int, bubblegum::TrackIdHash>          folderSidechainCountCache_;
    std::unordered_map<DAW::TrackID, juce::StringArray, bubblegum::TrackIdHash> folderSendNamesCache_;
    std::unordered_map<DAW::TrackID, juce::StringArray, bubblegum::TrackIdHash> folderSidechainNamesCache_;

    // Folder-bus badge hover tooltip
    struct FolderBadge
    {
        juce::Point<float> centre;
        juce::String       label;    // e.g. "3 sends"
        juce::String       tooltip;  // full tooltip text
        float              radius = 10.f;
        int                sendCount = 0;
        int                sidechainCount = 0;
    };
    mutable std::vector<FolderBadge> folderBadges_;
    mutable juce::String             hoveredTooltip_;
    mutable juce::Point<int>         tooltipPos_;

    // Badge visibility settings
    bool showSendBadges_ = true;
    bool showSidechainBadges_ = true;
    float sidechainCableThickness_ = 1.5f;

    // ── Wet/dry knob drag state (destination anchor) ───────────────────────
    bool    draggingWetDry_ = false;
    TrackID wetDrySrc_;
    TrackID wetDryDest_;
    float   wetDryStartLevel_ = 0.f;
    float   wetDryStartY_     = 0.f;

    // ── RoutingGraph::Listener ────────────────────────────────────────────
    void connectionAdded(RoutingConnection* conn) override
    {
        // If the new connection is a Send/Direct and sourceSync doesn't already
        // have a source set, sync it from the connection so paint() doesn't bail
        // early at "sourceId.isEmpty()" — covers full-matrix sends from any track.
        if (conn && bgV2_ && bgV2_->routingGraph)
        {
            const bool isSend   = (conn->type == DAW::ConnectionType::Send);
            const bool isDirect = (conn->type == DAW::ConnectionType::Direct);
            const bool isSC     = (conn->type == DAW::ConnectionType::Sidechain);
            if ((isSend || isDirect || isSC) && bgV2_->sourceSync.getSourceTrackId().isEmpty())
            {
                if (auto* srcNode = bgV2_->routingGraph->getNode(conn->sourceNodeId))
                    if (srcNode->trackId.isNotEmpty())
                        bgV2_->sourceSync.sync(srcNode->trackId);
            }
        }

        requestTopologyRefresh();
    }
    void connectionRemoved(const RouteID&) override
    {
        requestTopologyRefresh();
    }
    void graphChanged() override { requestTopologyRefresh(); }

    RoutingGraph* listenedRoutingGraph_ = nullptr;
    void attachRoutingGraphListener(RoutingGraph* graph)
    {
        if (listenedRoutingGraph_ == graph) return;
        if (listenedRoutingGraph_) listenedRoutingGraph_->removeListener(this);
        listenedRoutingGraph_ = graph;
        if (listenedRoutingGraph_) listenedRoutingGraph_->addListener(this);
    }

    // ── Honest paint-rate counter: counts real paint() calls per wall-clock second ──
    struct PaintRateMeter
    {
        void tick() noexcept
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            ++framesSinceLastSample_;

            if (lastSampleTime_ <= 0.0)
            {
                lastSampleTime_ = now;
                return;
            }

            const double elapsedMs = now - lastSampleTime_;
            if (elapsedMs >= 500.0)
            {
                const double instantFps = (framesSinceLastSample_ * 1000.0) / elapsedMs;
                smoothedFps_ = (smoothedFps_ <= 0.0)
                    ? instantFps
                    : (smoothedFps_ * 0.6 + instantFps * 0.4);
                framesSinceLastSample_ = 0;
                lastSampleTime_ = now;
            }
        }

        double getFps() const noexcept { return smoothedFps_; }

    private:
        double lastSampleTime_      = 0.0;
        int    framesSinceLastSample_ = 0;
        double smoothedFps_         = 0.0;
    };
    mutable PaintRateMeter paintRateMeter_;

    // Callback set by whoever can supply MixerPanel's paint FPS (set in bind())
    std::function<double()> mixerFpsFn_;

    // ── Per-frame timing profiler (always-on, displayed on-screen) ────────
    struct PaintTimingHUD
    {
        // Rolling average over kWindow frames
        static constexpr int kWindow = 60;
        double setupMs[kWindow]  = {};
        double renderMs[kWindow] = {};
        double totalMs[kWindow]  = {};
        double timerMs[kWindow]  = {};
        int    snapCount[kWindow]= {};
        int    cacheHits[kWindow] = {};
        int    cacheMisses[kWindow] = {};
        int    head = 0;
        int    filled = 0;

        double lastTimerCall = 0.0; // set each timerCallback

        void record(double setup, double render, double total, int snaps, int hits, int misses)
        {
            setupMs [head] = setup;
            renderMs[head] = render;
            totalMs [head] = total;
            snapCount[head]= snaps;
            cacheHits[head] = hits;
            cacheMisses[head] = misses;
            head = (head + 1) % kWindow;
            if (filled < kWindow) ++filled;
        }

        void recordTimer(double ms) { timerMs[head] = ms; }

        double avg(const double* arr) const
        {
            if (filled == 0) return 0.0;
            double s = 0;
            for (int i = 0; i < filled; ++i) s += arr[i];
            return s / filled;
        }

        void draw(juce::Graphics& g, juce::Rectangle<int> area,
                  double overlayFps = -1.0, double mixerFps = -1.0) const
        {
            if (filled == 0) return;
            double aSetup  = avg(setupMs);
            double aRender = avg(renderMs);
            double aTotal  = avg(totalMs);
            double aTimer  = avg(timerMs);
            int    aSnaps  = (filled > 0) ? snapCount[(head - 1 + kWindow) % kWindow] : 0;
            int totalHits = 0;
            int totalMisses = 0;
            for (int i = 0; i < filled; ++i)
            {
                totalHits += cacheHits[i];
                totalMisses += cacheMisses[i];
            }

            juce::String txt;
            txt << "=== CABLE PAINT PROFILER ===\n";
            if (overlayFps >= 0.0)
                txt << "Overlay FPS:  " << juce::String(overlayFps, 1) << "\n";
            if (mixerFps >= 0.0)
                txt << "Mixer FPS:    " << juce::String(mixerFps, 1) << "\n";
            txt << "Total paint:  " << juce::String(aTotal,  2) << " ms\n"
                << "  Setup/maps: " << juce::String(aSetup,  2) << " ms\n"
                << "  paintAll:   " << juce::String(aRender, 2) << " ms\n"
                << "Cable cache: " << totalHits << " H / " << totalMisses << " M\n"
                << "Timer cb:     " << juce::String(aTimer,  2) << " ms\n"
                << "Snapshots:    " << aSnaps << "\n"
                << "Budget @60Hz: 16ms";

            auto lines = juce::StringArray::fromLines(txt);
            float lineH = 13.f;
            float boxW  = 190.f;
            float boxH  = (float)lines.size() * lineH + 8.f;
            float bx    = (float)area.getRight()  - boxW - 4.f;
            float by    = (float)area.getBottom() - boxH - 4.f;

            g.setColour(juce::Colours::black.withAlpha(0.78f));
            g.fillRoundedRectangle(bx, by, boxW, boxH, 4.f);
            g.setColour(aTotal > 45.0 ? juce::Colours::red.withAlpha(0.9f)
                      : aTotal > 30.0 ? juce::Colours::yellow.withAlpha(0.9f)
                                      : juce::Colours::lime.withAlpha(0.9f));
            g.drawRoundedRectangle(bx, by, boxW, boxH, 4.f, 1.f);
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.f, juce::Font::plain));
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            for (int i = 0; i < lines.size(); ++i)
                g.drawText(lines[i], (int)(bx + 4.f), (int)(by + 4.f + i * lineH),
                           (int)(boxW - 8.f), (int)lineH, juce::Justification::centredLeft, false);
        }
    };
    mutable PaintTimingHUD timingHUD_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumCableOverlayComponent)
};

} // namespace DAW
