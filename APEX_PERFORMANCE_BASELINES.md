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
