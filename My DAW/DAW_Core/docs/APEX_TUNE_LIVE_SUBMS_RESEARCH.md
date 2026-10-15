# APEX Tune Live — near-one-millisecond LIVE research

Status: **experimental, not registered as an APEX Native insert**. The existing
40 ms HQ motor and V5 GUI stay untouched until a candidate beats their sonic
quality in real tests.

## Why we cannot just set the plugin latency to 1 ms

The microphone sample-to-headphone delay, reported plug-in delay compensation
(PDC), first usable pitch estimate, first audible pitch correction and
envelope/transient recovery are **different measurements**. Detecting F0 from
a monophonic voice requires observing part of its period(s); for A2 (110 Hz)
one period is about 9.09 ms. This DOES NOT mean all practical Auto-Tune
engines must report >=9.09 ms PDC: a predictive or causal design may avoid a
fixed lookahead delay while keeping pitch tracking inherently delayed.
A false PDC=0 or PDC=48 without measuring real wet audio is unacceptable.

At 48 kHz a **1.0 ms plug-in PDC budget = 48 samples**, but achieving that
number alone does not guarantee that the singer hears timely tuning. End-to-
end mic monitoring also includes ASIO/device input-output, APEX routing and
other inserts. Retune Speed is not audio path latency.

## First original hypothesis, with runnable evidence

`Source/TuneLiveCore/Research/ApexTuneLiveHarmonicLivePrototype.h`

- Uses the **actual** production PitchTracker (YIN) and musical VoiceControl;
  the engine receives only original audio, **not** a known input-F0 oracle.
- Reconstructs up to 12 harmonic partials with a rolling causal EMA envelope
  and forward-moving phase oscillators at the corrected target frequency.
- No explicit lookahead, no delay line in the dry branch; input is passed
  straight through while the detector/synthesizer acquires the singer's F0.
  Oscillator phase continuity and stereo isolation use fixed-size arrays.
- No heap allocation in the audio sample path. Trig functions are evaluated
  only when a new pitch estimate arrives, not once per sample/partial.
- WARNING: passing samples straight through does **not** establish
  <1ms *audible wet response*. A pitch tracker may need tens of ms to react.
  Harmonic-only synthesis can sound synthetic/metallic, and will not preserve
  vocal articulation and formants in all cases. No quality claim is made.

`Tests/TuneLive/test_harmonic_live_research.cpp` measures the actual
processed output F0 (not only control values), first F0 acquisition time,
first >=50% wet-synthesis time, realtime processing time, finite output,
right-input-only stereo isolation, and immediate dry output at start, across
44.1/48/96 kHz and varied callback sizes. This is a **hypothesis test**, not
a release test. Synthetic F0 eligibility is diagnostic; a GREEN job does not
mean the candidate reached its wet latency or perceived quality target.

## Promotion gates (hard blockers, no fake success)

1. First prove intelligible, natural **real recorded male/female voices**,
   preserving formants, breath, sibilants, falsetto and phrase onsets.
   Compare blind level-matched wet to HQ40, not just to a sine.
2. Measure actual output response **to note transitions and consonants**,
   separately from a reported PDC. At 48 kHz aim for **<=48 samples of
   true fixed plug-in PDC** and transparently report larger tonal settling.
3. Compare 44.1/48/96 kHz; 64/128/256 and 1/257 block partitions, real
   C2–C6 vocals, +/−30 cent and glide/vibrato; no false octaves or clicks.
4. Validate zero allocations/locks, multi-insert CPU, no overflows/NaNs,
   and correct mono/stereo and wet/dry/bypass behavior.
5. Hook into APEX host **only after** reliable full-track automation and
   project restore tests. Preserve the existing hosted parameter IDs/state.
6. Measure real Apollo Solo USB3, APEX ASIO and headphones round-trip:
   **target a comfortable total (<10ms where hardware/host allow)**.
   Apollo Console and APEX monitoring must not run simultaneously.

The experimental synthesis **must stay off by default**; no claim of
near-zero latency, Antares parity or commercially usable sound is justified
at this stage. Measuring and rejecting unsuccessful designs is progress.
