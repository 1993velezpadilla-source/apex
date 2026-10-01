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
#include <mutex>

namespace ArrangementEditor
{
    class ClipRenderCore : public juce::Component
    {
    public:
        ClipRenderCore(ArrangementClipModel& model,
                       ArrangementZoomCore& zoom,
                       DAW::AudioFileManager::CachedAudioHandle cachedAudio = {},
                       bool allowFileFallback = true);
        ~ClipRenderCore() override;

        void setModel(ArrangementClipModel& model);
        void refresh();
        void setCachedAudioHandle(DAW::AudioFileManager::CachedAudioHandle cachedAudio,
                                  bool allowFileFallback);

        /** Engine/device sample rate for seconds<->samples conversions in
            peak-resolution, tooltip and fade-draw math (pushed by
            ArrangementViewCore; 44100 default is the legacy fallback). */
        void setEngineSampleRate(double sr)
        {
            engineSampleRate_ = (sr > 0.0) ? sr : 44100.0;
        }

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

        ArrangementClipModel* getClip() { return &m_clipData; }
        const ArrangementClipModel* getClip() const { return &m_clipData; }

        // Stable clip UUID — survives dangling m_model after clip removal.
        // Use this instead of getClip()->id in rebuildClipRenderersInternal
        // where m_model may be invalidated by m_clipState.erase().
        const juce::Uuid& getClipId() const { return m_clipId; }

        // Update the renderer's snapshot of clip data.
        // Called from upsertClipRenderer (message thread) whenever the
        // clip state changes.  Synchronises with paint() which reads
        // m_clipData from the Direct2D swap chain thread.
        void updateClipData(const ArrangementClipModel& data)
        {
            bool waveformSourceChanged = false;
            {
                std::lock_guard<std::recursive_mutex> lock(m_dataMutex);
                waveformSourceChanged = m_clipData.sourcePath != data.sourcePath
                    || m_clipData.sourceOffset != data.sourceOffset
                    || m_clipData.length != data.length
                    || m_clipData.sourceStartSample != data.sourceStartSample
                    || m_clipData.sourceEndSample != data.sourceEndSample
                    || m_clipData.processedWaveformVersion != data.processedWaveformVersion;

                m_clipData = data;
                m_clipId   = data.id;
                m_model    = &m_clipData;  // keep pointer valid
                if (waveformSourceChanged)
                    m_waveformNeedsUpdate = true;
            }

            // Geometry-only updates are common during scroll.  Do not start
            // waveform work for those; refresh only when the source window
            // actually changed and the waveform is currently drawable.
            if (waveformSourceChanged && isShowing() && shouldRenderWaveform())
                refresh();
        }

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
    // Clicking the pitch/stretch/mode badges toggles the preset curve
    // (clip pitch/stretch automation lane rows) visibility.
    std::function<void()> onPresetCurveToggle;
    // Global Free View flag, set by the arrangement view on toggle. Every
    // renderer checks it live on press/drag, so no creation path can miss the
    // wiring and leave a clip editable while the mode is on.
    static inline std::atomic<bool> s_viewOnlyMode { false };

    // Scroll/gesture LOD: while a pan or pinch is running every renderer stops
    // after its background + header, so each frame stays cheap and the gesture
    // is smooth. Full detail (waveform, badges, fades) returns on release.
    static inline std::atomic<bool> s_lightweightPaint { false };

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

        bool shouldRenderWaveform() const noexcept
        {
            return ArrangementClipLodPolicy::drawWaveform(getWidth(), getHeight());
        }

        // A renderer must remain interactive while an edge/fade gesture owns
        // the mouse, even if its bounds temporarily leave the viewport.
        bool isEditGestureActive() const noexcept { return m_activeZone != Zone::None; }

        // Waveform loading state — for batch loading optimization
        bool needsWaveformUpdate() const { return m_waveformNeedsUpdate; }
        void markWaveformUpdateComplete() { m_waveformNeedsUpdate = false; }

        // Visual state badges
        void setFrozen(bool frozen)       { m_frozen       = frozen;   repaint(); }
        void setHQRendered(bool hq)       { m_hqRendered   = hq;       repaint(); }
        void setCPUDraftForced(bool draft) { m_cpuDraft     = draft;    repaint(); }

    private:
        ArrangementClipModel  m_clipData;              // owned copy — immune to vector reallocation
        ArrangementClipModel* m_model = &m_clipData;   // always points to m_clipData
        juce::Uuid             m_clipId;                // stable copy of clip ID
        double                 engineSampleRate_ = 44100.0;
        ArrangementZoomCore&  m_zoom;
        DAW::AudioFileManager::CachedAudioHandle m_cachedAudio;
        bool m_allowFileFallback = true;

        // Mutex protecting m_clipData against concurrent read (paint /
        // swap chain thread) and write (updateClipData / message thread).
        mutable std::recursive_mutex m_dataMutex;

        std::unique_ptr<ClipWaveformCacheCore> m_waveformCache;
        std::unique_ptr<PianoRollButtonCore>   m_pianoRollBtn;

        // Hit-zones for interactive handles inside the clip.
        enum class Zone { None, FadeIn, FadeOut,
                          ResizeLeft, ResizeRight, VolumeBtn, MuteBtn };
        Zone m_activeZone   = Zone::None; // currently being dragged
        Zone m_hoveredZone  = Zone::None; // under cursor (drives cursor + paint)

        // Drawn pitch/stretch/mode badge area (local coords), refreshed by
        // drawPitchRateBadges(). Clicking it toggles the preset curve.
        juce::Rectangle<int> presetBadgeHitArea_;

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

    // Pro-Tools-style clip gain drag (Alt+drag on the waveform).
    bool  m_gainDragActive    = false;
    float m_gainDragStartY    = 0.0f;
    float m_gainDragStartGain = 1.0f;
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
        int m_lastWaveformRequestWidth = -1;
        bool m_lastWaveformRenderable = false;

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
