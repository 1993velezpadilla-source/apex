# Independent APEX Tune Live engine A/B

This non-shipping A/B benchmark compiles APEX Studio and Tracking in one
temporary executable, and Flayr (AGPL-3.0) Live and Studio in an **independent**
temporary executable. Flayr source is fetched at 4bd2454177888b7566fb4ce667ba078662d9cd25 exclusively in
GitHub Actions; no source, header or executable from Flayr is copied into the
shipping APEX application. Only generated float WAVs and JSON metrics are
published. Do not combine AGPL code into the shipping app without proper
licensing.

Both engines process the same 4-second 48kHz harmonic voice fixture, with
8ms and 7ms voiced gaps, a 140ms pause, note transitions and natural vibrato.
CPU time percentiles are GitHub runner timings, **not Pixel Fold callback
deadline guarantees**. Adjacent sample jumps can occur in legitimate vocal
harmonics, so measurements are diagnostic, not proof of audibility.

Other candidates reviewed, but not runnable as the same independent C++
DSP API: MIT Antares-patent-inspired JSFX (REAPER JSFX/EEL2 runtime needed)
https://github.com/superdandi/autotune-jsfx ; OpenTune (MIT, DSP coupled to
JUCE processor and voice/reverb effects) https://github.com/bemtorres/opentune .
Neither is proven to be original proprietary Antares source.

## Phase-continuous research variant
The APEX tracking research path optionally smooths the source oscillator frequency
at a 16-sample cadence using a 4ms time constant and extends the old-bank
release to 5ms. Its phase accumulator never resets during normal F0 updates.
This is a non-shipping experimental flag in RealtimeCore, off by default.
The A/B benchmark includes a fifth row, tracking_phase_slew_research.
Audible results, actual pitch accuracy, and real Android xrun behavior must be
validated before considering promotion. The code adds no new memory allocation.
