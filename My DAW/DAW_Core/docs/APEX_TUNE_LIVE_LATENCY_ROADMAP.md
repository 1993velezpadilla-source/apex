# APEX Tune Live — Antares-class latency & sung voice quality gates
**Engineering plan, not a performance claim — 2026-10-12.**

## Measured competitor targets, official Antares support

Antares AutoTune Advanced Live mode advertises **under 1 ms plugin latency**
from 44.1–192 kHz. Antares Low Latency mode at 48 kHz cites
**37 samples / 0.77 ms (Classic)** and
**112 samples / 2.3 ms (Modern)**.
AutoTune Pro 11 uses a separate "Use Low Latency" setting, not those
exact Advanced 2026 mode names.
Sources:
- https://help.antarestech.com/hc/en-us/articles/53453475818004-AutoTune-Advanced-FAQ
- https://help.antarestech.com/hc/en-us/articles/42858099043092-AutoTune-Best-Practices
- https://www.antarestech.com/documentation/auto-tune-pro-11

These numbers are the ***plugin's*** algorithmic latency, not actual
microphone-to-ear roundtrip with Apollo Solo, DAW routing, USB/driver, 128
samples of hardware buffer, any console effects or additional inserts.
At 48 kHz, a *single* 128-sample buffer corresponds to 2.667 ms. Never
conflate musical Retune Speed (how quickly we approach a note) with audio
I/O latency or plugin PDC. Pitch detection warmup and stabilization must
also be independently measured: a plugin may report 37 samples PDC
while it still takes tens of milliseconds to identify a new sung F0.

## Our verified latency gap — not marketing
- APEX production shifter: **1920 samples / 40 ms @ 48 kHz** PDC;
  **42.67 ms** low-register YIN analysis window. Windows JUCE 8 linked
  processor successfully ran with these settings at 48 kHz / 128.
- APEX **experimental** short-window production-DSP candidate:
  **target 864 samples / 18 ms @ 48 kHz** (true DSP PDC must be CI-
  measured before presenting as a pass). This is **7.7x slower than
  Antares Modern Low Latency** (112 samples) and **23.4x slower than
  Antares Classic Low Latency** (37 samples) on plugin PDC. This is
  *not* the Antares target.
- Current spectral architecture cannot meet <1 ms without a materially
  different **causal, pitch-synchronous** engine. Do **not** misreport a
  40-ms spectral process as a sub-1ms Live mode by changing metadata.
  Increasing hardware sample rate while quoting samples alone also
  does not certify end-to-end latency.
- The 18-ms candidate is *not* exposed on the user-facing APEX UI and
  is not enabled by default. It is an opt-in prepare() **research mode**.
  Never modify quality profiles in the audio callback because doing so
  changes PDC/ring allocations and can create loud discontinuities.

## Release goals
1. **Live engine (new causal architecture):** objective <48 reported
   samples plugin PDC at 48 kHz for parity with Antares Advanced Live;
   and separately a fallback <=112 samples at 48 kHz matching their
   cited Modern Low Latency class. This is an *aspirational target*,
   not a promise already achieved.
2. **Honest end-to-end measurements:** OS audio-loopback or recorded
   impulse, 44.1 / 48 / 96 kHz, 64 / 128 / 256 host buffer sizes;
   Apollo Solo USB3 + APEX local plugin and no double monitoring;
   record input-arrival -> audible-output latency and report PDC.
3. **F0 timing:** detect sung note transitions quickly and accurately
   across C2–C6, breath/sibilance, glides, pitch bends, transient attack;
   compare latency of an **audible** correction after a true note change
   separately from plugin-reported latency.
4. **Timbre:** level-matched recordings of sung AH/EE/OH, consonant
   bursts, transitions and whisper/sibilants, with and without formants;
   automatic octave errors, artifacts and vibrato-rate/distortion scans.
   Confirm with ABX listening tests and human auditions.
5. **RT trust:** no allocations, no locks, no nan/inf, stable bypass,
   actual mono/stereo PDC, background timeline playback, 8+ concurrent
   inserts on representative laptop CPU, session state/automation,
   APEX Debug/Release and final recorded vocals.
6. **UI order:** native model/integration and hosted ABI first, link
   and run APEX host, **then** refine visual controls and responsiveness,
   then Apollo studio testing. The base custom native dark editor has
   already compiled and run under JUCE. UI styling must not delay or
   mask latency/quality certification.

## Status and non-negotiable release rule
Main AudioPluginInstance keeps the proven 40ms profile until a lower
PDC is accompanied by a GREEN real-input fidelity gate (not only a
synthetic sine). Experimental 18ms is a reduced-latency intermediate
step, **not Antares equality**. Accurate notes alone do not guarantee
natural vocal timbre, no chorus, phase flanging or sustained vibrato.

**Never declare "finished", "Antares Quality" or "<1ms" until a
verified recording and benchmark justify the exact claim.**
