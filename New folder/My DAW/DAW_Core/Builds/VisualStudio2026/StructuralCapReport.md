# Structural Cap Report

## Summary
- Total suspects found: 24
- BLOCKING caps: 2
- LIKELY caps: 5
- POSSIBLE caps: 12
- INFORMATIONAL suspects: 5
- The single most likely root cause is: normal transport playback does not call `ClipPitchRenderPathCore` for the pitch-active Vocal/PitchOnly path; it routes through `DemonVoiceEngineCore` / `ClipIndependentPitchCore` instead (`..\..\Source\AudioEngineCore\AudioEngine.h:1141`).

> Phase 1 only. No DSP or cap-removal code was written.

## Suspect Category 1 — Parameter clamps on pitch

### Hit 1
- File: `..\..\Source\ClipCore\Clip.h:149`
- Code:
```cpp
149:     // Pitch shift in semitones (-36..+24)
150:     float getPitch() const { return pitch_; }
151:     void  setPitch(float semitones)
152:     {
153:         const char* source = PitchWriteAudit::currentSource;
154:         FORENSIC_LOG("[FORENSIC TRAP setPitch] value=" << juce::String(semitones) << "\n" << juce::SystemStats::getStackBacktrace());
155:         DBG("[PITCH WRITE] source=" << source << " value=" << semitones);
156:         DBG("[DEBUG FASE 1] Pitch semitones input: " << semitones);
157: 
158:         const float clamped = juce::jlimit(-36.f, 24.f, semitones);
159: 
160:         if (PitchWriteAudit::playbackActive.load(std::memory_order_relaxed)
```
- On active playback path: YES. `AudioEngine::renderClip()` reads `ac->getPitch()` at `AudioEngine.h:971`.
- Severity: LIKELY. Negative pitch reaches `-36`, but positive range is capped at `+24`, and this is not the single-source `PitchScaleMathCore::clampPitch` required by the spec.
- Action: WIDEN / centralize through `PitchScaleMathCore::clampPitch`.

### Hit 2
- File: `ArrangementEditor\DemonVoiceEngineCore.h:62`
- Code:
```cpp
62:     static DemonVoiceTargets map(const DemonVoiceRenderParams& params) noexcept
63:     {
64:         const auto& p = profile(params.preset);
65:         const float macro = juce::jlimit(0.f, 1.f, params.demonAmount);
66: 
67:         DemonVoiceTargets t;
68:         t.pitchSemitones = juce::jlimit(-36.f, 24.f,
69:             params.basePitchSemitones + p.pitchSemitones);
70:         t.formantSemitones = juce::jlimit(-12.f, 12.f,
71:             params.manualFormantSemitones + p.formantSemitones);
72:         t.drivePercent = juce::jlimit(0.f, 30.f,
```
- On active playback path: YES when `ultraDemonAmount > 0.0001f`; called from `AudioEngine.h:1296`.
- Severity: BLOCKING for any intended total demon pitch below `-36` or above `+24`; POSSIBLE for the reported `-36 st` cap because the macro profile forcibly saturates the combined pitch to `-36`.
- Action: WIDEN and route through `PitchScaleMathCore::clampPitch`.

### Hit 3
- File: `ArrangementEditor\DemonVoiceEngineCore.h:94`
- Code:
```cpp
94:     void process(const float* const* input, float* const* output,
95:                  int channels, int numSamples, float pitchSemitones, float fineTuneCents)
96:     {
97:         DBG("[DEBUG FASE 2] DemonPitchCore input pitchSemitones: " << pitchSemitones);
98:         ClipPitchProcessParams p;
99:         p.pitchSemitones = juce::jlimit(-2400.0, 24.0, (double) pitchSemitones);
100:         DBG("[DEBUG FASE 2] DemonPitchCore clamped pitchSemitones: " << p.pitchSemitones);
101:         p.fineTuneCents = fineTuneCents;
102:         p.formantSemitones = 0.0;
103:         p.preserveFormants = false; // DEBUG FASE 3: disable formant lock
```
- On active playback path: YES when `ultraDemonAmount > 0.0001f`.
- Severity: POSSIBLE. It allows extreme negative values but caps positive at `+24`, inconsistent with `-48..+48` internal headroom.
- Action: WIDEN / centralize.

### Hit 4
- File: `ArrangementEditor\ClipIndependentPitchCore.h:102`
- Code:
```cpp
102:         const double ratio = juce::jlimit(0.03125, 4.0, params.pitchRatio());
103:         if (std::abs(ratio - lastRatio_) > 1e-6) {
104:             updateAntiAliasCoeffs(ratio);
105:             lastRatio_ = ratio;
106:         }
107: 
108:         const double formantSemitones = effectiveFormantSemitones(params);
```
- On active playback path: YES. Used by `ClipIndependentPitchCore::processBlock()` from `AudioEngine.h:1311` and by `DemonPitchCore`.
- Severity: POSSIBLE. It permits `-60 st` but only `+24 st`. Not a `-36 st` floor, but still a live pitch-ratio cap.
- Action: WIDEN to `0.0625..16.0` for `-48..+48`, or use centralized ratio math.

