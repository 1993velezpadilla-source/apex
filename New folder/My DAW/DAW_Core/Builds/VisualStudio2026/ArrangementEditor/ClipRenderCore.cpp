// ===========================================================================
// ClipRenderCore.cpp
// ===========================================================================
#include "ClipRenderCore.h"
#include "ClipPitchCore.h"
#include "ClipGainCore.h"
#include "ArrangementViewCore.h"
#include "EditorToolCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t bg           = 0xFF1E1E1E;
        constexpr uint32_t text         = 0xFFFFFFFF;
        constexpr uint32_t textDim      = 0xFFAAAAAA;
        constexpr uint32_t accent       = 0xFFE07B39;
        constexpr uint32_t border       = 0xFF222222;
        constexpr uint32_t clipBase     = 0xFF2A2A2A;
        constexpr uint32_t waveform     = 0xCCFFFFFF;
        constexpr uint32_t muteOverlay  = 0xBB111111;
    }

    ClipRenderCore::ClipRenderCore(ArrangementClipModel& model, ArrangementZoomCore& zoom)
        : m_model(&model), m_zoom(zoom)
    {
        // CRITICAL: Disable JUCE's automatic component image caching
        // This was causing waveforms to not update when clip properties changed
        setCachedComponentImage(nullptr);
        setBufferedToImage(false);

        m_waveformCache = std::make_unique<ClipWaveformCacheCore>();
        auto safe = juce::Component::SafePointer<ClipRenderCore>(this);
        m_waveformCache->onPeaksReady = [safe]() { if (safe != nullptr) safe->repaint(); };

        m_pianoRollBtn = std::make_unique<PianoRollButtonCore>(PianoRollButtonCore::Style::ClipOverlay);
        m_pianoRollBtn->onClick = [this]() {
            if (onPianoRollClick)
                onPianoRollClick();
        };
        addAndMakeVisible(*m_pianoRollBtn);

        setSize(100, 80);
    }

    ClipRenderCore::~ClipRenderCore()
    {
        DBG("[CRASH TRACE] action=cliprender_destroyed");
        if (m_waveformCache)
            m_waveformCache->onPeaksReady = nullptr;
    }

    void ClipRenderCore::setModel(ArrangementClipModel& model)
    {
        m_model = &model;
        repaint();
    }

    void ClipRenderCore::refresh()
    {
        if (m_model)
        {
            const int widthPx = (int)(m_model->length * m_zoom.getPixelsPerSecond());

            // Visible-source hint → zoom-adaptive peak resolution.
            int64_t visibleSamples = m_model->sourceEndSample - m_model->sourceStartSample;
            if (visibleSamples <= 0)
                visibleSamples = (int64_t)std::llround(juce::jmax(0.0, m_model->length) * 44100.0);

            m_waveformCache->requestPeaks(m_model->sourcePath, widthPx, visibleSamples);
        }
        repaint();
    }

    void ClipRenderCore::setSelected(bool selected)
    {
        m_selected = selected;
        repaint();
    }

    void ClipRenderCore::paint(juce::Graphics& g)
    {
        if (!m_model) return;

        // DEBUG: Increment paint counter to verify paint() is being called
        ++m_paintDebugCounter;

        auto b = getLocalBounds().toFloat();

        // Layer 1: Background
        drawBackground(g);

        // Layer 2: Waveform
        drawWaveform(g);

        // Layer 3-4: Fades
        drawFades(g);

        // Layer 5: Gain line
        drawGainLine(g);

        // Layer 6: Mute overlay
        if (m_model->muted)
            drawMuteOverlay(g);

        // Layer 7: Lock icon
        if (m_model->locked)
            drawLockIcon(g);

        // Layer 8: Clip name  (always at very top, never on waveform)
        drawClipName(g);

        // Layer 9: Pitch/rate badges  (below name bar)
        drawPitchRateBadges(g);

        // Layer 9b: TimePitch cache/render status
        drawTimePitchStatus(g);

        // Layer 10: Selection highlight
        if (m_selected)
            drawSelectionHighlight(g);

        // Layer 11: Hover glow
        if (m_hovered)
            drawHoverGlow(g);

        // Layer 11b: Interactive handles (fade corners, edge resize, volume btn)
        drawClipHandles(g);
        drawVolumeButton(g);
        drawMuteButton(g);
        if (m_showTrimTooltip)
            drawTrimTooltip(g);

        // Layer 12: Automation-active dim overlay
        if (m_automationActive)
        {
            g.setColour(juce::Colour(0xFF05070A).withAlpha(0.72f));
            g.fillRoundedRectangle(b, 3.0f);
        }
    }

    void ClipRenderCore::resized()
    {
        // Position Piano Roll button in top-right corner only when the clip is
        // wide enough to keep header controls usable.
        if (m_pianoRollBtn)
        {
            const bool showPianoRollButton = getWidth() >= 44;
            m_pianoRollBtn->setVisible(showPianoRollButton);

            if (showPianoRollButton)
                m_pianoRollBtn->setBounds(getWidth() - 34, 2, 32, 20);
        }

        refresh();
    }

    // -----------------------------------------------------------------------
    // Handle hit-zones — geometry helpers
    // -----------------------------------------------------------------------
    // Fade handles SLIDE with the fade length so the grab triangle always
    // sits at the fade's end point (like Pro Tools / Logic / Live).
    // Edge zones do TRIM (left-edge trim does not slip the audio inside).
    static constexpr int kHandleSize = 14;
    static constexpr int kEdgeWidth  = 10;
    static constexpr int kVolBtnSize = 14;
    static constexpr int kMuteBtnSize = 14;

    juce::Rectangle<int> ClipRenderCore::fadeInHandleRect() const
    {
        // The fade-in grab zone is the LEFT half of the dedicated fade band
        // (the strip between the header and the green divider line). The grab
        // box tracks the end of the current fade-in so the triangle apex stays
        // under the cursor (Pro Tools / Logic style).
        if (!m_model) return {};
        const float pps = (float)m_zoom.getPixelsPerSecond();
        int fadePx = juce::jlimit(0, juce::jmax(0, getWidth() - 1),
                                   (int)std::lround(m_model->fadeInLength * pps));
        const int x = juce::jmax(0, fadePx - kHandleSize);
        return { x, kNameH, kHandleSize, kHandleSize };
    }

    juce::Rectangle<int> ClipRenderCore::fadeOutHandleRect() const
    {
        if (!m_model) return {};
        const float pps = (float)m_zoom.getPixelsPerSecond();
        int fadePx = juce::jlimit(0, juce::jmax(0, getWidth() - 1),
                                   (int)std::lround(m_model->fadeOutLength * pps));
        const int x = juce::jlimit(0, juce::jmax(0, getWidth() - kHandleSize),
                                    getWidth() - fadePx);
        return { x, kNameH, kHandleSize, kHandleSize };
    }

    // The full fade band (header bottom → green divider line). Hit-testing in
    // this band is reserved exclusively for fades.
    juce::Rectangle<int> ClipRenderCore::fadeBandRect() const
    {
        return { 0, kNameH, getWidth(), kHandleSize };
    }

    juce::Rectangle<int> ClipRenderCore::leftEdgeRect() const
    {
        // Edge trim/stretch strip lives BELOW the green fade divider line so
        // the band directly under the header is reserved exclusively for the
        // fade in/out handles. This stops the edge zone from stealing grabs
        // meant for the fade triangles.
        const int top = kNameH + kHandleSize;
        return { 0, top, kEdgeWidth,
                 juce::jmax(0, getHeight() - top) };
    }

    juce::Rectangle<int> ClipRenderCore::rightEdgeRect() const
    {
        const int top = kNameH + kHandleSize;
        return { getWidth() - kEdgeWidth, top, kEdgeWidth,
                 juce::jmax(0, getHeight() - top) };
    }

    juce::Rectangle<int> ClipRenderCore::volumeButtonRect() const
    {
        // Header strip, left side.
        const int pad = 2;
        return { pad, pad, kVolBtnSize, kNameH - pad * 2 };
    }

    juce::Rectangle<int> ClipRenderCore::muteButtonRect() const
    {
        // Right after the volume button.
        const int pad = 2;
        const int x = pad + kVolBtnSize + pad;
        return { x, pad, kMuteBtnSize, kNameH - pad * 2 };
    }

    ClipRenderCore::Zone ClipRenderCore::hitTestZone(juce::Point<int> pos) const
    {
        if (!m_model) return Zone::None;

        if (volumeButtonRect().contains(pos))  return Zone::VolumeBtn;
        if (muteButtonRect().contains(pos))    return Zone::MuteBtn;

        // The fade band (header bottom → green divider line) is reserved
        // exclusively for fade in/out. The left half grows fade-in, the right
        // half grows fade-out. Edges never steal grabs from this band.
        if (fadeBandRect().contains(pos))
        {
            return (pos.x < getWidth() / 2) ? Zone::FadeIn : Zone::FadeOut;
        }

        // Below the green line: edges do trim / stretch.
        if (leftEdgeRect().contains(pos))      return Zone::ResizeLeft;
        if (rightEdgeRect().contains(pos))     return Zone::ResizeRight;
        return Zone::None;
    }

    void ClipRenderCore::updateCursorForZone(Zone z)
    {
        switch (z)
        {
            case Zone::FadeIn:
            case Zone::FadeOut:     setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); break;
            case Zone::ResizeLeft:
            case Zone::ResizeRight: setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); break;
            case Zone::VolumeBtn:
            case Zone::MuteBtn:     setMouseCursor(juce::MouseCursor::PointingHandCursor);    break;
            default:                setMouseCursor(juce::MouseCursor::NormalCursor);          break;
        }
    }

    void ClipRenderCore::mouseEnter(const juce::MouseEvent& e)
    {
        m_hovered = true;
        m_hoveredZone = hitTestZone(e.getPosition());
        updateCursorForZone(m_hoveredZone);
        repaint();
    }

    void ClipRenderCore::mouseExit(const juce::MouseEvent&)
    {
        m_hovered = false;
        m_hoveredZone = Zone::None;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }

    void ClipRenderCore::mouseMove(const juce::MouseEvent& e)
    {
        const Zone z = hitTestZone(e.getPosition());
        if (z != m_hoveredZone)
        {
            m_hoveredZone = z;
            updateCursorForZone(z);
            repaint();
        }
    }

    void ClipRenderCore::mouseDown(const juce::MouseEvent& e)
    {
        if (!m_model)
        {
            if (auto* parent = getParentComponent())
                parent->mouseDown(e.getEventRelativeTo(parent));
            return;
        }

        const Zone z = hitTestZone(e.getPosition());

        // Volume / Mute buttons → fire callback and consume
        if (z == Zone::VolumeBtn)
        {
            if (onVolumeButtonClicked) onVolumeButtonClicked();
            m_activeZone = Zone::VolumeBtn; // marks the gesture as consumed
            return;
        }
        if (z == Zone::MuteBtn)
        {
            if (onMuteButtonClicked) onMuteButtonClicked();
            m_activeZone = Zone::MuteBtn;
            return;
        }

        // Fade / resize handles begin an interactive edit
        if (z != Zone::None)
        {
            m_activeZone        = z;
            m_dragStartX        = e.getScreenPosition().x;
            m_dragStartLength   = m_model->length;
            m_dragStartFadeIn   = m_model->fadeInLength;
            m_dragStartFadeOut  = m_model->fadeOutLength;
            m_dragStartTime     = m_model->startTime;
            m_dragStartSrcOff   = m_model->sourceOffset;
            m_dragStartSrcStart = m_model->sourceStartSample;
            m_dragStartSrcEnd   = m_model->sourceEndSample;
            m_dragStartStretch  = m_model->timePitch.stretchRatio;
            m_dragStartCurveIn  = m_model->fadeInCurve;
            m_dragStartCurveOut = m_model->fadeOutCurve;
            // Stretch mode is engaged either by the dedicated Stretch tool
            // or by holding Shift during an edge drag (power-user fallback).
            bool toolIsStretch = false;
            if (auto* arrView = findParentComponentOfClass<ArrangementViewCore>())
                toolIsStretch = (arrView->getToolState().getActiveTool() == EditorTool::Stretch);
            m_dragStretchMode   = toolIsStretch || e.mods.isShiftDown();

            if (onEditBegin) onEditBegin();
            return;
        }

        // Otherwise forward to parent for normal click/select/drag-move
        if (auto* parent = getParentComponent())
            parent->mouseDown(e.getEventRelativeTo(parent));
    }

    void ClipRenderCore::mouseDrag(const juce::MouseEvent& e)
    {
        if (m_activeZone == Zone::None || !m_model)
        {
            if (auto* parent = getParentComponent())
                parent->mouseDrag(e.getEventRelativeTo(parent));
            return;
        }

        const double pps     = juce::jmax(1.0, m_zoom.getPixelsPerSecond());
        // Use SCREEN x for the drag delta. Left-edge trim calls setBounds() on
        // this component every frame (the clip's left edge / local origin moves),
        // so a component-local getPosition().x would be measured from a shifting
        // origin and oscillate — producing visible jitter. Screen coordinates are
        // immune to the component being repositioned mid-drag.
        const double dxPx    = (double)e.getScreenPosition().x - m_dragStartX;
        const double dxSecs  = dxPx / pps;

        m_showTrimTooltip = false;
        m_trimTooltipText = {};

        switch (m_activeZone)
        {
            case Zone::FadeIn:
            {
                // Drag right = grow fade-in. Clamp to clip length.
                double newFadeIn = juce::jlimit(0.0,
                                                m_dragStartLength,
                                                m_dragStartFadeIn + dxSecs);
                m_model->fadeInLength = (float)newFadeIn;
                m_model->bumpWaveformVisualVersion();
                break;
            }
            case Zone::FadeOut:
            {
                // Drag left = grow fade-out (negative dx).
                double newFadeOut = juce::jlimit(0.0,
                                                 m_dragStartLength,
                                                 m_dragStartFadeOut - dxSecs);
                m_model->fadeOutLength = (float)newFadeOut;
                m_model->bumpWaveformVisualVersion();
                break;
            }
            case Zone::ResizeRight:
            {
                // DEFAULT = TRIM. Only the Stretch tool (or Shift override)
                // engages time-stretch on edge drag. Plain trim must never
                // extend the visible region past the actual source audio —
                // we hard-cap newLen at the remaining source material so a
                // user that drags outward past the source end just stops
                // (instead of silently stretching).
                double requestedLen = m_dragStartLength + dxSecs;

                if (m_dragStretchMode)
                {
                    // BUTTER-SMOOTH 1:1 STRETCH. The clip's visual width is
                    // length * stretchRatio. Keep `length` (the amount of source
                    // content) FIXED during a stretch and move ONLY stretchRatio
                    // so the right edge follows the cursor exactly like a trim.
                    // (Previously length AND stretchRatio both moved, making the
                    // visual width grow with newLen² — quadratic, hyper-sensitive.)
                    const double startVisual = m_dragStartLength * juce::jmax(0.01, (double)m_dragStartStretch);
                    const double newVisual   = juce::jmax(0.05, startVisual + dxSecs);
                    if (m_dragStartLength > 0.001)
                        m_model->timePitch.stretchRatio = (float)juce::jlimit(0.25,
                            4.0, newVisual / m_dragStartLength);
                }
                else
                {
                    // Cap visible length at the remaining source material
                    // (sourceTotalSamples - sourceStartSample), converted to seconds.
                    const double srcRate = (m_model->sourceSampleRate > 0.0)
                        ? m_model->sourceSampleRate : 44100.0;
                    double maxLen = std::numeric_limits<double>::infinity();
                    if (m_model->sourceTotalSamples > 0)
                    {
                        const int64_t available = juce::jmax((int64_t)0,
                            m_model->sourceTotalSamples - m_dragStartSrcStart);
                        maxLen = (double)available / srcRate;
                    }
                    const double newLen = juce::jlimit(0.01, maxLen, requestedLen);
                    m_model->length = newLen;

                    const int64_t newEnd = m_dragStartSrcStart
                        + (int64_t)std::llround(newLen * srcRate);
                    m_model->sourceEndSample = juce::jmax(m_dragStartSrcStart + 1, newEnd);
                }
                break;
            }
            case Zone::ResizeLeft:
            {
                // DEFAULT = TRIM left (no slip — audio stays in place).
                // Stretch tool / Shift = stretch from the left edge.
                const double requestedLen = m_dragStartLength - dxSecs;

                if (m_dragStretchMode)
                {
                    // BUTTER-SMOOTH 1:1 left-edge stretch. Keep the RIGHT edge
                    // anchored and `length` fixed; move only stretchRatio and
                    // startTime so the left edge tracks the cursor 1:1. dxSecs
                    // is negative when dragging left (clip grows wider).
                    const double startStretch = juce::jmax(0.01, (double)m_dragStartStretch);
                    const double startVisual  = m_dragStartLength * startStretch;
                    const double rightAnchor  = m_dragStartTime + startVisual; // fixed
                    const double newVisual     = juce::jmax(0.05, startVisual - dxSecs);
                    if (m_dragStartLength > 0.001)
                        m_model->timePitch.stretchRatio = (float)juce::jlimit(0.25,
                            4.0, newVisual / m_dragStartLength);
                    const double appliedVisual = m_dragStartLength
                        * (double)m_model->timePitch.stretchRatio;
                    m_model->startTime = rightAnchor - appliedVisual;
                }
                else
                {
                    // Trim from the head: we can only consume what's available
                    // before the original sourceStartSample (== drag outward to
                    // reveal more head material) and we cannot trim past the
                    // existing sourceEndSample (== drag inward to shrink).
                    const double srcRate = (m_model->sourceSampleRate > 0.0)
                        ? m_model->sourceSampleRate : 44100.0;
                    const double maxGrow = (double)m_dragStartSrcStart / srcRate; // reveal headroom
                    const double maxShrink = m_dragStartLength - 0.01;            // keep min length
                    // delta > 0 means we are growing the clip to the left (revealing head).
                    // delta < 0 means we are shrinking the clip from the left.
                    const double appliedDelta = juce::jlimit(-maxGrow, maxShrink,
                                                              m_dragStartLength - requestedLen);
                    const double newLen   = m_dragStartLength - appliedDelta;

                    m_model->startTime    = m_dragStartTime + appliedDelta;
                    m_model->length       = newLen;
                    m_model->sourceOffset = juce::jmax(0.0, m_dragStartSrcOff + appliedDelta);

                    const int64_t deltaSamples = (int64_t)std::llround(appliedDelta * srcRate);
                    m_model->sourceStartSample = juce::jmax((int64_t)0,
                        m_dragStartSrcStart + deltaSamples);
                    // Anchor the tail at the original end. If the drag-start end was
                    // unset (0), fall back to the full source length so the right
                    // edge stays put while the head trims.
                    int64_t anchorEnd = m_dragStartSrcEnd;
                    if (anchorEnd <= m_model->sourceStartSample)
                        anchorEnd = (m_model->sourceTotalSamples > 0)
                            ? m_model->sourceTotalSamples
                            : m_model->sourceStartSample + (int64_t)std::llround(newLen * srcRate);
                    m_model->sourceEndSample = juce::jmax(m_model->sourceStartSample + 1, anchorEnd);
                }
                break;
            }
            default: break;
        }

        // Populate the trim-feedback tooltip for edge/fade gestures.
        auto fmtSeconds = [](double s) -> juce::String
        {
            const bool neg = s < 0;
            s = std::abs(s);
            const int mins = (int)(s / 60.0);
            const double rem = s - mins * 60.0;
            char buf[48];
            if (mins > 0) std::snprintf(buf, sizeof(buf), "%s%d:%06.3f", neg ? "-" : "", mins, rem);
            else          std::snprintf(buf, sizeof(buf), "%s%.3fs",     neg ? "-" : "", rem);
            return juce::String(buf);
        };

        switch (m_activeZone)
        {
            case Zone::ResizeRight:
            case Zone::ResizeLeft:
            {
                const double delta = m_model->length - m_dragStartLength;
                const int64_t deltaSamp = (int64_t)std::llround(delta * 44100.0);
                m_trimTooltipText = juce::String("len ") + fmtSeconds(m_model->length)
                    + " (" + (delta >= 0 ? "+" : "") + fmtSeconds(delta)
                    + " / " + juce::String((juce::int64)deltaSamp) + " smp)";
                m_showTrimTooltip = true;
                break;
            }
            case Zone::FadeIn:
            {
                m_trimTooltipText = juce::String("fade in ") + fmtSeconds(m_model->fadeInLength);
                m_showTrimTooltip = true;
                break;
            }
            case Zone::FadeOut:
            {
                m_trimTooltipText = juce::String("fade out ") + fmtSeconds(m_model->fadeOutLength);
                m_showTrimTooltip = true;
                break;
            }
            default: break;
        }

        if (onEditLive) onEditLive();
        repaint();
    }

    void ClipRenderCore::mouseUp(const juce::MouseEvent& e)
    {
        if (m_activeZone == Zone::VolumeBtn || m_activeZone == Zone::MuteBtn)
        {
            // Button gesture already consumed on mouseDown — do not forward
            // to the parent (which would deselect / reselect the wrong clip).
            m_activeZone = Zone::None;
            return;
        }
        if (m_activeZone != Zone::None)
        {
            m_activeZone = Zone::None;
            m_showTrimTooltip = false;
            m_trimTooltipText = {};
            repaint();
            if (onEditCommit) onEditCommit();
            return;
        }

        if (auto* parent = getParentComponent())
            parent->mouseUp(e.getEventRelativeTo(parent));
    }

    void ClipRenderCore::mouseDoubleClick(const juce::MouseEvent& e)
    {
        DBG("[ClipRenderCore] double-click fired, forwarding to parent");

        if (auto* parent = getParentComponent())
            parent->mouseDoubleClick(e.getEventRelativeTo(parent));
    }

    // -----------------------------------------------------------------------
    // Layer rendering methods
    // -----------------------------------------------------------------------

    void ClipRenderCore::drawBackground(juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();

        // Logic Pro style: solid header strip, darker body
        const juce::Colour headerCol = m_model->colour;
        const juce::Colour bodyCol   = m_model->colour.darker(0.55f).withAlpha(0.92f);

        // Body
        g.setColour(bodyCol);
        g.fillRoundedRectangle(b, 4.f);

        // Header strip (solid colour, same as clip colour)
        juce::Rectangle<float> header(b.getX(), b.getY(), b.getWidth(), (float)kNameH);
        g.setColour(headerCol);
        g.fillRect(header);
        // Round top corners only
        g.fillRoundedRectangle(header, 4.f);
        // Square bottom of header
        g.fillRect(header.withTrimmedTop(4.f));

        // Border
        g.setColour(fromU32(Col::border));
        g.drawRoundedRectangle(b.reduced(0.5f), 4.f, 1.f);
    }

    void ClipRenderCore::drawWaveform(juce::Graphics& g)
    {
        if (!m_waveformCache->isReady())
            return;

        // Thread-safe peak access: lock to prevent race with background waveform generation
        juce::CriticalSection::ScopedLockType lock(m_waveformCache->getPeaksLock());

        auto& peaks = m_waveformCache->getPeaks();
        if (peaks.minPeaks.empty() || peaks.maxPeaks.empty())
            return;

        // Waveform area starts BELOW the name bar so they never overlap.
        const auto  full      = getLocalBounds();
        const float areaTop   = (float)(full.getY() + kNameH);
        const float areaBot   = (float)full.getBottom();
        const float areaH     = areaBot - areaTop;
        if (areaH < 4.f) return;

        const float centreY   = areaTop + areaH * 0.5f;
        const float halfHeight = areaH * 0.42f;
        const float clipW     = (float)full.getWidth();

        // ── Stretch-aware peak mapping ───────────────────────────────────
        //
        // The peaks array represents the source audio at 1:1.
        // We map each output pixel → source peak index accounting for:
        //
        //   Mode 0 Resample +12st: clip plays 2x speed → source window is
        //     compressed into half the visual width → peaks are stretched
        //     across full width (we show more source material per pixel).
        //
        //   Mode 1 Stretch 200%: clip visual is 2x wide, source material
        //     is spread across double → peaks are compressed (each pixel
        //     shows less source).
        //
        //   Mode 2 PitchOnly: duration unchanged → 1:1 mapping.
        //
        // Formula:
        //   Unified pitch: pitch does not change arrangement duration.
        //   Stretch remains the only timeline/waveform density control.
        //   peakIndex = outputPixel * readRatio / clipW * numPeaks
        //
        // This makes the waveform look correct relative to what you hear.

        const auto& tp   = m_model->timePitch;
        const int   mode = static_cast<int>(tp.mode);

        double readRatio = 1.0;
        if (mode == 2) // PitchOnly: duration unchanged
        {
            readRatio = 1.0;
        }
        else // Stretch / Vocal / unified pitch / etc.
        {
            // stretch > 1 = slower = expanded peaks spread over more pixels
            // readRatio < 1 means we read fewer source samples per pixel
            readRatio = (tp.stretchRatio > 0.001) ? 1.0 / tp.stretchRatio : 1.0;
        }

        const int totalPeaks = (int)peaks.minPeaks.size();
        if (totalPeaks <= 0)
            return;

        int visibleStartPeak = 0;
        int visiblePeakCount = totalPeaks;
        if (peaks.samplesPerPeak > 0)
        {
            int64_t visibleStartSample = m_model->sourceStartSample;
            int64_t visibleEndSample = m_model->sourceEndSample;

            if (visibleEndSample <= visibleStartSample)
            {
                const double sourceRate = peaks.sourceSampleRate > 0.0 ? peaks.sourceSampleRate : 44100.0;
                visibleStartSample = (int64_t)std::llround(juce::jmax(0.0, m_model->sourceOffset) * sourceRate);
                visibleEndSample = visibleStartSample + (int64_t)std::llround(juce::jmax(0.0, m_model->length) * sourceRate);
            }

            if (peaks.totalSamples > 0)
            {
                visibleStartSample = juce::jlimit((int64_t)0, peaks.totalSamples - 1, visibleStartSample);
                visibleEndSample = juce::jlimit(visibleStartSample + 1, peaks.totalSamples, visibleEndSample);
            }

            if (visibleEndSample > visibleStartSample)
            {
                visibleStartPeak = juce::jlimit(0, totalPeaks - 1,
                    (int)(visibleStartSample / (int64_t)peaks.samplesPerPeak));
                const int visibleEndPeak = juce::jlimit(visibleStartPeak + 1, totalPeaks,
                    (int)std::ceil((double)visibleEndSample / (double)peaks.samplesPerPeak));
                visiblePeakCount = juce::jmax(1, visibleEndPeak - visibleStartPeak);
            }
        }

        // ── Visual gain/fade multipliers (REALTIME, NO CACHE REGEN) ──────
        // This is the KEY FIX:
        //   displayPeak = cachedPeak * clipGainLinear * fadeMultiplier
        //
        // The cached peaks are from the source audio at 1:1 gain.
        // We apply the clip gain + fade envelope visually at draw time.
        // This makes gain/fade knobs update instantly without async peak regen.

        const float clipGainLinear = m_model->gain; // already in linear 0..4

        // For fade visual: we need clip length in samples (44.1k assumed for visual approx)
        const double visualSampleRate = 44100.0; // approximation for visual
        const int64_t clipLengthSamples = (int64_t)std::llround(m_model->length * visualSampleRate);
        const int64_t fadeInSamp  = m_model->fadeInSamples(visualSampleRate);
        const int64_t fadeOutSamp = m_model->fadeOutSamples(visualSampleRate);

        // Helper: compute fade gain for a given pixel position
        auto getFadeGainAtPixel = [&](int px) -> float
        {
            if (clipLengthSamples < 1) return 1.f;

            // Map pixel → sample position in clip
            const int64_t samplePos = (int64_t)std::llround(
                ((double)px / clipW) * clipLengthSamples);

            float fadeGain = 1.f;

            // Fade in
            if (fadeInSamp > 0 && samplePos < fadeInSamp)
                fadeGain *= (float)samplePos / (float)fadeInSamp;

            // Fade out
            if (fadeOutSamp > 0 && samplePos > (clipLengthSamples - fadeOutSamp))
            {
                const int64_t fromEnd = clipLengthSamples - samplePos;
                fadeGain *= (float)fromEnd / (float)fadeOutSamp;
            }

            return juce::jlimit(0.f, 1.f, fadeGain);
        };

        g.setColour(fromU32(Col::waveform).withAlpha(0.75f));

        // ── Pro waveform rendering ───────────────────────────────────────
        // Each pixel column covers a fractional RANGE of peak columns.
        //  • Zoomed out (>=1 peak per pixel): aggregate min/max across ALL
        //    covered peaks so no transient is ever skipped (no aliasing).
        //  • Zoomed in (<1 peak per pixel): linearly interpolate between
        //    adjacent peaks so the wave stays smooth instead of stepping.
        //  • Adjacent columns are connected (each column overlaps the
        //    previous one's range) so the waveform reads as a continuous
        //    shape at any zoom — the same approach used by pro editors.

        const double peaksPerPixel = readRatio * (double)visiblePeakCount / (double)clipW;
        const int    lastPeak      = visibleStartPeak + visiblePeakCount - 1;

        float prevTop = 0.f, prevBot = 0.f;
        bool  havePrev = false;

        for (int px = 0; px < (int)clipW; ++px)
        {
            // Map output pixel → source peak range
            const int visualPx = m_model->reversed ? ((int)clipW - 1 - px) : px;
            const double fStart = (double)visualPx * peaksPerPixel;

            float rawMin, rawMax;

            if (peaksPerPixel >= 1.0)
            {
                // Aggregate every peak column under this pixel
                int i0 = visibleStartPeak + (int)fStart;
                int i1 = visibleStartPeak + (int)std::ceil(fStart + peaksPerPixel) - 1;
                i0 = juce::jlimit(visibleStartPeak, lastPeak, i0);
                i1 = juce::jlimit(i0, lastPeak, i1);

                rawMin = peaks.minPeaks[(size_t)i0];
                rawMax = peaks.maxPeaks[(size_t)i0];
                for (int i = i0 + 1; i <= i1; ++i)
                {
                    rawMin = juce::jmin(rawMin, peaks.minPeaks[(size_t)i]);
                    rawMax = juce::jmax(rawMax, peaks.maxPeaks[(size_t)i]);
                }
            }
            else
            {
                // Sub-peak zoom: smooth interpolation between neighbours
                const double centre = fStart + peaksPerPixel * 0.5 - 0.5;
                const double fl     = std::floor(centre);
                const float  t      = (float)(centre - fl);

                const int ia = juce::jlimit(visibleStartPeak, lastPeak,
                                            visibleStartPeak + (int)fl);
                const int ib = juce::jlimit(visibleStartPeak, lastPeak, ia + 1);

                rawMin = peaks.minPeaks[(size_t)ia]
                       + (peaks.minPeaks[(size_t)ib] - peaks.minPeaks[(size_t)ia]) * t;
                rawMax = peaks.maxPeaks[(size_t)ia]
                       + (peaks.maxPeaks[(size_t)ib] - peaks.maxPeaks[(size_t)ia]) * t;
            }

            // ── APPLY VISUAL TRANSFORMS ──────────────────────────────────
            const float fadeGain  = getFadeGainAtPixel(px);
            const float totalGain = clipGainLinear * fadeGain;

            const float displayMin = rawMin * totalGain;
            const float displayMax = rawMax * totalGain;

            float top = centreY - displayMax * halfHeight;
            float bot = centreY - displayMin * halfHeight;

            // Connect with the previous column so the shape stays continuous
            if (havePrev)
            {
                top = juce::jmin(top, prevBot);
                bot = juce::jmax(bot, prevTop);
            }
            prevTop  = centreY - displayMax * halfHeight;
            prevBot  = centreY - displayMin * halfHeight;
            havePrev = true;

            // Guarantee at least a hairline so silence still draws a centreline
            if (bot - top < 1.0f)
            {
                const float mid = (top + bot) * 0.5f;
                top = mid - 0.5f;
                bot = mid + 0.5f;
            }

            const float x = (float)(full.getX() + px);
            g.fillRect(x, top, 1.0f, bot - top);
        }
    }

    void ClipRenderCore::drawFades(juce::Graphics& g)
    {
        // Keep fade visuals strictly below the header strip so the dim wedge
        // and ramp line never paint over the V/M buttons or clip name.
        auto b = getLocalBounds().toFloat().withTop((float)kNameH);
        if (b.getHeight() <= 1.f) return;

        juce::Colour fadeCol = m_model->colour.brighter(0.2f);

        ClipFadeRenderCore::drawFadeIn(g, b, *m_model, m_zoom.getPixelsPerSecond(), fadeCol);
        ClipFadeRenderCore::drawFadeOut(g, b, *m_model, m_zoom.getPixelsPerSecond(), fadeCol);
    }

    void ClipRenderCore::drawGainLine(juce::Graphics& g)
    {
        if (std::fabsf(m_model->gain - 1.f) < 0.01f)
            return; // default gain, no line

        auto b = getLocalBounds().toFloat();
        float normGain = (m_model->gain - ClipGainCore::kMinLinear) /
                         (ClipGainCore::kMaxLinear - ClipGainCore::kMinLinear);
        float y = b.getBottom() - normGain * b.getHeight();

        g.setColour(fromU32(Col::accent).withAlpha(0.7f));
        g.drawHorizontalLine((int)y, b.getX(), b.getRight());

        // Center grab dot
        g.fillEllipse(b.getCentreX() - 3.f, y - 3.f, 6.f, 6.f);
    }

    void ClipRenderCore::drawMuteOverlay(juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(fromU32(Col::muteOverlay));
        g.fillRoundedRectangle(b, 3.f);
    }

    void ClipRenderCore::drawLockIcon(juce::Graphics& g)
    {
        g.setColour(fromU32(Col::textDim));
        g.setFont(juce::Font("Segoe UI", 10.f, juce::Font::plain));
        g.drawText(juce::CharPointer_UTF8("\xf0\x9f\x94\x92"), 4, 4, 16, 16, juce::Justification::centred);
    }

    void ClipRenderCore::drawClipName(juce::Graphics& g)
    {
        // ── Name bar: sits at the very top of the clip, kNameH pixels tall.
        // A semi-opaque background strip guarantees text never overlaps waveform.
        // The waveform drawWaveform() starts its bounds at kNameH, so they can
        // never intersect.
        const auto b   = getLocalBounds();
        const int  bx  = b.getX();
        const int  bw  = b.getWidth();
        const int  textW = bw - 10;

        if (textW <= 0)
            return;

        // Header is already drawn in drawBackground() with the clip colour.
        // Just draw the name text on top of the header.
        auto name = m_model->clipName.empty() ? "(unnamed)" : m_model->clipName;
        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.setFont(juce::Font("Segoe UI", 9.5f, juce::Font::bold));
        g.drawText(juce::String(name), bx + 5, 1, textW, kNameH - 2,
                   juce::Justification::centredLeft, true /*ellipsis*/);
    }

    void ClipRenderCore::drawPitchRateBadges(juce::Graphics& g)
    {
        if (getWidth() < 32)
            return;

        // Badges live inside the name bar (top kNameH pixels), right-aligned.
        // Order right-to-left: stretch badge, then pitch badge.
        // This keeps them completely above the waveform area.

        const auto& tp   = m_model->timePitch;
        const int   mode = static_cast<int>(tp.mode);

        // Collect badge strings
        char pitchBuf[20]  = "";
        char stretchBuf[20]= "";
        char modeBuf[20]   = "";

        // Pitch badge: show if any pitch shift
        const double totalSt = tp.pitchSemitones + tp.fineTuneCents / 100.0;
        if (std::abs(totalSt) > 0.05)
            snprintf(pitchBuf, sizeof(pitchBuf), "%+.0fst", tp.pitchSemitones);

        // Stretch badge
        if (mode == 0) // Resample
        {
            // Show rate as multiplier if pitch is set (duration changes)
            if (std::abs(totalSt) > 0.05)
            {
                const double rate = std::pow(2.0, totalSt / 12.0);
                snprintf(stretchBuf, sizeof(stretchBuf), "%.2fx", rate);
            }
        }
        else
        {
            if (std::abs(tp.stretchRatio - 1.0) > 0.01)
                snprintf(stretchBuf, sizeof(stretchBuf), "%.0f%%",
                         tp.stretchRatio * 100.0);
        }

        // Mode badge (only show if not default Resample)
        if (mode > 0)
        {
            static const char* const kModeLabels[] =
                { "", "STR", "PCH", "VOC", "PRC", "TEX", "HQ" };
            if (mode < 7)
                snprintf(modeBuf, sizeof(modeBuf), "%s", kModeLabels[mode]);
        }

        g.setFont(juce::Font("Segoe UI", 7.5f, juce::Font::bold));
        const int bx = getLocalBounds().getX();
        int x = bx + getWidth() - 4;
        const int badgeY = 1;
        const int badgeH = kNameH - 2;

        auto drawBadge = [&](const char* txt, juce::Colour col)
        {
            if (txt[0] == '\0') return;
            const juce::String s(txt);
            const int tw = g.getCurrentFont().getStringWidth(s) + 6;
            x -= tw;
            // pill background
            g.setColour(col.withAlpha(0.82f));
            g.fillRoundedRectangle((float)x, (float)badgeY,
                                   (float)tw, (float)badgeH, 2.f);
            // label
            g.setColour(juce::Colours::white);
            g.drawText(s, x, badgeY, tw, badgeH,
                       juce::Justification::centred);
            x -= 3;
        };

        // Right-to-left: mode, stretch, pitch
        drawBadge(modeBuf,    fromU32(0xFF5588BB));
        drawBadge(stretchBuf, fromU32(Col::accent));
        drawBadge(pitchBuf,   fromU32(Col::accent).brighter(0.3f));
    }

    void ClipRenderCore::drawTimePitchStatus(juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();
        const int  bx = getLocalBounds().getX();

        // ── Bottom-left badge row ─────────────────────────────────────────
        int badgeX = bx + 3;
        const int badgeY = (int)(b.getBottom() - 13.f);
        const int badgeH = 11;

        g.setFont(juce::Font("Segoe UI", 7.5f, juce::Font::bold));

        auto drawBottomBadge = [&](const char* label, juce::Colour col)
        {
            const juce::String s(label);
            const int tw = g.getCurrentFont().getStringWidth(s) + 6;
            g.setColour(col.withAlpha(0.88f));
            g.fillRoundedRectangle((float)badgeX, (float)badgeY,
                                   (float)tw, (float)badgeH, 2.f);
            g.setColour(juce::Colours::white);
            g.drawText(s, badgeX, badgeY, tw, badgeH,
                       juce::Justification::centred);
            badgeX += tw + 3;
        };

        // Frozen badge (highest priority, leftmost)
        if (m_frozen)
            drawBottomBadge("\xf0\x9f\xa7\x8a FROZEN", juce::Colour(0xFF2255AA));

        // HQ Rendered
        if (m_hqRendered && !m_frozen)
            drawBottomBadge("HQ\xe2\x9c\x93", juce::Colour(0xFF228844));

        // CPU Draft forced
        if (m_cpuDraft)
            drawBottomBadge("DRAFT", juce::Colour(0xFF886622));

        // ── Rendering... banner (OfflineHQ in progress) ────────────────────
        const int mode = static_cast<int>(m_model->timePitch.mode);
        if (m_renderingStretch && mode == 6)
        {
            const float bannerH = 13.f;
            const juce::Rectangle<float> banner {
                b.getX(), b.getBottom() - bannerH - (m_frozen ? 14.f : 0.f),
                b.getWidth(), bannerH };

            g.setColour(juce::Colour(0xCC1A3A5A));
            g.fillRect(banner);
            g.setColour(juce::Colour(0xFF7AC0FF));
            g.setFont(juce::Font("Segoe UI", 8.f, juce::Font::plain));
            g.drawText("Rendering HQ stretch\xe2\x80\xa6", banner.toNearestInt(),
                       juce::Justification::centred);
        }
        else if (m_usingFallback && mode == 6)
        {
            // Realtime fallback dot (orange, bottom-left, after any badges)
            g.setColour(juce::Colour(0xFFE07B39).withAlpha(0.85f));
            g.fillEllipse((float)(badgeX), b.getBottom() - 7.f, 5.f, 5.f);
        }
    }

    void ClipRenderCore::drawSelectionHighlight(juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(fromU32(Col::accent));
        g.drawRoundedRectangle(b.reduced(1.f), 3.f, 2.f);
    }

    void ClipRenderCore::drawHoverGlow(juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(fromU32(Col::accent).withAlpha(0.15f));
        g.drawRoundedRectangle(b.reduced(0.5f), 3.f, 1.5f);
    }

    void ClipRenderCore::drawClipHandles(juce::Graphics& g)
    {
        if (getWidth() < 24) return; // too tiny — skip handle visuals

        auto highlight = [&](Zone z) -> float
        {
            if (m_activeZone == z)  return 0.95f;
            if (m_hoveredZone == z) return 0.70f;
            return 0.35f;
        };

        const juce::Colour handleCol = juce::Colours::white;

        // Fade-in wedge (top-left). Draw the wedge plus a dark outline for
        // contrast against any waveform colour. No anchor dot.
        {
            auto r = fadeInHandleRect().toFloat();
            const float a = highlight(Zone::FadeIn);
            juce::Path p;
            p.startNewSubPath(r.getX(),     r.getBottom());
            p.lineTo         (r.getRight(), r.getY());
            p.lineTo         (r.getX(),     r.getY());
            p.closeSubPath();
            g.setColour(juce::Colours::black.withAlpha(0.45f * a + 0.15f));
            g.strokePath(p, juce::PathStrokeType(1.5f));
            g.setColour(handleCol.withAlpha(juce::jmax(0.5f, a)));
            g.fillPath(p);
        }

        // Fade-out wedge (top-right) — mirror of the fade-in handle.
        {
            auto r = fadeOutHandleRect().toFloat();
            const float a = highlight(Zone::FadeOut);
            juce::Path p;
            p.startNewSubPath(r.getRight(), r.getBottom());
            p.lineTo         (r.getX(),     r.getY());
            p.lineTo         (r.getRight(), r.getY());
            p.closeSubPath();
            g.setColour(juce::Colours::black.withAlpha(0.45f * a + 0.15f));
            g.strokePath(p, juce::PathStrokeType(1.5f));
            g.setColour(handleCol.withAlpha(juce::jmax(0.5f, a)));
            g.fillPath(p);
        }

        // Edge resize hints — always show a faint vertical grip bar so the
        // user can find the trim edge, and brighten it on hover/active.
        const float leftAlpha  = highlight(Zone::ResizeLeft);
        const float rightAlpha = highlight(Zone::ResizeRight);
        {
            auto r = leftEdgeRect().toFloat();
            g.setColour(handleCol.withAlpha(leftAlpha * 0.5f + 0.12f));
            g.fillRect(r.withWidth(juce::jmin(3.0f, r.getWidth())));
        }
        {
            auto r = rightEdgeRect().toFloat();
            g.setColour(handleCol.withAlpha(rightAlpha * 0.5f + 0.12f));
            g.fillRect(r.removeFromRight(juce::jmin(3.0f, r.getWidth())));
        }

        // Curve handles removed — fades are straight ramps now.
    }

    void ClipRenderCore::drawTrimTooltip(juce::Graphics& g)
    {
        if (m_trimTooltipText.isEmpty()) return;

        g.setFont(juce::Font("Consolas", 11.f, juce::Font::plain));
        const int textW = g.getCurrentFont().getStringWidth(m_trimTooltipText) + 12;
        const int textH = 18;
        int x = juce::jlimit(2, juce::jmax(2, getWidth() - textW - 2),
                              (getWidth() - textW) / 2);
        int y = juce::jmax(kNameH + 2, getHeight() - textH - 4);
        juce::Rectangle<int> r(x, y, textW, textH);
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.fillRoundedRectangle(r.toFloat(), 3.f);
        g.setColour(juce::Colours::white);
        g.drawText(m_trimTooltipText, r, juce::Justification::centred, false);
    }

    void ClipRenderCore::drawVolumeButton(juce::Graphics& g)
    {
        if (getWidth() < 50) return; // hide on very small clips

        auto r = volumeButtonRect().toFloat();
        const bool hot = (m_hoveredZone == Zone::VolumeBtn);

        g.setColour(juce::Colours::black.withAlpha(hot ? 0.55f : 0.35f));
        g.fillRoundedRectangle(r, 2.f);

        g.setColour(juce::Colours::white.withAlpha(hot ? 0.95f : 0.78f));
        g.setFont(juce::Font("Segoe UI", (float)(kNameH - 6), juce::Font::bold));
        g.drawText("V", r.toNearestInt(), juce::Justification::centred, false);
    }

    void ClipRenderCore::drawMuteButton(juce::Graphics& g)
    {
        if (getWidth() < 66) return; // need a bit more room for the second pill

        auto r = muteButtonRect().toFloat();
        const bool hot   = (m_hoveredZone == Zone::MuteBtn);
        const bool muted = m_model && m_model->muted;

        // Background pill: red when muted, dark otherwise.
        if (muted)
            g.setColour(juce::Colour(0xFFE04848).withAlpha(hot ? 1.0f : 0.85f));
        else
            g.setColour(juce::Colours::black.withAlpha(hot ? 0.55f : 0.35f));
        g.fillRoundedRectangle(r, 2.f);

        g.setColour(juce::Colours::white.withAlpha(muted ? 1.0f : (hot ? 0.95f : 0.78f)));
        g.setFont(juce::Font("Segoe UI", (float)(kNameH - 6), juce::Font::bold));
        g.drawText("M", r.toNearestInt(), juce::Justification::centred, false);
    }

} // namespace ArrangementEditor
