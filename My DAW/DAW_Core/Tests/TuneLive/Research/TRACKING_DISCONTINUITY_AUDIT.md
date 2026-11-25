# Tracking discontinuity audit — November 25, 2026

Field report: Android audio is clean with Tune Live bypassed, and clicks/pops
and a doubled voice occur with Tracking active at C# major, Hard Tune 1 ms,
100% wet, preserve formants. This localizes the investigation but does not
exclude callback deadline misses caused by active DSP.

## Public technical references
- McAulay/Quatieri, sinusoidal speech analysis/synthesis:
  https://www.ll.mit.edu/r-d/publications/speech-analysissynthesis-based-sinusoidal-representation
  Phase continuity and track birth/death motivate ramped retirement rather
  than instantaneous harmonic coefficient deletion.
- JUCE SmoothedValue:
  https://docs.juce.com/master/classjuce_1_1SmoothedValue.html
  Audible parameter changes require smoothing on a sample timeline.
- Android Oboe low latency:
  https://developer.android.com/games/sdk/oboe/low-latency-audio
  Device xruns and callback deadlines must be independently measured.
- Antares best practices:
  https://help.antarestech.com/hc/en-us/articles/42858099043092-AutoTune-Best-Practices
  Vendor describes tracking sensitivity, fast retune and quality-mode tradeoffs.
- DAFx time domain effects:
  https://www.dafx.de/DAFX_Book_Page/chapter7.html
  Pitch-synchronous overlap-add is an alternative worth an independently
  benchmarked research candidate; not part of this shipping patch.

No third-party implementation is copied. Current patch keeps existing controls
and zero algorithmic PDC, uses prior residual/gain state so a newly arriving
impulse is not immediately cancelled, and retires harmonic tracks with a fade.

## Evidence and gates
- Initial synthetic sustained 252 Hz to C# major target tests passed; residual
  reduction relative to old implementation is not proof of real vocal quality.
- Extended run 45631004675 failed exit 22 (timeline/impulse/reset).
- Do not qualify the next build until the exact shipping replacement mode
  passes extended stress tests, including impulse transparency, bypass, note
  changes, allocation and CPU gates. Require real vocals and physical Android
  xruns before claiming click-free operation.
