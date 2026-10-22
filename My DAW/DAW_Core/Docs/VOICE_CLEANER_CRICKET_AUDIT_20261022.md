# APEX Voice Cleaner — cricket-noise research and engineering audit (2026-10-22)

## Scope and honesty
APEX is a Windows/JUCE DAW with an *offline* per-clip Voice Cleaner already integrated. The previous engine used a fixed 0.68 amount, short-time 2048-point spectral reduction, an 18% quiet-frame noise profile and a 0.24 spectral floor. The original WAVs were preserved and processed versions published through immutable AudioFileManager snapshots plus unified global Undo/Redo. We audited that implementation before adding controls.

**APEX does not include BandLab's or Waves' proprietary neural networks.** The improvement in this PR is adaptive, noise-profile-driven *spectral DSP*, not an AI-model integration. Neural-quality parity must be demonstrated by listening and objective validation on the user's own field recordings before any such claim.

## Audited approaches
| Product / project | Confirmed method or relevant feature | Valid APEX design takeaway |
|---|---|---|
| Waves Clarity Vx v2 | One main amount control; multiple specialized neural network profiles; offline and insert modes; Broad ECO aimed at *spoken* dialogue, not singing. | One prominent Amount knob; avoid algorithm profiles specialized for speech when cleaning sung vocals. Do not copy the network or UI art. |
| BandLab Voice Cleaner | Proprietary AI Noise Remover, DeReverb and AutoEQ with selectable combination; BandLab Denoise separately applies direct region cleanup. | Separate noise cleanup from coloration and room correction; clip-specific action, apply/cancel and undo. No official assertion that BandLab trains specifically on crickets. |
| iZotope RX 12 | Spectral De-noise learns stationary tonal/broadband profiles with adaptive options; Dialogue Isolate helps non-stationary interference. | Detect high-Q cricket frequency ridges, not just broadband level; protect sustained vocal harmonics. |
| Steinberg SpectraLayers 12 | Improved Unmix Noisy Speech and Voice DeNoise; spectral unmixing useful for stronger, irregular background. | Model-based separation should remain a future optional HQ strategy, not be faked with a gate. |
| Adobe Enhance Speech | Simple voice and background controls but network/hosted constraints and speech-oriented processing. | Simple UX, do not assume it preserves sung timbre. |
| FL Studio Edison | Noise Removal with acquired noise profile and Amount; Vocal Denoise/Isolator. Official docs caution that transient/variable bird-like sounds are not suitable for stationary cleanup and recommend Removed Noise audition. | Dedicated cricket model or user-marked noise examples needed when chirps drift; roadmap for "Listen Removed" A/B. |
| REAPER ReaFIR | Subtract mode learns an explicit background noise profile. | Noise learning should be controllable and reviewed; higher reduction increases artifacts. |
| Logic Pro for iPad | Noise Gate threshold, attack, hold and release reduces sounds *between* words, not chirps simultaneous with singing. | Do not use a gate as the entire cricket solution; keep vocal tails. |
| DeepFilterNet (research candidate) | 48-kHz full-band speech enhancement; code dual MIT/Apache 2.0. Offline 48-kHz model path needs Windows packaging, deployment and validation. | Candidate for separately packaged/optional AI HQ model, after licensing/model-weight audit and sung-vocal/cricket comparisons; **not bundled** here. |
| RNNoise (research candidate) | BSD-3-Clause, RNN noise reduction (48 kHz, frame based). | CPU-light speech-oriented alternative; needs careful sung-vocal testing and multi-sample-rate adapters, not wired into production. |

## APEX phase-1 implementation
- Tiny 210 x 245 native touch-safe CallOutBox launches on **Voice Cleaner | Noise Reduction**. No processing when opened/cancelled.
- 0–100 **Amount** knob; **Focus** Auto / Crickets / Hiss-Room; **Protect vocal detail** on by default; Apply and Cancel only.
- Apply targets previously selected clip(s) using the existing snapshot-safe offline transaction and never touches the realtime audio callback.
- Tonal cricket mode derives a spectral ridge confidence map only from quiet learn frames above 1.1 kHz and emphasizes attenuation in those noise-dominated bins; soft mask floors, frame smoothing and voice-detail preservation avoid hard gating.
- Source WAV never overwritten. Batch remains all-or-nothing and undoable. 5-minute-per-source safety cap currently retained. This is **not** an APEX real-time native insert or neural model yet.

## Release gates / conservative listening gauntlet
1. **Compilation:** Windows Release + Debug, Touch portable and plug-in-host regressions green. Verify JUCE CallOutBox on Lenovo.
2. **Synthetic:** bit-identical 0% bypass; two simultaneous narrow cricket tones mixed under vowel (220–300 Hz); no NaNs; voice energy retained; quiet-gap attenuation significant.
3. **Field recordings:** actual Puerto Rico cricket bed, quiet-only segment, whispered words, consonants /s/ /f/ /t/, sung high notes, vibrato, falsetto, breaths and harmonies. Listen especially to overlap where crickets are still sounding while singing. No blanket notch of the 3–8 kHz vocal presence range.
4. **Compare fair:** Match loudness of dry, Clarity Vx, BandLab processed and APEX output. Blind subjective artifacts (lisping, phasey metallic tone, missed syllables), SNR improvement in quiet segments, noise/voice leakage ratio, clipping and spectral artifacts. Distinguish speech vs singing.
5. **Regression:** Undo/redo 5–50 runs, 48k/44.1k, mono/stereo, missing files, cancel mid-selection, not blocking audio thread, source not overwritten, memory/disk bounds, multiselect and save/reopen.
6. **Release criteria:** Never claim "same/better than Clarity" or "BandLab AI" without actual scored comparison. Preserve Waves VST3 as an external user option if APEX cannot meet quality.

## Source material (read 2026-10-22)
- Waves Clarity Vx official user guide: https://www.waves.com/1lib/pdf/plugins/clarity-vx-v2.pdf
- BandLab Voice Cleaner: https://help.bandlab.com/hc/en-us/articles/37657276691097-Remove-Noise-with-Voice-Cleaner
- BandLab Denoise: https://help.bandlab.com/hc/en-us/articles/900003878206-Remove-background-noise-with-Denoise
- iZotope RX features: https://www.izotope.com/en/products/rx/features
- Steinberg SpectraLayers 12 release: https://ocl-steinberg-live.steinberg.net/_storage/asset/696230/storage/master/Press%20Release%20-%202025-07-02%20-%20SpectraLayers%2012-%20EN.pdf
- DeepFilterNet: https://github.com/Rikorose/DeepFilterNet
- RNNoise: https://github.com/xiph/rnnoise
- FL Studio Edison Noise Removal: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/editortool_clean.htm
- REAPER ReaFIR guide: https://reaper.fm/guides/ReaEffectsGuide.pdf
- Logic for iPad Noise Gate: https://support.apple.com/en-gb/guide/logicpro-ipad/lpip6c3a00ab/3.3/ipados/26
