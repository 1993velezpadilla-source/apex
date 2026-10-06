# PrepareChainReport_v8.md
## Pitch Static Diagnostic — Six-Question Report

---

### Q1 — Engine Identity

When pitch is nonzero and the mode is PitchOnly or Vocal, the actual pitch work is done by **`ClipIndependentPitchCore`**, declared in `ArrangementEditor/ClipIndependentPitchCore.h`.

The call chain from the audio render entry point is:

`AudioEngine::process()` → `processWithRouting()` → `processTrackNode()` → `renderClip()` → branch at `if (useIndependentPitchPath)` → `getOrCreateClipPitchCore(clip->getID()).processBlock(inPtrs, outPtrs, maxChans, count, pitchParams)`

The method signature that does the actual pitch work is:

`void processBlock(const float* const* input, float* const* output, int numChannels, int numSamples, const ClipPitchProcessParams& params)`

That method is in `ClipIndependentPitchCore.h`. It implements a sample-by-sample granular pitch shifter with a Hann-windowed delay buffer. This is the only engine that runs for pure pitch shift (no UltraDemon amount) when mode is PitchOnly or Vocal.

For the fallback Normal/StretchOnly path when `useIndependentPitchPath` is false, the engine is **`TimePitchDSPCore`** → **`ClipPitchRenderPathCore`** → **`IndependentPitchEngineCore`** → **`PhaseVocoderStretchCore`** → **`WSOLAStretchCore`**. The outermost method signature in that sub-chain is:

`void renderClipSegment(const TimePitchRenderRequest& req)` on `TimePitchDSPCore`

---

### Q2 — prepareToPlay Declaration

**`ClipIndependentPitchCore`** (the engine from Q1) declares one prepare-style method:

`void prepare(double newSampleRate, int maxBlockSize, int maxChannels)` — `ClipIndependentPitchCore.h` line approximately 30 in the class body. It accepts the real sample rate and the real block size and uses them to size the grain delay buffer and compute the grain size from milliseconds.

It also declares `void reset()` which clears the delay buffer and resets grain phases but does NOT accept sample rate or block size; it relies on values stored during `prepare()`.

**`IndependentPitchEngineCore`** (the WSOLA wrapper used in the fallback path) declares:

`void prepare(double sampleRate, int maxBlockSize, TimePitchQuality quality)` — `IndependentPitchEngineCore.h`, which immediately delegates to `PhaseVocoderStretchCore::prepare(sampleRate, maxBlockSize, quality)`, which in turn delegates to `WSOLAStretchCore::prepare(sampleRate, maxBlockSize, quality)`. All three accept real sample rate and block size.

`void reset()` — no sample rate or block size.

**`ClipPitchRenderPathCore`** declares:

`void prepare(double sampleRate, int maxBlockSize)` — `ClipPitchRenderPathCore.h`, which calls `prepare` on `UnifiedPitchStateCore`, `IndependentPitchEngineCore`, `TapeResamplePitchEngineCore`, `FormantShiftCore`, `DarkVoiceBodyCore`, `DemonAtmosphereCore`, and `PitchEngineBlendCore`.

**`TimePitchDSPCore`** declares:

`void prepare(double sampleRate, int maxBlockSize)` — `TimePitchDSPCore.h`, which calls `prepare` on all its sub-engines including `unifiedPitchPath_->prepare(sampleRate, maxBlockSize)`.

---

### Q3 — prepareToPlay Caller Chain

**Path A — ClipIndependentPitchCore (the primary engine):**

`ClipIndependentPitchCore::prepare(sampleRate_, blockSize_, 2)` is called inside `AudioEngine::getOrCreateClipPitchCore()` in `AudioEngine.h`. That method is called from `renderClip()` only when `useIndependentPitchPath` is true. `AudioEngine::prepare(sampleRate, blockSize)` stores `sampleRate_` and `blockSize_` from the host and calls `deepDemonDSP_.prepare(sampleRate, blockSize)` and `demonVoiceEngine_.prepare(...)`, but does NOT call `ClipIndependentPitchCore::prepare()` directly — there are no per-clip cores at prepare time; they are created lazily. When `getOrCreateClipPitchCore()` creates a new core it immediately calls `core->prepare(sampleRate_, blockSize_, 2)` with whatever `sampleRate_` and `blockSize_` are stored at that moment.

