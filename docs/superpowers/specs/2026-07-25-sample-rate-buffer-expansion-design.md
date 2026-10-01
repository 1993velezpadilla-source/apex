# APEX — Professional Sample-Rate Set + 32-Sample Buffer Expansion — Design

**Date:** 2026-07-25
**Status:** Approved decisions recorded; implementation authorized by user (2026-07-25). **No git commits — working tree only.**
**Authority:** DAW_BRAIN v7 §2 (callback deadline), §3 (requested-vs-actual device configuration), §8 (BS.1770 metering), §28 (test evidence), §34 (telemetry overflow policy), §41/§42 (agent evidence + engineering memory).
**Hardware status:** 32-sample buffers and rates above 48 kHz remain **EXPERIMENTAL_ULTRA_LOW_LATENCY / HARDWARE_UNVERIFIED** until the Apollo Solo matrix is run. No combination is claimed VERIFIED from automated tests alone.

---

## 1. Goal

Extend APEX beyond the working 44.1/48 kHz configuration:

- Buffer sizes: **32, 64, 128, 256, 512, 1024, 2048** — where the active driver/device actually exposes them. 32 is exposed as `32 (experimental)` and treated as EXPERIMENTAL_ULTRA_LOW_LATENCY until hardware validation.
- Sample rates: **32000, 44100, 48000, 88200, 96000, 176400, 192000 Hz** — device-enumerated ∩ this professional set.
- The device/driver is the authority for selectable rates and sizes. No unsupported configuration is forced, synthesized, or offered.
- Preserve: 256-sample protected baseline, current 64 behavior, realtime safety, rollback behavior, offline-export isolation, one-owner monitoring.

## 2. Verified repository facts (evidence before design)

### 2.1 What already works (do not redesign)