### Hit 5
- File: `ArrangementEditor\PitchScaleMathCore.h:42`
- Code:
```cpp
42:     static inline double clampPitch(double st, bool allowLegacyHeadroom = true) noexcept
43:     {
44:         st = sanitizeNaN(st);
45:         return std::clamp(st,
46:                           allowLegacyHeadroom ? kInternalMinSemitones : kUiMinSemitones,
47:                           allowLegacyHeadroom ? kInternalMaxSemitones : kUiMaxSemitones);
48:     }
```
- On active playback path: UNKNOWN/partial. Used by the new unified path, but the main pitch-active playback path bypasses that path.
- Severity: INFORMATIONAL. Correct canonical clamp (`-48..+48` internal, `-36..+36` UI).
- Action: KEEP and make it the single source of truth.

### Hit 6
- File: `ArrangementEditor\UnifiedPitchKnobMappingCore.h:16`
- Code:
```cpp
16:     static double knobPositionToSemitones(double position) noexcept
17:     {
18:         position = std::clamp(PitchScaleMathCore::sanitizeNaN(position, 0.5), 0.0, 1.0);
19:         const double signedNorm = (position - 0.5) * 2.0;
20:         const double shaped = std::copysign(std::pow(std::abs(signedNorm), 1.55), signedNorm);
21:         return shaped * PitchScaleMathCore::kUiMaxSemitones;
22:     }
```
- On active playback path: YES upstream UI/value mapping.
- Severity: INFORMATIONAL. Correct UI range to `-36..+36`.
- Action: KEEP.

### Hit 7
- File: `ArrangementEditor\PitchValueCore.h:63`
- Code:
```cpp
63:     static double clampAudibleSemitones(double semitones)
64:     {
65:         return std::max(TimePitchConstants::kPitchAudibleMinSemitones,
66:                std::min(TimePitchConstants::kPitchAudibleMaxSemitones, semitones));
67:     }
68: 
69:     static double pitchToAudibleRatio(double semitones, double cents)
72:         return PitchScaleMathCore::semitonesToRatio(total);
```
- On active playback path: YES in `TimePitchDSPCore::renderResample()` and `PhaseVocoderStretchCore::setState()`; not used by the independent pitch path.
- Severity: INFORMATIONAL. Correct `-48..+48` audible clamp.
- Action: KEEP.

## Suspect Category 2 — Filters in the source chain

### Hit 1
- File: `ArrangementEditor\DarkVoiceBodyCore.h:57`
- Code:
```cpp
51:         const double bodyCenterHz = bassHeavy
52:             ? PitchScaleMathCore::lerp(185.0, 245.0, snapshot.darkIntensity)
53:             : PitchScaleMathCore::lerp(125.0, 190.0, snapshot.darkIntensity);
54:         const double bodyGainDb = bassHeavy
55:             ? PitchScaleMathCore::lerp(0.0, 4.5, snapshot.darkIntensity)
56:             : PitchScaleMathCore::lerp(0.0, 9.0, snapshot.darkIntensity);
57:         const double safetyHpfHz = bassHeavy ? 72.0 : 58.0;
58:         const float bodyGain = static_cast<float>(std::pow(10.0, bodyGainDb / 20.0) - 1.0);
59:         const float shelfCut = static_cast<float>(PitchScaleMathCore::lerp(1.0, std::pow(10.0, -5.5 / 20.0), snapshot.darkIntensity));
60:         const float drive = static_cast<float>(snapshot.darkIntensity * 0.48);
```
- On active playback path: NO for current normal transport pitch path because `ClipPitchRenderPathCore` is bypassed. YES if `TimePitchDSPCore::renderClipSegment()` receives pitch-active requests.
- Severity: LIKELY in the new unified path. The effective high-pass is `58/72 Hz`, above the `≤20 Hz` deep-pitch requirement, and it removes the `14/28/42/55 Hz` components that define `-36 st` voice.
- Action: LOWER to `≤20 Hz`, move later, or bypass when `snapshot.darkIntensity > 0.05`.

### Hit 2
- File: `ArrangementEditor\ClipIndependentPitchCore.h:261`
- Code:
```cpp
261:     void updateAntiAliasCoeffs(double ratio) {
262:         const double cutoff = juce::jlimit(2000.0, sampleRate_ * 0.45, (sampleRate_ * 0.5) / ratio * 0.9);
263:         const double w = 2.0 * juce::MathConstants<double>::pi * cutoff / sampleRate_;
264:         const double cosw = std::cos(w);
265:         const double sinw = std::sin(w);
266:         const double Q = 0.707;
```
- On active playback path: YES for `ClipIndependentPitchCore` and `DemonVoiceEngineCore` pitch.
- Severity: INFORMATIONAL for down-pitch. It only filters when `ratio > 1.0001` in lines `117-124`, so it is not the `-36 st` low-frequency cap.
- Action: KEEP.

