// ===========================================================================
// ClipRenderCore.h
// Main JUCE component representing one clip in the arrangement.
// Handles all visual layers: background, waveform, fades, gain line,
// mute overlay, badges, selection highlight, hover.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "ArrangementClipModel.h"
#include "ClipWaveformCacheCore.h"
#include "ClipFadeRenderCore.h"
#include "ArrangementZoomCore.h"
#include "PianoRollButtonCore.h"
#include <memory>

namespace ArrangementEditor
{
    class ClipRenderCore : public juce::Component
    {
    public:
        ClipRenderCore(ArrangementClipModel& model, ArrangementZoomCore& zoom);
        ~ClipRenderCore() override;

        void setModel(ArrangementClipModel& model);
        void refresh();

        // Force immediate synchronous repaint (used when clip properties change)
        void forceUpdate()
        {
            repaint();
            // JUCE trick: calling repaint() multiple times can help force immediate update
            if (auto* peer = getPeer())
                peer->performAnyPendingRepaintsNow();
        }

        void setSelected(bool selected);
        bool isSelected() const { return m_selected; }
        void setAutomationActive(bool active) { if (m_automationActive != active) { m_automationActive = active; repaint(); } }

        ArrangementClipModel* getClip() { return m_model; }
        const ArrangementClipModel* getClip() const { return m_model; }

        // Access waveform cache so ArrangementViewCore can push processed peaks
        ClipWaveformCacheCore& getWaveformCache() { return *m_waveformCache; }

        void paint(juce::Graphics& g) override;
        void resized() override;

        void mouseEnter(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseMove(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;

        // Callbacks
        std::function<void()> onClick;
        std::function<void()> onDoubleClick;
        std::function<void()> onPianoRollClick;

        // Interactive handle callbacks — wired by ArrangementViewCore
        // onEditBegin: called when user starts dragging a fade/resize handle.
        //              Parent should capture a pre-edit snapshot for undo.
        // onEditLive:  called on every drag tick so engine/UI can sync in realtime.
        // onEditCommit:called on mouseUp; parent should push an undo command and
        //              sync the final state to the audio engine.
        // onVolumeButtonClicked: opens the existing properties window focused on gain.
        std::function<void()> onEditBegin;
        std::function<void()> onEditLive;
        std::function<void()> onEditCommit;
        std::function<void()> onVolumeButtonClicked;
        std::function<void()> onMuteButtonClicked;

        // TimePitch cache/render status — set from message thread, read in paint()
        // Call these after receiving background render callbacks.
        void setRenderingStretch(bool rendering) { m_renderingStretch = rendering; repaint(); }
        void setUsingFallback(bool fallback)      { m_usingFallback    = fallback;  repaint(); }

        // Waveform loading state — for batch loading optimization
        bool needsWaveformUpdate() const { return m_waveformNeedsUpdate; }
        void markWaveformUpdateComplete() { m_waveformNeedsUpdate = false; }

        // Visual state badges
        void setFrozen(bool frozen)       { m_frozen       = frozen;   repaint(); }
        void setHQRendered(bool hq)       { m_hqRendered   = hq;       repaint(); }
        void setCPUDraftForced(bool draft) { m_cpuDraft     = draft;    repaint(); }

    private:
        ArrangementClipModel* m_model = nullptr;
        ArrangementZoomCore&  m_zoom;

        std::unique_ptr<ClipWaveformCacheCore> m_waveformCache;
        std::unique_ptr<PianoRollButtonCore>   m_pianoRollBtn;

        // Hit-zones for interactive handles inside the clip.
        enum class Zone { None, FadeIn, FadeOut,
                          ResizeLeft, ResizeRight, VolumeBtn, MuteBtn };
        Zone m_activeZone   = Zone::None; // currently being dragged
        Zone m_hoveredZone  = Zone::None; // under cursor (drives cursor + paint)

        // Drag state for handle interaction
        double  m_dragStartX        = 0.0; // mouse SCREEN x at mouseDown (frame-stable)
        double  m_dragStartLength   = 0.0;
        double  m_dragStartFadeIn   = 0.0;
        double  m_dragStartFadeOut  = 0.0;
        double  m_dragStartTime     = 0.0;
        double  m_dragStartSrcOff   = 0.0;
        int64_t m_dragStartSrcStart = 0;
        int64_t m_dragStartSrcEnd   = 0;
        double  m_dragStartStretch  = 1.0;
        bool    m_dragStretchMode   = false; // false = trim (default), true = stretch (Shift)
        float   m_dragStartCurveIn  = 0.f;
        float   m_dragStartCurveOut = 0.f;

        // Trim-feedback tooltip — populated during edge drag, cleared on mouseUp.
        juce::String m_trimTooltipText;
        bool         m_showTrimTooltip = false;

        Zone    hitTestZone(juce::Point<int> pos) const;
        void    updateCursorForZone(Zone z);

        // Geometry helpers (computed from current bounds + name bar)
        juce::Rectangle<int> fadeInHandleRect()  const;
        juce::Rectangle<int> fadeOutHandleRect() const;
        juce::Rectangle<int> fadeBandRect()      const;
        juce::Rectangle<int> leftEdgeRect()      const;
        juce::Rectangle<int> rightEdgeRect()     const;
        juce::Rectangle<int> volumeButtonRect()  const;
        juce::Rectangle<int> muteButtonRect()    const;

        bool m_selected        = false;
        bool m_hovered         = false;
        bool m_automationActive = false;
        bool m_renderingStretch = false;
        bool m_usingFallback    = false;
        bool m_frozen           = false;  // clip is frozen (CPU saved)
        bool m_hqRendered       = false;  // OfflineHQ cache ready
        bool m_cpuDraft         = false;  // CPU safety forced Draft
        bool m_waveformNeedsUpdate = true;  // Flag: waveform needs loading/refresh

        // DEBUG: Paint counter to verify paint() is actually being called
        mutable int m_paintDebugCounter = 0;

        // NAME_H: height reserved at top for clip name label
        // Waveform starts below this so name never overlaps waveform.
        static constexpr int kNameH = 16;

        void drawBackground(juce::Graphics& g);
        void drawWaveform(juce::Graphics& g);
        void drawFades(juce::Graphics& g);
        void drawGainLine(juce::Graphics& g);
        void drawMuteOverlay(juce::Graphics& g);
        void drawLockIcon(juce::Graphics& g);
        void drawClipName(juce::Graphics& g);
        void drawPitchRateBadges(juce::Graphics& g);
        void drawTimePitchStatus(juce::Graphics& g);
        void drawSelectionHighlight(juce::Graphics& g);
        void drawHoverGlow(juce::Graphics& g);
        void drawClipHandles(juce::Graphics& g);
        void drawVolumeButton(juce::Graphics& g);
        void drawMuteButton(juce::Graphics& g);
        void drawTrimTooltip(juce::Graphics& g);

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
