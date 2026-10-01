# APEX — CROSS-VENDOR AUDIO INTERFACE COMPATIBILITY FORENSIC AUDIT REPORT

**Date:** 2026-07-26 · **Branch:** `feature/apex-windows-baseline-evidence` · **Commits:** none (all work in the working tree)
**Authority:** DAW_BRAIN v7 §3 (request-versus-grant / device authority), §11 (cross-platform device contracts), §31 (recording/monitoring clock mapping), §34 (telemetry overflow policy). Vendor/host claims carry source URLs and are tagged FACT / TIME-SENSITIVE / UNKNOWN.
**Method:** current-code verification (file:line) + current vendor/DAW primary-source research + classification A–E. No architecture modified except one proven generic fix (§7.1). Historical session context used as leads only; everything re-verified against current code.

**Classification legend**
- **A** — Already correct (code-proven generically; family cells still need hardware confirmation where marked B).
- **B** — Needs hardware verification (generic path exists; behavior on real devices unproven).
- **C** — Proven generic host defect (one fixed this session; two deferred pending product decisions).
- **D** — Vendor/driver behavior requiring documentation (APEX correct; users need guidance).
- **E** — Potential vendor-specific quirk — do NOT patch without reproduction.

---

## 1. Executive summary

APEX's generic ASIO/WASAPI device contract is architecturally sound against the professional-interface behaviors researched. The request-versus-grant rule (Brain §3) is honored end-to-end: device-authoritative enumeration for rates/buffers (added 2026-07-25), engine preparation from the actual granted configuration, rollback on rejected configurations, and full re-prepare on driver-initiated resets (JUCE `sampleRateDidChange`/`kAsioResetRequest` → `audioDeviceAboutToStart` → `applyAudioDevicePreparation`).

**One proven generic defect was fixed during the audit (F1, §7.1).** Item 22 (project sample-rate identity) was subsequently implemented as pre-beta project-integrity work (§7.3). Item 19 (recording latency compensation) is designed but explicitly deferred pending product design (§7.2). Everything else classifies as A/B/D/E per the matrix. No vendor-specific hacks were added anywhere.

**Classification accounting (updated 2026-07-26 — the classes are NOT mutually exclusive by design):** an audit item may legitimately carry several classes (e.g., item 14 is **A** for the busy-device error path and **B** for the multi-client behavior matrix; item 8 is **A** for the generic reset path and **D** for the Focusrite driver history; item 16 is **D** for documented behavior and **E** for any future patch). Earlier in this report the tallies therefore summed to 35 over 25 items — intentional tagging overlap, not an error. The mutually-exclusive **primary** classification below is the authoritative accounting (sums to 25):

| Primary class | Count | Items |
|---|---|---|
| A (already correct) | 11 | 1, 4, 5, 6, 8, 9, 10, 11, 14, 23, 24 |
| B (needs hardware verification) | 6 | 3, 7, 12, 13, 18, 25 |
| C (proven generic defect) | 2 | 19 (deferred), 20 (**fixed**) |
| C → implemented | 1 | 22 (**implemented**, §7.3) |
| D (document) | 5 | 2, 15, 16, 17, 21 |
| E (no patch w/o repro) | 0 primary | — |

Secondary tags still apply where noted in §3: B also applies to items 14 and 24 (per-family cells); D also applies to items 8, 12, 13, 18, 25; E-tag on item 16 (ADM). The §9 E-class records are five separate quirk entries (not additional audit-item classifications).

## 2. APEX's generic device contract (verified current state)

- **Backends registered** (`SafeAsioDeviceTypeCore.cpp:1084-1091`): WASAPI shared, WASAPI exclusive, DirectSound, then Safe-ASIO (SEH-guarded wrapper). **Startup never auto-picks ASIO** (`MainComponent.cpp:5399-5440`) — shared "Windows Audio" first; ASIO only via explicit panel commit with verification and rollback.
- **Enumeration:** JUCE ASIO probes `canSampleRate` over a list including 176400/192000 and enumerates driver buffer sizes (32 included when min ≤ 32); APEX intersects with the professional set {32000…192000} / ladder {32…2048} (`DeviceCapabilityCore.h`), the panel offers only the intersection, and `DeviceSessionCore::validate` rejects unreported values. 32 appears only as `32 (experimental)` when the driver reports it.
- **Commit/reconfigure:** `DeviceSessionCore::commit` → recording guard → validation → export drain (`beforeDeviceMutation_`) → `setAudioDeviceSetup` → JUCE stop/open/start → `audioDeviceStopped`/`audioDeviceAboutToStart` → `applyAudioDevicePreparation` (re-prepares engine, plugins, recording, click, meters, master bus, PDC, audit period; rescales clips; re-syncs MIDI). Failure → rollback to previous type/setup with user-visible reason.
- **Driver-initiated changes (verified):** JUCE maps `sampleRateDidChange`, `kAsioResetRequest`, `kAsioBufferSizeChange` (SDK: arrives as ResetRequest in practice), `kAsioResyncRequest` to a 500 ms restart that re-opens and **adopts the driver's current values**; APEX re-prepares from the actuals. `kAsioLatenciesChanged` → JUCE refetches latencies. `kAsioOverload` → xrun counter (surfaced in B4 audit log).
- **Resilience:** ASIO zombie detector (callback proof-of-life, restart + non-ASIO fallback), input watchdog (dead-input repair via forced restart with rollback), device-panel "last known good" restore, recording-in-progress mutation guard.
- **Monitoring:** software monitoring with one-owner dry/wet invariant; monitoring-PDC **Full/Reduced/Bypass** (default Bypass for record-armed live-monitored tracks) to bound performer latency on high-PDC sessions.

