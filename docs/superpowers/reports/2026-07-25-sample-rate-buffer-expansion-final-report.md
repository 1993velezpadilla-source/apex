# APEX — Professional Sample-Rate Set + 32-Sample Buffer Expansion — Final Report

**Date:** 2026-07-25
**Branch (nested DAW_Core repo):** `feature/apex-windows-baseline-evidence` — **zero commits; all changes in the working tree** (explicit user directive).
**Authority:** DAW_BRAIN v7 §2, §3, §8, §28, §34; spec `docs/superpowers/specs/2026-07-25-sample-rate-buffer-expansion-design.md`; plan `docs/superpowers/plans/2026-07-25-sample-rate-buffer-expansion.md`.
**Method:** per-task RED→GREEN TDD with evidence manifests; no speculative rewrites; device authority preserved throughout.

---

## 1. Files changed

**New (this work order):**
- `My DAW/DAW_Core/Source/DeviceCore/DeviceCapabilityCore.h` — pure, `juce_audio_devices`-free capability logic (unit-testable).
- `My DAW/DAW_Core/Tests/Source/Device/DeviceCapabilityTests.cpp` — `device.capabilities.v1` + `device.config-validation.v1`.
- `My DAW/DAW_Core/Tests/Source/DrumSampler/DrumSamplerRateTests.cpp` — `drumsampler.rate.v1`.
- `My DAW/DAW_Core/Tests/Source/Arrangement/ClipTimingDefaultTests.cpp` — `clip.timing-defaults.v1`.

**Modified (this work order only — the branch already carried unrelated 2026-07-24 forensic-hardening modifications):**
- Device/UI: `Source/DeviceCore/DevicePanelModelCore.h`, `Source/DeviceCore/DeviceSessionCore.h`, `Source/UICore/AudioDevicePanelUI.h`
- B4: `Source/DiagnosticsCore/CallbackAuditCore.h`, `Source/MainComponent.h`, `Source/MainComponent.cpp`
- DrumSampler: `Source/DrumSamplerCore/DrumSamplerVoice.{h,cpp}`, `DrumSamplerVoicePool.{h,cpp}`, `DrumSamplerEngine.h`
- Timing: `Source/MidiCore/MidiClip.{h,cpp}`, `Source/MidiCore/MidiInputCore.h`, `Source/ClipCore/Clip.{h,cpp}`
- Metering: `Source/MeteringCore/LufsMeterCore.h`
- ArrangementEditor: `Builds/VisualStudio2026/ArrangementEditor/ClipAutomationPanel.h`, `ClipPropertiesWindowCore.h`, `ArrangementViewCore.cpp`, `ClipRenderCore.{h,cpp}`, `ClipPanelKnobBridgeCore.h`
- Tests: `Tests/Source/Diagnostics/CallbackAuditCoreTests.cpp`, `Tests/Source/Diagnostics/LufsMeterCoreTests.cpp`, `Tests/Builds/VisualStudio2026/APEXTests_ConsoleApp.vcxproj`
- Registries (outer workspace): `APEX_FIX_REGISTRY.md` (FIX-007…012), `APEX_TEST_REGISTRY.md` (TEST-018…023)

## 2. All sample-rate/buffer assumptions found (audit catalog)

**C-class (proven defects — all fixed, §3):** hardcoded panel lists `DevicePanelModelCore.h:96,101`; DrumSampler `sampleRateRatio = 1.0f` + ADSR at hardcoded 44100 (`DrumSamplerVoice.cpp:30,50-52`); MidiClip `setLength(352800)` 44.1k-pinned default + no device-rate path (`MidiClip.cpp:14`); LUFS exact K-weighting only at 48k (`LufsMeterCore.h:96-137`); ArrangementEditor `clip_.startTime/length * 44100.0` automation-region targets (`ClipAutomationPanel.h:383-384,642-643` — audio-affecting), `out.sampleRate = 44100.0 // TODO` (`ClipPanelKnobBridgeCore.h:92`), `visualSampleRate = 44100.0` + two further conversions (`ClipRenderCore.cpp:70,537,729`); B4 ring 5 s fixed drain (`MainComponent.cpp:6644`).

**B-class (fallback defaults — reviewed, inert once prepared; unchanged):** ~60 sites catalogued (engine/meter/click/monitor/plugin member defaults 44100/512; `SafeAsioDeviceTypeCore.cpp:468-469` 48000/512 used only when a driver reports empty lists; `DeviceSessionCore.h:17-18` request defaults; `MainComponent.cpp:6189` device-null fallback; `MidiClip::restoreState` legacy-property fallback; `Clip::restoreState` missing-property fallback). These are honest fallbacks for "no authoritative rate exists", not behavioral assumptions.