### Hit 3
- File: `ArrangementEditor\DemonSubCore` inside `DemonVoiceEngineCore.h:352`
- Code:
```cpp
344:         ClipPitchProcessParams p;
345:         p.pitchSemitones = -12.0;
346:         p.fineTuneCents = 0.0;
347:         p.formantSemitones = 0.0;
348:         p.preserveFormants = true;
349:         p.channels = chans;
350:         subPitch_.processBlock(inPtrs, subPtrs, chans, numSamples, p);
351: 
352:         const float target = juce::jlimit(0.f, 20.f, subPercent) / 100.f;
353:         const float smoothing = smoothingCoefficient(55.f);
354:         const float lpCoeff = onePoleCoeff(180.f);
```
- On active playback path: YES when `ultraDemonAmount > 0.0001f`.
- Severity: POSSIBLE. It low-passes generated sub content at `180 Hz` and adds it; not a high-pass cap, but changes perceived low-end.
- Action: KEEP unless subjective masking is proven.

## Suspect Category 3 — Third-party library limits

### Hit 1
- File: `ArrangementEditor\RubberBandSelectCore.h:1`
- Code:
```cpp
1: // ===========================================================================
2: // RubberBandSelectCore.h
3: // Selection rectangle drawn during drag (Select tool).
4: // Selects all clips intersecting the rectangle on mouse up.
5: // ===========================================================================
6: #pragma once
```
- On active playback path: NO. This is UI rubber-band selection, not RubberBand Audio.
- Severity: INFORMATIONAL.
- Action: KEEP.

### Hit 2
- File: `ArrangementEditor\PhaseVocoderStretchCore.h:3`
- Code:
```cpp
3: // Motor WSOLA real para Stretch (Mode 1), PitchOnly (Mode 2), Vocal (Mode 3).
4: //
5: // ALGORITMO REAL (WSOLA — Waveform Similarity Overlap-Add):
6: //   Mismo algoritmo base que SoundTouch / Reaper / muchos DAWs:
7: //     1. Divide source en frames solapados (Hann window).
8: //     2. Busca el frame fuente más similar al anterior (cross-correlation).
```
- On active playback path: YES only for fallback `TimePitchDSPCore` paths; NO for the dedicated independent pitch branch in `AudioEngine.h:1141`.
- Severity: POSSIBLE. Custom WSOLA has buffer sizing and pitch/stretch coupling risks, but no external RubberBand/SoundTouch hard limit was found.
- Action: KEEP for stretch; do not use it as the extreme pitch path below `-22 st`.

### Hit 3
- File: `ArrangementEditor\WSOLAStretchCore.h:106`
- Code:
```cpp
103:     // -----------------------------------------------------------------------
104:     // setState — desde UI thread (o prepare del DSP), sin lock.
105:     // -----------------------------------------------------------------------
106:     void setStretchRatio(double ratio)
107:     {
108:         stretchRatio_.store(juce::jlimit(0.1, 8.0, ratio));
109:     }
110: 
111:     double getStretchRatio() const { return stretchRatio_.load(); }
```
- On active playback path: YES for stretch/fallback paths.
- Severity: POSSIBLE for stretch extremes, not direct `-36 st` pitch.
- Action: KEEP for stretch unless v4 requires wider stretch.

## Suspect Category 4 — Two playback paths

### Hit 1
- File: `..\..\Source\AudioEngineCore\AudioEngine.h:1017`
- Code:
```cpp
1017:                 if (tpMode == DAW::TimePitchModeIds::Resample)
1018:                 {
1019:                     // ═══════════════════════════════════════════════════════════════
1020:                     // FORENSIC AUDIT — Resample/Tape Path Counter
1021:                     // ═══════════════════════════════════════════════════════════════
1022:                     ++PitchAuditCore::resampleTapeHits;
1023: 
1024:                     // ── MODE 0: Resample (inline, fastest path) ────────────
1025:                     // pitch and duration change together (tape/vinyl/DJ)
1026:                     const double readRate = pitchRatio / (double)clipStretch;
```
- On active playback path: YES when clip mode is Resample.
- Severity: POSSIBLE. This bypasses `ClipPitchRenderPathCore` and uses inline linear resampling.
- Action: Bypass only if Resample mode intentionally remains legacy; otherwise route through `ClipPitchRenderPathCore`.

### Hit 2
- File: `..\..\Source\AudioEngineCore\AudioEngine.h:1141`
- Code:
```cpp
1119:                     // useIndependentPitchPath: true for PitchOnly and StretchThenPitch
1120:                     // (both need the independent pitch core in their final step).
1121:                     const bool useIndependentPitchPath = (renderPath == RenderPath::PitchOnly
1122:                                                        || renderPath == RenderPath::StretchThenPitch);
1123: 
1124:                     static int renderLogCounter = 0;
...
1141:                     if (useIndependentPitchPath)
1142:                     {
1143:                         // ═══════════════════════════════════════════════════════════════
1144:                         // FORENSIC AUDIT — Independent Pitch Path Counter
1145:                         // ═══════════════════════════════════════════════════════════════
1146:                         ++PitchAuditCore::independentPitchPathHits;
```
- On active playback path: YES for default `Vocal` mode because `DefaultUserMode = Vocal` and pitch active selects `PitchOnly` path.
- Severity: BLOCKING. This branch never calls `TimePitchDSPCore::renderClipSegment()` and never calls `ClipPitchRenderPathCore`; it calls `DemonVoiceEngineCore` or `ClipIndependentPitchCore` directly.
- Action: Bypass this alternate path and route every pitch-active clip through `ClipPitchRenderPathCore`.

