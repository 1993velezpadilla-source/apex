# C4 Phase 5 Render Pack - MANIFEST (measured facts only)

Generated deterministically by `C4.RenderPack` (APEX.C4 suite).

Source: C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core\stftPitchShift-main\examples\voice.wav
Format: 44100 Hz, 1 ch, 274432 frames
Block size: 512 (bounded chunking; block-size independent by contract)

Processor: APEX C4 production profile (Phase 5 frozen),
rendered at the source rate.
Production profile: BLOOM default 4.0; coupling 0.50/0.32/1.1/1.6;
color W/S/B/O 0.055/0.055/0.050/0.028, evenW 0.75/0.65/0.50/0.40,
activationExp 0.85, cutSuppression 1.0, bloomLaw 0.85, OS 2x;
HALO softness 0.5, amount unity (see C4_PHASE5_PERCEPTUAL_TUNING.md).

Settings (BOOM): WEIGHT +3 dB @100 Hz, SCULPT +2 dB @400 Hz,
BITE +2 dB @2.5 kHz, OPEN +3 dB @10 kHz (bell modes).
No limiter, no compressor, no external saturation.

Files:
- 01_REF_bypass.wav             C4 bypassed (reference)
- 02_C4_BLOOM0_FLAT.wav         BLOOM 0, all bands 0 dB
- 03_C4_BOOM_BLOOM0.wav         BOOM settings, BLOOM 0 (linear contour)
- 04_C4_BOOM_BLOOM4.wav         BOOM settings, BLOOM 4.0 (production default)
- 05_C4_BOOM_BLOOM4_MATCHED.wav BOOM settings, BLOOM 4.0, output trim -1.559 dB (level-matched to REF)
- 06_C4_BOOM_BLOOM10.wav        BOOM settings, BLOOM 10 (high reference)

MEASURED (this run):
- 04 vs 01 RMS delta: 1.56 dB
- 04 vs 01 peak delta: 1.65 dB
- 05 vs 01 RMS delta: -0.00 dB (level-match compensation = -1.559 dB)
- 05 vs 01 peak delta: 0.09 dB
- Crest factor (peak - RMS): 01=16.67 dB, 04=16.76 dB, 05=16.76 dB
- File hashes: not available in the test project (juce_cryptography is
  not part of APEXTests); file sizes are verifiable on disk.

Known limitation: this is the ONLY real musical source in the
repository (a vocal example). No bass/drums/bus/mix corpus claims
are made. The listening pass criterion (level-matched bypass feels
smaller) is a human judgment — not asserted by this suite.