**A-class (already correct — verified, unchanged):** engine/plugin/recording/click/meter/PDC/transport/fade/automation DSP derives from the prepared device rate; all `8192` constants are grow-only worst-case allocations; **no clamp anywhere rejects 32-sample blocks**; reconfiguration path (stop → `audioDeviceStopped` → start → `audioDeviceAboutToStart` → `applyAudioDevicePreparation`) re-prepares the full stack and recomputes the audit period from the actual device.

## 3. Proven defects corrected

Every C-class item in §2 was fixed with RED→GREEN evidence. Notable corrections discovered *by* the tests during TDD (not by assumption): (a) the K-weighting RBJ sin/cos formulation failed to reproduce the published 48k constants (b0 1.5293 vs 1.53512) and was replaced by the bilinear/tan-form prototype transform; (b) the first LUFS test signal used amplitude-−23 dBFS instead of the BS.1770 RMS convention; (c) the panel's unconditional re-enumeration would have wiped combo text mid-keystroke — made conditional on backend/device change; (d) the rate-intersection epsilon was 1 Hz (admitted 48000.5 as 48000) — tightened to 0.01 Hz.

## 4. Device-authoritative enumeration behavior

- `DevicePanelModelCore::getAvailableSampleRates/getAvailableBufferSizes(type, device)` — fast path reads the **currently open** device; otherwise creates a temporary device **once per type+device per panel session** (same proven pattern as output-channel probing; caches invalidated on panel open). JUCE ASIO already probes `canSampleRate()` (includes 176400/192000) and enumerates driver buffer sizes (includes 32 when min ≤ 32); `SafeAsioDeviceTypeCore` passes enumeration through unchanged.
- Selectable rates = device-reported ∩ {32000, 44100, 48000, 88200, 96000, 176400, 192000} (0.01 Hz tolerance, ascending). Oddball driver rates outside the set are not offered.
- Selectable buffers = device-reported ∩ {32, 64, 128, 256, 512, 1024, 2048}. WASAPI shared: JUCE's list starts at 64 → 32 simply never appears (accepted; nothing synthesized).
- `DeviceSessionCore::validate` rejects requested values the device did not report (explicit reason), enforced on the commit path; empty enumeration = advisory mode (previous behavior). Rollback-on-failure and post-commit read-back of granted values are unchanged.
- No cross-session persistence exists (verified: `createStateXml` never called; `MainComponent.cpp:6417,6425` initialise with null XML) — nothing to migrate.

## 5. 32-sample implementation behavior

- Offered **only** when the active driver enumerates 32; UI label `32 (experimental)` (`formatBufferSizeLabel`), parse-back to 32 on the commit path; validation accepts it only when device-reported.
- Static RT-safety proof: zero minimum-block clamps in `Source/`; every 8192 is grow-only worst-case; JUCE ASIO passes 32 verbatim to `ASIOCreateBuffers`; B4 nominal period and adaptive drain are correct at 32 (§7). Internal/documentational status: **EXPERIMENTAL_ULTRA_LOW_LATENCY**.

## 6. Professional sample-rate behavior

All seven rates (32000/44100/48000/88200/96000/176400/192000) flow through the existing safe reconfiguration path: `DeviceSessionCore::commit` → JUCE stop/open/start → `audioDeviceStopped`/`audioDeviceAboutToStart` → `applyAudioDevicePreparation`, which re-prepares engine, all plugin chains (`prepareToPlay`), recording, click, metering, master bus, republishes PDC latencies, recomputes the B4 period, and re-syncs MIDI clips (seconds-preserving). No engine redesign was needed or performed; the 256-sample protected baseline and 64-sample behavior are preserved (all pre-existing suites green in both configurations).

## 7. B4 ring/drain correction

- Proven (arithmetic, not speculation): 1024-slot ring + 5 s guard on a 2 s watchdog tick → at 48k/64 (750 cb/s) ~2726 overflows/cycle; drained records are discarded, accumulator is authoritative → no data was ever lost; the counter measured cadence, not a stalled drain.
- Fix: `computeCallbackPeriodTicks` (pure, device-derived, 0=unarmed) replaces the inline math; `computeAuditDrainIntervalSeconds` = clamp(0.5 × capacity × period, 0.05, 5.0); a dedicated `CallbackAuditDrainTimer` drains adaptively (48k/32 ≈ 0.34 s; 192k/32 ≈ 0.085 s), armed in `applyAudioDevicePreparation`, stopped on release; the 5 s report is untouched. `ringOverflows` now only fires on a genuinely stalled drain (Brain §34.21).
- Tests: `callback-audit-period.v1` — full 7×7 period matrix, spot-exact deadlines (48k/64 = 13333 ticks … 192k/32 = 1666, 48k/256 = 53333 baseline), drain bounds, invalid-input handling.

