# APEX Fix Registry

**Last verified:** 2026-07-26

## Known Fixes in Codebase

### FIX-001 Bug 17: Sample Rate Mismatch on Device Restart
- **File:** `AudioEngineCore/AudioEngine.h:336-342`
- **Description:** Re-prepare ALL per-clip DSP cores at new sample rate on device restart
- **Root Cause:** Cores created at 44100 Hz retained stale calibration when device reopened at 48000 Hz

### FIX-002 Bug 16: Stale DSP State After Release/Prepare Cycle
- **File:** `AudioEngineCore/AudioEngine.h:385-400`
- **Description:** Clear ALL per-clip DSP state on device close
- **Root Cause:** Cores survived release/prepare cycle carrying stale ring-buffer and filter state

### FIX-003 Bug 46: PDC Sidechain Heap Allocation in Audio Callback
- **File:** `AudioEngineCore/AudioEngine.h:839-843`
- **Description:** Pre-allocated scratch buffers for PDC sidechain reads
- **Root Cause:** Heap allocation inside audio callback violated RT-safe contract

### FIX-004 Transport-Start Snap Zipper
- **File:** `AudioEngineCore/AudioEngine.h` (resetAllClipDSPState)
- **Description:** Reset all clip DSP state on transport discontinuity to prevent snap-on-start glitches

### FIX-005 Auto-Arm Track 0 Overwrite (Bug #1)
- **File:** `RecordingCore/RecordingEngine.h:176-208`
- **Description:** Auto-arm first track with NO existing clips instead of always track 0
- **Root Cause:** Always arming track 0 would record over imported beats

### FIX-006 ASIO4ALL Buffer Size Crash
- **File:** `AudioEngineCore/AudioEngine.h:296-299`
- **Description:** Pre-allocate worst-case block size (8192) to prevent crashes on driver reconfiguration

### FIX-007 Device Panel Not Device-Authoritative (2026-07-25 sample-rate expansion)
- **File:** `DeviceCore/DevicePanelModelCore.h`, `DeviceCore/DeviceCapabilityCore.h` (new), `DeviceCore/DeviceSessionCore.h`, `UICore/AudioDevicePanelUI.h`
- **Description:** Rate/buffer combos now enumerate the actual device (fast path: open device; probe: temp device, cached per panel session), intersected with the professional set {32000..192000} and ladder {32..2048}; 32 shown as `32 (experimental)`; session validate rejects values the device did not report; empty lists = advisory fallback
- **Root Cause:** Hardcoded lists `{44100,48000,88200,96000}` / `{64..2048}` were the sole selectable source; 32000/176400/192000 and 32-sample buffers were unselectable
- **Tests:** `device.capabilities.v1`, `device.config-validation.v1`

### FIX-008 DrumSampler Hardcoded 44.1 kHz + No Pad Resampling
- **File:** `DrumSamplerCore/DrumSamplerVoice.{h,cpp}`, `DrumSamplerVoicePool.{h,cpp}`, `DrumSamplerEngine.h`, `MainComponent.cpp` (prepare call site)
- **Description:** Voices derive ADSR from the actual engine rate and resample pads by fileRate/engineRate with linear interpolation (RT-safe, no alloc/lock); engine prepare propagates on every device (re)start
- **Root Cause:** `sampleRateRatio = 1.0f` and `* 0.001f * 44100.0f` — pads played wrong pitch/speed when file rate != engine rate; envelope times wrong at any rate != 44.1k
- **Tests:** `drumsampler.rate.v1`

### FIX-009 MidiClip Hardcoded Default Length + No Device-Rate Path
- **File:** `MidiCore/MidiClip.{h,cpp}`, `ClipCore/Clip.{h,cpp}`, `MidiCore/MidiInputCore.h`, `MainComponent.cpp` (prepareToPlay re-sync)
- **Description:** Default MIDI clip = 8 s at the engine rate (was 352800 samples pinned to 44.1k); `setSampleRate` rescales seconds-preserving; device-rate re-sync runs synchronously inside the safe `prepareToPlay` lifecycle (message thread, no RT mutation)
- **Root Cause:** 44.1k-pinned default length; no update path on device rate change
- **Tests:** `clip.timing-defaults.v1`