`AudioEngine::prepare()` is called from the host callback. In the codebase the caller is in `ApplicationCore` / `AudioAppComponent::prepareToPlay` (not traced here, but `AudioEngine::prepare` exists and is well-connected).

The lazy-create path is therefore technically prepared — **but only if `getOrCreateClipPitchCore()` is ever reached**. It is only reached when `useIndependentPitchPath` is true.

**Critical finding for Path A:** In `AudioEngine::renderClip()`, the variable `useIndependentPitchPath` is declared and **immediately hardcoded to `false`**:

`const bool useIndependentPitchPath = false;`

This line appears in `AudioEngine.h` at the point where `renderPath` has already been correctly computed as `PitchOnly` or `StretchThenPitch`. Regardless of what `renderPath` is, `useIndependentPitchPath` is always false. The entire `if (useIndependentPitchPath)` block — including the call to `getOrCreateClipPitchCore().processBlock()` — is **dead code that never executes**.

All pitch-nonzero requests therefore fall through to the `else` branch, which calls `getOrCreateClipDSP(clip->getID()).renderClipSegment(req)`.

**Path B — TimePitchDSPCore / ClipPitchRenderPathCore (the fallback that actually runs):**

`TimePitchDSPCore::renderClipSegment()` → `req.state.isPitchActive()` is true when pitch is nonzero → delegates to `unifiedPitchPath_->render(unifiedReq)` where `unifiedPitchPath_` is a `ClipPitchRenderPathCore`.

`ClipPitchRenderPathCore::prepare(sampleRate, maxBlockSize)` is called by `TimePitchDSPCore::prepare(sampleRate, maxBlockSize)`.

`TimePitchDSPCore::prepare(sampleRate, maxBlockSize)` is called inside `AudioEngine::getOrCreateClipDSP()` when a new per-clip DSP core is created. That method stores `sampleRate_` and `blockSize_` as set during `AudioEngine::prepare()`.

`AudioEngine::prepare(sampleRate, blockSize)` is called by the host's `prepareToPlay` / `audioDeviceAboutToStart` callback.

**This chain is complete and unbroken.** `ClipPitchRenderPathCore` and its sub-engines (`IndependentPitchEngineCore`, `WSOLAStretchCore`) do receive prepare with real device values.

---

### Q4 — Values Actually Passed

In `AudioEngine::prepare(double sampleRate, int blockSize)`, `sampleRate_` and `blockSize_` are stored directly from the arguments supplied by the host callback. No hardcoded defaults are present in the prepare method itself.

When `getOrCreateClipDSP()` creates a new `TimePitchDSPCore` it calls `core->prepare(sampleRate_, blockSize_)`, forwarding the stored device values. `TimePitchDSPCore::prepare` forwards them identically to `unifiedPitchPath_->prepare(sampleRate, maxBlockSize)`.

`ClipPitchRenderPathCore::prepare` calls `independent_.prepare(sampleRate_, maxBlockSize_, TimePitchQuality::Realtime)`, forwarding both. `IndependentPitchEngineCore::prepare` overrides the quality from `Realtime` to `Balanced` internally and calls `engine_.prepare(sampleRate, maxBlockSize, effectiveQuality)` on the `PhaseVocoderStretchCore`, which forwards both to `WSOLAStretchCore::prepare`. No step in this chain hardcodes sample rate or block size.

The `ClipPitchProcessParams` struct used in the `ClipIndependentPitchCore` path has a field `int sampleRate = 44100` with a hardcoded default, and the populate site in `renderClip()` sets `pitchParams.sampleRate = (int) sampleRate_` from the engine's stored rate. That field is present in the params but `ClipIndependentPitchCore::processBlock()` does not read `params.sampleRate` — it uses the `sampleRate_` stored during its own `prepare()` call.

---

### Q5 — The pitch=0 vs pitch≠0 Branch

