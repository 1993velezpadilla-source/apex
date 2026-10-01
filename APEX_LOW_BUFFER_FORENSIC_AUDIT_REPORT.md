# APEX — LOW-BUFFER / CLICK-POP FORENSIC AUDIT & HARDENING REPORT

**Date:** 2026-07-24 · **Branch:** feature/apex-windows-baseline-evidence · **Method:** measure-first static forensics → staged smallest-correct fixes → regression-gated validation
**Validated binaries:** Debug `786B9F59C3D73F11BEC840EC342DC3FA98A6DD6CCD2966567DD7470ED15E4DE7` · Release `5E35147DD3AA9DCBC1CD0E29B400A7BCC3AE113897CA08E841C6F3BF66386E23` (both unsigned, both with PDBs)
**Gates:** Debug build PASS · Release build PASS · Debug tests ALL PASS · Release tests ALL PASS (evidence manifests published under `evidence/runs/`)

---

## 1. Initial state

APEX already recorded cleanly at 256 samples and was "surprisingly usable" at 64 with occasional minimal clicks/pops, heavily optimized around a Universal Audio Apollo Solo. `APEX_PERFORMANCE_BASELINES.md` (2026-07-24) documented known risks: `connectionGainRamps_` first-use emplace, on-demand per-clip DSP creation, and per-track plugin-chain traversal. The mandate: characterize and harden 64/128 while protecting the 256 baseline — measure, trace, prove, fix, validate; no speculative rewrites.

**Authority contract:** DAW_BRAIN §2 (callback deadline, prohibition set, worst-case-not-average measurement) governed the audit. Deadline reference: 64/128/256 samples at 48 kHz = **1.333 / 2.667 / 5.333 ms**; at 44.1 kHz = 1.451/2.902/5.805 ms; at 96 kHz = 0.667/1.333/2.667 ms.

## 2. Audio callback map (verified file:line)

```
ASIO driver → GuardedAsioDevice (SEH wrapper, DeviceCore/SafeAsioDeviceTypeCore.cpp:808)
→ MainComponent::audioDeviceIOCallbackWithContext        MainComponent.cpp:6080
   ├─ RealtimeDeviceCallbackGuard (offline/admission)   :6089
   ├─ PendingAudioPreparationGate (silence during reprepare) :6090-6096
   ├─ audit start ticks (APEX_CALLBACK_AUDIT=1)         :6098
   ├─ input acquisition → preallocated liveCallbackInputBuffer_ :6107-6144
   └─ processNextAudioBlock                             :6049 → ApplicationCore::getNextAudioBlock  ApplicationCore.cpp:846
        ├─ offline barrier re-check                     :855-860
        ├─ plugin transport-control flush (atomics)     :877
        ├─ input preservation                           :897-915   [STAGE Input]
        ├─ RECORDING capture → ThreadedWriter FIFO      :936-963   [STAGE Recording]
        ├─ C1 suspension check (plugin re-prepare)      post-recording
        ├─ metronome/count-in render                    :975       [STAGE Click]
        ├─ live input monitor (trim+meter only)         :984-986   [STAGE MonitorTrim]
        ├─ AudioEngine::process                         :988       [STAGE Engine]
        │    AudioEngine.h:403 → snapshot adoption (routing/automation/folderBus atomic loads) :563-566
        │    → version-change sync (node buffers/PDC)   :1490-1499
        │    → processWithSnapshot :1481 → per node: clips (try-lock), live-input mix,
        │      track input processing, MIDI, plugin chain (published snapshot), vol/pan,
        │      metering, mute/solo fades, sends, master-edge PDC, sidechain sends
        │    → master contract + sanitize → transport fade → transport advance
        ├─ MasterBusEngine (inserts, master chain, meters, fader, ceiling, dither, render tap) :996 [STAGE MasterBus]
        ├─ clickEngine_.sumIntoOutput (+C9 fade ramp)   :997       [STAGE ClickSum]
        ├─ ControlRoomEngine (monitor-only)             :1000      [STAGE ControlRoom]
        └─ count-in handoff via callAsync               :1038-1047
   ├─ step sequencer → drum sampler tail (conditional)  MainComponent.cpp:6057-6077
   └─ audit telemetry record (SPSC ring + accumulator)  :6152-6165
```

Publication model: routing graph, automation lanes, folder-bus state, per-chain plugin slots, chain latencies, chain map, clip-region entries, step-sequencer pattern — all **immutable COW snapshots adopted by atomic acquire-load**; each publisher now holds a **message-thread retire slot** so replaced snapshots are not destroyed on the audio thread.

