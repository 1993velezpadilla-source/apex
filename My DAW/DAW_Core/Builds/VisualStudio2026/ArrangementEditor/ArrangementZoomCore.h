// ===========================================================================
// ArrangementZoomCore.h
// Drawing policies shared by the ruler and clip renderers.
// ===========================================================================
#pragma once
#include "TimelineZoomController.h"

namespace ArrangementEditor
{
    // ── Grid LOD Policy ──────────────────────────────────────────────────
    // Pure zero-dependency helper shared by ArrangementRulerCore and
    // ArrangementViewCore.  Both callers compute actual pxPerBeat and
    // pxPerBar from their real BPM / time-signature / zoom state and ask
    // this single source of truth what to draw.
    //
    // Thresholds:
    //   beat  lines → drawn only when pxPerBeat >= 6.0
    //   bar   lines → aim for major spacing of ~28 px
    //   barStep      → ceil(28 / pxPerBar), rounded to musically natural
    //                   powers-of-two: 1,2,4,8,16…
    // Alignment and snapping are never affected — only visual line density.
    struct GridLodPolicy
    {
        static constexpr double kMinPxPerBeat = 6.0;
        static constexpr double kMinMajorSpacing = 24.0;

        /** True when beat subdivision lines should be suppressed entirely. */
        static bool suppressBeats(double pixelsPerBeat) noexcept
        {
            // Invalid density must not enable unreadable beat subdivisions.
            return !(pixelsPerBeat >= kMinPxPerBeat);
        }

        /** Musically natural bar step for the given pixel density.
         *  Cap the step at 32 bars. Compare spacing thresholds directly:
         *  ceil(24 / pxPerBar) can overflow int at extreme zoom-out or
         *  convert NaN to int after malformed tempo/viewport input. */
        static int barStep(double pixelsPerBar) noexcept
        {
            if (!(pixelsPerBar > 0.0)) return 1; // also rejects NaN
            if (pixelsPerBar >= kMinMajorSpacing) return 1;
            if (pixelsPerBar >= kMinMajorSpacing / 2.0) return 2;
            if (pixelsPerBar >= kMinMajorSpacing / 4.0) return 4;
            if (pixelsPerBar >= kMinMajorSpacing / 8.0) return 8;
            if (pixelsPerBar >= kMinMajorSpacing / 16.0) return 16;
            return 32;
        }

        static int barLabelStep(double pixelsPerBar) noexcept
        {
            constexpr double labelWidth = 40.0;
            return barStep(pixelsPerBar * (kMinMajorSpacing / labelWidth));
        }
    };

    // ── Clip rendering LOD policy ────────────────────────────────────────
    // At the minimum horizontal and vertical zoom a clip is often only a few
    // pixels wide and a very shallow lane.  The clip block and selection
    // outline remain authoritative, but waveform/detail layers become either
    // unreadable or disproportionately expensive.  Keep these thresholds in
    // a pure policy so the renderer and deterministic tests share one contract.
    struct ArrangementClipLodPolicy
    {
        static constexpr int kViewportOverscanPx = 32;
        static constexpr int kWaveformMinWidthPx = 8;
        static constexpr int kWaveformMinHeightPx = 28;
        static constexpr int kNameMinWidthPx = 24;
        static constexpr int kDetailMinWidthPx = 36;
        static constexpr int kDetailMinHeightPx = 28;
        static constexpr int kFadeMinWidthPx = 18;
        static constexpr int kFadeMinHeightPx = 24;

        static bool drawWaveform(int width, int height) noexcept
        {
            return width >= kWaveformMinWidthPx && height >= kWaveformMinHeightPx;
        }

        static bool drawPatternGrid(int width, int height) noexcept
        {
            return drawWaveform(width, height);
        }

        static bool drawClipName(int width, int height) noexcept
        {
            return width >= kNameMinWidthPx && height >= kNameHForPolicy();
        }

        static bool drawSecondaryDetails(int width, int height) noexcept
        {
            return width >= kDetailMinWidthPx && height >= kDetailMinHeightPx;
        }

        static bool drawFades(int width, int height) noexcept
        {
            return width >= kFadeMinWidthPx && height >= kFadeMinHeightPx;
        }

        static bool drawGainLine(int width, int height) noexcept
        {
            return width >= kWaveformMinWidthPx && height >= kFadeMinHeightPx;
        }

        static juce::Rectangle<int> renderRegion(const juce::Rectangle<int>& viewport,
                                                  int trackHeightPx) noexcept
        {
            // Prepare clips ahead of a pan in either direction. A viewport
            // can travel much farther than 32 px between touch frames.
            return viewport.expanded(juce::jmax(kViewportOverscanPx, viewport.getWidth() / 2),
                                     juce::jmax(kViewportOverscanPx, trackHeightPx));
        }

        static juce::Rectangle<int> visibilityGuard(const juce::Rectangle<int>& viewport,
                                                     int contentWidth, int trackHeightPx) noexcept
        {
            const int verticalTravel = juce::jmax(kViewportOverscanPx / 2, trackHeightPx / 2);
            return viewport.expanded(0, verticalTravel)
                .withX(0).withWidth(juce::jmax(contentWidth, viewport.getRight()));
        }

        // The viewport can travel between the closest hidden clips without
        // rescanning the clip list. Use the entire vertical guard so a small
        // diagonal pan cannot expose a clip in a neighbouring lane.
        static void excludeHiddenClip(juce::Rectangle<int>& guard,
                                      const juce::Rectangle<int>& viewport,
                                      const juce::Rectangle<int>& clip) noexcept
        {
            if (!guard.intersects(clip))
                return;
            if (clip.getRight() <= viewport.getX())
                guard = guard.withLeft(clip.getRight());
            else if (clip.getX() >= viewport.getRight())
                guard = guard.withRight(clip.getX());
        }

        static bool intersectsViewport(const juce::Rectangle<int>& clipBounds,
                                       const juce::Rectangle<int>& viewport,
                                       int trackHeightPx) noexcept
        {
            return renderRegion(viewport, trackHeightPx).intersects(clipBounds);
        }

    private:
        static constexpr int kNameHForPolicy() noexcept { return 18; }
    };

} // namespace ArrangementEditor