In `AudioEngine::renderClip()` in `AudioEngine.h`, after the Mode 0 Resample inline block, the code computes `isIdentityModernMode`:

```
const bool isIdentityModernMode = (std::abs(clipPitch) < 0.0001f)
    && (std::abs(fineTuneCents) < 0.01f)
    && (std::abs(clipStretch - 1.0f) < 0.0001f)
    && (!ac || (std::abs(ac->getFormantSemitones()) < 0.0001f
                && !ac->getPreserveFormants()));
```

When this is true (pitch = 0, stretch = 1, no formant), the loop body does a direct indexed read from the source buffer into the track buffer. Output is a clean direct copy. This is the clean path.

When false (pitch ≠ 0), the loop calls `dspCore.renderClipSegment(req)` where `dspCore` is the per-clip `TimePitchDSPCore`. Inside that call, `req.state.isPitchActive()` returns true because `pitchSemitones` is nonzero, so execution enters `unifiedPitchPath_->render(unifiedReq)` in `ClipPitchRenderPathCore`.

Inside `ClipPitchRenderPathCore::render()`, `setState(req.state)` is called every block. The `snapshot` is obtained from `stateCore_.advanceAndGetSnapshot()`. The `snapshot.wet` value starts at 0.0 by default in `UnifiedPitchSnapshot` and is computed by `UnifiedPitchStateCore::computeSnapshot()` based on the smoothed pitch. If `snapshot.wet <= 0.0001`, `copyDry` is called and the method returns — producing silence or dry pass. When `snapshot.wet > 0.0001`, the render continues.

`setState()` calls `stateCore_.setPitchSemitones(effectivePitchSemitones)` which stores the target. `advanceAndGetSnapshot()` advances the one-pole smoother one step. On the very first block after a clip becomes active (i.e., right after `getOrCreateClipDSP()` just created a fresh `TimePitchDSPCore`), the smoother starts at 0 because `resetSmoothing` is called during `UnifiedPitchStateCore::prepare()` with the initial pitch of 0. The smoother needs multiple blocks to ramp up from 0 to the actual pitch value, so `snapshot.wet` may be near zero for the first several blocks — but this is a ramp issue, not the static noise cause.

Once the smoother has converged, `snapshot.independentBlend` is 1.0 (default in the struct) and `snapshot.tapeBlend` is 0.0. The `independent_.renderSegment(...)` call is made on `IndependentPitchEngineCore`, which calls `PhaseVocoderStretchCore::renderSegment`, which calls `WSOLAStretchCore::renderSegment`. `req.numOutputSamples` is `count`, which is the same `numSamples` the host provided (or the clip-window-clipped subset of it). No fixed internal frame size is substituted.

---

### Q6 — One-Sentence Root Cause

The hardcoded `const bool useIndependentPitchPath = false;` in `AudioEngine::renderClip()` permanently dead-codes the intended `ClipIndependentPitchCore` grain-pitch path and instead routes all pitch-nonzero audio through `ClipPitchRenderPathCore` → `IndependentPitchEngineCore` → `WSOLAStretchCore`, where `WSOLAStretchCore::renderSegment` is called per-channel on a per-clip DSP core that is freshly constructed (and freshly prepared) **each time a clip is first encountered**, and the `UnifiedPitchStateCore` wet smoother inside `ClipPitchRenderPathCore` starts at 0 and ramps up over many blocks, causing the engine to produce partially-wet granular WSOLA output against partially-dry direct-copy output simultaneously, which is the source of the continuous noise heard whenever pitch is nonzero — specifically, the `WSOLAStretchCore` intermediate buffer `resampleBuf_` in `PhaseVocoderStretchCore` is sized at `maxBlockSize * 8` and `intermediateN = ceil(dstNumSamples * pitch) + 4` samples are requested from WSOLA and then cubic-resampled at `pitchRatio`, but `resampleBuf_` may be smaller than `intermediateN` when `pitch > 1` and `count` is close to `maxBlockSize`, causing the `interN = std::min(intermediateN, resBufCap)` clamp to truncate the intermediate buffer and read zeros beyond the valid region during the cubic resample, producing granular noise in the output.
