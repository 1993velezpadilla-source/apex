# Mixer Ø / polarity — engineering and acceptance

## What is implemented on PR #64
- A dedicated **Ø** toggle appears on regular tracks and bus/folder-bus mixer
  strips, alongside the automation-mode control. It is not offered on Master.
- A track-local atomic polarity state is persisted to the project state as
  `polarityInverted`. Legacy projects default to OFF.
- Realtime `TrackPolarityCore::apply` negates each audio sample on each existing
  channel if enabled, after the track/bus plugin chain, before pre- and
  post-fader sends, the post-fader recording tap, and output routing.
- The flat graph-free fallback invokes the same processor.
- A polarity change preserves timeline/clip positions and does not time-stretch,
  move, offset, or change the stored source waveform.

## Terminology
A polarity inversion multiplies an audio waveform by -1. It is **not**
a frequency-dependent phase rotation or a time-alignment operation. It can
cancel an identical, otherwise unaltered parallel signal; it cannot generally
repair a multi-mic timing offset by itself.

## Deterministic regressions added
- `Tests/Source/Smoke/RunnerSmokeTests.cpp`: bypass transparency, stereo
  inversion/reversibility, mono, matched-signal cancellation, persistence,
  legacy OFF fallback, and the Master guard.
- `Tests/Source/QuickTrack/TrackCreationInputTests.cpp`: actual
  `AudioEngine::process` live mono input through the routed stereo mix,
  verifying normal → inverted → restored output after settling.

## Remaining release gates (not certified by repository policy CI)
1. Windows x64 Debug and Release app + test binary build.
2. Focused real-engine test run with zero failures, including the tests above.
3. Manual mixer hit target and feedback under small strip sizes/high-DPI.
4. ASIO live input, monitoring and actual audible A/B click behavior at
   48 kHz / 128 frames.
5. Regular track, mono, stereo, folder bus, aux/bus, sends, sidechain routing,
   post-fader recording, offline stem export, project close/reopen,
   audio-loop playback, and plugin-chain/PDC integration.
6. If toggling Ø mid-signal clicks, add a dedicated transition de-click method
   without RT allocation and then rerun null/cancellation regressions.

## Further professional phase tools: **not implemented**
- Manual **sample delay** per route to align earlier microphone channels
  with later arrivals (with validated PDC and offline-render parity).
- Phase-correlation metering and explicit mono compatibility preview.
- Automatic alignment **suggestions** from time-windowed cross-correlation,
  with bounded search, confidence scores and manual audition/approval.
- Frequency-dependent all-pass phase rotation is a separate DSP design.

Avoid pretending the polarity toggle itself performs any of these operations.

## Research sources
- Sound On Sound, Mike Senior, *Phase Demystified* (2008):
  https://www.soundonsound.com/techniques/phase-demystified
- Sound On Sound, Sam Inglis, *Making Multi-mic Recordings Work* (2020):
  https://www.soundonsound.com/techniques/making-multi-mic-recordings-work