- **Safe reconfiguration path exists and is complete.** `DeviceSessionCore::commit` (`Source/DeviceCore/DeviceSessionCore.h:130-272`) blocks during recording, validates, calls `beforeDeviceMutation_` (bound to `MainComponent::prepareForAudioDeviceMutation` → `cancelAndJoinExportBeforeAudioRelease`, `MainComponent.cpp:6470-6500`), then `AudioDeviceManager::setAudioDeviceSetup`. JUCE stop/start fires `MainComponent::audioDeviceStopped` (`MainComponent.cpp:6290`) → `releaseStoppedDeviceResources` and `audioDeviceAboutToStart` (`:6180`) → `applyAudioDevicePreparation` (`:6205-6240`), which re-prepares the full engine stack (`ApplicationCore::prepareRenderGraph`, `ApplicationCore.cpp:806-850`: master chain, MIDI, clip-region plugins, `AudioEngine::prepare` `AudioEngine.h:344-448`, recording, live-input monitor, click, meters, per-track input chains, **all plugin chains' `prepareToPlay`**, PDC latency republish, master bus, control room), resizes worst-case buffers (`jmax(block, 8192)`), and **recomputes the B4 nominal period from the actual device rate/block** (`MainComponent.cpp:6234-6235`). Failure rolls back to the previous type/setup (`DeviceSessionCore.h:235-236, 265-266`).
- **JUCE enumeration is already device-authoritative.** ASIO probes `canSampleRate()` over a candidate list that includes 176400/192000 (`juce_ASIO_windows.cpp:358-395`); buffer sizes come from driver min/max/preferred/granularity and include 32 whenever min ≤ 32 (`:938-964`); the exact requested in-range size is passed to `ASIOCreateBuffers` (`:487`). `SafeAsioDeviceTypeCore` SEH-wraps but does not alter enumeration (`SafeAsioDeviceTypeCore.cpp:645-659`) and applies nearest-match sanitisation only inside the open call (`:471-522, 760-761`).
- **No clamp in `Source/` rejects 32 samples.** All `8192` constants are grow-only worst-case over-allocation (AudioEngine.h:351,1171,1175,1423; ApplicationCore.cpp:797; MainComponent.cpp:6213; PluginChainCore.h:113; RecordingEngine.h:101; MasterPdcCore.h:84; LiveInputMonitorEngine.h:27,39; LiveInputBusManagerCore.h:15,19; LufsMeterCore.h:30). No `jmax(64,…)` on block size anywhere.
- **DSP timing is overwhelmingly rate-derived already.** Transport fades, gain/mute/trim/monitor smoothers, click synthesis/pattern/count-in, PDC fade (`MasterPdcCore.h:37` = 5 ms × fs), step-sequencer tick math, VocalTune, TapeStop, meters' windows/decays all derive from the prepared sample rate. Plugin prepare propagation reaches every slot.

### 2.2 Defects to fix (C-class, evidence-cited)

| # | Location | Defect | Consequence |
|---|----------|--------|-------------|
| C1 | `Source/DeviceCore/DevicePanelModelCore.h:96,101` | Hardcoded rate list `{44100,48000,88200,96000}` and buffer list `{64…2048}` consumed by the active panel (`Source/UICore/AudioDevicePanelUI.h:263-264`) | 32000/176400/192000 and 32 unselectable; UI not device-authoritative |
| C2 | `Source/DrumSamplerCore/DrumSamplerVoice.cpp:30,50-52` | `sampleRateRatio = 1.0f`; ADSR stage lengths computed at hardcoded 44100 Hz | Pads play wrong pitch/speed when file rate ≠ engine rate; envelope times wrong at any rate ≠ 44.1k |
| C3 | `Source/MidiCore/MidiClip.cpp:14` | Default length `352800` samples pinned to 44.1 kHz; no device-rate-change re-sync | New MIDI clips wrong musical length at other rates; tick length drifts on rate change |
| C4 | `Source/MeteringCore/LufsMeterCore.h:96-137` | Exact BS.1770 K-weighting only at 48 kHz; RBJ approximations (1500 Hz/+4 dB/Q .707 shelf, 38 Hz/Q .5 HP) elsewhere | LUFS readings deviate from the standard at all non-48k rates |
| C5 | `Builds/VisualStudio2026/ArrangementEditor/ClipAutomationPanel.h:383-384, 642-643` | `clip_.startTime/length * 44100.0` (seconds→samples) for automation region targets | Automation regions mis-registered at non-44.1k rates (**audio-affecting**) |
| C6 | `Builds/VisualStudio2026/ArrangementEditor/ClipPanelKnobBridgeCore.h:92` | `out.sampleRate = 44100.0` with `TODO: read from actual source file` | Consumers receive a fabricated rate |
| C7 | `Builds/VisualStudio2026/ArrangementEditor/ClipRenderCore.cpp:70,537,729` | `* 44100.0` / `visualSampleRate = 44100.0` in waveform-peak resolution hint, trim tooltip, fade draw | Visual-only, but fade/peak visuals misalign at non-44.1k rates |
| C8 | `Source/ClipCore/Clip.h:133`, `Clip.cpp:131` | Default clip length `44100` samples ("1 second") | Default-length clips 0.92 s @48k … 0.23 s @192k |
| C9 | `Source/DiagnosticsCore/CallbackAuditCore.h` + `MainComponent.cpp:6644` | Audit ring = 1024 records drained every 5.0 s fixed | At 48k/64 → 750 cb/s → ~2726 ring overflows per cycle (cosmetic; accumulator is authoritative, drained records are discarded) |

### 2.3 Proven non-defects (§6 of work order — measured, not speculative)

- **ringOverflows at 64:** capacity/drain-cadence mismatch proven by arithmetic (1024 slots vs 3750 pushes/5 s @48k/64). Records are folded into the accumulator on every callback (`MainComponent.cpp:6176`) and ring pops are discarded (`:6648-6649`); no evidence is lost. The counter currently measures drain cadence, not data loss.
- **lateDeliveries:** threshold math is device-derived and correct (`interval > nominal period`, strict). At small periods it is a **delivery-jitter metric**, not an audible-failure or engine-overrun signal. Semantics preserved unchanged by user decision; documentation added.
- **Stream/device reconfiguration:** first callback after reconfigure cannot produce a false late-delivery (`callbackAuditPrevStartTicks_` reset, `streamGeneration` incremented, accumulator reset — `MainComponent.cpp:6231-6237`).

## 3. Approaches considered

### Device capability exposure (C1)

- **A1 (chosen): Device-authoritative staged panel.** Extend `DevicePanelModelCore` with `getAvailableSampleRates(type, device)` / `getAvailableBufferSizes(type, device)` using the established fast-path + temp-device + per-session-cache pattern of `getOutputChannelChoices` (`DevicePanelModelCore.h:113-158`). Intersect rates with the professional set; buffers with the 32–2048 ladder; pure helper functions extracted for unit tests. Panel populates combos from enumeration; `DeviceSessionCore::validate` rejects out-of-list values with a reason; commit read-back of granted values stays.
  - *Pros:* satisfies device authority + professional-set intersection + testability; preserves staged apply/rollback UX; no new UI.
  - *Cons:* temp ASIO probe can block (seconds for ASIO4ALL) — mitigated by fast path (open device answers directly) and per-session cache; probe happens at most once per device per panel session.
- **A2: Revert to the legacy `AudioDeviceSelectorComponent` panel.** Already device-authoritative but abandons the staged panel, shows non-professional oddball rates (violates the intersection decision), and regresses UX. Rejected.
- **A3: Extend the static lists.** Minimal diff but keeps UI authority above the device (violates §3 contract and user Q4/Q5 decisions). Rejected.

### MidiClip timing (C3)

- **B1 (chosen): rate-derived construction + tick-preserving re-sync.** Default length computed from the engine rate at construction (4 bars @120 BPM = 8 s × fs); on device rate change, tick length is the invariant and sample length is recomputed (`samplesToTicks`/`ticksToSamples` at the new rate). *Pros:* musical content stable across rate changes; sample domain derived per §3 actual-configuration rule. *Cons:* requires an explicit rate-change broadcast to MIDI clips.
- **B2: pure tick-domain length.** Larger refactor of persistence/serialization; out of scope. Rejected.

### LUFS K-weighting (C4)

- **L1 (chosen): keep exact published 48k coefficients when |fs−48000|<1 (bit-exact, zero regression); for all other rates compute both biquads from the BS.1770 analog prototype** using the refined prototype constants (shelf f0=1681.974450955533 Hz, G=+3.99984385397 dB, Q=0.7071752369554193; RLB HP f0=38.13547087602444 Hz, Q=0.5003270373238773 — the De Man refinement used by reference implementations). *Pros:* standard-derived at every rate; 48k path untouched. *Cons:* none material; validated by response tests.
- **L2: table of precomputed coefficients per supported rate.** Exact but adds a maintenance table and leaves 32k/odd rates approximate. Rejected (L1 covers any rate).

## 4. Design

### 4.1 Device-authoritative enumeration (C1)

`DevicePanelModelCore` gains:

```cpp
// Pure, unit-testable helpers (free functions or static):
juce::Array<double> intersectProfessionalRates(juce::Array<double> deviceRates);   // ∩ {32000,44100,48000,88200,96000,176400,192000}
juce::Array<int>    filterSupportedBufferSizes(juce::Array<int> deviceSizes);       // ∩ {32,64,128,256,512,1024,2048}
juce::String        formatBufferSizeLabel(int size);                                // 32 → "32 (experimental)"
bool                isExperimentalUltraLowLatency(int size);                        // size == 32

// Device-facing enumeration (same fast-path/temp-device/cache pattern as getOutputChannelChoices):
juce::Array<double> getAvailableSampleRates(const juce::String& typeName, const juce::String& outputDeviceName) const;
juce::Array<int>    getAvailableBufferSizes(const juce::String& typeName, const juce::String& outputDeviceName) const;
```

Behavior:
- Fast path: if the requested device is the currently open device, read `getAvailableSampleRates()`/`getAvailableBufferSizes()` from it directly. Otherwise create a temporary device once per panel session (cached, exactly like channel choices).
- Rates: device list ∩ professional set, ascending. If the intersection is empty (driver reported nothing), fall back to the current setup rate so the combo is never empty; the commit read-back remains the final authority.
- Buffers: device list ∩ ladder, ascending; 32 is included only when the device reports it and is labeled `32 (experimental)` at the UI layer. WASAPI shared: JUCE's enumerated list starts at 64 → 32 simply never appears (accepted; no synthesis).
- Oddball driver rates outside the professional set are logged in the final report, not offered.

`AudioDevicePanelUI` (`Source/UICore/AudioDevicePanelUI.h`):
- `refreshFromSnapshot` and type/device combo changes repopulate the rate/buffer combos from the model enumeration for the **currently selected** type+output device (not merely the open one).
- Buffer combo items use `formatBufferSizeLabel`; value parsing strips the label suffix.
- Post-commit read-back of granted rate/size is retained (`:705-711`).

`DeviceSessionCore::validate` (`DeviceSessionCore.h:51-103`):
- After existing checks, when the model can enumerate the selected device, reject a requested rate/buffer outside the enumerated∩professional lists with an explicit reason ("Selected sample rate/buffer size is not supported by the device"). Rollback behavior unchanged.
- Validation is advisory-bypassed only when enumeration is impossible (device creation failed): commit proceeds and JUCE/Safe-ASIO nearest-match + read-back govern, as today.

### 4.2 DrumSampler rate derivation + pad resampling (C2)

`DrumSamplerVoice`:
- New member `double engineSampleRate_ = 44100.0;` with `setSampleRate(double)` invoked from the engine/pool at prepare (engine stores the prepared rate; `DrumSamplerEngine` passes it to every voice on prepare and before `start`).
- Source playback position becomes double-precision source-domain: initialised from the pad start offset at `start()`; per output sample, read with **linear interpolation** at `srcPosition_`, then `srcPosition_ += rateRatio_` where `rateRatio_ = layer.sampleRate / engineSampleRate_` (captured at `start()`; 1.0 when either rate is invalid). End-of-layer when `srcPosition_ >= layer.numSamples` (respecting `endSample` where already honored).
- ADSR: `attackSamples = pad_->attackMs * 0.001f * (float) engineSampleRate_` (same for decay/release).
- Voice stealing/choke/reverse behavior unchanged. Linear interpolation is the smallest correct resampler for one-shot drum playback; higher-quality SRC is a documented future upgrade if aliasing is ever measured (BS.1770-style evidence required first).

Tests: envelope stage durations invariant in milliseconds across 44.1/48/88.2/96/176.4/192k; a 44.1k-sourced pad played at 48/96/192k preserves pitch (zero-crossing or FFT peak within tolerance) and duration in seconds; 48k pad at 44.1k likewise.

### 4.3 MidiClip / Clip default timing (C3, C8)

- `MidiClip` ctor: default sample length = `8.0 × sampleRate_` (4 bars @120 BPM), i.e. rate-derived, not 352800.
- `MidiClip::setSampleRate(double)` (already exists, called from `MainComponent.cpp:1091-1099` on tempo change): extended so that when the rate changes, the **tick length is preserved** and the sample length recomputed. Construction call sites pass the current engine rate.
- Device-rate-change propagation: `MainComponent::prepareToPlay` (`:6005-6019`) already pushes the new rate to the arrangement; extend the same message-thread path to re-sync every MIDI clip's `setSampleRate` (enumeration via the same owner used by the tempo-change path). Recording-target creation path (`MidiInputCore.h:235`) unchanged.
- `Clip` default `length_` (Clip.h:133 / Clip.cpp:131): "1 second" becomes engine-rate-derived at creation (creation sites know the engine rate; member default stays as an inert pre-construction placeholder). Persistence formats unchanged in both classes.

### 4.4 LUFS K-weighting at all supported rates (C4)

- `Biquad::setCoefficientsForSampleRate(fs, shelf)` (`LufsMeterCore.h:94-140`):
  - `|fs − 48000| < 1` → keep the published BS.1770 coefficients verbatim (bit-exact; zero regression at the reference rate).
  - Otherwise compute the shelf biquad from prototype (f0=1681.974450955533, G=+3.99984385397 dB, Q=0.7071752369554193) and the HP biquad from prototype (f0=38.13547087602444, Q=0.5003270373238773) via the standard biquad transform at `fs`.
- Gating windows are already rate-derived (`WindowMeanSquare::prepare`, `:154-158`); 400 ms/100 ms timing unchanged.
- Tests: 48k coefficient identity (exact); computed-vs-published coefficient proximity at 48k documented (≤1e-3, proving the prototype derivation); magnitude-response sanity at 32000/44100/88200/96000/176400/192000; integrated loudness of a −23 dBFS 1 kHz sine ≈ −23.0 LUFS ±0.2 at each supported rate; block-size independence extended to 32 samples.

### 4.5 ArrangementEditor 44100 conversions (C5, C6, C7)

- **C5 (audio-affecting):** `ClipAutomationPanel.h:383-384, 642-643` — plumb the engine sample rate into the panel (single member set by the creating context, which has engine access; inert 44100 default until set) and use it for both seconds→samples conversions. Automation region targets then register at the correct samples at every rate.
- **C6:** `ClipPanelKnobBridgeCore.h:92` — resolve `out.sampleRate` from the clip's actual source rate (via the owning audio-file cache / clip model); remove the fabricated constant. Consumer audit during implementation determines whether any consumer is audio-affecting; fix is identical either way.
- **C7 (visual-only):** `ClipRenderCore.cpp:70,537,729` — use the same plumbed engine rate (the panel/render core receives it once) for the peak-resolution hint, trim tooltip, and fade-draw math. Each site is visual-only; fixed anyway because the plumbing is shared with C5. Any site where a rate genuinely cannot reach without redesign is documented in the final report as a proven visual-only approximation.

### 4.6 B4 CallbackAuditCore (C9)

- Extract pure period math into `CallbackAuditCore.h`:

```cpp
constexpr int64_t computeCallbackPeriodTicks(int numSamples, double sampleRate, double ticksPerSecond) noexcept;
double computeAuditDrainIntervalSeconds(double periodSeconds, size_t ringCapacity) noexcept; // clamp(0.5×capacity×period, 0.05, 5.0)
```

- **Ring drain is decoupled from the 5 s report.** Today one guard (`MainComponent.cpp:6644`) covers both the ring pop-all and the log/JSON write; making that single interval adaptive would spam the log at ultra-small buffers and, clamped at 0.25 s, would still overflow the 1024-slot ring at 192k/32 (6000 cb/s → 1500 pushes/0.25 s). The fix: drain the ring (cheap pop-all, records are discarded — the accumulator is authoritative) at an adaptive cadence satisfying `cadence ≤ 0.5 × capacity × periodSeconds` (floor 0.05 s), while the snapshot/log/JSON report stays on its existing 5 s cadence. Implementation uses the fastest safe cadence available at the drain site (timer-tick drain if the timer period satisfies the inequality, otherwise the computed adaptive interval); the timer period is verified during implementation and recorded in the final report. `ringOverflows` regains its §34.21 meaning (a genuinely stalled drain) instead of firing continuously by design: 48k/32 → ~0.34 s cadence; 192k/32 → ~0.085 s; 48k/2048 → 5 s (unchanged).
- Header documentation: `lateDeliveries` = strict start-to-start interval > nominal period — a **delivery-jitter metric**; at ultra-small periods jitter is expected and this counter alone does not imply an audible failure, deadline miss, engine overrun, or xrun. Semantics, thresholds, and all other counters unchanged.
- Tests: period-tick matrix over {32000,44100,48000,88200,96000,176400,192000} × {32,64,128,256,512,1024,2048} against exact rational expectations; drain-interval clamp bounds; existing ring/accumulator/forensic suites unchanged and re-run.

### 4.7 Reconfiguration path — verification, not redesign

The existing path (§2.1) satisfies every work-order §4 requirement. Work here is **regression evidence**: a prepare-propagation test proving a sample-rate change reaches plugin chains (`prepareToPlay` called with the new rate), engine, click, recording, and master bus, using existing test hooks; plus re-running the PDC/click RT, recording, and step-sequencer determinism suites.

### 4.8 Error handling

- Enumeration failure (temp device cannot be created): combos fall back to current setup values; commit remains possible; JUCE/Safe-ASIO nearest-match + read-back govern. Logged.
- Commit failure: existing rollback to previous type/setup (`DeviceSessionCore.h:235-236, 265-266`) — preserved, covered by no new code.
- Unsupported typed value: rejected in validate with explicit reason; device state untouched.
- Driver grants different values than requested: read-back displays actuals (existing); engine always prepares from actuals (existing §3 contract).

## 5. Test plan (work order §7 mapped)

| Requirement | Coverage |
|---|---|
| Sample/block period math, all rates × buffers (incl. 32 kHz) | New `computeCallbackPeriodTicks` matrix tests |
| Device-selection fallback + unsupported rate/buffer rejection | New `DeviceCapabilityTests`: intersection, ladder filter, 32-labeling, empty-list fallback, validate rejection |
| Serialization/persistence | No cross-session device persistence exists (verified: `createStateXml` never called; `MainComponent.cpp:6417,6425` initialise with null XML) — documented in report; in-session snapshot/validate round-trip covered in `DeviceCapabilityTests` |
| DrumSampler timing/resampling | New `DrumSamplerRateTests`: ADSR ms-invariance; pitch/duration preservation 44.1↔48↔96↔192 |
| LUFS | Extended `LufsMeterCoreTests`: 48k identity, per-rate response, sine loudness per rate, 32-sample block independence |
| MidiClip/Clip defaults | New defaults tests at 44.1/48/96/192; tick-preservation on rate change |
| Transport sample↔time | Matrix test over supported rates (TransportController is already rate-parameterized, `:237-248`) |
| Metronome timing | Click pattern matrix test at multiple rates (rate-derived already) |
| Fades/crossfades | Existing C7/C9 PDC/click RT suites re-run; transport-fade coefficient spot test at 44.1/96 |
| PDC sample calculations | Existing PDC suites re-run (PDC is sample-domain; rate change re-publishes latencies — propagation test) |
| Plugin prepare propagation | New/extended test: rate change → every chain slot receives `prepareToPlay` with new rate |
| Recording duration | Existing writer-integrity + low-buffer suites re-run (block sizes 32 added where hardware-independent) |
| B4 nominal deadline | `computeCallbackPeriodTicks` matrix + drain-interval tests |
| 32-sample acceptance | Static proof (no clamps) + engine smoke at 32-sample blocks where hardware-independent |

## 6. Regression gates (all required before completion)

1. `Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned` — exit 0.
2. `Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned` — exit 0.
3. `Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026` — exit 0, zero failed assertions.
4. `Scripts\run_apex_tests.ps1 -Configuration Release -Seed 0xA9E12026` — exit 0, zero failed assertions.
5. Policy gates: `test_repository_policy.ps1`, `verify_dependencies.ps1`, `test_validate_test_evidence.ps1` — exit 0.
6. 44.1/48 behavior preserved; 256 baseline behaviorally unchanged; 64 behavior preserved; no unsupported configuration forced.

## 7. Explicitly out of scope

- WASAPI 32-sample synthesis (device-infeasible; accepted).
- Driver-oddball rates outside the professional set (documented, not offered).
- Cross-session device-settings persistence (does not exist today; separate work order if desired).
- The dead "Advanced…" button (`onOpenAdvancedPanel` unbound) — noted, not fixed here.
- LiveRecordWaveformCore ring capacity (display-history depth shrinks at 32-sample blocks; documented limitation, not a functional defect).
- MasterOversamplingCore exact-size growth sensitivity (pre-existing; prepare-on-start mitigates; flagged in forensic audit §9).
- Any redesign of the engine, routing, monitoring, PDC, or export architecture.

## 8. Final-report obligations

The final report must state: files changed; sample-rate assumptions found (§2.2 table + B-class catalog); fixes made; enumeration behavior; 32-buffer implementation; B4 corrections; test evidence (manifests); build evidence (hashes); remaining hardware-only verification (Apollo Solo matrix: 48k 256→128→64→32, then 96k where supported, capturing deadlineMisses/engineOverruns/lateDeliveries/ringOverflows/maxConsecutive/percentiles/intervalMax/CPU/xruns/stage timing); and the exact status of 32 samples: **IMPLEMENTED / HARDWARE_UNVERIFIED** until the matrix passes.
