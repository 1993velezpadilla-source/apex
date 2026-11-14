# APEX Mobile GUI — Adversarial Gauntlet, Pass 1 (2026-10-31)

**Branch:** `feature/mobile-portrait-bubblegum-gauntlet-20261031` (branched from Android PR #164).
**Safety:** No DSP, routing model, project format, Windows mixer docking, or `main` changes.
**Platform intent:** Android phone/iPhone = portrait-first mobile sends panel, no Matrix. iPad, Windows, Mac = original desktop-like Matrix.

## P0 findings and fixes this pass

| Finding | Failure mode | Mitigation in this branch | Current evidence |
| --- | --- | --- | --- |
| Bubblegum panel used fixed 260×380 and invalid `jlimit` bounds | Landscape parent shorter than 396 could break positioning/assert | `MobilePanelGeometry.h` with bounded dimensions and margin for any nonnegative viewport | Portable C++17 compile and stress geometry tests GREEN locally |
| Eye button opens `BUBBLEGUM MATRIX` in phone presentation | Contradicts approved product policy; hiding cables does not hide Matrix | Hide eye painting, deny eye hit, and guard `toggleFullScreenMode`; iOS iPad keeps Matrix | Source inspection; not Android device verified |
| 34px send rows and closely spaced controls | Hard to tap on phone, accidental send/delete | 52-unit rows in phone mode, 50-unit inter-control centres, larger invisible knob/SC/trash targets | Source inspection; real touch QA pending |
| Rotation can strand floating panel off-screen | Controls unreachable following orientation change | Re-fit panel in `MainComponent::resized()` without mutating sends or plugin state | Geometry unit tests; hardware QA pending |
| Android/iPhone platform policy absent in panel initialization | Wrong mobile behavior | Android explicitly phone; iOS `iPad` device description retains desktop presentation | Requires iOS runtime QA and simulator check |

## Adversarial negative cases for the next round

- **All entry points:** Eye, keyboard, overflow, restored panel, programmatic toggle must never show Matrix on phones. Android must not modify any saved send.
- **Inadequate viewport:** 360×640, 393×852, 852×300, 245×220 and Android split-window; popup must be within safe bounds, and close/action controls reachable.
- **Rotate while panel open:** remain reachable, preserve selected source and PRE/POST, SC/send states, level gestures and Undo.
- **Mobile fader hit zones:** geometry of SC/knob/trash must match actual paint and must not overlap under high font scaling.
- **iPad regression:** iPad full desktop Matrix must remain reachable; detect iPad through a reliable native platform signal, not solely a free-form device description.
- **GUI identity:** original pink Bubblegum renderer stays intact; new mobile-only spacing is presentation, not an alternate DSP path.
- **Mixer:** must remain **floating** even after loading a previously saved docked desktop project.
- **Audio callback:** GUI allocation and redraw must not occur on the audio thread.

## Important limitations / no false GREEN

- This is **only Pass 1** of a broader portrait/landscape GUI redesign.
- The phone-wide Arrangement, bottom transport, full responsive Mixer, plugin editors, Radial Wheel safe-area/gesture QA, and iPhone package are still **not finished**.
- A JUCE/Windows production compilation and real Android/iPhone device regression have **not been run** as part of this pass.
- Android PR #164 still describes experimental full-shell compilation, not a proven full DAW APK.
- The portable panel geometry test was run with `g++ -std=c++17 -O2 -Wall -Wextra -Werror`; GitHub CI was configured to rerun it on relevant PRs.
- Do not merge this into `main` without native compile, visual and touch QA, and project save/reopen regression.

## Next passes (not yet complete)

1. Native compile audit for the new JUCE panel code on Android ARM64 and Windows.
2. Portrait Arrangement + persistent lower transport with controls that fit 360dp widths without clipping.
3. Floating Mixer responsive strip viewport and drag/resize constraints.
4. Native plugin editor touch layouts, gesture arbitration, safe keyboard & window insets.
5. Portrait/landscape on-device workflows: Volt 1 input → record → play → open Mixer → Bubblegum send → reopen project.

## Pass 2 — drag-release viewport recovery (2026-10-31)

- **Observed code defect:** `BubblegumV2PanelUI::mouseDrag()` calls `ComponentDragger::dragComponent(..., nullptr)` without bounds constraints; releasing beyond the activity could strand the header and close button off-screen. Rotation-only recovery did not handle ordinary drag release.
- **Small UI-only fix:** On phone presentation, `mouseUp()` now calls the existing `constrainPhonePanelToParent()` after a header drag. Desktop/iPad drag behavior remains unchanged; no DSP, routing, send level, project, or `main` changes.
- **Regression coverage:** Added explicit negative/large drag-anchor cases and multiple tiny portrait/landscape viewports to the portable geometry test. This checks recovery geometry, **not** JUCE event dispatch or physical touch behavior.
- **Remaining gates:** CI on the new head, native compile, actual drag/touch/rotation on Android and iPhone, iPad Matrix regression, save/reopen, and USB audio recording. Keep this PR draft.
