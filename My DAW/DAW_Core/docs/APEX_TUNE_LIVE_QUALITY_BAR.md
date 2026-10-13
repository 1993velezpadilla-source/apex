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