### Hit 3
- File: `..\..\Source\AudioEngineCore\AudioEngine.h:1371`
- Code:
```cpp
1371:                     // ═══════════════════════════════════════════════════════════════
1372:                     // FORENSIC AUDIT — DSP Core Path Counter
1373:                     // StretchOnly falls here (pitch≈0, dspCore handles duration change).
1374:                     // Normal falls here for all other non-independent-mode cases.
1375:                     // ═══════════════════════════════════════════════════════════════
1376:                     if (renderPath == RenderPath::StretchOnly)
1377:                         ++PitchAuditCore::stretchOnlyPathHits;
1378:                     else
1379:                         ++PitchAuditCore::normalPathHits;
1380: 
1381:                     // ──────────────────────────────────────────────────────────────
1382:                     // Fallback to existing TimePitchDSPCore for modes that need
```
- On active playback path: YES for stretch-only and non-independent modes.
- Severity: LIKELY. This is a second pitch/time path and can bypass the v4 engine for combined or non-default modes.
- Action: Route pitch-active cases through `ClipPitchRenderPathCore`.

### Hit 4
- File: `..\..\Source\AudioEngineCore\AudioEngine.h:1451`
- Code:
```cpp
1440:                     const bool isIdentityModernMode = (std::abs(clipPitch) < 0.0001f)
1441:                         && (std::abs(fineTuneCents) < 0.01f)
1442:                         && (std::abs(clipStretch - 1.0f) < 0.0001f)
1443:                         && (!ac || (std::abs(ac->getFormantSemitones()) < 0.0001f
1444:                                     && !ac->getPreserveFormants()));
...
1451:                         if (isIdentityModernMode)
1452:                         {
1453:                             auto* src = srcBuf->getReadPointer(sch);
1454:                             const auto directReadStart = sourceStartBound + engineClipOffset;
```
- On active playback path: YES for identity clips.
- Severity: POSSIBLE. This is a direct source path bypass, but it is guarded by pitch/stretch/formant identity checks.
- Action: KEEP if guard remains strict.

## Suspect Category 5 — Buffer length / sample-count mismatch

### Hit 1
- File: `ArrangementEditor\ClipPitchRenderPathCore.h:79`
- Code:
```cpp
73:     void render(const ClipPitchRenderPathRequest& req)
74:     {
75:         juce::ScopedNoDenormals noDenormals;
76:         if (req.outputData == nullptr || req.numOutputSamples <= 0)
77:             return;
78: 
79:         const int safeSamples = std::min(req.numOutputSamples, dryBuffer_.getNumSamples());
80:         if (safeSamples < req.numOutputSamples)
81:             juce::FloatVectorOperations::clear(req.outputData + safeSamples, req.numOutputSamples - safeSamples);
82: 
83:         setState(req.state);
```
- On active playback path: NO for the main pitch branch; YES only through `TimePitchDSPCore` pitch-active requests.
- Severity: POSSIBLE. If `req.numOutputSamples > maxBlockSize_`, processing silently truncates to `safeSamples` and clears the rest.
- Action: Add ring/resize safety; do not assume fixed max block.

### Hit 2
- File: `..\..\Source\AudioEngineCore\AudioEngine.h:97`
- Code:
```cpp
90:     void prepare(double sampleRate, int blockSize)
91:     {
92:         sampleRate_ = sampleRate;
93:         blockSize_  = blockSize;
94:         mixBuffer_.setSize(2, blockSize);
95:         trackBuffer_.setSize(2, blockSize);
96:         preFxBuffer_.setSize(2, blockSize);
97:         scratchBuf_.assign(blockSize * 8, 0.f); // 8x for stretch overrun
98: 
99:         // Per-clip independent pitch cores are created on demand in getOrCreateClipPitchCore()
100:         deepDemonDSP_.prepare(sampleRate, blockSize);
```
- On active playback path: YES for fallback `TimePitchDSPCore` output via `scratchBuf_`.
- Severity: INFORMATIONAL/POSSIBLE. Oversized scratch exists, but render requests still pass `count`, and consumers copy exactly `count`.
- Action: KEEP unless variable output count is introduced.

### Hit 3
- File: `ArrangementEditor\PhaseVocoderStretchCore.h:91`
- Code:
```cpp
91:             const int intermediateN = (int)std::ceil(dstNumSamples * pitch) + 4;
92:             const int resBufCap     = (int)resampleBuf_[chIdx].size();
93:             const int interN        = std::min(intermediateN, resBufCap);
94:             float* interBuf         = resampleBuf_[chIdx].data();
95: 
96:             wsola_.renderSegment(src, srcTotal, srcStart, srcEnd,
97:                                  interBuf, interN, channel);
```
- On active playback path: YES for fallback pitch-only/vocal in `TimePitchDSPCore`, but not for the independent pitch branch.
- Severity: POSSIBLE. `intermediateN` is clamped to a fixed buffer; high pitch or unusual block sizes can truncate intermediate data. For `-36 st`, `pitch=0.125`, so not the deep-pitch overrun case.
- Action: Use explicit available/produced counts or ring buffer if retained.

