# APEX Tune Live: Antares-class release bar (sound-first)

**Product requirement**: a vocalist inserts **APEX Tune Live** on any audio
track, enables input monitoring, and hears the *actual tuned microphone* while
singing. A waveform that merely displays the right note is NOT a passing
condition. This is an original processor; proprietary code/graphics are out
of scope.

Reference for public product expectations: Antares' March/October 2026
support materials identify Retune Speed, Flex-Tune, Humanize, formants, low
latency, and a *real difference* between Live and HQ modes. The vocal tone is
the authority; matching parameters does not imply matching sound.

## Release gates

| Gate | How to measure | Stop condition |
|---|---|---|
| Actual per-track live FX | Apollo mic -> armed/monitored APEX track -> Tune Live insert -> headphones, record and monitor | Dry when tuning should be active; only file render tunes |
| Pitch accuracy | Monophonic dry male/female voiced clips with independently labeled F0; output analysis at 44.1,48,96k | Inability to hold reference pitch without wandering/octave jumps |
| Natural vibrato | Tuned sustained 4–7 Hz human vibrato, +/-15–40 cents; compare modulation range and perceived tone | Smooth note reduced to fixed/flat tone in Natural preset |
| No exaggerated artifacts | Breaths, sibilants, consonants, creak, attacks, sliding notes, transitions and full mixes | Metallic chorus, pumping, lisping, unnatural scratch or phasing |
| Timbre integrity | ABX-style level-matched audition on diverse sung vowel samples, altered and unaltered | Unacceptable chipmunk/dark vowels, altered identity |
| Bypass timing | Exact dry waveform after reported plugin PDC, check across block sizes and 44.1/48/96k | Delayed/doubled monitor, clicks or crossfade discontinuity |
| Latency | Actual plugin PDC + device round trip measured on real Apollo Solo 48k/128; compare Antares UAD hardware route separately | Uncomfortable singer monitoring; no unsupported "ultralow latency" claim |
| CPU/RT | Callback profiler and allocation watchdog while sustaining voiced audio for hours; multisessions | xruns, heap allocations, stuck locks, stalls, NaNs or blowups |
| Session/host | Windows Debug+Release, native plugin insertion, multiple instances, automation Write->Read, presets, bypass, save/reopen, offline render, freeze/bounce | Missing plugin, corrupted automation/restore, export mismatch |

## Performance modes (future)

**Live** must use a deliberately optimized low-latency pitch tracker and
shifter, not an offline stretch preset relabeled "zero latency". **HQ** can
use more analysis/lookahead for improved formants and pitch transitions.
The current 512-sample detector at ~12k is ~43ms of accumulated history,
plus the separate Signalsmith shifter PDC. This is **not** near-zero
latency and requires a genuinely different solution before claiming
Antares-class live responsiveness.

The existing V1 algorithm uses original YIN+Signalsmith Stretch.
The V1 Humanize now separates a slow note centre from the faster expressive
pitch component, so natural vibrato is kept to a greater degree; it is
*not* the same algorithm or settings semantics as Antares Humanize.
The new regression mandates preservation of +/-25-cent, 5-Hz vibrato
compared with hard tune, while still rejecting out-of-key detuning.

**Natural / Modern / Hard Tune** now have actual user-facing artist
preset buttons in a dedicated responsive dark APEX plug-in editor. Presets
modify real hosted parameters using change gestures and host notifications;
key and scale intentionally remain under the singer's control. The natural
profile includes gentle *Flex Tune* (small intonation dead-zone), a slow
tracked note centre for vibrato, Humanize and note hysteresis. All modes
are experimental sonic starting points, **not artist-certified**. The initial
default stays expressive (Humanize 0.70, amount 0.85, retune ~31ms,
Flex Tune 0.35). Headroom, mix, formants and latency still require measured
real singing tests.

## Recent head implementation notes

- A4 concert-reference slider now covers **430.0 to 450.0 Hz**, with 440.0
  default. This parameter is appended after every previous hosted ID and is
  saved with the session and eligible for APEX automation. The live note/cents
  readout obeys the selected concert reference as well.
- Mono voice tracking remains authoritative. For stereo stems, the pitch
  tracker now follows the channel with a stronger smoothed vocal envelope;
  a right-only microphone no longer silently bypasses detection. A specific
  right-only stereo regression checks the actual processed right-channel
  F0 and zero unintended left-channel signal.
- Tests added for A4=432/440/442, a low male ~65.4 Hz C2, artist preset
  parameter bounds, real dry/wet output, mono/stereo and oversized callback
  handling, and sample-accurate reported dry bypass delay.
- The native JUCE editor has six host-aware rotary controls, an editable
  concert-A4 control, scale/key selection, 3 artist styles, formant switch,
  bypass, pitch/cents/F0 and PDC readouts. This is real source code but UI
  build/runtime usability has not been certified in Windows Debug/Release.
- These extra tests remain requirements, **not successful outcomes**, until
  CI reports a green conclusion for the exact final PR head SHA.

