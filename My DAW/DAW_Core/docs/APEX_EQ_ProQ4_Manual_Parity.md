# APEX EQ — Pro-Q 4 manual compatibility program (2026-10-22)

## Reference
FabFilter Pro-Q 4 **official** PDF, 64 pages:
https://www.fabfilter.com/downloads/pdf/help/ffproq4-manual.pdf

Use the manual as a **functional specification and usability reference**. Build original APEX visual assets and code; do not copy FabFilter trademarks, graphics, presets, DSP or UI source.

## Non-destructive replacement strategy
The previous large Prism inspector is retired. Preserve canonical hosted parameter IDs, saved project state, native JUCE/Apex plugin registration, audio engine, dynamic processing, and 24-band layout to protect existing sessions. Keep the October 22 voice-dropout/real-time DSP corrections until independently validated.

## Evidence and completeness gates

| Manual section | State in this branch | Next acceptance gate |
|---|---|---|
| Display and workflow (p. 8–10): single tap, double-click, edit/drag band, scroll wheel Q and modifiers | Core gestures wired; single tap now deselects first, creates on next; Alt bypass/shape/slope, right-click context menu | Test every supported modifier, precision and band-selection edge case |
| Band controls (p. 12–13): always-contextual shape, FREQ, GAIN, Q, slope, channel placement and bypass | Redesigned compact contextual controls; advanced controls preserved in scrollable MORE view | Native CI screenshot geometry + real finger editing |
| Spectrum analyzer / grab (p. 33–39) | Existing native PRE/POST FFT, peak-hold and grab are retained | Measured audio-driven display verification vs silence; responsiveness |
| Dynamic EQ / spectral dynamics (p. 14–18) | Existing native controls/engine retained; **not independently verified as Pro-Q equivalent** | Automated impulse, sine-sweep, detector, overshoot and distortion tests |
| EQ Sketch (p. 11–12) | Existing APEX sketch engine retained | Gesture, path and undo parity tests |
| Stereo/left/right/mid/side (p. 26–27) | Existing APEX per-band routing/placement retained | Multichannel routing gauntlet |
| Phase modes (p. 29–32) | Existing native mode controls retained | Listening + impulse/phase + PDC tests; **not exact Pro-Q DSP** |
| EQ Match / multiple instances (p. 37) | Native APEX matching and limited shared-reference functionality retained | Reference acquisition / session instances parity; not full Instance List |
| Undo/redo, A/B, precise numeric entry (p. 41+) | Not all manual behaviors present | Implement and test |
| Multiselect, rectangle, linked edits, automatic filter shape, range zoom | **Pending** | Implement, do not label 1:1 until complete |
| Surround / Atmos 9.1.6, character, output options, Auto Gain, MIDI mapping, full preset import | **Pending manual-parity audit** | Explicit pass/fail evidence by feature |
| Proprietary FabFilter DSP, visual branding | Out of scope; original APEX implementation only | Independent quality metrics, never claim copied code |

## Quality bar
1. No EQ node hold can accidentally solo the singer.
2. No audio-thread allocation/lock, no EQ update retarget mid-transition.
3. 48 kHz/128-sample sustained live parameter movement passes finite audio, RMS continuity, zipper threshold, CPU checks and real-world Apollo Solo listening.
4. Desktop, 900×300 short landscape, 640×400 and portrait/phone screenshots show no obscured response nodes, huge blocking popovers, clipped controls or off-screen footer.
5. Do not merge until Windows focused compiler, Debug/Release EQ Gauntlet and full Windows tests are GREEN.
6. Do not claim complete 1:1 until every in-scope manual feature has a runnable acceptance test.