### Hit 4
- File: `ArrangementEditor\WSOLAStretchCore.h:158`
- Code:
```cpp
158:         const int accumCap = (int)outputAccum_[chIdx].size();
159: 
160:         // Generate only enough frames to satisfy this callback plus a small
161:         // lookahead reserve. Do not regenerate a full window every callback:
162:         // that over-consumes source audio, drains the stretch core after a few
163:         // seconds, and leaves silence in the output accumulator.
164:         const int targetFilled = std::min(accumCap - hopSyn_, dstNumSamples + windowSize_);
165:         while (accumFilled_[chIdx] < targetFilled)
```
- On active playback path: YES for stretch/fallback paths.
- Severity: POSSIBLE. This is a ring-like accumulator for stretch, but not used by the active independent pitch branch.
- Action: KEEP for stretch; add equivalent variable-rate safety around extreme pitch if unified path becomes active.

## Suspect Category 6 — Source data caches

### Hit 1
- File: `..\..\Source\AudioEngineCore\AudioFileManager.h:49`
- Code:
```cpp
49:         // Pre-compute raw waveform overview for the full file.
50:         // findMinAndMax runs here exactly once per audio load; drawWaveform never scans samples.
51:         {
52:             const int total = cached->buffer.getNumSamples();
53:             const int cols  = CachedAudio::kOverviewCols;
54:             cached->rawMin.assign(cols, 0.f);
55:             cached->rawMax.assign(cols, 0.f);
```
- On active playback path: NO. Playback uses `getBuffer()` at `AudioEngine.h:952`, not waveform overview data.
- Severity: INFORMATIONAL.
- Action: KEEP.

### Hit 2
- File: `ArrangementEditor\ClipRenderCore.cpp:55`
- Code:
```cpp
51:     void ClipRenderCore::refresh()
52:     {
53:         if (m_model)
54:         {
55:             int widthPx = (int)(m_model->length * m_zoom.getPixelsPerSecond());
56:             m_waveformCache->requestPeaks(m_model->sourcePath, widthPx);
57:         }
58:         repaint();
59:     }
```
- On active playback path: NO. UI cache only.
- Severity: INFORMATIONAL.
- Action: KEEP.

## Suspect Category 7 — Sample rate mismatches

### Hit 1
- File: `..\..\Source\AudioEngineCore\AudioEngine.h:955`
- Code:
```cpp
955:                 int srcTotal = srcBuf->getNumSamples();
956:                 int srcChans = srcBuf->getNumChannels();
957:                 double srcRate = audioFiles_->getSourceSampleRate(clip->getID());
958:                 double srcPerEngineSample = srcRate / sampleRate_;
959:                 float clipGain      = 1.0f;
960:                 float clipPitch     = 0.0f;    // semitones (legacy)
961:                 float clipStretch   = 1.0f;
```
- On active playback path: YES.
- Severity: LIKELY. `srcPerEngineSample` is computed but not used in source indexing in the independent pitch path (`AudioEngine.h:1257`) or fallback source bounds. A source file at a different sample rate than the session is read as if it matches the session rate.
- Action: Add proper source-rate conversion before pitch path; force upsample before extreme pitch.

### Hit 2
- File: `..\..\Source\AudioEngineCore\AudioFileManager.h:97`
- Code:
```cpp
97:     /** Get the source file's sample rate. */
98:     double getSourceSampleRate(const ClipID& clipId) const
99:     {
100:         auto it = cache_.find(clipId);
101:         return (it != cache_.end() && it->second) ? it->second->sampleRate : 44100.0;
102:     }
```
- On active playback path: YES as metadata.
- Severity: POSSIBLE. Metadata exists, but active playback currently ignores the conversion ratio.
- Action: Use this value in a resampling stage before pitch.

## Suspect Category 8 — Limiters / saturators / safety processors

### Hit 1
- File: `ArrangementEditor\DemonVoiceEngineCore.h:398`
- Code:
```cpp
398:     void process(const float* const* dry, float* const* wet, int numChannels, int numSamples, float wetMix) noexcept
399:     {
400:         const float wetGain = juce::jlimit(0.f, 1.f, wetMix);
401:         const float dryGain = 1.f - wetGain;
402: 
403:         for (int ch = 0; ch < numChannels; ++ch)
404:         {
405:             for (int s = 0; s < numSamples; ++s)
406:             {
407:                 const float wetSample = juce::jlimit(-0.98f, 0.98f, wet[ch][s]);
408:                 wet[ch][s] = dry[ch][s] * dryGain + wetSample * wetGain;
```
- On active playback path: YES when `ultraDemonAmount > 0.0001f`.
- Severity: LIKELY. Hard wet clamp and dry/wet blending can mask the deep pitch and cap low-frequency peak growth.
- Action: Move safety limiting after the full pitch/atmosphere chain or make it transparent.