## Targeted DSP hardening — new unverified regression gates

- APEX YIN now short-circuits its cumulative-mean difference scan after
  the first valid completed sub-threshold minimum, rather than computing all
  lag differences for every voiced 440 Hz block. `lastLagEvaluations()`
  exposes the per-frame search length to a deterministic portable gate.
  This should lower callback CPU especially at high vocal fundamentals;
  real CPU measurements are still required.
- For upper-register fundamentals (>=165 Hz), a 256-sample preliminary
  detector can establish the first pitch around **21–28 ms**, before the
  normal 512-sample long-history detector becomes available. Deep vocal/bass
  analysis continues with the full window and is *not* claimed to improve.
  This shortens the first F0 estimate only; the shifter, device and host
  roundtrip latency **do not disappear**, and must be measured separately.
- Wet DSP now sanitizes nonfinite audio samples and host parameter values
  (NaN/Inf) at its realtime boundary to avoid poisoning sustained FFT state.
  Denormal-sized inputs are silenced, pathological >8-float samples are
  bounded without touching typical singing amplitudes. Sanitization is
  deterministic and allocation-free in the callback.
- Added synthetic regression gates for poisoned upstream plugin output,
  YIN CPU work reduction, upper-register startup with guarded bass octave,
  and unvoiced high-frequency sibilance retaining the original time-aligned
  dry consonant. These are assertions only until the exact branch CI
  completes, *not certified studio-quality audio*.

## 2026-10-12 experimental shifter study (measured CI, not subjective quality)

The pinned Signalsmith 1.3.2 native shifter was measured with a 447.691 Hz
sine sung ~30 cents sharp of A4, a fixed -0.30 semitone request, 48 kHz,
and the **same original YIN analyzer** on the final 0.5s output segment.
Results are actual Linux ASan/GCC and Windows MSVC portable regression
measurements; they are not listening-test evidence.

| Shifter window | Native plugin latency | Output residual error |
| --- | ---: | ---: |
| 18 ms | 18 ms | +19.4 cents |
| 24 ms | 24 ms | +20.0 cents |
| 30 ms | 30 ms | +19.4 cents |
| 40 ms (shipping prototype) | 40 ms | +16.7 cents |

A separate *fixed-transpose* scan at 447.691 Hz illustrates that the
requested spectral shift is **not** identical to the actual correction:

| Requested shift | Residual at 18 ms | Residual at 40 ms |
| --- | ---: | ---: |
| -0.30 semitones | +19.4 c | +16.7 c |
| -0.45 semitones | +9.7 c | -1.2 c |
| -0.60 semitones | -3.2 c | -24.4 c |
| -0.75 semitones | -19.0 c | -44.8 c |

**Interpretation:** shorter shifter windows can lower its PDC, but fidelity
with the *same* transpose settings is not equivalent, and arbitrary gains
may massively overshoot. We must not bake in a universal 1.5x or 2x gain
based on one sine: real sung vowels, pitch ranges, voice formants and rising
versus falling intervals must be covered first. Real latency further
includes input and output device + the host routing path; 18 ms here
is **not** an APEX Apollo Solo end-to-end latency claim.

The real **production DspCore** wet benchmark with 40 ms shifter, hard
correction, zero Flex Tune and 100% amount measured +15.87 cents residual,
passing the **existing** ±18-cent synthetic gate but **not** tight pro
intonation; the previous +21.2 cents was the intended Flex Tune dead-zone
incorrectly left ON in the hard-tune accuracy fixture. That fixture and
a cross-platform CI issue (Windows command scripts swallowed assertion
errors) have now been corrected. Actual GCC/ASan and MSVC wet/PDC
gauntlets completed GREEN on commit `ded83bfb033beb4e17e32af2787e14d6af9e01f9`.
Additional frequency-response study is still exploratory.

Next sonic milestone is a *measured, source-clean calibrated pitch-shifter*
or different low-latency architecture, **plus** independent confidence-
weighted real-voice listening and tests. Correct numerical cents alone
never certifies Antares-class sound.

## Objective / subjective evidence policy

- CI green for portable detector/note-selection is only a component gate.
- Exact production `DspCore` must pass processed-audio correction and
  waveform/PDC bypass tests on the pinned dependency.
- Native APEX Windows Debug+Release, Apollo live mic and recorder-path
  tests are essential before shipping.
- Final sound quality is a *listening and artist feedback decision*, not
  something inferred from asserts or a screenshot.
- Keep the PR draft until this acceptance report exists. Do not infer
  production readiness from the standalone tester.

## References

- Antares Auto-Tune Pro 11 User Guide, official public documentation
  https://www.antarestech.com/documentation/auto-tune-pro-11
- Antares 2026 Best Practices (live vs HQ, humanize, flex tune)
  https://help.antarestech.com/hc/en-us/articles/42858099043092-AutoTune-Best-Practices
- de Cheveigné & Kawahara YIN (JASA 2002), DOI 10.1121/1.1458024
- Signalsmith Stretch, MIT license; use the pinned APEX submodule
