# APEX — Logic Pro for iPad touch interaction parity matrix
**Audited 2026-10-21.** Applies to the native APEX JUCE Windows touchscreen
(including Lenovo Legion Go), future iPad/iPhone ports, and Android APK.
Goal is parity in *interaction*, not copying Apple graphics or brand assets.
A source change is not evidence of a device pass.

## Audited Apple first-party references
- [Gesture overview](https://support.apple.com/guide/logicpro-ipad/overview-of-gestures-lpipfdd2e00b/ipados)
- [Main gestures](https://support.apple.com/guide/logicpro-ipad/main-gestures-lpip05674184/ipados)
- [Drag gestures](https://support.apple.com/guide/logicpro-ipad/drag-gestures-lpip3db67ae4/ipados)
- [Shortcut menu gestures](https://support.apple.com/en-au/guide/logicpro-ipad/lpip57e16d92/ipados)
- [Tracks area gestures](https://support.apple.com/en-ca/guide/logicpro-ipad/lpipded6da0f/ipados)
- [Selecting regions](https://support.apple.com/en-sa/guide/logicpro-ipad/lpip39124d82/ipados)
- [Cut, copy and paste](https://support.apple.com/en-ca/guide/logicpro-ipad/lpip6381dd59/ipados)
- [Trim regions](https://support.apple.com/en-ca/guide/logicpro-ipad/lpipc283e4e1/ipados)
- [Split/join regions](https://support.apple.com/en-au/guide/logicpro-ipad/lpip98d86a3e/ipados)
- [Function/momentary/modifier buttons](https://support.apple.com/guide/logicpro-ipad/work-with-function-buttons-lpipad359f54/3.3/ipados/26)
- [Mixer gestures](https://support.apple.com/en-gb/guide/logicpro-ipad/lpip70ec7de4/ipados)
- [Fader](https://support.apple.com/en-lamr/guide/logicpro-ipad/lpip25780c0a/ipados)
- [Mute/solo multiple tracks](https://support.apple.com/guide/logicpro-ipad/mute-and-solo-tracks-lpipf45500e0/ipados)
- [Plugin-area gestures](https://support.apple.com/en-asia/guide/logicpro-ipad/lpipc9c9ed73/ipados)
- [Plugin slot operations](https://support.apple.com/en-gb/guide/logicpro-ipad/lpipbc563110/ipados)
- [Write automation in real time](https://support.apple.com/en-ca/guide/logicpro-ipad/lpip38f0b9e0/ipados)
- [Use trackpad](https://support.apple.com/en-mide/guide/logicpro-ipad/lpip73037126/ipados)

Legend: SOURCE PATCHED = code change in PR #151, no hardware pass.
EXISTING = previously existed, not certified. TODO = not implemented
or source inspection insufficient. The Apple feature is the reference, not an
assertion that the equivalent APEX editing backend is available.

## 1. Regions / clips / playlist comping

| Logic iPad interaction | APEX expected behaviour | Source/status | Acceptance probe |
| --- | --- | --- | --- |
| Tap unselected region | Select it once, no movement | SOURCE PATCHED: ClipRenderCore onClick -> canonical engine selected state | Tap first take, ensure visual + inspector identity sync |
| Drag unselected region *without hold* | Scroll timeline, don't move clip | SOURCE PATCHED: ClipRenderCore touchPanFromUnselected + ArrangementViewCore | Swipe clip left/right; check session/sample positions unchanged |
| Drag selected region | Move region with snap, stable undo; one finger | EXISTING ArrangementViewCore drag path | Select, drag, undo/reopen, no duplicate event |
| Tap already-selected region | Reveal native clip shortcut actions, like right-click | SOURCE PATCHED: ClipRenderCore onTouchContextMenu -> showClipContextMenu | One tap select, next stationary tap opens context |
| Hold selected region | Also reach shortcut menu | SOURCE PATCHED on release for stationary hold; instantaneous on-hold menu not implemented | Hold then release >500ms; menu actions same as right-click |
| Secondary click with mouse/trackpad | Existing desktop menu | EXISTING ArrangementViewCore mouseUp | Right-click still works; keyboard/trackpad |
| Hold background, nothing selected | Show empty-track context actions | SOURCE PATCHED: timer begins on down, context on release | Deselect, hold empty area 600ms; menu; swipe must not show |
| Tap background | Deselect region(s) | SOURCE PATCHED: handleSelectTool touch | Tap elsewhere, inspector/selection cleared |
| Long-press background + drag | Marquee select multiple regions | EXISTING hold -> rubber band implementation; device unverified | Drag marquee across clips on different tracks |
| Select mode / multi-select modifier | Toggle region memberships by tap; don't accidentally move | TODO: expose explicit touch Select toggle, keep Ctrl/Shift mouse | Tap A, B, A; correct set and multi-action |
| Copy modifier | Drag a copy, not original; undo as one command | EXISTING desktop Alt drag; TODO touch Copy button | Select + Copy + drag; confirm original preserved |
| Cut / Copy / Paste Here / Paste at playhead | Popup commands at finger position and cursor | EXISTING menu callbacks; TODO verify all semantic targets | Paste across tracks and timeline with snap |
| Paste at original position | Preserve original timestamp across tracks | TODO parity/probe | Move target track, paste original time |
| Trim left/right | Resize selected region via explicit edge targets | EXISTING ClipRenderCore hit zones | Finger test left/right without waveform move |
| Hold trim handle | Temporary zoom magnifier while editing, restore at release | TODO | Hold handle ~1s, drag precise, no timeline jump |
| Region Loop | Visible loop end handle/gesture | TODO direct Logic-style Loop function | Loop to grid and verify persistent playback |
| Split | Dedicated non-destructive touch split position marker + downward stroke | EXISTING desktop tool but TODO iPad touch exact gesture | Multi-clip split with no accidental cut |
| Stretch | Touch stretch edges, precise mode, correct engine rendering | EXISTING mouse/shift; TODO touch-specific ownership | Time stretch, render, undo, reopen |
| Fade | Touchable fade handles with temporary precision | EXISTING ClipRenderCore; TODO larger hit area | Left/right handles + curve controls |
| Mute region | Touch context toggle; visual feedback + undo | EXISTING native context callback | Mute one/multiple, reopen |
| Rename/color | Context/inspector touch entry; keyboard focus | EXISTING native callback; touch UX unverified | Rename, resize viewport, save/load |
| Playlist take promote | Touch-selected phrase promotes, no false promo | EXISTING TrackPlaylistPanel/track header; TODO touch target and test | Highlight phrase, promote arrow, undo |
| Context-menu destructive actions | Full confirmation/undo where needed | TODO safety audit | Delete/duplicate/freeze with undo and IDs |

## 2. Timeline and whole-screen navigation

| Logic interaction | APEX expected behaviour | Status |
| --- | --- | --- |
| One-finger background scroll x/y | Screen-space movement and velocity, inertial decay | SOURCE PATCHED |
| Two-finger drag | Pan without editing | SOURCE PATCHED |
| Two-finger horizontal expansion | Time zoom, center stays anchored | SOURCE PATCHED |
| Two-finger vertical expansion | Track height zoom, center stays anchored | SOURCE PATCHED |
| Slow pinch or pinch reversal | Accumulate subthreshold changes, no dead zone | SOURCE PATCHED + portable math tests |
| Change finger count 1→2→1 | No stale anchors, no accidental clip context menu | SOURCE PATCHED with multi-touch epoch |
| Pinch while adjusting mixer knob | Timeline does not follow external pointer | SOURCE PATCHED scope filtering |
| Double-tap ruler | Start playback at ruler tap or stop | TODO, must avoid transport side effects |
| Drag view resize handle | Dock inspector, editors, mixer, play surface | TODO responsive pane framework |
| Tap resize handle | Toggle default/maximized editor size | TODO |
| Swipe top for main menu | Native platform-adapted menu, not OS gesture conflict | TODO |
| Swipe dialog down | Dismiss dialogs safely, never commit unintended edits | TODO |
| Disclosures/pop-ups | 1 tap opens menu/expanded group, not hover-only | PARTIAL existing UI |
| Numeric fields swipe up/down | Fine parameter edit + entry keyboard + bounds | TODO |
| Pinch while scrolling & heavy waveforms | Consistent LOD and >=60Hz target | PARTIAL painting LOD; device profiling TODO |

## 3. Tracks, header, mixer, automation

| Logic interaction | APEX expected behaviour | Status |
| --- | --- | --- |
| Tap track header | Focus/select without moving channel | EXISTING |
| Hold track header | Context menu equivalent to desktop right-click | SOURCE PATCHED TrackList TrackRow |
| Swipe a track header | Vertically scroll rather than reorder | SOURCE PATCHED with viewport routing |
| Hold then drag track header | Explicit reorder with stable IDs, no quick-swipe move | SOURCE PATCHED gating; integration test TODO |
| Track Mute/Solo/Arm/Monitor | Single deliberate touch, no swipe accidental changes | EXISTING controls; hit-size audit TODO |
| Hold mute/solo and sweep across adjacent tracks | Mute/solo multiple channels in one continuous stroke | TODO |
| Double-tap track icon | Toggle plugin/play surface pane | TODO |
| Tap selected-track icon in mini view | Select Track pop-up | TODO |
| Fader finger grab | Entire track lane hit zone, not only thumb | SOURCE PATCHED MixerPanel |
| Double-tap fader | 0dB/reset through same parameter authority | SOURCE PATCHED MixerPanel |
| Double-tap knob | Reset to default via parameter authority | EXISTING BubblegumKnobComponent; plug-in-by-plugin audit TODO |
| Move two mixer faders at once | Independent source capture & automation begin/end | PARTIAL per-component, hardware test TODO |
| Mix pan or send via finger drag | Drag stable on touch, no cursor capture | EXISTING UI; device test TODO |
| Track/region automation mode | Distinguish track vs region Touch and Latch | TODO parity; existing APEX track-native modes must not be regressed |
| Touch/Latch record multiple controls | Write the parameters independently with matching IDs | TODO end-to-end native and hosted plugin gate |
| Output/sends quick fader | Fast routing and aux focus without lost context | TODO responsive work surfaces |
| Transport context hold | Play/Record/Count-in/Metronome shortcut options | TODO |

## 4. Plugins, editors and mobile OS

| Logic interaction | APEX expected behaviour | Status |
| --- | --- | --- |
| Tap unselected plugin tile | Select the slot | TODO slot gesture parity |
| Tap selected plugin tile | Open detailed parameters | TODO |
| Double-tap plugin tile | Details editor | TODO |
| Hold plugin tile | Add-before, replace, remove, copy, paste context menu | TODO |
| Swipe plugin pane down | Dismiss without changing inserts | TODO |
| Reorder plugin chain | Touch drag with activation/deadzone; no dangling host pointer | TODO |
| Touch plug-in controls | Each parameter gets stable begin/change/end automation gesture | EXISTING partially; audit TODO |
| Open piano roll/edit region | Single purposeful toolbar action or context item | EXISTING editor, touch parity TODO |
| Select / move MIDI notes | Touch modes + pinch in note editor | TODO |
| Step sequencer rows / note lengths | Touch-friendly grids and playhead-aware pop-ups | TODO |
| External mouse or trackpad | Desktop semantics preserved (right-click, wheel, Ctrl/Shift) | EXISTING, regression required |
| Lenovo Legion Go Windows touchscreen | Native touch + mouse/gamepad coexist; scaling; rotation | Required **first physical validation target** |
| Android tablet / foldable | 48dp touch targets + window/safe area & density | TODO Android native build/device |
| iPadOS | 44pt touch targets + safe area + orientation | TODO native build/device |
| iPhone | Responsive compact arrangement/mixer, no hidden controls | TODO native build/device |

## Regression matrix for Lenovo Legion Go (Windows)
1. Touch only: select an audio clip once; confirm its track and inspector.
2. Second tap selected clip: same actions as desktop right-click menu. Choose a
   safe action, Undo, Save, reopen.
3. Swipe first-tap unselected clip; timeline scrolls and the clip's project
   sample start and source offset remain bitwise unchanged.
4. Touch selected clip then drag: region moves, snaps correctly and Undo is
   a single action; an ordinary tap never moves it.
5. Two-finger pan/zoom across a clip; NO context popup when fingers lift.
6. Slow two-finger spread (<2px sampled each frame) eventually zooms and
   never oscillates; zoom focus does not drift.
7. Hold empty arrangement background -> empty context menu; fast swipe
   scrolls; hold+drag creates marquee.
8. Track header fast vertical swipe -> track-list scroll, no channel reorder.
9. Track header stationary hold -> track right-click menu. Hold + drag ->
   reorder only when intentional.
10. Fader touch anywhere in slider lane: gain changes with one automation
    begin/end. Double-tap returns to default. Drag two different faders at once.
11. While turning an effect knob and moving mixer fader, timeline does not pan.
12. Switch between finger, mouse, hardware keyboard and trackpad; no stuck
    virtual cursor/drag gesture; right-click remains correct.
13. Save, close, reopen, then repeat clip menu, drag, playlist promotion,
    automation Touch/Latch, native plugin parameter editing.
14. Measure p50/p95/p99 frame time at 60/120Hz, 32+ tracks; compare against
    old baseline; check audio glitch/xrun and transport under load.
15. Test 100%/125%/150%/200% Windows DPI and landscape/tablet orientation.
16. Verify that no touch gesture writes state in an audio callback, and that
    plugin GUI/lifecycle work remains on its designated thread.

**Blocking rule:** no mobile "1:1" or Release declaration until missing TODO
items and device checks are materially completed and measured; a passing
portable-math unit test is only a small sub-gate.