## 8. lateDeliveries semantics clarification

Semantics **unchanged** per user decision (strict `interval > nominal period`, no tolerance band). Documented in `CallbackAuditCore.h`: at 32/64-sample periods this is a **callback delivery-jitter metric**; it does not by itself mean an audible failure, deadline miss, engine overrun, or xrun — four distinct facts that must not be conflated. Stream reconfiguration cannot create false positives (prevStart reset, stream generation increment, accumulator reset).

## 9. DrumSampler sample-rate/resampling correction

- `DrumSamplerVoice`: `engineSampleRate_` (44100 legacy default), double `srcPosition_`, `rateRatio_ = fileRate/engineRate` captured at `start()`; linear-interpolation resampling (allocation/lock/I-O-free — Brain §2 compliant); ADSR from the actual rate. `DrumSamplerVoicePool::setSampleRate`; `DrumSamplerEngine::prepare`; called from `applyAudioDevicePreparation` beside the step-sequencer prepare.
- Tests (`drumsampler.rate.v1`, 6/6): pitch preserved within ±30 Hz @1 kHz for a 44.1k pad at 44.1/48/88.2/96/176.4/192k engine **and** a 96k pad at 48k engine; duration-in-seconds preserved on 32-sample blocks; ADSR 10 ms attack invariant at four rates; engine-level propagation; legacy default without prepare.
- **Intentional behavior correction (flagged per work order):** pads whose file rate ≠ device rate previously played at wrong pitch/speed (e.g. 44.1k pad at 48k ≈ +8.8% fast/sharp); they now play correctly.
- Pre-existing warning noted (not introduced): `DrumSamplerEngine.cpp:53` C4244 double→int on `layer.sampleRate = reader->sampleRate` — file rates are integral in practice; left unchanged (scope discipline).

## 10. MidiClip/Clip timing correction

