# APEX Tune Live — offline singer audition (non-shipping research)

This workflow creates **audible WAV outputs** for testing the actual APEX Tune Live 40ms production DSP against a separate, experimental causal time-domain prototype. It does **not** require a microphone inside GitHub Actions and never sends your personal singing recordings to any service. The synthetic CI fixture is an automatically generated harmonic/vowel-like tone **not a real singer**.

## Local Windows procedure

1. Open PR #85 → **Actions** → **Tune Live portable DSP regression** → the most recent **successful** run → **Artifacts** → `tune-live-offline-research-windows`. The artifact contains a Windows console utility, `tune_live_audition.exe`, and four approximately 2-second synthetic demonstration files. The artifact is temporary; keep it only for local evaluation. The executable is an unsigned CI build, not an APEX installer or a general plugin release.
2. Export an **unprocessed dry vocal** from APEX (or another DAW), preferably mono 48 kHz, in standard **PCM 24-bit WAV**. The utility also supports PCM 16-bit, PCM 32-bit integer and IEEE float32 WAV, 1 or 2 channels, 8–96 kHz, up to two minutes. WAV Extensible/RF64 and MP3 are **not** supported. Take care that Apollo hardware Auto-Tune or other preexisting inserts are disabled on the recording; otherwise this cannot isolate APEX's tuning quality.
3. In a Windows Terminal opened in the same directory:
   ```powershell
   .\tune_live_audition.exe --input "vocal.wav" --prefix "vocal_test"
   ```
   This produces:
   - `vocal_test_dry.wav` — decoded input without pitch correction.
   - `vocal_test_hq40.wav` — processed using the **actual shipping DspCore** with HighQuality40ms profile; chromatic hard tuning, A440, 100% wet, fastest retune, Humanize/FlexTune zero.
   - `vocal_test_causal_research.wav` — processed using isolated streaming YIN → scale policy → experimental period-guided shifter. The 200Hz+ fast early-onset seed is **OFF** by default, and this path is **not** registered in APEX or certified for release.
4. Import the 3 WAVs into separate APEX tracks. **IMPORTANT:** their temporal responses are different and the prototype's delay varies with note period. Manually align before any level-matched blind comparison; do **not** simply toggle between files to judge quality or claim a fixed plugin PDC. All files are 24-bit PCM with identical frame counts; the output reported samples at/above 0dBFS are **clipped at export**, so a clip report above 0 means the A/B is invalid until levels are reduced.
5. Assess vowels (AH/EE/OH), attacks, slides, vibrato, sustains, consonants (S/F/T), deep notes, breathiness, falsetto and unnatural robotic or watery/grain artifacts. Note at which word/time/note issues occur. Report the APEX/plugin latency *separately* from observed detection onset. No **Antares-equivalence** claim is justified without human singing tests and time-aligned level-matched blind comparison.

## Safety and privacy

- The utility only reads the named input WAV and writes three WAV files to the named local prefix; it has **no network access or recording upload**. Do not commit real, identifiable vocal takes into a public repository or GitHub Actions.
- CI runs `--self-test tune_live_audition`, writes an artificial 2-second source WAV, creates dry/HQ40/causal WAVs and reads them back. CI then runs the same WAV as `--input` to exercise the actual file reader and CLI. This guarantees portable file-format paths and test coverage, **not** subjective quality.
- The tester is designed for trusted local WAVs; avoid untrusted binary files. It rejects malformed RIFF chunk boundaries, unsupported formats, nonfinite float input and oversized files.
- The utility does not measure Apollo Solo mic-to-ear latency, does not provide low-latency direct monitoring and is **not** a realtime audio plugin. The shipping 40ms mode is preserved unchanged.

## Hard promotion blockers

A singing audition is one prerequisite, not a release certificate. Before exposing the causal backend to users: solve fixed PDC, wet/dry alignment, note-change click/transient behavior, missing-fundamental octave ambiguity, multi-vocal CPU load, real ASIO Apollo Solo USB3 monitoring at 48k/128, and an independent level-matched voice-quality evaluation.
