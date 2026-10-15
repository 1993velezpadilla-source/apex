# APEX graph PDC V2 — release certification gate (2026-10-14)

Status: **BLOCKED / research only**. PR #108 is a draft. This checklist is not evidence that tests have passed.

## Required automated end-to-end fixture
Run through the real AudioEngine track and bus render loop, not only a prepared edge dispatcher. Construct two stereo tracks (vocal insert exactly 32 samples, bass 0), pre-fader and post-fader sends to an aux with a 16-sample insert, plus direct-to-master edges. Feed impulses with unique source amplitudes and compare rendered master against a sample-by-sample reference: all programme paths must reconverge at 48 samples, with no premature dry signal. Repeat at 44100, 48000 and 96000 Hz; callback partitions 1, 31, 64, 128, 256, 1024; mono and stereo.

## Live-edit stress (render continuously)
1. Create/remove QuickSend and AutoSend edges while playback advances; ensure every published plan uses one coherent routing version and latency-map revision.
2. Toggle sends, adjust send gain only, change pan and mute; assert gain-only edits retain DSP history, while identity/format/latency changes invalidate it.
3. Change insert latency 0 -> 32 -> 16 -> 64 -> 0 while audio is playing; measure discontinuities at each publish boundary and ensure no duplicate master compensation.
4. Exercise plugin bypass, track bypass, bus bypass, pre/post-fader taps, folder sums, and seek/stop/reset.
5. Exercise multiple independent sidechain sources feeding one destination; each source must have isolated history.
6. Exercise sandbox worker latency changes, plugin unload/reload, and VST3 latency notifications.
7. Exercise an armed vocal track with C6 Bypass/Reduced/Full monitoring; require fail-closed legacy path on stale graph generations.

## Pass criteria and evidence
- Sample-exact impulse alignment and deterministic partition invariance for stable topologies.
- No audio-thread heap allocations, mutex waits, map growth or retired-bank destruction on the render callback; verify with instrumented counters and sanitizer/stress runners.
- Record worst transition peak, discontinuity metric, sample rate, callback size and topology revision; **do not label click-free without measurements and listening tests**.
- Windows Debug and Release build/test logs linked to exact commit SHA.
- Apollo Solo real-device loopback and vocal-monitor test, with VST3 plugins, must be performed on hardware; CI cannot substitute.
- Compare long-session recording reliability against legacy engine; preserve rollback switch.
- Enable Release only after reviewed results for all gates; otherwise keep experimental flags off.

## Current implementation boundary
The opt-in experimental graph renderer is compiled only into APEXTests; APEX Desktop Release is unchanged. The standalone edge tests and JUCE dispatch probes do not establish real-session or hardware certification.