### FIX-010 LUFS K-Weighting Approximated at Non-48k Rates
- **File:** `MeteringCore/LufsMeterCore.h`
- **Description:** 48 kHz keeps the published BS.1770 coefficients verbatim; all other rates derive both biquads from the BS.1770 analog prototype (bilinear/tan-form; RLB numerator pinned [1,-2,1]) — validated to reproduce the published 48k constants <=5e-4 and to read -23.0 LUFS (+-0.3) on the reference sine at all 7 professional rates x {32,256} blocks
- **Root Cause:** RBJ 1500 Hz/+4 dB/Q.707 shelf and 38 Hz/Q.5 HP approximations used at every rate != 48k — not the standard filters
- **Tests:** `metering.lufs-kweighting-rates.v1`

### FIX-011 ArrangementEditor Hardcoded 44100 Conversions
- **File:** `Builds/VisualStudio2026/ArrangementEditor/ClipAutomationPanel.h`, `ClipPropertiesWindowCore.h`, `ArrangementViewCore.{h,cpp}`, `ClipRenderCore.{h,cpp}`, `ClipPanelKnobBridgeCore.h`
- **Description:** Engine-rate plumbed from `ArrangementViewCore::m_engineSampleRate` into automation panels (region seconds->samples targets — audio-affecting), clip renderers (peak hint, trim tooltip, fade draw), knob bridge now reads `sourceSampleRate` (fallback 44100 when unknown)
- **Root Cause:** `clip_.startTime/length * 44100.0`, `out.sampleRate = 44100.0 // TODO`, `visualSampleRate = 44100.0`
- **Tests:** build-verified (no headless harness for the editor UI); remaining 44100 occurrences are documented fallbacks only

### FIX-012 B4 Audit Ring Drain Cadence Fires ringOverflows by Design at Low Buffers
- **File:** `DiagnosticsCore/CallbackAuditCore.h`, `MainComponent.{h,cpp}`
- **Description:** Pure `computeCallbackPeriodTicks` (device-derived) + `computeAuditDrainIntervalSeconds` (clamp(0.5xcapacityxperiod, 0.05, 5.0)); dedicated adaptive drain timer decoupled from the 5 s report; lateDeliveries documented as strict delivery-jitter metric (semantics unchanged)
- **Root Cause:** 1024-slot ring drained on a 2 s watchdog tick behind a 5 s guard — at 48k/64 (750 cb/s) ~2726 overflows per cycle; counter measured cadence, not a stalled drain
- **Tests:** `callback-audit-period.v1`

### FIX-013 I/O Topology Change Silently Clamped (cross-vendor audit, item 20)
- **File:** `MainComponent.cpp` (inputWatchdogTick, :6807-6821)
- **Description:** The watchdog now consumes `needsInputBufferReprepare_` and drives the proven `ensureInputChannelsActive("topology-change")` restart path so mid-session I/O changes (S/MUX, custom I/O matrix) are adopted; guarded against panel ownership and recording
- **Root Cause:** Flag was set in the realtime callback but never consumed — topology changes were silently clamped until the next device restart
- **Tests:** four gates green (Debug/Release builds + tests, 2026-07-26T00:24/00:29Z)

### FIX-014 Project Sample-Rate Identity (pre-beta project integrity, audit item 22)
- **File:** `ProjectCore/ProjectSampleRateReconcileCore.h` (new), `ProjectCore/ProjectManager.{h,cpp}`
- **Description:** Projects persist the authoritative device-granted sample rate (only when proven); on reopen, a pure plan decides: none / seconds-preserving reconcile / legacy-unverified / device-rate-unknown. Reconcile scales clips (AudioClip engine-domain fields; MIDI via the tested setSampleRate path), transport position, and markers; PPQ automation is untouched by construction; LoadRateReport exposed for the GUI phase
- **Root Cause:** Project files carried no sample-rate identity — sample-domain timelines were silently reinterpreted at whatever rate the device happened to run
- **Tests:** `project.sample-rate-reconcile.v1` (11 cases: same-rate, 44.1->48, 48->96, 96->48, legacy, unavailable device rate, clip/MIDI time-correctness, clamps, no-op guards)