## 3. Compatibility matrix (25 audit items)

| # | Item | Class | Evidence (code) + family notes |
|---|---|---|---|
| 1 | Device enumeration | A | `SafeAsioDeviceType::createDevice` scanned-list validation + default/first-output resolution (`SafeAsioDeviceTypeCore.cpp:1008-1074`); panel one-scan-per-open caches. Antelope: audio-plane vs Launcher-plane divergence is vendor-side (§5.2, D). |
| 2 | Stable device identity | D | Name-only (`DeviceRequest`); no stable IDs (NOT FOUND); JUCE `appendNumbersToDuplicates` (juce_WASAPI_windows.cpp:1998-1999); no cross-session persistence exists. Brain §3 wants stable IDs — relevant only if persistence is added later. |
| 3 | I/O topology and channel names | B | Read from device at open/probe with fast path + per-session cache (`DevicePanelModelCore.h:113-158`). Mid-session changes: see #20. Per-family I/O re-enumeration on SR change = B (hardware matrix). |
| 4 | Device-authoritative sample rates | A | `getAvailableSampleRates` probe ∩ professional set; validation rejects unreported (suites `device.capabilities.v1`, `device.config-validation.v1`). |
| 5 | Device-authoritative buffer sizes | A | Same path; 32 only when reported. RME USB-series min 48 → 32 correctly never offered; MOTU Gen5 32 → offered (B per driver). |
| 6 | Request-versus-grant readback | A | Panel reads back granted values post-commit (`AudioDevicePanelUI.h:705-711`); Safe-ASIO nearest-match only inside open (`SafeAsioDeviceTypeCore.cpp:471-522`); engine prepares from actual (`MainComponent.cpp:6205-6240`). |
| 7 | Device-owned control-panel settings | B | Pass-through `showControlPanel`; temp-device branch re-applies old setup after 400 ms (APEX-authoritative) while driver-side panel edits propagate via ResetRequest and are adopted. Current-device branch lacks a read-back (refreshes manager cache only) — documented; family panel behavior B. |
| 8 | SR changes initiated outside APEX | A | JUCE `sampleRateDidChange` → reset → APEX re-prepares at the **actual** new rate (verified chain, §2). Clock loss → `sampleRateDidChange(0)` tolerated via fallback. Focusrite pre-4.143 host-hang class = vendor driver (E/D, §5.4). |
| 9 | Buffer changes initiated outside APEX | A | `kAsioBufferSizeChange` → same reset path; SDK confirms drivers use `kAsioResetRequest` (asio.h:444-447). Per-family panel flows B. |
| 10 | Safe stop/reconfigure/reprepare/start lifecycle | A | Proven path (§2) incl. `PendingAudioPreparationGateCore` (silence during reprepare) and export drain. |
| 11 | Rollback after rejected configurations | A | `DeviceSessionCore.h:236-265` rollback + ASIO post-activation check + panel ⚠ display (verified strings). |
| 12 | Clock-source changes and synchronization | B/D | APEX exposes no clock UI (JUCE keeps clocks private, `juce_ASIO_windows.cpp:769-770, 1001-1037`); driver clock changes arrive as rate-change/reset and are handled per #8. Clock-loss/no-valid-rate tolerance B. No clock-status surfacing (D — vendor panels own this; UA shows red text, Focusrite "SYNCED"). |
| 13 | Hotplug/disconnect/reconnect | B/D | WASAPI: JUCE `IMMNotificationClient` → manager closes missing device → `audioDeviceStopped` → APEX releases resources (safe, silent — no Cubase-class "device removed" surfacing). ASIO: no notification mechanism exists in JUCE; driver ResetRequest (if sent) → restart; dead-callback → zombie detector restart/fallback. Disconnect mid-take: recording stops receiving callbacks (writer starves; user stops take) — hardware-matrix item. |
| 14 | ASIO exclusive/multi-client | A / B | Busy-device error path verified: driver error → JUCE "another application" text → wrapper → rollback + panel message (§2). Multi-client drivers (RME confirmed official; Focusrite 4.x fixed) work by driver grace — B matrix. |
| 15 | Windows WDM/ASIO sample-rate conflicts | D/B | Startup deliberately lands on shared WASAPI; user moves to ASIO manually. Vendor guidance (UA/Ableton/Steinberg): match Windows/device/DAW rates; Microsoft: exclusive mode preempts shared. APEX does not detect or warn on mismatch — documentation item (D), detection = optional follow-up. |
| 16 | ASIO Direct Monitoring (ADM) | D/E | Not implemented (JUCE advertises `kAsioSupportsInputMonitor` in the selector list but never issues `kAsioSetInputMonitor` — verified `juce_ASIO_windows.cpp:1386-1388` vs. no usage). APEX software monitoring + C6 Bypass covers the performer-latency need. Implementing ADM = E (no patch without a reproducing device). |
| 17 | External hardware mixer monitoring (UAD Console / RME TotalMix / dspMixFx / CueMix) | D | Structural double-monitoring risk is a **user-routing** matter — vendor mixers are invisible to the ASIO host (FACT: UA, RME, Focusrite, Antelope docs). APEX's one-owner monitoring invariant holds host-side. Guidance: use one path (Console/TotalMix direct monitoring **or** APEX software monitoring); Focusrite documents the exact echo symptom. |
| 18 | Reported input/output latency | B/D | Consumed for display only (`AudioDeviceSettingsModel.h:39-40`); wrapper pass-through (`SafeAsioDeviceTypeCore.cpp:901-909`). Known vendor misreport history: UA Solo USB (fixed 11.8.1), Focusrite accuracy fixes, RME hidden +32-sample USB safety buffer (RTL > 2×buffer). Do not assert latency from buffer size alone (D). Accuracy per family = B. |
| 19 | Recording offset correctness | **C** | **No input/output latency compensation on takes** (verified `RecordingEngine.h:211,229,590` + `RecordingClipFinalizerCore.h:71-80`; device latencies never consumed for placement). REAPER ("use audio driver reported latency" + manual offsets) and Logic ("Recording Delay") compensate by default. See §7.2. |
| 20 | I/O topology changes (e.g., Apollo custom I/O Matrix) | **C → fixed** | `needsInputBufferReprepare_` was set in the callback but **never consumed** — topology changes were silently clamped until next restart. **Fixed this session (§7.1).** Whether each vendor's matrix edit triggers a driver reset = B. |
| 21 | Device names/order changing after restart | D/B | In-session name identity + JUCE duplicate-numbering; JUCE tracks the live device by name and closes it if missing. No persistence → no cross-session exposure. Focusrite's model-generic name ("Focusrite USB ASIO") is vendor-correct (D). |
| 22 | Unsupported-rate project reopening | **C → implemented** | Was: project files stored **no sample rate**; sample-domain timelines had no rate identity. Now: rate persisted when proven, mismatch reconciled seconds-preserving, legacy/unknown handled explicitly — see §7.3. Suite `project.sample-rate-reconcile.v1` (11/11). |
| 23 | 32-sample availability varying by driver/device | A | Enumeration-driven; never forced; labeled experimental. Vendor floors documented: RME USB-series 48 / MADIface-series 32 (official forum, RME's Matthias Carstens), MOTU Gen5 32 @96k official, others UNKNOWN → B matrix. |
| 24 | High-rate channel-count reductions (ADAT S/MUX) | A | Channel counts/names read from the device at open; 64-channel input mask is clamped by the device (`DeviceSessionCore.h:206-207`); reduced high-rate counts are driver truth. Family matrices (UA/Antelope/Focusrite/MOTU publish scaling) = B cells. |
| 25 | Clean failure when hardware disappears | B/D | WASAPI: JUCE closes → safe release (proven path). ASIO: zombie detector restarts; watchdog repairs; no crash class found in loss paths. User-facing "device removed" surfacing = absent (D). Per-family unplug behavior (UA relay tone fixed 9.10; boot-order enumeration quirk) = B matrix. |

## 4. Vendor pattern log → APEX vulnerability assessment

Patterns from public vendor release notes/docs; each assessed against current APEX code.

1. **Host hang on sample-rate change** (Focusrite driver < 4.143.0, 2025-09-01 — "some ASIO DAWs hang on a sample rate change"; Steinberg AXR4T stall class requiring ASIO reset). *APEX assessment:* the reset path is JUCE's 500 ms close/reopen + APEX re-prepare — no indefinite blocking point found; the reset is timer-deferred while a vendor panel is modal. Vulnerability: **low**; verify on hardware (B). Guidance: keep Focusrite driver ≥ 4.143.0 (D).
2. **Driver exposing unsupported buffer options to hosts** (Focusrite < 4.65.5; Studio One/Reason/Sibelius affected). *APEX assessment:* APEX now offers only the driver-enumerated ∩ ladder set, and validation rejects anything unreported — **not vulnerable** by construction (A).
3. **Device running at a different rate than the host session** (Focusrite 4.63.24; vendor guidance to match rates). *APEX assessment:* engine always prepares from the device's actual rate (§2) — **not vulnerable** to silent mismatch; cross-subsystem (WDM vs ASIO) mismatch is a documentation item (15/D).
4. **Latency misreporting** (UA Solo USB 11.8.1; Focusrite accuracy fixes; Antelope "improved latency reporting" 5.01; RME hidden +32). *APEX assessment:* latency values are display-only; recording placement does not consume them (see C-19 — compensation deferred precisely because reports are proven unreliable). **Not vulnerable** today; any future compensation must tolerate misreports (manual offset).
5. **External mutation while host runs is normal** (RME Settings/TotalMix on-the-fly; MOTU "Sync Windows sample rate"; UA Console grayed-while-active; Antelope panel). *APEX assessment:* handled generically via the reset path (A-8/9) — **not vulnerable**; family specifics = B.
6. **WDM holder degrades the device under ASIO** (RME + Sonarworks Systemwide pattern: locks on-the-fly changes, robotic audio/BSOD reports; Microsoft exclusive-mode preemption). *APEX assessment:* environmental; APEX can only surface errors and stay stable (watchdog/zombie/rollback) — **tolerated**, documented (D).
7. **Topology re-enumeration on rate change is designed behavior** (UA I/O Matrix + S/MUX; MOTU published 18×22→8×12 scaling; Focusrite feature reductions at 176.4/192k; Antelope ADAT halving). *APEX assessment:* channel truth re-read at every open/restart; mid-session topology change previously under-handled — **fixed (F1)**.
8. **Driver stream flapping on client attach/detach** (MOTU stream-restart bugs fixed 2026-03; UA connect/disconnect churn fixed 9.13). *APEX assessment:* JUCE restart + APEX re-prepare is idempotent and gated; **tolerated**.
9. **Large-buffer ASIO output glitches** (MOTU 2048/4096 until 2026-05; UA ≥2048 "DAW unresponsive" on Twin USB). *APEX assessment:* buffer ladder tops at 2048 (offered only if driver lists it); residual glitches at extreme buffers are vendor/driver class — documented (D), hardware matrix cell.
10. **Vendor panel instability independent of the audio device** (Antelope Launcher/Manager-Server/Logi_LampArray_Service; Focusrite FC2 desyncs; UA Console known issues). *APEX assessment:* APEX never blocks on vendor panel state — control-panel launch is a bounded pass-through with forced-repaint feedback. **Not vulnerable.**
11. **Thunderbolt-generation incompatibility below the ASIO layer** (PreSonus TB2 on TB4 hosts; Antelope TB2≠TB4; Focusrite Clarett TB caveats). *APEX assessment:* device simply won't enumerate/function at bus level; APEX's scanned-list validation and safe fallback handle absence cleanly — documented (D).
12. **Boot-order / power-up enumeration quirks** (Apollo USB fails to enumerate on simultaneous power-up; Antelope USB reconnect sequencing for clock changes). *APEX assessment:* panel re-scan per open + watchdog repair covers late appearance; startup deliberately avoids ASIO until user-selects — **tolerated** (D).

## 5. Family profiles (audit-relevant facts; sources cited, classed)

### 5.1 Universal Audio Apollo (Solo / Twin / x-series — primary target)
- Windows buffer size is a **UAD Console hardware setting** (grayed values depend on SR; DAW-adjustable since UAD 9.12.2); SR via Console or DAW (grayed while host active) — FACT (help.uaudio.com/hc/en-us/articles/25403573794836).
- Console DSP mixer = below-driver hardware monitoring; **double-monitoring risk is structural** (use one path) — FACT (Apollo Software Manual USB).
- Apollo **Solo: internal clock only**; bigger models: S/PDIF/ADAT/Word — FACT (same). External clock mismatch → glitches by design (vendor).
- S/MUX ADAT reduction; S/PDIF forced internal above 96k; relay mute click on SR change; Input Delay Compensation applies only after DAW quit; USB Safeguard adds latency (not reflected in Console monitoring) — FACT (same URL + /115002497703).
- Known issues history: Solo USB latency-reporting fix (11.8.1), 88.2/96 input access fix (11.7.1), ADAT channel offset (11.5.0), ≥2048 buffer hang (standing), boot-order enumeration quirk — TIME-SENSITIVE (UA Version History + /208139613).
- APEX vulnerability: none found beyond documented tolerances; Apollo Solo remains the primary hardware-matrix device.

### 5.2 Antelope Audio (Zen Tour/Go SC, Discrete, Orion)
- Buffer set in vendor ASIO panel (+ "Streaming mode" sliders); device SR 32–192k; external-clock change may require USB reconnect — FACT (Zen manuals; support.antelopeaudio.com 42000019332).
- Management plane (Launcher/Manager Server) can fail while the audio device works (incl. Logitech G HUB interference) — TIME-SENSITIVE (42000110197). APEX never touches the management plane — D.
- TB driver: single-device, ASIO-only, DPC-sensitive; TB2 incompatible with TB4 hosts — TIME-SENSITIVE (42000102286).
- No DAW-named failure entries found — UNKNOWN (absence of evidence).
- E: nothing to patch host-side without reproduction.

### 5.3 PreSonus (Quantum / Quantum 2 / 2626)
- **Quantum has NO hardware direct monitoring** — monitoring is software-only: matches APEX's model exactly; any "direct monitor" concept must be a no-op — FACT (support.presonus.com 4411233536013).
- TB2 Quantums unsupported on TB4 Windows hosts; C-states workaround; UC owns settings — FACT (4406516070541, 360028620552).
- Minimum ASIO buffer on Quantum: UNKNOWN → B. Multi-client: UNKNOWN → B.

### 5.4 RME (Fireface USB-series / MADIface-series)
- **Minimum reliable buffer: USB-series = 48 samples; MADIface-series = 32** ("16 is marketing BS") — FACT (forum.rme-audio.de/viewtopic.php?id=42401, RME's Matthias Carstens). APEX correctly offers 32 only where the driver lists it.
- **Multi-client ASIO confirmed** (DIGICheck prerequisite); all clients must share one SR (no SRC in ASIO path) — FACT (id=32560, id=29567).
- On-the-fly buffer/SR changes are designed-in; WDM holders (Sonarworks) can lock/degrade this — FACT (id=33350).
- TotalMix FX = DSP mixer (invisible to host; Loopback channels are explicit inputs); SteadyClock FS; UCX adds hidden +32-sample USB safety buffer — FACT (rme-audio.de/totalmix-fx.html; id=23822).
- APEX vulnerability: none; the 48-floor and hidden +32 are documentation items (D).

### 5.5 MOTU (UltraLite-mk5 / 828 / 624 / 16A)
- Gen5: **32-sample buffer @96k official** (2.4 ms RTL) — FACT (motu.com ultralite-mk5 specs). Channel scaling by rate published (18×22→8×12).
- "Sync Windows sample rate to device" option crash history; stream flap on client detach (fixed 2026-03); Pro Audio v2 ≤512-sample requests broken until 2026-03; large-buffer glitch fixed 2026-05 — TIME-SENSITIVE (motu.com download pages 483/676).
- CueMix = network app (panel latency unknown — B); multi-client UNKNOWN — B.

### 5.6 Focusrite (Scarlett 1st–4th gen, Clarett)
- Unified 4.x driver ("Focusrite USB ASIO", model-generic name — FACT); SR/buffer via Device Settings/Notifier/FC2; **pre-4.143 hosts could hang on SR change (fixed 2025-09-01)**; pre-4.65.5 exposed unsupported buffers to hosts; multi-client supported on 4.x USB (glitch-fixed) — TIME-SENSITIVE (support.focusrite.com/hc/en-gb/articles/13070702714130).
- Direct Monitoring documented incl. the double-monitoring echo symptom — FACT (360006972599). High-rate feature reductions published (10868568108562).
- Guidance: driver ≥ 4.143.0 recommended (D).

### 5.7 Steinberg/Yamaha (UR series, YSUSB driver)
- dspMixFx + Cubase "direct monitoring" = the ASIO ADM implementation class; external SR change can stall ASIO (reset required); SuperSpeed dropout history — FACT (Steinberg helpcenter AXR4T/UR-C articles).
- Buffer list / multi-client: UNKNOWN (release-notes PDFs encrypted) → B.

### 5.8 Avid-compatible devices
- Avid KB is login-gated; only article titles verified ("HD Driver v12.3 Notes", "How to use Pro Tools and ASIO4ALL"). All Avid-specific behavior: **UNKNOWN → B/E** (retrieve KB from an installed Pro Tools system before any claim).

## 6. Host-pattern reference (major DAWs)

- **Rate authority while running:** Live and Studio One own the rate (external changes blocked/alerted; Steinberg AXR4T guidance) — APEX instead *follows* driver resets and re-prepares (REAPER-class async handling). Both are documented host strategies; APEX's matches Brain §3 (negotiation result is authoritative).
- **Cubase/Nuendo:** external SR change needs ASIO reset; "Audio Hardware Removed" dialog on disconnect; buffer owned by driver panel on Windows; built-in ASIO driver does SRC but guidance is match-rates — FACT (Steinberg helpcenter).
- **REAPER:** async SR-change tolerance (6.76 changelog), manual close/re-open action (7.68), **"use audio driver reported latency" + manual record offsets** — FACT (reaper.fm/whatsnew.txt, wiki.cockos.com).
- **Logic:** project rate is a project property; "Recording Delay" slider (auto compensation implied, manual override) — FACT (Apple guide).
- **Ableton:** "sample rate in Live must match Windows"; exclusive-mode disable guidance; buffer chooser starts at 32 — FACT (help.ableton.com 209770485/211476789).

## 7. C-class defects (proven)

### 7.1 F1 — I/O topology change silently clamped (FIXED this session)
- **Defect:** `MainComponent` sets `needsInputBufferReprepare_` when the driver exceeds prepared input capacity (MainComponent.cpp:6130-6134) but **no consumer existed** — topology changes (S/MUX, custom I/O matrix) were silently clamped until the next device restart.
- **Fix (generic, no vendor hacks):** the 2 s `inputWatchdogTick` now consumes the flag (exchange false) and invokes the proven `ensureInputChannelsActive("topology-change")` repair path — forcing a safe device restart so `audioDeviceAboutToStart` re-reads true topology and re-prepares. Guarded identically to the watchdog: never while the panel owns the device or a take is rolling (`MainComponent.cpp:6807-6821`).
- **Regression evidence:** Debug build PASS; Release build PASS; Debug tests PASS (manifest `20260726T002438Z-Debug-78b267d`); Release tests PASS (`20260726T002933Z-Release-78b267d`) — all pre-existing suites green, zero failed assertions.

### 7.2 Item 19 — Recording latency compensation: DEFERRED / PRODUCT-DESIGN REQUIRED

**The defect (proven by code):** recorded takes are placed at the raw transport position with no input/output latency compensation (`RecordingEngine.h:211,229,590`; `RecordingClipFinalizerCore.h:71-80`; `getInputLatencyInSamples`/`getOutputLatencyInSamples` are consumed for display only, `AudioDeviceSettingsModel.h:39-40`). The captured content lands late by roughly IL+OL (input + output latency) relative to the performed timing — several ms on typical USB interfaces, and it grows with buffer size.

**Why automatic driver-only compensation is unsafe to ship blindly:**
1. Driver-reported latencies are **proven unreliable on the primary target family**: UA Apollo Solo USB latency-reporting bugs were only fixed in UAD 11.8.1 (2025-09); Focusrite issued multiple "increased accuracy of latency reporting" fixes; RME documents a hidden +32-sample USB safety buffer (RTL > 2×buffer). Compensating by a wrong constant is worse than the known deterministic offset.
2. The correct constant is **configuration-dependent** (device + rate + buffer + driver version + vendor options like USB Safeguard / Safe Mode), and can change mid-session (latencies-changed events, topology/mode changes).
3. There is **no generic way to prove correctness from inside the host** — only a loopback measurement can establish the true round-trip offset on a given machine.

**Future design (not yet implemented):**
- **(a) Driver-reported baseline:** capture `getInputLatencyInSamples()`/`getOutputLatencyInSamples()` at `beginRecording`; subtract from take start position (clamped ≥ 0); log the applied offset per take.
- **(b) Per-device manual recording offset:** persisted per configuration identity — (device type + device name + sample rate + buffer size), since APEX has no stable device IDs yet (audit item 2); editable in the device panel; overrides (a) when set.
- **(c) Optional loopback calibration:** generate a known click, measure the round-trip through the user's physical loop, derive the true offset, store per configuration identity (Brain §31 clock-mapping/calibration pattern; REAPER's documented manual-offset precedent).
- **(d) Configuration identity where necessary:** offset keyed by (type, name, rate, buffer, driver-visible options); on any configuration change the offset re-derives from (a) until recalibrated.
- **Data APEX already exposes:** device type/name, granted rate/buffer (`DeviceSessionCore::snapshotCurrent`), device-reported IL/OL (`GuardedAsioDevice` pass-throughs, `SafeAsioDeviceTypeCore.cpp:901-909`), settings display model, B4 audit infrastructure for measurement windows.
- **Data required later:** persisted per-configuration offset store; calibration capture path (click render + input correlation); placement hook in `RecordingClipFinalizerCore` (apply offset to `req.startPosition`, clamp ≥ 0); regression tests on finalizer placement math at 44.1/48/96k with offset 0 (fake devices) preserving writer-integrity/identity suites.

**Conclusion:** no universally safe generic fix can be proven from the host side today. Item 19 stays **DEFERRED / PRODUCT-DESIGN REQUIRED** with the design above as the approved shape when scheduled.

### 7.3 Item 22 — Project sample-rate identity: IMPLEMENTED (2026-07-26, pre-beta project integrity)

**Defect (was proven):** project files stored no sample rate (`Source/ProjectCore` had zero rate metadata), so sample-domain timelines (clip start/length/sourceOffset, transport position, markers) were silently reinterpreted at whatever rate the device happened to run.

**Implementation (generic, Brain §3 request-vs-grant):**
- **Persist:** `ProjectManager::buildState` writes `projectSampleRate` = the current device-granted rate — **only when proven** (`> 0`); a missing property stays meaningful (legacy/unprovable). Never invented.
- **Decide:** new `ProjectCore/ProjectSampleRateReconcileCore.h` — pure `makePlan(stored, deviceRate, metadataPresent)` → `none` (match within 0.01 Hz) / `reconcile` (proven mismatch, factor = device/project) / `legacyUnverified` (absent or corrupt metadata) / `deviceRateUnknown` (stored rate exists but device rate cannot be proven). `shouldReconcile` is true only for a proven mismatch.
- **Reconcile (seconds-preserving, runs last in `restoreFromState`):** every clip's engine-domain start/sourceOffset scaled by the factor; AudioClip length scaled (clamped ≥ 1); **MIDI clips go through the previously tested `MidiClip::setSampleRate` path** (seconds-preserving length rescale + rate update, so tick conversion stays correct); transport position and marker position/length scaled. **PPQ automation is untouched by construction** (`timePPQ` is rate-independent musical time). AudioClip source-file sample bounds are never scaled (source-domain).
- **Explicitness:** reconciled loads log project→device rate and factor; legacy loads log UNVERIFIED with guidance to verify and re-save; unknown-device-rate loads log NOT-reinterpreted with guidance to reopen after the device runs. A `LoadRateReport` (action, rates, factor, metadataPresent) is exposed via `ProjectManager::getLastLoadRateReport()` for the GUI phase.
- **Regression tests (`project.sample-rate-reconcile.v1`, 11/11 PASS):** same-rate reopen; 44.1→48; 48→96; 96→48; legacy-no-metadata never reinterpreted; corrupt-zero metadata treated as legacy; unavailable device rate never fabricated; scalePosition rounding; clip timeline seconds-preserving; ≥1-sample clamp; MIDI seconds + tick extent preserved with clip rate updated; invalid factor/rate no-op.

## 8. D-class documentation items (user/support guidance to publish)

1. **Pick ONE monitoring path:** vendor mixer direct monitoring (UAD Console / TotalMix / dspMixFx / CueMix) **or** APEX software monitoring — never both (Focusrite documents the echo/"thinner" symptom).
2. **Keep vendor drivers current:** Focusrite ≥ 4.143.0 (SR-change hang); UA ≥ 11.8.1 (Solo latency reporting); MOTU Gen5 ≥ 4.5.0.551 / Pro Audio v2 ≥ 1.1.11 (small/large-buffer bugs); Antelope TB 1.55 caveats; PreSonus TB2 ≠ TB4 hosts.
3. **Match sample rates across Windows shared mode, vendor panel, and APEX** when using the same hardware across subsystems (UA/Ableton/Steinberg guidance; Microsoft exclusive-mode preemption).
4. **Reported latency is not ground truth** on some devices (UA Solo history; RME hidden +32; USB Safeguard). Latency shown in APEX's settings panel is the driver's claim.
5. **Buffer-size floors are driver-family-specific** (RME USB-series 48, MADIface 32, MOTU 32, others vary). 32 appears in APEX only when the driver offers it, as `32 (experimental)`.
6. **Clock source lives in the vendor panel** (APEX shows none); external clock mismatch produces glitches by vendor design — match the panel to the session rate.
7. **Avid-family behavior is unverified** (KB gated) — no claims; test before use.
8. **ASIO hotplug has no notification standard** — reconnect mid-session may need a device re-select in APEX on some drivers; WASAPI re-enumerates automatically.

## 9. E-class quirks (recorded; NO patches without reproduction)

1. **ASIO ADM (input monitoring API)** — JUCE advertises support but never drives it; implementing ADM for dspMixFx-class devices only after a reproducing device + Brain-compliant design.
2. **Antelope management-plane fragility** (Launcher/G HUB interference) — vendor-side; nothing host-side to patch.
3. **Focusrite pre-4.143 host hang** — fixed vendor-side; verify APEX doesn't wedge on older drivers before considering any guard.
4. **Device-name stability across driver versions** (UA release notes show behavior fixes; Focusrite name is model-generic by design) — watch only.
5. **MOTU CueMix panel latency** (network app) — if panel-open stalls are observed, bound the call like the ASIO4ALL guards; no evidence yet.

## 10. Hardware verification matrix (nothing here may be marked VERIFIED without physical testing)

Cells: **T** = test required before any claim. Blank = covered generically (A) but still recommended on the primary Apollo Solo.

| Test | Apollo Solo | Antelope (Zen) | PreSonus Quantum | RME (UCX/MADIface) | MOTU (mk5) | Focusrite (Scarlett 4G) |
|---|---|---|---|---|---|---|
| 1 Enumeration + channel names/topology at open | T | T | T | T | T | T |
| 2 Rate set per family (32k–192k incl. S/MUX limits) | T (internal clock only) | T | T | T | T | T |
| 3 Buffer ladder incl. 32 (RME USB-series must NOT offer 32; MADIface/MOTU must) | T | T | T | T (48-floor check) | T | T |
| 4 Request-vs-grant readback (unsupported value rejection) | T | T | T | T | T | T |
| 5 External SR change (vendor panel) → clean reset + re-prepare, no hang | T | T | T | T | T | T (≥4.143.0) |
| 6 External buffer change → clean reset | T | T | T | T | T | T |
| 7 External topology change (I/O Matrix / S/MUX) → F1 reprepare adopts new I/O | T | T | T | T | T | T |
| 8 Vendor control panel open/close latency + post-panel state | T | T | T | T | T | T |
| 9 Multi-client (second ASIO host concurrently) | T | T | T | T (documented OK) | T | T (documented OK) |
| 10 WDM holder alongside (e.g., Sonarworks-class) stability | T | T | T | T | T | T |
| 11 Hotplug disconnect/reconnect mid-session (not recording) | T | T | T | T | T | T |
| 12 Disconnect mid-take (take survives or fails cleanly; no crash) | T | T | T | T | T | T |
| 13 Reported latency sanity vs. measured RTL (loopback) | T (Solo history) | T | T | T (+32 hidden) | T | T |
| 14 Recording offset accuracy (clap test vs. grid; documents F2 need) | T | T | T | T | T | T |
| 15 Double-monitoring check (vendor mixer on + APEX monitoring on) | T | T | T (n/a — no HW monitoring) | T | T | T |
| 16 48k/96k × 256/128/64/32 B4 audit matrix (deadlineMisses, engineOverruns, lateDeliveries, ringOverflows, xruns, stage timing) | T | T | T | T | T | T |
| 17 Clock-source change with external sync (models with external clock) | n/a (Solo internal-only) | T | T | T | T | T |
| 18 Unsupported-rate project reopen behavior (post-F3) | T | T | T | T | T | T |
| 19 Long-session stability (≥ 2 h, all rates/buffers above) | T | T | T | T | T | T |

## 11. Final verdict and status taxonomy (2026-07-26)

**Final gates (all green):** Debug build PASS (`DAW_Core.exe` SHA-256 `3977A30D33649EC7BB979BE58C3AF0B4236027A90B959E23F629C349C5C3F287`); Release build PASS (`8C2A6ECBA8ECE5E715C62893E8B476920E42A378272A34489BA43454927A7A6D`); Debug tests PASS (manifest `02fd06fbe1bd`, 2026-07-26T01:13:52Z); Release tests PASS (manifest `8a0176a33634`, 2026-07-26T01:21:42Z); `test_repository_policy.ps1` PASS; `verify_dependencies.ps1` PASS; `test_validate_test_evidence.ps1` PASS. All pre-existing suites remain green; 256-sample baseline and 64-sample behavior preserved.

| Status | Items |
|---|---|
| **IMPLEMENTED / CODE-PROVEN** | Items 4, 5, 23 (device-authoritative rates/buffers, 32-sample enumeration); item 20 (topology reprepare F1, gate-verified); item 22 (project sample-rate identity, 11/11 suite); B4 audit corrections; DrumSampler/MIDI/LUFS/ArrangementEditor rate work (sample-rate expansion phase) |
| **HARDWARE-UNVERIFIED** | Every per-family cell in §10 — including 32-sample operation and all rates above 48 kHz; items 3, 7, 12, 13, 18, 25 (B); multi-client and WDM-conflict behavior on real devices |
| **DEFERRED PRODUCT DESIGN** | Item 19 (recording latency compensation — design recorded in §7.2; driver-only auto-compensation proven unsafe) |
| **VENDOR-DOCUMENTED** | Items 2, 8(Focusrite history), 12, 13(ASIO), 15, 16, 17, 21; family profiles §5; guidance §8 |
| **UNKNOWN** | Avid-family behavior (KB login-gated); per-vendor 32-sample availability beyond RME/MOTU (needs §10 cells); Antelope multi-client; MOTU multi-client; PreSonus Quantum minimum buffer; YSUSB buffer list |

- **Generic contract:** healthy. Nothing is claimed hardware-VERIFIED — physical testing on the Apollo Solo (§10) is the only path to any VERIFIED marking.
- **No vendor-specific hacks; no commits; everything in the working tree.**
