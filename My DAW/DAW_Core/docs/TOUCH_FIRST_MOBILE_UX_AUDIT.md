# APEX touch-first DAW audit — Logic Pro for iPad interaction reference

Date: 2026-10-21. Target: iPadOS, iOS, Android (including foldables), Windows tablets.
Reference is *interaction behaviour*, not Apple's proprietary UI, iconography, assets, or code.
Preserve the APEX visual identity, transport, automation, routing and project authority.

## Primary-source observations

- Logic Pro for iPad supports pinch to zoom the tracks area and editors on two axes,
  vertical resizing of editors and play surfaces, and direct one-finger scroll.
  Source: https://support.apple.com/guide/logicpro-ipad/main-gestures-lpip05674184/ipados
  and https://support.apple.com/en-mide/guide/logicpro-ipad/lpip878dfe7e/ipados
- Selected regions can be dragged immediately; an unselected region touched and
  dragged without a hold scrolls the workspace rather than being moved.
  Source: https://support.apple.com/guide/logicpro-ipad/drag-gestures-lpip3db67ae4/ipados
- Touch/hold on a trim handle temporarily magnifies the working area; double-tap
  the ruler can start or stop playback.
  Source: https://support.apple.com/en-ph/guide/logicpro-ipad/lpipded6da0f/ipados
- Mixer supports two simultaneous fingers on different controls and double-tap
  to reset slider/knob to default.
  Source: https://support.apple.com/en-gb/guide/logicpro-ipad/lpip70ec7de4/ipados
- Plugin tiles have separate selection, details, and long-press context behaviours.
  Source: https://support.apple.com/en-asia/guide/logicpro-ipad/lpipc9c9ed73/ipados
- Apple recommends minimum 44x44pt touch targets for iPad/iPhone (do not assume
  JUCE device pixels equal points). Android's standard ergonomic target is 48dp.
  Source: https://developer.apple.com/design/tips/

## Code audit (source, not an APK/iPad runtime test)

| Area | Actual code | Observed issue | Status in this branch |
| --- | --- | --- | --- |
| Scroll | `ArrangementViewCore.cpp::applyTouchPanInertia` | Local-space finger deltas changed as Viewport scrolled and fed motion back into input. | **Patched**: screen-space pan/velocity origin. |
| Pinch | same | >6px step threshold reset *every frame*: very slow pinches never registered. Zoom targeted the timeline origin rather than gesture midpoint. | **Patched**: cumulative threshold, bounded ratio, centroid anchor. |
| Two fingers | same | A one-finger pan anchor/velocity survived into multi-touch; two-finger translation did not pan. | **Patched**: phase reset and two-finger translation. |
| Gesture ownership | same | Global Desktop source scan could include fader and popup touches during an active timeline timer. | **Patched**: component/viewport filtering. Needs device regression. |
| Region-first scroll | `ClipRenderCore.cpp::mouseDown/mouseDrag` | First tap selected a region but swallowed drag and prevented scrolling over that region. | **Patched**: unselected touch drag forwards to navigation without editing. |
| Mouse/trackpad | `ArrangementViewCore.cpp` and `ClipRenderCore.cpp` | Must retain mouse editing, wheel and shortcut semantics. | **Preserved paths**; CI and device checks pending. |
| Mixer | `Source/UICore/MixerPanel.h::mouseDown/mouseDrag/mouseDoubleClick` | Fader required hitting a narrow thumb and ordinary touch of strip started a reorder. | **Patched**: whole fader lane accepts touch, double-tap on lane resets, reorder needs hold. Multi-fader and responsive geometry still need device verification. |
| Plugin/knobs | `Source/BubblegumKnobCore/`, plugin views | Needs explicit multi-pointer capture, enlarged invisible hit zones, one gesture per parameter and reset affordance. | **Not yet ported**. |
| Timeline / take playlists | `Source/UICore/TrackPlaylistPanel.h` and editor | Needs large explicit promote/comp targets, scroll vs edit arbitration. | **Not yet ported**. |
| Mobile packaging | `DAW_Core.jucer` and platform layers | Native iOS/iPadOS/Android lifecycle, safe-area, scale and plugin-host policies are not proven by JUCE Windows build. | **Unverified**. |

## UX behaviour contract before mobile release

1. **Touch directness**: one finger empty-background pan; unselected region tap to
   select and swipe to navigate; selected region drag edits intentionally.
   Vertical and horizontal pinch, two-finger translation, and optional Pencil
   editing must never move audio regions accidentally.
2. **Stable anchors**: scrolling and zooming track pointer movement in screen
   coordinates; zoom around centroid, keep the musical point under fingers fixed;
   avoid pinch/pan jitter and stale anchors on 1-to-2-to-1 transitions.
3. **Per-pointer ownership**: mixing faders and plugin controls can move together,
   without causing an arrangement pan. No UI allocations/locks/file calls
   from the audio callback.
4. **Visible controls**: iPhone compact controls and popovers, iPad resizable
   panels, safe-area placement for notch/gesture bar; minimum 44pt Apple
   / 48dp Android effective hit target. Small artwork may sit inside a larger
   hit region. Avoid overlays that hide transport Record, Stop, Play or Undo.
5. **No hover-only actions**: long press context actions and explicit touch
   affordances; Pencil/trackpad/mouse alternatives stay available.
6. **Undo and automation**: every committed region/fader/plugin touch gesture
   uses the same canonical action, stable parameter ID and Undo boundary as
   desktop. Touch, Latch and Write automation paths must be regression-tested.
7. **No destructive gesture collisions**: edge-trim vs scroll, region move vs
   pinch, two-finger gestures vs automation point edits, drag-drop vs
   transport interactions must be deterministic.
8. **60Hz+ budget**: benchmark 30+ tracks + waveforms + hosted plugins during
   scrolling/pinching; frame-time percentiles and drop rate are release gates,
   not guarantees based on source inspection.

## Acceptance gates (not yet claimed)

- [x] Portable deterministic math regressions authored (`Tests/Portable/TouchNavigationMathPortableTests.cpp`).
- [ ] Linux portable regression passes in GitHub Actions.
- [ ] Windows Release and Debug app + tests pass GitHub Actions.
- [ ] Real tablet or simulator: 1-finger pan over empty space and unselected clips.
- [ ] Real tablet or simulator: 2-finger pan, slow pinch, centric anchor, 1→2→1 continuity.
- [ ] Mixer 2+ simultaneous faders / plug-in controls with no timeline movement.
- [ ] 44pt/48dp target audit, landscape/portrait/foldable/safe-area audit.
- [ ] Playlist promotion, wave editing, automation gesture Undo, no unintended
      arm, stop, clip move, split, erase or crash.
- [ ] iPad/iPhone/Android native builds and physical audio-I/O tests.

**Status**: targeted source-level navigation repair only, NOT full Logic Pro
iPad feature parity, NOT a mobile APK or iOS build, NOT a passed device gate.