## 3. Root causes discovered (evidence → mechanism → affected sizes)

| # | Location | Mechanism | Class | Sizes |
|---|---|---|---|---|
| 1 | `MeteringCore/LufsMeterCore.h:42,158-183` | unbounded `blockMeans_.push_back` + full-history `recomputeIntegrated()` **every callback ×6 meter instances** → CPU/callback grows linearly with session length | growth | 64/128 fatal over time; 256 eventually |
| 2 | `AudioEngine.h:1035-1041` | `connectionGainRamps_` no `reserve` + ramps sized to `blockSize_` only → rehash/realloc in callback; also on driver block-size reconfigure | allocation | 64/128 transient after routing changes |
| 3 | `AudioEngine.h:1502-1523` (old) | per-block `nodeLookup.clear()`+per-node emplace, `activeEdges` rebuild, ramp retirement → N hash-node allocs **every block** | allocation | all, worst at 64 |
| 4 | `StepSequencerModel.h:83` | `std::mutex` in `getSnapshotRT()` every block (and per pattern clip) | lock | all (beat sessions) |
| 5 | `AudioEngine.h:1735` (old) | full step-sequencer `Snapshot` deep copy per pattern clip per block | allocation | all (pattern clips) |
| 6 | `AutomationParameterRegistryCore.h:112-117` | registry `forEach` under `CriticalSection` for the **entire walk** every rolling block | lock/inversion | all; scales with param count |
| 7 | `PluginInstanceCore.h:1202-1274` (old) | per plugin-parameter per block: 3 heap `juce::String` builds + 3 `findID` locks + 2 `findLane` locks + 1 `registry.find` lock + `std::map` inserts | alloc+lock storm | all; worst with plugin-heavy sessions at 64 |
| 8 | `AudioEngine.h:1498-1499` + `MasterPdcCore.h:21-55` + `rebuildPdcLines` (old) | PDC resync on audio thread: **iterated live RoutingGraph + live pluginChains_ (data race)**, mass `clear()`/realloc/zeroed delay history, stepped delay | race+alloc+click | every plugin/routing edit during playback |
| 9 | `AudioFileManager.h:188` | `ScopedReadLock` 2×/clip/block; message-thread cache writes stall all clip rendering | lock | all with audio clips |
| 10 | `ApexTuneIntegrationCore.cpp:297,309` | `std::mutex` up to 3×/clip/block; interior raw pointer into mutable record vectors (latent UAF) | lock+UAF | tuned clips |
| 11 | `DrumSamplerVoicePool.cpp:11` | `ScopedLock` per note-on (proven single-threaded — unnecessary) | lock | drum sessions |
| 12 | `RecordingEngine.h:365` | per-block `TrackManager::getTrack` on live `juce::Array` (race; UAF if track deleted mid-take) | race/UAF | recording sessions |
| 13 | `ApplicationCore.cpp:652→PluginChainCore.h:1160-1193` | `setBusesLayout/prepareToPlay` on live plugins from message thread while RT inside same instance | crash class | sidechain edits |
| 14 | `pluginChains_` (map) + `ClipRegionPluginCore::entriesByClip_` | RB-tree/std::map read on RT concurrent with message-thread mutation | race/UB | plugin/clip-FX sessions |
| 15 | `PluginChainCore.h:1343` (old) | plugin destruction via `MessageManager::callSync` if last ref dropped on RT | unbounded block | plugin removal during playback |
| 16 | `AudioEngine.h:1113-1115` (old) | pitch smoother reset to **0 st** on transport discontinuity → swoop after play/seek/loop | artifact | pitched clips |
| 17 | Click path | summed after the transport fade (`ApplicationCore.cpp:997`); burst truncation at block boundary (`ClickRoutingCore.h:27`); stop cleared in-flight bursts | discontinuity | transport edges |
| 18 | PDC value changes | `setDelaySamples` stepped tap; B1 removed mass-clear but step remained | discontinuity | latency changes |
| 19 | Monitoring path | full playback PDC on monitored signal (`LiveMonitoringBypassCore` was dead code) | performer latency | high-latency sessions |
| 20 | `StepSequencerModel.cpp:42` | `getPattern` const returned reference to temporary (C4172 symptom) | UB | latent |
| 21 | Delay-line capacity math | `capacity*4` negative-offset window for runtime-created lines at small block (latent OOB read) | latent OOB | oversize blocks |
| 22 | CallbackAuditAccumulator | plain members written on RT, read on message thread (data race) | telemetry UB | audit mode |

## 4. Fixes implemented (behavior before → after; smallest architecture-correct change)

