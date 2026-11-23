# APEX Performance Baselines

**Last verified:** 2026-07-24

## Audio Callback Budget

- **Sample rate:** 48,000 Hz (typical)
- **Buffer size:** 128–256 samples (typical ASIO)
- **Budget at 128/48k:** ~2.667 ms per callback
- **Budget at 256/48k:** ~5.333 ms per callback

## RT-Safe Contract

The audio thread MUST NOT:
- Allocate heap memory
- Acquire locks (mutex, critical section)
- Perform file I/O
- Make GUI calls
- Call plugin lifecycle methods
- Trigger graph rebuilding

## Current Mitigations

- `ScopedNoDenormals` in AudioEngine::process()
- Pre-allocated scratch buffers (8192 worst-case)
- Lock-free routing snapshot reads
- Atomic transport state
- Pre-reserved hash maps (256 tracks, 1024 clips)
- Connection gain ramps with pre-allocated capacity
- Transport fade: one-pole soft start (~5 ms)

## Known Risks

- `unordered_map::emplace` on first use per edge (connectionGainRamps_) — amortized after first block
- Per-clip DSP cores created on demand (`getOrCreateClipPitchCore`) — first block may allocate
- Plugin chain traversal per track per block — complexity depends on plugin count

## Baseline Measurements

| Metric | Value | Source |
|---|---|---|
| Audio callback budget (128/48k) | 2.667 ms | Calculated |
| Worst-case block size allocated | 8192 samples | AudioEngine.h:299 |
| Transport fade duration | ~5 ms | AudioEngine.h:615 |
| Live input monitor smoothing tau | 5 ms | AudioEngine.h:328 |
| Plugin playhead debug interval | 1 second | AudioEngine.h:518 |
| Forensic audit report interval | 2 seconds | AudioEngine.h:374 |

## Arranger navigation on Android and Windows

The 2026-11-22 APEX Mobile 9.10.0 emulator replay started with bars 34–37
visible and panned to reveal bars 38 and 39 to the right and bar 33 to the
left. Ruler draw CPU time for the first reveal went from 13.9 to 3.4 ms at
bar 38, 16.1 to 2.7 ms at bar 39, and 6.9 to 3.7 ms at bar 33 compared with
9.9.9. The replay passed all 12 cases. Late frames remained in the emulator.

The Android build is [4aea676](https://github.com/1993velezpadilla-source/apex/commit/4aea676446a4bb351e3b8888914612724f247498).
The [Android runtime replay](https://github.com/1993velezpadilla-source/apex/actions/runs/45064671706)
verified reuse of the prepared digits and colon after zooming on both axes.
The visible arranger is drawn directly.

The Windows port uses the same navigation and ruler policies on the desktop
JUCE peer. The Windows executable replay covers the first reveal of bars 33,
38 and 39, reuse of the numeral atlas across both zoom axes, and scroll and
zoom with imported audio, at deep zoom, during playback, and while recording
in a reopened 24-track session containing a normalized clip. The recording
replay compares the captured WAV samples with its known input signal.