### Hit 2
- File: `ArrangementEditor\DemonVoiceEngineCore.h:275`
- Code:
```cpp
275:     void process(float* const* channels, int numChannels, int numSamples, float drivePercent) noexcept
276:     {
277:         const float target = juce::jlimit(0.f, 30.f, drivePercent) / 100.f;
278:         const float smoothing = smoothingCoefficient(12.f);
279: 
280:         for (int s = 0; s < numSamples; ++s)
281:         {
282:             currentDrive_ += (target - currentDrive_) * smoothing;
283:             const float preGain = 1.f + currentDrive_ * 14.f;
284:             const float norm = std::tanh(preGain);
```
- On active playback path: YES when `ultraDemonAmount > 0.0001f`.
- Severity: POSSIBLE. Saturation is post-pitch in this branch and can compress perceived dynamics.
- Action: KEEP if intentional, otherwise move/retune after cap removal.

### Hit 3
- File: `..\..\Source\MasterCore\MasterBusEngine.h:71`
- Code:
```cpp
71:         // 1. Master insert chain (part of render path)
72:         inserts_.process(L, R, numSamples);
73: 
74:         // 2. Real master plugin chain (same visible chain used by the mixer strip)
75:         if (masterPluginChain_ != nullptr)
76:         {
77:             float* channels[] = { L, R };
78:             juce::AudioBuffer<float> masterView(channels, 2, numSamples);
79:             masterPluginChain_->processBlock(masterView, numSamples);
```
- On active playback path: YES, after `AudioEngine` output.
- Severity: POSSIBLE. Master inserts/plugins may include compressors/limiters, but actual slot list is runtime state and not statically visible here.
- Action: Audit runtime plugin chain during reproduction.

## Suspect Category 9 — Stretch-pitch coupling

### Hit 1
- File: `..\..\Source\AudioEngineCore\AudioEngine.h:1005`
- Code:
```cpp
1005:                 const double totalSt    = (double)clipPitch + (double)fineTuneCents / 100.0;
1006:                 const double pitchRatio = std::pow(2.0, totalSt / 12.0);
1007: 
1008:                 DBG("[AUDIO RENDER] clip=" << clip->getID()
...
1024:                     // ── MODE 0: Resample (inline, fastest path) ────────────
1025:                     // pitch and duration change together (tape/vinyl/DJ)
1026:                     const double readRate = pitchRatio / (double)clipStretch;
```
- On active playback path: YES in Resample mode.
- Severity: POSSIBLE. Intentional for Resample mode, but it bypasses independent pitch semantics.
- Action: KEEP only for explicit Resample mode.

### Hit 2
- File: `ArrangementEditor\PhaseVocoderStretchCore.h:84`
- Code:
```cpp
84:             // PitchOnly / Vocal / OfflineHQ fallback:
85:             // Stage 1: WSOLA stretches by stretchRatio * pitchRatio.
86:             // Stage 2: Resample at pitchRatio.
87:             // Combined duration = stretchRatio, while pitch changes independently.
88:             const double wsolaStretch = juce::jmax(0.0001, stretch * ((pitch > 0.0001) ? pitch : 1.0));
89:             wsola_.setStretchRatio(wsolaStretch);
90: 
91:             const int intermediateN = (int)std::ceil(dstNumSamples * pitch) + 4;
```
- On active playback path: YES for fallback pitch modes.
- Severity: LIKELY if fallback path handles deep pitch. At `pitch=0.125`, `wsolaStretch` becomes `0.125`, then is clamped by WSOLA to `0.1`; this is close to the floor and couples stretch and pitch.
- Action: Use `TapeResamplePitchEngineCore`/unified path for extreme pitch; do not combine stretch/pitch upstream.

### Hit 3
- File: `..\..\Source\AudioEngineCore\AudioEngine.h:1195`
- Code:
```cpp
1195:                             ArrangementEditor::TimePitchState tsSTP;
1196:                             tsSTP.pitchSemitones   = 0.0;  // pitch=0: stretch only in step 1
1197:                             tsSTP.fineTuneCents    = 0.0;
1198:                             tsSTP.stretchRatio     = clipStretch;
1199:                             tsSTP.mode             = ArrangementEditor::TimePitchMode::Stretch;
1200:                             tsSTP.preserveFormants = false;
1201:                             tsSTP.formantSemitones = 0.0;
```
- On active playback path: YES for stretch+pitch independent path.
- Severity: POSSIBLE. It decouples by serializing stretch then pitch, but still uses the old stretch core first.
- Action: KEEP if final unified pitch path is used after stretch.

## Suspect Category 10 — Hidden bypass flags

### Hit 1
- File: `ArrangementEditor\ClipPitchRenderPathCore.h:88`
- Code:
```cpp
85:         auto snapshot = stateCore_.advanceAndGetSnapshot();
86:         audit_.markUnified();
87: 
88:         if (snapshot.wet <= 0.0001)
89:         {
90:             copyDry(req, req.outputData, safeSamples);
91:             audit_.markBypass();
92:             reportedLatencySamples_ = 0;
93:             audit_.setReportedLatencySamples(reportedLatencySamples_);
94:             return;
```
- On active playback path: NO for current main pitch path; YES if unified path is used.
- Severity: POSSIBLE. Wet is derived from smoothed pitch; near zero only, so not expected at `-36 st`.
- Action: KEEP.

