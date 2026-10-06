# Trim / classic VU gain staging

Implemented in the official Apex workspace on 2026-10-05. Focused verification;
not a claim of complete application or microphone hardware certification.

## Findings

The previous trim needle read the last callback's block RMS, multiplied it by
sqrt(2), and smoothed it at an assumed 60 UI ticks per second. A UI tick could
miss intervening blocks; block lengths changed the instantaneous RMS window;
different display schedules could give different movements. It was a calibrated
RMS approximation, not an audio-clock classic VU detector.

LiveInputMonitorEngine also fed a silent meter block for ordinary, unarmed
tracks before the graph published their real post-trim input. Last-block RMS
hid this double update. A stateful detector instead advances twice and reads
low. The preview now publishes only armed, unmonitored, valid hardware input;
the graph remains the single publisher for playback and monitored tracks.

## Implementation and signal contract

- `Source/AnalogVuMeterCore/ClassicVuDetectorCore.h`: full-wave linear
  rectification, sine calibration (pi/2), and a double-precision second-order
  movement. Its parameters produce approximately 99% deflection at 300 ms and
  1.25% mechanical overshoot. Coefficients are prepared outside processing.
- `Source/InputMonitorCore/InputMeterCore.h`: processes every audio sample,
  publishes current VU and holds per-mode VU maxima. Sample peak and block RMS
  remain separate measurements. UI readers use atomics and never advance the
  detector. No audio buffer is modified by metering; no allocation, GUI, locks,
  disk access or plugin lifecycle work is introduced by the detector.
- `Source/InputMonitorCore/LiveInputMonitorEngine.h`: fixes duplicate silent
  meter updates. It retains the armed/monitor-off post-trim preview and does
  not alter recording samples or monitoring output.
- `Source/AnalogVuMeterCore/AnalogVuMeterComponent.h` and `CompactVuNeedle.h`:
  classic VU consumes the shared measured value without another UI smoothing
  stage. Existing hidden compact widgets remain hidden; no removed UI is
  re-enabled. Mixer and arranger Trim entry points still use the shared panel.
- `Source/UICore/InputTrimFloatingPanel.h`: current VU, maximum VU, sample peak
  dBFS and Trim dB have distinct readouts. Clicking the meter, maximum readout,
  or Reset meters clears held readings without resetting gain or the current
  needle. The OL latch means sample clipping at 0 dBFS; the red VU range means
  above the nominal reference, which need not be digital clipping. The channel
  selector advances exactly once per click (L+R, L, R, MAX); restoring its lit
  indicator no longer fires a second advance. Its label fits without ellipsis,
  and values rounding to zero display 0.0 rather than -0.0.
- `Source/TrackCore/Track.h` / `Track.cpp`: `setTrimVuReferenceDb` and
  `setTrimVuChannelMode` notify project listeners. Track XML stores
  `trimVuReferenceDb` and `trimVuChannelMode`; old projects default to -18 dBFS
  and stereo averaging. Reference selection does not alter audio gain.

The existing Trim stage remains post-source/pre-FX/pre-fader. Meter calibration
defaults to **0 VU for a steady 1 kHz sine with a -18 dBFS sample peak**. Stereo
mode averages the independently detected L/R levels; L, R and MAX are also
available. It is not a phase-aware mono sum. The separate peak readout checks
both channels so selecting a quiet channel cannot hide clipping on the other.

## Reference comparison

[TBProAudio's mvMeter2 manual](https://www.tbproaudio.de/assets/content/manuals/mvMeter2%20manual.pdf)
distinguishes classic VU from RMS, documents stereo averaging for its single
VU display, adjustable reference, current/maximum readings and the -18 dBFS
VU preset. Those behaviors guide this Trim panel. This change does not claim
to clone its full plugin, its PPM/LUFS modes or its automatic gain matching.

The movement coefficients follow Appendix A of
[Lobdell and Allen, JASA 121 (2007)](https://doi.org/10.1121/1.2387130):
damping 0.81272 and natural angular frequency 13.512. Apex uses a linear
full-wave rectifier; it does not claim to reproduce every nonlinear rectifier
characteristic of a physical instrument or bit-match mvMeter2. VU timing and
the distinction from peak are also described in
[ITU-R BS.645-2](https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.645-2-199203-I%21%21PDF-E.pdf).

## Verification

Tests live in `Tests/Source/Diagnostics/MixerResponsiveTests.cpp` under
`trim.classic-vu.v1`. Both Debug and Release pass:

- Steady sine calibration, 300 ms rise/release, overshoot and silence at
  44.1, 48, 96 and 192 kHz.
- Equal-peak square versus sine response (distinguishes rectified-average VU
  from RMS); a 10 ms burst remains visible to sample peak but does not read as
  a sustained VU tone.
- Current and held VU invariance across blocks 17, 32, 64, 128, 256, 480, 512,
  1024 and 2048, at both 44.1 and 48 kHz.
- Stereo averaging, unilateral input, opposite-polarity stereo, independent
  peak safety, reset and shared large/compact readings regardless of UI ticks.
- Track XML round trip for calibration, channel and Trim; legacy defaults.
- Single meter ownership and finite recovery after NaN/Infinity samples.
- Actual panel channel-selector transitions, including returning to L+R,
  preserve Trim gain.

Additional focused regressions pass in **each** configuration:

| Suite | Result groups | Passed assertions | Failed |
| --- | ---: | ---: | ---: |
| `mixer.clip-input-meter-signal.v1` | 8 | 36 | 0 |
| `dsp.block-invariance.v1` | 14 | 25,417 | 0 |
| `recording.writer-integrity.v1` | 13 | 66 | 0 |

See `trim-vu-{classic,signal,dsp,recording}-{Debug,Release}-2026-10-05.json`.
An offscreen image of the real JUCE panel is in
`trim-vu-panel-2026-10-05.png`; three screenshot checks are additional to the
94 core VU assertions (97 total, nine result groups). This is a rendered component check, not an automated
desktop session with the user's song.

Application and test builds passed for Release and Debug. Debug application
linking initially rejected a generated Main.obj (LNK1163); preserving and
regenerating that single object resolved the build without source changes.
The repository policy check also passed. Builds use the existing unsigned
mode. The broader previously outstanding dependency/full-suite gates are
not reclassified as passing by this focused work.

Release executable: `Builds/VisualStudio2026/x64/Release/App/DAW_Core.exe`.
SHA-256: `8E6591671A5605505DF110CFCE6FF944F389E36DC0787C89619C98BF2D7F10BC`.
The corrected WASAPI source remains byte-identical to the prior 2048 fix:
`B0D577BCC9D4F03721A0AF594753EDF739A39048E43E38BF24B1EC4DA7A77696`.

## Use

Open the track's Trim panel, play a representative louder section and adjust
the Trim knob around the chosen nominal VU reference. Use PEAK dBFS separately
to watch transient headroom. Reset meters before measuring another passage.
VU and peak should normally show different numbers. Neither requires every
transient or every track to hit one universal peak target.
