# APEX EQ — independent Pro-Q 4 workflow audit and adversarial acceptance

Reference: [FabFilter Pro-Q 4 official user manual](https://www.fabfilter.com/downloads/pdf/help/ffproq4-manual.pdf), **for behavior and interaction only**. All APEX code, logo and art must be original. Do not copy FabFilter copyright or claim proprietary algorithm equivalence.

## Verified from automated, executable behavior
- New native editor factory launches `ApexQ4Editor`, not a renamed instance of rejected Prism GUI.
- Audio chain uses stable APEX project parameter IDs and the already-reviewed realtime transition guard.
- `APEX EQ` heading, existing boot-eye rendering source, nonanimated.
- Rendered frequency graph with native host-controlled filter nodes, direct click/double-click, precision move, modifier creation, and selected-band context settings.
- Contextual HUD only appears when a band is selected; mobile prioritizes large ON, shape, HOLD SOLO, MORE and close targets, with advanced slope, channel and dynamics accessible through MORE.
- Real PRE/POST analyzer: test creates and decodes a **3-second stereo 48 kHz 16-bit WAV**, runs it through actual APEX EQ at **128 samples**, asserts finite FFT energy and positive processed-vs-original harmonic difference. Both spectrum curves share a calibrated dBFS ruler independent of the EQ gain scale.
- **Display-only** Gaussian log-frequency power integration at 1/36, 1/18 or 1/9 octave. Every drawn point is derived from genuine worker FFT bins; no fabricated harmonic content, no synthetic noise added to the screen. Tests reject any displayed dB peak greater than the highest measured raw bin.
- Native JUCE screenshots at 320×568, 480×800, 900×300, 1280×720, 1920×1080. Adversarial code checks panel containment, eye/preset separation, and pairwise overlapping contextual button hit regions.

## UNVERIFIED and not yet 1:1 (these must be addressed)
- Complete multi-select, grouping and proportional adjustment of EQ nodes.
- Drag interactions and gestures tested with actual Windows touch input rather than only synthesized JUCE mouse events.
- Dynamic band outer ring + threshold behavior, automatic dynamic threshold, full spectral mode controls, associated detector settings.
- All cut, shelf, bell, notch, all-pass, tilt slope cycling and full contextual drag/scroll behavior across channels.
- Analyzer freeze, peak hold, slope, collision checks, output metering, auto gain measurement.
- Phase-mode fidelity incl. natural/linear latency, oversampling, character modes and delay compensation.
- Full stereo/L/R/M/S placements, surround/channel layouts, crossover audition.
- Preset management + A/B, undo/redo history across host automation, Pro-Q instance workflows and EQ Match.
- Detailed visual reference comparison on actual screenshot: fonts, spacing, curve/node response, spectrum prominence and animation, menu density, touch margins.
- Apollo Solo **48 kHz / 128 samples** vocal song: uninterrupted moving bands, measured output, audible artifacts, host-save/reopen.

## Release gate
**Never merge to MAIN solely for a GREEN compiler or a resemblance claim.** Require focused MSVC/JUCE, EQ gauntlet Debug+Release, Windows complete suite, screenshot artifact review, and user approval. This new editor intentionally does NOT replicate FabFilter trade dress, source, DSP, icons or presets.