### Hit 2
- File: `..\..\Source\AudioEngineCore\AudioEngine.h:525`
- Code:
```cpp
522:                 auto clipsOnTrack = clips_->getClipsOnTrack(track->getID());
523:                 for (auto* clip : clipsOnTrack)
524:                 {
525:                     if (clip->isMuted()) continue;
526:                     if (isPlaying)
527:                         renderClip(clip, position, numSamples);
528:                 }
```
- On active playback path: YES.
- Severity: INFORMATIONAL. Mute bypasses clip rendering intentionally.
- Action: KEEP.

### Hit 3
- File: `..\..\Source\AudioEngineCore\AudioEngine.h:646`
- Code:
```cpp
642:         // ── Pre-fader sends ──────────────────────────────────────────────
643:         auto outConns = routing_->getOutputConnections(node->id);
644:         for (auto* conn : outConns)
645:         {
646:             if (!conn->active || conn->bypassed) continue;
647:             if (conn->type != ConnectionType::PreSend) continue;
648: 
649:             auto itBuf = nodeBuffers_.find(conn->destNodeId);
```
- On active playback path: YES for routing/sends.
- Severity: INFORMATIONAL. Routing bypass does not skip clip pitch directly.
- Action: KEEP.

### Hit 4
- File: `..\..\Source\PluginHostCore\PluginInstanceCore.h:593`
- Code:
```cpp
592:     /** Process audio in-place. Buffer must be stereo. No allocation, no locks. */
593:     void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
594:     {
595:         if (!plugin_ || !prepared_ || bypassed_.load(std::memory_order_relaxed)) return;
596:         plugin_->processBlock(buffer, midi);
597:     }
```
- On active playback path: YES after clip render.
- Severity: INFORMATIONAL. Plugin bypass skips plugin processing, not pitch processing.
- Action: KEEP.

## Suspect Category 11 — Plugin / FX chain interference

### Hit 1
- File: `..\..\Source\AudioEngineCore\AudioEngine.h:554`
- Code:
```cpp
554:         // Capture pre-FX snapshot before the plugin chain runs (for PreFX tap)
555:         if (preFxBuffer_.getNumSamples() < numSamples || preFxBuffer_.getNumChannels() < 2)
556:             preFxBuffer_.setSize(2, juce::jmax(numSamples, blockSize_));
557:         for (int ch = 0; ch < 2; ++ch)
558:             preFxBuffer_.copyFrom(ch, 0, trackBuffer_, ch, 0, numSamples);
559: 
560:         // MIDI playback: generate note events for MIDI tracks and feed to instrument plugin
```
- On active playback path: YES.
- Severity: INFORMATIONAL. Confirms clip rendering, including pitch, occurs before track inserts.
- Action: KEEP.

### Hit 2
- File: `..\..\Source\AudioEngineCore\AudioEngine.h:576`
- Code:
```cpp
576:         // Plugin chain (inserts) — pass the sidechain buffer so processors
577:         // with a sidechain input bus can read it via processBlockWithSidechain().
578:         if (pluginChains_)
579:         {
580:             auto it = pluginChains_->find(track->getID());
581:             if (it != pluginChains_->end() && it->second)
582:             {
```
- On active playback path: YES.
- Severity: POSSIBLE. Track inserts are post-pitch and may include gates/EQs/limiters that reduce low frequencies; actual runtime slots are not statically known.
- Action: Runtime audit plugin names/order during reproduction.

### Hit 3
- File: `..\..\Source\PluginHostCore\PluginChainCore.h:270`
- Code:
```cpp
270:     void processBlock(juce::AudioBuffer<float>& buffer, int numSamples)
271:     {
272:         // Lock-free: atomically load the published snapshot.
273:         auto snap = std::atomic_load(&published_);
274:         if (!snap) return;
275: 
276:         juce::MidiBuffer emptyMidi;
277:         for (auto& slot : snap->slots)
278:         {
279:             if (!slot) continue;
280:             slot->processBlock(buffer, emptyMidi);
```
- On active playback path: YES for track and master plugin chains.
- Severity: POSSIBLE. Plugin order is runtime-configured; no hard slot cap was observed.
- Action: KEEP, but log active slots.

## Suspect Category 12 — PDC / latency reporting

### Hit 1
- File: `ArrangementEditor\ClipPitchRenderPathCore.h:151`
- Code:
```cpp
146:         if (snapshot.zone == UnifiedPitchZone::DarkExtreme)
147:             audit_.markDarkExtreme();
148:         else if (snapshot.zone == UnifiedPitchZone::ChipmunkExtreme)
149:             audit_.markChipmunkExtreme();
150: 
151:         reportedLatencySamples_ = calculateLatency(snapshot);
152:         audit_.setReportedLatencySamples(reportedLatencySamples_);
153:     }
154: 
155:     int getReportedLatencySamples() const noexcept { return reportedLatencySamples_; }
```
- On active playback path: NO for main pitch branch; YES if unified path is used.
- Severity: POSSIBLE. Reporting exists, but transport does not appear to compensate per-clip pitch latency.
- Action: Verify after single playback path consolidation.