- `MidiClip(id, name, sampleRate = 44100)`: default length = `llround(8.0 × rate)` (4 bars @120 BPM) — 352800 at the legacy default, bit-identical to before when no rate is passed. `setSampleRate` rescales seconds-preserving (tick extent stable within ±1 sample rounding; ignores non-positive/same rates). `createMIDIClip` passes the rate through; `MidiInputCore.h:228` (record-target) passes its prepared rate. `AddClipCommand` verified **not** to know the rate (`engineSampleRate_` belongs to a different class in `GeneralCommands.h`) — its explicit `setLength` makes the default moot; documented.
- Device-rate re-sync: synchronous loop in `MainComponent::prepareToPlay` — message thread, inside the safe device-reconfiguration window (the new device's callbacks have not started; same lifecycle as startup prepare); **no callAsync, no audio-thread mutation, consistent with the established tempo-change path**. Seconds (and ticks) preserved across rate changes.
- Tests (`clip.timing-defaults.v1`, 7/7).

## 11. LUFS per-rate K-weighting evidence

- 48 kHz: published BS.1770 coefficients used **verbatim** — test asserts identity to 1e-12 (zero regression on the verified rate).
- All other rates: both biquads derived from the BS.1770 analog prototype via bilinear/tan-form (shelf: full `/A0`; RLB HP: numerator pinned `[1,-2,1]` published-style so fs→48k lands exactly on the standard). De Man refined constants.
- Validation (not "approximately plausible"): the derivation **reproduces the published 48k constants within 5e-4**; oracle cross-check matches to 1e-9 at 32000/44100/88200/96000/176400/192000; end-to-end −23 dBFS (RMS) 1 kHz sine reads −23.0 ± 0.3 LUFS (momentary **and** integrated) at all 7 rates × {32, 256} block sizes. All pre-existing LUFS suites (48k-pinned) remain green.

## 12. ArrangementEditor corrections

- `ClipAutomationPanel.h` (both panel classes): `setEngineSampleRate` + member; the two `* 44100.0` automation-region conversions (audio-affecting: region sample targets) now use the engine rate. `ClipPropertiesWindowCore.h`: panel setter + forwarder + push on rebuild. `ArrangementViewCore`: push at properties-window creation, at every `setEngineSampleRate` (to the window **and** every `m_clipRenderers`), and at renderer creation (`:1093`).
- `ClipRenderCore`: `setEngineSampleRate` + member; peak-resolution hint, trim tooltip, and fade-draw `visualSampleRate` now engine-derived (the fade-draw rate cancels mathematically — was provably visual-only — but is now honest).
- `ClipPanelKnobBridgeCore.h:92`: reads `m_clip->sourceSampleRate` (44100 only when the model reports unknown); the field had zero live consumers (dead write) — fixed to prevent future fabrication.
- `TimePitchExportCore.h:107` verified: comment-only; the code returns `windowSize/2` samples — rate-independent. No change.
- Review: remaining `44100` occurrences in these files are documented fallbacks only (setter clamps, member defaults, `sourceSampleRate > 0 ? : 44100`).

## 13. Debug/Release build evidence

| Gate | Result | Binary SHA-256 |
|---|---|---|
| Debug `build_apex.ps1 -Rebuild -Unsigned` | **PASS** (exit 0) | `FA5EF5C3912992C03DAD900BE7AC1EC1970B7AFECFC7D3B829A7FD330D659EAC` |
| Release `build_apex.ps1 -Rebuild -Unsigned` | **PASS** (exit 0) | `2E8720368487719E4F5268A7946D6B1F484CF8A0E01BA7F399FE4DEB7AFB043A` |

No new compiler warnings from the sample-rate plumbing (only pre-existing C4996 Font deprecations, C4456, C4100, and the noted C4244). Two transient OneDrive `.obj` write races (Debug `PatternClip`/`Clip`/`MidiClip`, Release `PatternClip`) occurred during the session — environmental sync locks, resolved by delete-and-retry; not code defects.

## 14. Debug/Release test evidence

| Gate | Result | Evidence manifest |
|---|---|---|
| Debug `run_apex_tests.ps1 -Seed 0xA9E12026` | **PASS** (exit 0, zero failed assertions) | `evidence/runs/runner-smoke/20260725T224803Z-Debug-78b267d` (`7485b3d30c36`) |
| Release `run_apex_tests.ps1 -Seed 0xA9E12026` | **PASS** (exit 0, zero failed assertions) | `evidence/runs/runner-smoke/20260725T225019Z-Release-78b267d` (`ce8cc9902511`) |

New suites (all green in both configurations): `device.capabilities.v1` (6), `device.config-validation.v1` (6), `callback-audit-period.v1` (5), `drumsampler.rate.v1` (6), `clip.timing-defaults.v1` (7), `metering.lufs-kweighting-rates.v1` (4). All pre-existing suites green — 256-sample baseline, 64-sample behavior, recording, PDC/click RT, automation RT, blade/split, step-sequencer determinism, writer integrity preserved.

## 15. Policy-script evidence

| Script | Result |
|---|---|
| `test_repository_policy.ps1` | **PASS** (exit 0) — application repository policy |
| `verify_dependencies.ps1` | **PASS** (exit 0) — pinned Windows dependencies verified |
| `test_validate_test_evidence.ps1` | **PASS** (exit 0) — 6 passed, 0 failed |

## 16. Remaining hardware-only verification matrix

Nothing below is claimed verified. Runbook for the Apollo Solo session (driver-supported cells only): 48 kHz 256→128→64→32, then 96 kHz 256→128→64→32 where offered, then remaining driver-supported rates (44.1k/88.2k/176.4k/192k spot cells). Per cell: enable `APEX_CALLBACK_AUDIT=1` (+ `APEX_CALLBACK_AUDIT_OUTPUT=<path>`), ≥10 min per scenario: empty project; 1-track rec; multi-track rec+monitor; rec+plugins; playback+plugins; heavy chain; sends/buses; automation rolling; loop; rapid transport; rapid rec/stop; long recording. Capture: `deadlineMisses`, `engineOverruns`, `lateDeliveries`, `ringOverflows`, `maximumConsecutiveMisses`, callback percentiles, `intervalMaximum`, CPU, xruns, per-stage timing of the slowest callback. Expected healthy state at 32: `ringOverflows ≈ 0` (post-fix), `engineOverruns = 0`; residual `lateDeliveries` interpretable as delivery jitter per §8. Success criterion: no APEX-attributable engine overruns; any residual misses classifiable as late delivery/driver/OS.

## Final status

- **32 samples: IMPLEMENTED / HARDWARE_UNVERIFIED** (EXPERIMENTAL_ULTRA_LOW_LATENCY; visible as `32 (experimental)` only when the driver offers it).
- **88.2 / 96 / 176.4 / 192 kHz: IMPLEMENTED / HARDWARE_UNVERIFIED** — automated evidence covers configuration math, timing, DSP derivation, metering conformance, and rejection behavior only.
- 44.1/48 kHz behavior, 256-sample baseline, and 64-sample behavior: preserved (four gates green; no steady-state processing math changed except the flagged DrumSampler resampling correction and the non-48k LUFS conformance fix).
- No commits were made; everything lives in the working tree.
