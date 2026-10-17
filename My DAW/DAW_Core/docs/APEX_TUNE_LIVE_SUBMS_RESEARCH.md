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
- Reconstructs up to 24 harmonic partials with a rolling causal EMA envelope
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

## New 48-sample / 1ms transient gate (research only)

We added an optional **causal residual-preservation** path that replaces
predicted *unshifted* harmonics with the tracked corrected harmonics while
leaving the microphone's current-sample spectral residual (consonants,
breath, unmodeled partials) in the output. That means the direct transient
path no longer waits for the pitch-shifter's spectral frame.

The strict `test_live_one_ms_transient.cpp` inserts an isolated impulse
into a **previously acquired, actively tuned** voice at 48kHz and checks
whether it is audible **in the same output sample**, not after the 48-sample
(1ms) budget. The first 1ms of an entirely new, unknown sung note is
explicitly asserted **not** to have a pitch classification. Do not confuse
the two phenomena.

Initial Linux ASan/UBSan test run `38659642372` was GREEN:
- Voiced source ~223.846Hz, immediate impulse gain **0.941968**.
- Voiced source ~447.691Hz, immediate impulse gain **0.889493**.
- Both corrected paths were at 100% wet when the impulse arrived,
  with finite output; no added lookahead on this isolated transient.
- The streaming short-onset tracker still takes around **14.65ms** to
  recognize a fresh A3/A4 note at 48kHz, and the actual 50%-wet first
  correction still requires **26.85–34.02ms** on synthetic signals.
  This is **NOT a 1ms note-response or overall latency result**.

A full real-singer pipeline now writes separate residual-preserving 24-bit
research WAVs for Vocadito reference, high and low registers and tests that
the renders remain finite and non-silent. Their levels and tone must
still be evaluated. A silent/zero-delay *dry* branch alone is not
an acceptable substitute for actually hearing pitch correction.

## October 16, 2026 — observed measurements and production boundary

Measured independently on Linux and MSVC research CI, using 44.1/48/96 kHz
synthetic streaming test audio. These are **not interchangeable latencies**:

| 48 kHz condition | Measured time | What actually changed |
| --- | ---: | --- |
| Existing shipped HQ40 declared nominal delay | ~40 ms | Full production processing model |
| Ultra high F0 first-note acquisition, 8-hop | 7.3125 ms | Fast YIN detector observation only |
| Ultra high F0 first-note acquisition, opt-in 4-hop | **6.9792 ms** | Fast YIN detector observation only |
| Fast 4-hop: 440Hz, 50% *wet control envelope* | **9.875 ms** | Does not establish audible full correction |
| Fast 4-hop: 660Hz, 50% *wet control envelope* | **9.125 ms** | Does not establish audible full correction |
| Synthetic A3→A4, no dual high F0 consensus | 5.979 ms after leap | Note-leap detection, not final wet audio |
| Synthetic A3→A4, with dual high F0 consensus | **7.979 ms after leap** | 2 ms longer than unguarded fast leap |

**Do not advertise this as 40 ms → 6.98 ms equivalent processing
latency.** One number is a declared production buffering budget; the other
measures a *research-only new high-register F0 detection event*. None of
these prove a 1 ms pitch correction or live mic-to-headphones result.

**Real singer transition hardening.** At the pinned high-register Vocadito
singer's 5.6657-second hotspot, the opt-in dual-detector consensus reduced
the local PCM difference relative to the previous high-register algorithm
from ~0.168 FS RMS to ~0.011 FS RMS on the worst 5ms slice. A strict
dataset-pinned localized-waveform regression gate passes all 11 significant
5ms windows. This proves waveform similarity to that reference, **not**
audible transparency, objectively correct formants, or artist preference.
Across three singer takes, sustained-note F0 noninferiority gates pass.
Actual synthetic A3→A4 with dual consensus passes 3/3 on MSVC and Linux
sanitizers and sustains final independent output F0 <1.1 cents from target.
No complete acoustic onset or end-to-end ASIO validation is implied.

**Other safeguards.** The hostile sibilance/plosive unvoiced regression
covers 18 seeded cases at 44.1/48/96k. Both guarded and unguarded paths
preserve callback partition results, reject false high-register note leaps,
and pass verified MSVC and Linux ASan/UBSan tests. A separate eight-insert
test combines four original and four opt-in dual-consensus instances with
allocation watch and independently measured output F0.

**Release blocker remains:** all production DSP and the native V5 GUI
continue untouched. The prototype requires blind human listening, transient
quality judgment, precise plugin-PDC measurement, host session restore
testing and physical Apollo Solo/ASIO mic-monitoring measurements. A GREEN
research CI is not permission to activate it in production. APEX Windows
x64 Debug/Release artifacts exist for the PR, but successful compilation
alone is not the full production certification gate.

## October 17, 2026 — Antares-compatible monitoring target (distinct from note detection)

**Official comparison target (manufacturer's claims; not independently
measured by APEX):** Antares AutoTune Advanced *Live* processing is specified
as **under 1 ms** at 44.1–192 kHz. Antares specifies *Low Latency Classic*
at **37 samples / 0.77 ms** at 48 kHz, and *Low Latency Modern* at
**112 samples / 2.33 ms** at 48 kHz. Antares AutoTune Pro 11 separately
has a *Use Low Latency* preference; do not silently associate the Advanced
Live/Classic sample counts with the Pro 11 product.

Manufacturer sources:
- https://help.antarestech.com/hc/en-us/articles/53453475818004-AutoTune-Advanced-FAQ
- https://help.antarestech.com/hc/en-us/articles/42858099043092-AutoTune-Best-Practices
- https://www.antarestech.com/products/pitch-correction/advanced

**APEX delivery definition:** Match **<1 ms of additional plugin
signal-through / reported algorithmic latency** in a real hosted APEX Live
mode, while maintaining demonstrable genuine *corrected* wet audio, not
disguising a dry bypass as pitch correction. At 48 kHz **strictly under
1 ms is at most 47 whole samples**; 48 samples is exactly 1.000 ms.
At 44.1 kHz use <=44, at 96 kHz <=95, and at 192 kHz <=191
samples. The actual correct rounding is
`maximumPermittedSamples=ceil(rate*0.001)-1`. These are plugin-signal
budgets, excluding interface buffer and DAC/ADC roundtrip. The user-facing
mode must not be named "1ms Live" without real host measurements.

Separate hard gates: (a) streaming direct-consonant component first
output delay under 1ms **while an already tuned note is active**;
(b) independently measured output pitch is actually corrected;
(c) host sets honest PDC and verifies impulse offset in processing;
(d) first newly sung F0 / note-leap recognition times explicitly disclosed,
which remain >1ms for unknown notes; (e) blind human vowel/formant/breath
audition at multiple registers; (f) session automation, mono/stereo,
bypass and Apollo ASIO monitoring integrity. Never infer (c)-(f) from
(a) or from the fact the code has no explicit delay buffer.

New `test_live_sub1ms_signal_path.cpp` measures (a) and (b) at
44.1/48/96/192k, low and high source F0, both baseline and guarded 4-hop
research paths, with arbitrary host block boundaries; emits the actual
first F0 acquisition and wet-control time in the same test to prevent
confusion. New CI gates are research-only. The HQ40 shipping native host
processor and its original ~40ms PDC remain untouched.