**Stage A (baseline hardening)**
- A1 `connectionGainRamps_`: `reserve(4096)` in `prepare()`; ramps (re)sized to worst-case 8192 at creation/re-prepare → no rehash, no ramp realloc, ever.
- A2 `processWithSnapshot`: node lookup + active-edge set + ramp retirement gated on adopted `snap->version` **and** snapshot pointer → zero per-block hash churn.
- A3 `LufsMeterCore`: fixed 1000-bin/0.1 LU gating histogram + dirty-flag recompute (≤2.5 Hz) → O(1) bounded per callback, memory constant; BS.1770 semantics within 0.1 LU. *(Tests: silence −inf, block-size independence 64/256/1024, gate behavior, allocation-free, 120 s bounded run.)*
- A5 `StepSequencerModel`: mutex → release/acquire atomic `shared_ptr` publication; transpose==0 fast path uses published snapshot directly (no deep copy).

**Stage B (realtime-safety conversion)**
- B1 PDC: `MasterPdcCore::sync`/`rebuildPdcLines` rewritten to consume **only** the adopted `RoutingSnapshot` + new atomically published chain-latency map (`publishChainLatencies` at chain create/change/clear/prepare; release/acquire ordering); in-place line updates (no mass clear/free/zero); worst-case capacity (also fixes latent negative-offset OOB). Live-graph iteration on the audio thread: **eliminated**.
- B2 automation: KeyRegistry/LaneStore/ModeState/Registry converted to COW published snapshots with lock-free RT accessors (`findIDRT/findLaneRT/getModeRT/isReadingRT/findRT/forEachRT`); registry uses shared ownership + retire-on-unregister; `PluginInstanceCore::rebuildRtAutomationBindings()` builds all strings/IDs on the message thread (smoother seeded at current value) — per-block plugin-parameter automation is now **zero-alloc, zero-lock**; `PluginChainCore` automation pre-pass iterates the **published** slot snapshot (was a data race); `std::map` smoother states → pre-sized vectors. *(Tests: `automation.rt-access.v1` — snapshots, retire semantics, generations, allocation-free proofs, end-to-end evaluator.)*
- B3 `AudioFileManager`: `changeGeneration_` on all 7 mutation sites; engine `resolveClipAudio()` per-clip cache (steady state: 1 map find + 1 atomic load) + 256-slot retire ring (large `CachedAudio` destruction deferred to `prepare()`). VocalTune: snapshot mutex → atomic publication; interior pointer → `shared_ptr<const std::vector<float>>` (fixes latent UAF). DrumSampler: unnecessary lock removed (single-thread proven).
- B4 instrumentation: `CallbackAuditRecord` gains measured start-to-start `intervalTicks` (engine-overrun vs **late-driver-delivery** classification), 8-stage `stageTicks`, spike context (flags/track count/graph version); accumulator members → relaxed atomics (race fix); `maxRecord_` captures the slowest block's full context; report/JSON extended additively. *(Tests: `callback-audit-forensic.v1` — classification, maxRecord, reset, allocation-free.)*

**Stage C (approved product/design items)**
- C1 drain-gate: `PluginChainCore::wouldSidechainBusConfigChange` pre-check; only when a slot would actually re-prepare: suppress engine processing **after** recording capture (`pluginReprepareSuspensionActive_`) → `waitForRealtimeDeviceCallbacksToDrain(2000)` → re-prepare → resume; drain timeout skips + logs (config recomputed next topology event). Recording capture is never suspended.
- C2 `getPattern` UB → `getReference`.
- C3 `pluginChains_` → `shared_ptr` map + COW snapshot (`publishPluginChainsSnapshot`); all engine chain finds read the per-block snapshot (1 atomic load/block).
- C4 `ClipRegionPluginCore`: entries → `shared_ptr`, `bypassed` → atomic, COW `publishEntries()` + retire slot; RT reads hold the snapshot per block.
- C5 `callSync` deleter → bounded 256-slot retire queue drained ~2 s from `inputWatchdogTick`; **no plugin destruction or callSync on the audio thread**.
- C6 monitoring-PDC: `LiveMonitoringBypassCore::Mode` wired into the master-edge path; **default Bypass** for record-armed live-monitored tracks (skip compensation; ring stays fed → continuous history); Reduced caps compensation at `max(2×blockSize, 1024)`; Full preserved; `setMonitoringPdcMode` engine API.
- C7 PDC delay lines (both `ApexPdcDelayLineCore` and `MasterPdcCore::DelayLine`): any effective-delay change after priming arms a ~5 ms equal-gain crossfade (covers resyncs **and** Bypass/Reduced toggles); first use starts clean from silence. *(Test: crossfade blends then settles; disabled/first-use = clean step.)*
- C8 pitch smoothers reset to each clip's **current target** (`AudioClip::getPitch()` atomic) under `ScopedTryLock`; legacy 0 st fallback.
- C9 click: engine stores the exact per-sample transport fade ramp (`getTransportFadeRamp`); click sum applies it samplewise; truncated bursts carry tails across block boundaries (`carryTailIntoBlock`). *(Tests: tail carry exactness; ramp scaling; zero-ramp silence.)*
- C10 `prewarmClipDSP(clipId)` (message thread) creates pitch smoother/independent pitch core/time-pitch DSP; fired from `ClipManager::createAudioClip` + `recreateClipFromState` via `onAudioClipReady`; small ramp/fade cores remain lazy (approved).
- C11 recording: takes bind `resolvedTrack`/`liveWaveform` at `beginRecording` (no live TrackManager iteration on RT); `TrackManager::trackDeletionGate` (installed by ApplicationCore) **refuses deletion of any track with an active take**; `RecordingEngine::isTrackBeingRecorded`.
- Retirement bundle: retired-snapshot slots on RoutingSnapshotPublisher, FolderBusStateModel, all four automation stores, StepSequencerModel — replaced snapshots destroyed on the message thread at next publish (documented residual: two publishes inside one audio block — unreachable via UI cadence and the preparation gate).

