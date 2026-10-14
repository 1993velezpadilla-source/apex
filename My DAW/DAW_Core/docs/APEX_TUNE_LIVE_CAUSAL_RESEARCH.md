# APEX Tune Live — period-guided causal pitch-shift research

**Research only.** This component does not replace the shipping 40ms Signalsmith backend, does not appear in the APEX Tune Live host or UI, and has no valid fixed plugin delay-compensation (PDC) claim. Do not merge the experimental path into live monitoring until the gates below pass. Independent C++17 implementation; no proprietary Antares code.

## Why another architecture?

The 12ms spectral-window experiment passed 8/8 safety cases but only 4/8 preliminary F0 accuracy criteria. At 48k, +30c sharp inputs retained about +26.1c (A3) and +22.7c (A4). Research with a second F0 oracle and a direct raw Signalsmith command showed that the 12ms window itself under-corrected even when the requested transposition reached the processor. Changing YIN thresholds alone will not repair that shifter limitation.

The new `Source/TuneLiveCore/Research/ApexTuneLivePeriodGuidedPrototype.h` explores a **two-read-head time-domain shifter** with the two read offsets separated by one detected *source* period. Each read head is faded out at its grain wrap. The continuously moving delay produces the requested ratio without short-window spectral analysis. The component has bounded preallocated history, independently isolated mono/stereo state, a finite-input guard, and no allocations in `process()`.

**Critical methodology constraint:** the first pitch-accuracy experiment passes an **exact known F0 oracle** to the shifter. That is deliberately NOT the same thing as singing into a microphone, where source F0 changes, the note may be unvoiced and the existing detector has ~21–43ms history. An F0 oracle benchmark tests a limited hypothesis (can the synthesis engine realize a precise pitch ratio on a stationary periodic vowel-like waveform?), not product readiness.

## Offline tests and acceptance meaning

- `test_period_guided.cpp`: 44.1/48k, A3/A4, +/-30c, synthetic harmonic waveforms with AH/EE-like spectral emphasis, independent 8192-sample autocorrelation of processed audio; outputs residual F0, peak, period-dependent buffer history bound and number passing the preliminary 18c *synthetic-only* threshold.
- `test_period_guided_safety.cpp`: 36 stress cases across 44.1/48/96/192k, source F0 55/220/1200Hz and ratio 0.85/1/1.15; right-only stereo signal/no L leak, NaN upstream input, bounded history, zero observed callback `operator new`/`new[]` allocation.
- Both tests are compiled with Linux ASan/UBSan and Windows MSVC by `.github/workflows/apex-tune-live-dsp.yml`. The offline shifter is never selected by APEX Native.

**Latency honesty:** `requiredHistorySamples()` is an upper bound on the look-back region, NOT measured/plugin-reported PDC and NOT microphone-to-ear latency. At 48k, using two periods implies about 9.1ms of history at A3 and 4.5ms at A4, but ~36.4ms near 55Hz. The period-varying read heads do not provide a single fixed wet/dry alignment. These measurements cannot be compared 1:1 with Antares 37/112-sample PDC. The existing F0 detector and hardware buffer remain separate sources of reaction time.

## Hard blockers before a production live mode

1. Feed **real streaming YIN/pitch estimates**, not known oracle F0. Test estimated-source accuracy, octaves and jumps for C2–C6, falsetto, vibrato, glides and consonants.
2. Stabilize and interpolate period and transposition changes without click, grain-size jumps, hunting, phase resets or false-note locking. Test silence/unvoiced blend and note onset explicitly.
3. Find a valid and consistent wet/dry timing/PDC design, including bypass and APEX automation/session recall. **Never advertise required history as fixed PDC.**
4. Test formant preservation and articulation: AH/EE/OH with real male/female singing, consonants, sibilants, vibrato and timbral artifacts. Level-matched ABX against the original 40ms engine and artist approval required.
5. Capture real-time CPU, harmonic distortion, buffer underruns, full APEX GUI/native insert, ASIO and Apollo Solo USB3 end-to-end at 48k/128 and multiple simultaneous tracks.
6. Continue exploring predictive causal pitch tracking and synthesis if the 37/112-sample target remains desired. Period-guided overlap synthesis alone does not meet universal <1ms PDC.

## Promotion policy

Keep `ShifterProfile::HighQuality40ms` in production. A research-only 8/8 synthetic pitch result demonstrates **feasibility on ideal periodic tones**, not Antares quality. No recorded-vocal certification, no new user-facing latency mode and no marketing until all hard gates are measured and green.
