# Mobile navigation UI work — 2026-11-03

Status: IMPLEMENTED; portable checks FOCUSED_VERIFIED; native CI and physical
Pixel Fold frame timings pending. Baseline: PR #170, `ecd61a09`.

The user reports whole-program lag during scroll and pinch, with and without a
recorded clip. The previous waveform-cache APK did not resolve that report.
This change addresses verified unnecessary message-thread work; it does not
claim that any one of these paths explains every device stall.

## Paths corrected

| Owner / symbol | Baseline behavior | New behavior |
| --- | --- | --- |
| `ArrangementView::timerCallback` → `ArrangementRulerCore::setTempo` | Every 60 Hz tick requested a ruler repaint even at unchanged tempo, including idle/empty sessions. | No-op tempo updates issue no repaint. An actual tempo change still invalidates the ruler and body grid. |
| `ArrangementViewCore::paint` | Grid candidate loop extended from scroll position through the entire song component width; graphics clipping happened after enumeration. | Query `GridLodPolicy::visibleBars` using the intersection of the viewport and Graphics dirty rectangle; iterate by the LOD stride directly. |
| `ArrangementRulerCore::drawGrid` | Bars skipped by grid LOD still generated text labels; partial playhead repaints enumerated the whole ruler. | Bound enumeration to the dirty strip plus the preceding label, space labels by at least 48px at supported densities, preserve beats entering from the preceding bar. |
| `TimelineViewport::visibleAreaChanged` | Scroll synchronization depended on scrollbar notifications, with a fallback in `paint()` that moved controls and changed component visibility. | Synchronize the actual visible area before painting, including with hidden phone scrollbars. No layout mutation in the body paint callback. |
| `ArrangementViewCore::applyTouchPanInertia` | A diagonal pinch could pan, notify horizontal layout, notify vertical layout, and set the viewport repeatedly. | Capture the previous midpoint's content anchor; publish both zoom axes together; calculate one final pan/zoom viewport target. |
| `MainComponent` phone zoom callback | Vertical pinch fell back to scanning all engine clips for the project extent. | Both axes reuse the cached project extent, update presentation bounds and synchronize track-header geometry. Expanded inline playlists retain their existing layout path. |
| Timeline, wrapper, ruler component opacity | These components filled their complete backgrounds but remained non-opaque to JUCE. | Mark their solid backgrounds opaque to avoid painting obscured ancestors for timeline invalidations. |

All of this runs on the message thread. Track/clip/audio ownership, peak-cache
workers, audio callback processing and project state remain with their existing
owners. Waveforms remain visible during navigation.

## Focused checks

- C++17 optimized and UndefinedBehaviorSanitizer builds of
  `ArrangementZoomPortableTests.cpp`: passed locally. Covers a single notification
  with both final axes, clamped/no-op/invalid input, and grid candidate bounds
  across tempo/zoom/distant-origin/partial-repaint cases.
- C++17 optimized and UndefinedBehaviorSanitizer builds of
  `TouchNavigationMathPortableTests.cpp`: passed locally. Added combined midpoint
  pan/zoom anchors with the fixed 80px header and pan reversals.
- Android manifest transformation: 8 Python tests passed locally, including
  profileable preview scope, idempotence and no debuggable attribute.
- Added a native `grid.lod-policy.v1` regression using the actual ruler and a
  `CachedComponentImage` invalidation counter: 600 same-tempo calls must cause
  zero invalidations; changing tempo must still invalidate. Runs in native CI.
- `git diff --check`: passed locally.

The portable assertions count actual grid candidates, not milliseconds or FPS.
There is no physical Android device connected in this environment.

## Device diagnostics

The separate `com.xziel.apex.mobileportrait` preview manifest is profileable by
local Android shell tools without enabling a debuggable build. The fulltest
package's profiling configuration is not changed.

Native Android System Trace / Perfetto sections identify:

- `APEX.timeline.gesture`
- `APEX.timeline.zoomLayout`
- `APEX.timeline.clipGeometry`
- `APEX.timeline.scroll`
- `APEX.timeline.paint`
- `APEX.timeline.ruler`
- `APEX.timeline.overlays`

Scopes are message-thread-only, balanced by RAII, and emit only while tracing
is enabled. They do not write logs or files during normal interaction. Keep the
optimized Release configuration when measuring. Capture an empty session, the
same gestures with a recorded clip, and stopped versus playing transport.
Compare UI-thread durations, scheduler stalls and Android frame presentation;
the arrangement timer is not a presented-FPS measurement.

Android references:
[native trace API](https://developer.android.com/ndk/reference/group/tracing),
[profileable manifest](https://developer.android.com/guide/topics/manifest/profileable-element).

The PR stays draft until native builds and physical Pixel Fold navigation QA
pass. A successful build alone does not establish that the reported lag is fixed.