## 5. Things inspected and proven safe (no modification needed)

- **Recording disk handoff** — `RecordingEngine::processBlock` lock-free; JUCE `ThreadedWriter` AbstractFifo with non-blocking whole-block drop on full (~0.34–0.74 s capacity); file open/close/flush/WAV-header finalize on message thread or `Priority::high` TimeSliceThread; no thread join per take.
- **Double-monitoring fix intact** — dry owner exactly one (`addLiveInputToTrackBuffer`); wet owner exactly one (track plugin chain); no second injection point (dead `TrackInputMonitorEngine` confirmed never instantiated).
- **Transport fade** — per-sample one-pole ~5 ms soft start / single-block linear stop fade, sample-rate-derived, block-size-independent, covers playback + monitoring (and now click via C9).
- **All gain/mute/trim/monitor smoothers** — per-sample one-pole with SR-derived coefficients (block-size independent).
- **Routing publication** — atomic acquire/release `shared_ptr`; cycle detection at connect time (message thread); versioned plain-data snapshots.
- **Plugin hosting** — instantiation/removal/bypass crossfade (5 ms equal-power)/editor lifecycle all message-thread; processing via published chain snapshot.
- **Master chain** — ceiling/dither/meters/render tap preallocated; dither default-Off.
- **TransportController** — fully atomic; `setPositionFromAudioThread` atomic store.
- **ClickCore** — stack beat array, pre-rendered sounds, no locks/heap.
- **Offline export isolation** — `OfflineRenderBarrierCore` + callback drain + fresh snapshot publication; export publication preserves previous artifact (test-covered).

## 6. Performance evidence

- **Deadline contract (Brain §2):** 64/128/256 @48 kHz = 1.333/2.667/5.333 ms. Audit targeted worst-case contributors, not averages.
- **Eliminated unbounded/growing costs:** LUFS recompute O(session)→O(1) ×6 meters; per-block node/edge hash churn (N allocs/block)→0; plugin-parameter automation (3 String builds + 6 locks + map insert)/param/block → **0**; automation registry walk under global lock → lock-free snapshot; clip audio reads 2 ReadWriteLocks/clip/block → 0 steady-state; VocalTune 3 mutexes/clip/block → 0; step-sequencer snapshot mutex → 0.
- **Eliminated change-block hazards:** PDC resync (race + mass free + zeroed history + step) → published snapshot + in-place crossfaded update; topology-change lookups → version-gated; live re-prepare race → bounded drain-gate.
- **B4 instrumentation (runtime, `APEX_CALLBACK_AUDIT=1`):** whole-callback duration vs nominal period, measured start-to-start interval, p50/p95/p99/p99.9/p99.99/max, consecutive misses, **engineOverruns vs lateDeliveries** (driver/OS fault isolation), per-stage breakdown of the slowest callback with session context; 5 s log + optional atomic JSON (`APEX_CALLBACK_AUDIT_OUTPUT`). Overhead when disarmed: ~2 relaxed loads per callback.
- **No hardware runtime numbers are claimed in this report** — the runtime matrix is scheduled on the real Apollo Solo (Section 7).

## 7. Test matrix

