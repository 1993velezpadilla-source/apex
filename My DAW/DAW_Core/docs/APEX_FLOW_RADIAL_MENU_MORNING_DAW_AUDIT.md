# APEX FLOW radial menu — MorningDAW and Logic iPad input audit
Date: 2026-10-21. Source-level implementation on PR #151. **Do not mark
Release ready until Windows compiler + real Lenovo Legion Go gates pass.**

## Direct manufacturer references
- [MorningDAW product](https://edu.morningdewmedia.com/product/morningdaw) — contextual pie menu, one-flick actions, stable positions, mouse cursor anchor, press-hold/flick and release.
- [MorningDAW 1.3 user manual](https://edu.morningdewmedia.com/pages/morningdaw/manual/) — right-click pie; press-flick or click wedge; Esc/centre exits; up to seven direct choices and More; lane, region, automation, MIDI, audio-edit, mixer contextual action scopes; piano-roll Pencil uses Shift+right-click because plain right-click erases.
- [Apple — Logic for iPad gestures](https://support.apple.com/en-ca/guide/logicpro-ipad/lpipfdd2e00b/ipados) — tap, hold, drag, pinch, multi-touch mixer/plugins; Pencil performs one-finger precision gestures and handwriting.
- [Apple — trackpad](https://support.apple.com/en-mide/guide/logicpro-ipad/lpip73037126/ipados) — secondary click and modifier shortcuts, and important distinct trackpad-vs-touch pinch behavior.
- [JUCE MouseInputSource](https://docs.juce.com/master/classjuce_1_1MouseInputSource.html) and [MouseEvent](https://docs.juce.com/master/classjuce_1_1MouseEvent.html) — distinguishes mouse/touch/pen and reports conditional pressure, tilt and rotation.

## Architecture / no regressions allowed
1. Right-click on desktop and hold/context on direct-input devices choose one
   **canonical** action builder, not a new clip/track/mixer codepath.
2. APEX Radial default (persisted separately in user settings).
   Settings > General > Context / Right-click menu switches at runtime
   between APEX Radial and the **unchanged classic JUCE/OS-look PopupMenu**.
3. Child pie levels use the same menu data. Centre button **BACK** on
   submenus; **CLOSE** at root. Esc and Backspace have the same behavior.
   Paging handles long menus via MORE, never clips actions silently.
4. No context menu logic runs on audio thread. Menu hosts/targets are JUCE
   message-thread-owned; protected by SafePointer where callbacks rely on a
   living component. Clip selection actions resolve a UUID at invocation.
5. Unsupported complex JUCE menu items (custom component, command manager,
   custom callback) **fall back to classic**, rather than breaking commands.
6. Pointer geometry is pixel-space and independent of current arrangement
   scroll/zoom. Core hit test tested with portable C++17 assertions.
7. One finger scrolls the arranger, two-finger pan/pinch anchors to viewport.
   Pen is precise, not treated as a kinetic-scrolling finger; valid
   pressure/tilt/rotation are preserved as optional metadata, never fabricated.
8. Only UI painting is animated, currently entrance glow for 0.18s then
   timer stops. No DSP dependency.

## Actually wired in this PR
- Clip/empty lane: ClipContextMenuCore canonical menu, nested presets,
  Safe Defaults and native action IDs, old selection callbacks.
- Track headers: selected/multi-select track actions and hold.
- Track virtual take playlist, playlist selector, input source, track
  automation mode/target.
- Mixer strip right-click, recording input mode, mixer settings.
- Plugin slot context and plugin browser: plugin choices/manufacturers,
  bypass, remove, replace, open editor.
- Dedicated take playlist comp context.
- Pencil/pen (when platform/backend emits JUCE pen events):
  double-tap/menu on selected clips, long hold for plugin slots and
  track header, parameter-compatible pointer source identity.

## Differences still OPEN vs MorningDAW / Logic iPad
- **Press/flick/release while holding the original right mouse button** is
  NOT fully wired for every widget; currently release/right-click then
  hover/click is supported on many paths. This is not yet MorningDAW 1:1.
- Existing context menus in *other* codebase components still use
  PopupMenu directly; no dishonest whole-app coverage claim.
- Wheel's per-wedge labels (long manufacturer names) need hardware
  readability/accessible name and 44pt/48dp hit-area validation.
- Desktop wheel + touchscreen menus coexist, but plugin editor native VST3
  windows and system windows cannot be forcibly converted to APEX wheel.
- Contextual keyboard/gamepad access, stylus hover, barrel click, Pencil
  double tap and scribble require platform-specific validation/bridges.
- Multitouch MIDI editors, transport menus and all automation-lane
  gestures are not complete. Pressure-sensitive automation drawing is
  **not enabled**; only event metadata is extracted.
- No benchmark yet on Lenovo Legion Go 100/125/150/200% DPI or
  32+ track projects, no p95/p99 latency/xrun measurements.
- Native macOS/iPadOS/iPhone/Android builds and device input dispatch
  must be tested after platform bring-up. Code portability is not
  device parity proof.

## Repro / acceptance gauntlet
- Windows mouse right-click clip -> wheel appears near pointer. Navigate
  Presets > nested action, BACK -> clip root. Esc -> close. Delete via
  wheel then Undo. Switch Settings General to Classic and repeat using
  original vertical PopupMenu. Restart APEX; preference persists.
- Touch selected region first/second tap and two-finger pinch -> never
  trigger both action and edit. Open wheel, page MORE, BACK, cancel,
  reopen, and act. Hit disabled wedge -> no action.
- Open wheel at each window corner and in compact narrow layout; confirm
  all wedges are visible and collision-clamped. Move overlay across
  scrollable arrangement -> content does not scroll or edit.
- Test plugin slot touch tap vs hold, Replace submenu/browser, disabled
  sandbox hosting gating, remove, session save/reopen. No plugin pointer
  use-after-free when wheel remains open during track deletion.
- Two-finger real physical faders, knob plus timeline pinch, pen draw/tap,
  track reorder hold; no stuck gestures and no audio interruption.