### Hit 2
- File: `ArrangementEditor\ClipPitchRenderPathCore.h:170`
- Code:
```cpp
170:     int calculateLatency(const UnifiedPitchSnapshot& snapshot) const noexcept
171:     {
172:         const double ind = static_cast<double>(independent_.getCurrentLatencySamples());
173:         const double tap = static_cast<double>(tape_.getCurrentLatencySamples());
174:         const double atmo = static_cast<double>(atmosphere_.getCurrentLatencySamples());
175:         return static_cast<int>(std::lround(ind * snapshot.independentBlend + tap * snapshot.tapeBlend + atmo));
176:     }
```
- On active playback path: NO for main pitch branch.
- Severity: POSSIBLE. At `-36 st`, `tapeBlend` should dominate and latency should be approximately `2` samples; not likely to cause inaudible deep voice.
- Action: KEEP, but assert/report actual value at runtime.

### Hit 3
- File: `..\..\Source\AudioEngineCore\AudioEngine.h:413`
- Code:
```cpp
413:     /** (Re)build PDC delay lines based on current plugin chain latencies.
414:      *  Call after prepare() and whenever a plugin is added/removed.
415:      *  For each sidechain dest node, computes how many samples the source track
416:      *  chain is ahead of the dest track's main path and sets up a matching delay. */
417:     void rebuildPdcLines(int numSamples)
418:     {
419:         if (!routing_ || !pluginChains_) return;
```
- On active playback path: YES for sidechain routing only.
- Severity: INFORMATIONAL. PDC is for sidechain/plugin latency, not pitch-engine latency.
- Action: KEEP.

## Active playback path trace

Normal transport playback path found in source:

```text
Application audio callback / AudioEngine::process
  → AudioEngine::processWithRouting (if routing graph lock acquired)
    → AudioEngine::processTrackNode
      → AudioEngine::renderClip
        → AudioFileManager::getBuffer          ← original cached full-resolution AudioBuffer
        → AudioClip::getPitch/getTimeStretch/getFineTuneCents
        → mode dispatch:
          - Resample mode:
              inline readRate = pitchRatio / stretch; NO ClipPitchRenderPathCore
          - Default Vocal/PitchOnly pitch-active mode:
              useIndependentPitchPath == true
              → read source directly into pitchInputBuffer_
              → if ultraDemonAmount > 0: DemonVoiceEngineCore::process
                   → DemonVoiceMappingCore clamp
                   → DemonPitchCore → ClipIndependentPitchCore
                   → DemonBlendCore wet clamp / dry blend
                else: ClipIndependentPitchCore::processBlock
              → mix to trackBuffer_
              → return; NO TimePitchDSPCore and NO ClipPitchRenderPathCore
          - Fallback normal/stretch modes:
              TimePitchDSPCore::renderClipSegment
              → if req.state.isPitchActive(): ClipPitchRenderPathCore::render
      → preFxBuffer capture
      → track PluginChainCore inserts
      → fader/pan/routing
    → AudioEngine::processMasterNode
  → MasterBusEngine::processBlock
    → master insert chain
    → master plugin chain
    → master fader/mute
    → output
```

## Conclusions

The active cap stack preventing the v4 `-36 st` deep voice from being reliably audible is:

1. **BLOCKING:** The actual normal transport pitch-active path bypasses `ClipPitchRenderPathCore` and therefore bypasses the new `TapeResamplePitchEngineCore + FormantShiftCore + DarkVoiceBodyCore + DemonAtmosphereCore` chain (`AudioEngine.h:1141`).
2. **LIKELY:** The active demon branch clamps combined demon pitch to `-36..+24` and wet audio to `±0.98`, then blends with dry according to `ultraDemonAmount` (`DemonVoiceEngineCore.h:68`, `DemonVoiceEngineCore.h:407`).
3. **LIKELY once unified path is active:** `DarkVoiceBodyCore` applies a `58/72 Hz` high-pass-style subtraction after pitch, which is above the `≤20 Hz` requirement and removes the lowest harmonics required for `-36 st` perception (`DarkVoiceBodyCore.h:57-70`).
4. **LIKELY:** Source sample-rate conversion is computed but not applied in `AudioEngine::renderClip`; low-SR sources or SR mismatches will cap bandwidth before pitch (`AudioEngine.h:957-958`).

Recommended Phase 2 priority:
1. Route all pitch-active realtime playback through `ClipPitchRenderPathCore`.
2. Centralize pitch clamps through `PitchScaleMathCore::clampPitch` and remove the `DemonVoiceMappingCore`/`AudioClip::setPitch` narrow clamps.
3. Lower/bypass the `DarkVoiceBodyCore` safety HPF for extreme dark pitch.
4. Apply source sample-rate conversion before pitch processing.
5. Runtime-log active track/master plugins to rule out post-pitch EQ/limiter interference.