**Automated (all passing, Debug + Release):** 20+ suites — callback-audit ring/accumulator/forensic, LUFS (incl. **block-size independence 64/256/1024**), automation RT access, PDC/click RT (C7/C9), recording writer integrity, live-record waveform **low-buffer (64/128/512)**, recording identity, clip split/blade/source-render, audio-cache share chain + change generation, step-sequencer (7 suites incl. **deterministic across block sizes**), smoke. Evidence manifests: `evidence/runs/**/manifest.json` (zero failed assertions).

**Hardware runtime matrix — NOT YET RUN (no faked data). Runbook for the Apollo Solo session:**

| Buffer | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| 64 / 128 / 256 / 512 / 1024 / 2048 | ✓ each | ✓ each | ✓ where supported |

Per cell: enable `APEX_CALLBACK_AUDIT=1` (+ `APEX_CALLBACK_AUDIT_OUTPUT=<path>`), run ≥10 min per scenario: empty project; 1-track rec; multi-track rec+monitor; rec+plugins; playback+plugins; heavy chain; sends/buses; automation rolling; loop; rapid transport; rapid rec/stop; long recording. Capture: deadlineMisses, maxConsecutive, **engineOverruns vs lateDeliveries**, intervalMax, maxRecord stage breakdown, xruns, CPU. Success criterion: no APEX-attributable engine overruns; any residual misses classifiable as late delivery/driver/OS.

## 8. Regression status (all four gates green)

| Area | Status | Evidence |
|---|---|---|
| Recording | PASS | writer-integrity, live-waveform-low-buffer, identity suites; C11 lifetime + deletion gate |
| Monitoring | PASS | one-owner paths preserved (static re-verified); C6 default Bypass changes compensation policy as approved |
| Routing | PASS | snapshot model intact; sends/buses/master unchanged; C3 publication |
| Plugins | PASS | hosting/bypass/latency paths green; C1 drain-gate, C3/C4/C5 hardening |
| PDC | PASS | B1 published recompute; C7 crossfaded changes; C6 modes |
| Automation | PASS | B2 RT accessors; evaluator end-to-end test; mixer lanes unchanged |
| Transport | PASS | fades preserved + extended to click (C9); C8 no-swoop reset |
| Export | PASS | offline barrier + publication-preserve tests green; no RT-only behavior leaks into export |
| **256 protected baseline** | PASS | no behavioral change to steady-state processing math; only safety/ownership/timing hardening |

## 9. Remaining limitations (classified)

- **APEX engine (residual, bounded, documented):** first-use hash-node inserts for newly created clips/tracks/edges mid-session (maps reserved — no rehash); `connectionGainRamps_` erase frees one small ramp on version-change blocks; retired-slot residual requires two publishes inside one block; `PluginChainCore.h:788` sidechain `combinedBuffer_` growth for >2-channel sidechain plugins; `ClipRegionPluginCore.h:300` unguarded DBG in **Debug builds only**; send-level drags publish a full snapshot per UI tick (message-thread cost, not RT); MIDI playback path retains brief JUCE-standard locks (`MidiMessageCollector`, keyboard, note list); automation is block-rate (10 ms de-zipper), not sample-accurate; LUFS integrated value quantized ≤0.1 LU at the gate boundary; no plugin silence/sleep optimization (CPU floor = full chain); no plugin sandboxing (in-process hosting — a plugin crash is a host crash; scanning is out-of-process).
- **Third-party plugins:** internal `processBlock` safety is out of APEX's control; runtime self-reported latency changes are still **not detected** (PDC refreshes on next chain edit — flagged follow-up).
- **Driver/device:** ASIO xrun visibility depends on the driver; WASAPI exposes input-side xruns only; ASIO4ALL provisional patch per build docs.
- **OS/hardware:** MMCSS priority, DPC/ISR, storage stalls, and scheduling are environmental; B4's late-delivery metric exists precisely to attribute these correctly instead of blaming the engine.

## 10. Final verdict

**PARTIAL → pending hardware verification.**

Rationale (no exaggeration): every realtime-safety violation found by static forensics has a root-caused, smallest-correct, regression-tested fix; all four gates are green in both configurations; the 256 baseline is behaviorally preserved; forensic instrumentation to prove/disprove future spikes is shipped. What is **not** yet evidenced: sustained runtime behavior at 64/128 on real hardware across the full scenario matrix. The path to **VERIFIED_LOW_BUFFER_CANDIDATE** is exactly the Section 7 runbook: if the matrix shows zero APEX-attributable engine overruns at 64/128 with recording + monitoring + plugins over long sessions, the verdict upgrades without further code change.

**PRODUCTION_CANDIDATE at 256 samples** (protected baseline, unchanged and fully gated).
