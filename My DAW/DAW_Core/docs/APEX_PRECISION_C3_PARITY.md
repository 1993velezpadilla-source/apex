# APEX Precision C3 — reference contract and release gates

Reference: [FabFilter Pro-C 3 manual](https://www.fabfilter.com/downloads/pdf/help/ffproc3-manual.pdf) (47 pages). This document audits product **capabilities and operating workflow**, not copyrighted artwork or proprietary signal processing.

## Product boundary

- APEX Opto 2A (`APEX::Opto2A`, unique ID `0x4F503241`) remains intact, openable by old projects.
- APEX Precision C3 (`APEX::PrecisionC3`, unique ID `0x41504333`) is a second distinct native plugin, hosted by the existing sole `APEX Native` format.
- New code is clean-room original. It must **not** be advertised as a FabFilter binary-compatible or sonically 1:1 clone without a matched measurement suite.

## Current source state (experimental PR)

| Feature / workflow | Manual expectation | Current APEX Precision C3 | Gate |
| --- | --- | --- | --- |
| Separate Dynamics insert | Independent compressor | Distinct ID and format registration | Windows host tests |
| Threshold / ratio / knee / range | Stable param automation, 0–72 dB knee | Implemented in original algorithm | Compare transfer curves |
| Attack / release / hold | Fast attack, program-dependent response, hold | Implemented with sample-accurate per-sample envelope | Transient sweeps |
| 14 styles | 14 distinct calibrated algorithms | 14 selectable **approximate** original musical voicings | NOT 1:1. Implement/style-calibrate each |
| Upward | Gain boost below threshold | Signed gain path functional | Portable tests |
| TTM | Three-band up/down compression | **Broadband proof-of-concept only** | Add independent three-band crossover topology |
| Vari-Mu | Feedback variable-mu transfer | Feedback-inspired reduction | Model and measure transfer |
| Auto Threshold | Trigger behaviour independent of absolute level | Adaptive detector baseline | Robustness/loudness measurement |
| Auto Release and Auto Gain | Program-dependent recovery, makeup estimate | Approximate functions exposed | Gain-match sweeps |
| Wet / Dry / Input / Output | Parallel processing and trim | Separate wet gain, dry injection, trim | Crossfade/phase tests |
| Stereo link | Variable linking and M/S modes | Variable L/R peak linking | Mid/side modes still required |
| Sidechain | External, MIDI, host tempo | Internal only; core supports optional sidechain sample pointers | Host wiring, triggering and safety |
| Sidechain EQ | Up to six bands, sophisticated shapes | 1 internal HP + 1 LP | 6-band EQ and band editor |
| Character | Off/Tube/Diode/Bright; Drive and pre/post | Original post-compression color approximations | Pre/post option; oversampling |
| Lookahead/PDC | Configurable 0–20 ms | Not implemented; zero reported latency | Preallocated delay and exact host PDC |
| Oversampling | Up to 32× | Not implemented | Anti-alias engine + CPU/load limits |
| Input/output and GR metering | Accurate peak/loudness + dynamic graph | Block peak and GR, transfer graph | Meter ballistic verification |
| Surround | Up to immersive 9.1.6 | Mono/stereo only | Multi-bus and channel policies |
| Presets / A-B / undo | Browser, tags and editing | Session state only | UI and migration tests |
| MIDI learn / keyboard / touch | Desktop/multi-touch UX | Stable host automation, touch-sized knobs | Touchscreen gauntlet |
| Instance List | Links to other FabFilter processors | Not applicable to independent APEX branding | Optional APEX native instance manager |

## Engineering gates before merge to main

1. Windows Debug + Release builds, host unit tests and portable C++17 sanitizer tests GREEN.
2. Reopen older Opto 2A sessions and confirm identical plugin identity and state.
3. Save/load Precision C3 on tracks, buses and clips; verify 21 stable parameter IDs and Read/Write/Touch/Latch automation.
4. Zero allocations/locks in audio processing, deterministic results across block sizes, mono/stereo and no crashes on NaN, silence or extreme parameters.
5. Auditory AB blind comparison against reference recordings at several sample rates, input levels, compression curves and transient programs **before any sonic 1:1 claim**.
6. Real audio interface recording at 48 kHz / 128 frames, moving every parameter while streaming, no mutes/stalls/dropouts.
7. Windows DPI 100–250%, touch drag, double-click value entry, panels scale without clipped hit targets.

**Status:** preview implementation; functionality parity and sound calibration are *not* complete. Do not merge automatically merely because compilation passes.
