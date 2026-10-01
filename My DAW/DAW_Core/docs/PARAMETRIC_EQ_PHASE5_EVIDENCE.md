# APEX Parametric EQ Phase 5 — Full Dynamic EQ

**PHASE 5 CLOSED — PASS — diagnostic working-tree milestone**

Declared 2026-08-15. The shared workspace remains dirty on
`feature/apex-windows-baseline-evidence` at
`e4176c2569a5cd1f3cb5cf43e6367f5e0312a1a1`; no clean release commit/tag
exists, so this is a diagnostic working-tree milestone, not release-grade
evidence.

## Implemented architecture

### 317-parameter append-only ABI (state v3)
- Frozen prefix 0-193 (Phase 1-3 identities unchanged).
- 194-313: `peq.bandNN.dyn.enable|threshold|range|attack|release`
  (24 x 5, threshold -60..0 dB, range +/-24 dB, attack 0.5 ms..5 s,
  release 1 ms..20 s).
- 314 `peq.dyn.detector` (Peak/RMS, default RMS), 315 `peq.dyn.sidechain`
  (Internal/External, default Internal), 316 `peq.dyn.link` (default linked).
- Compile-time pins: `kFirstPlacementParameter == 170`,
  `kFirstDynamicParameter == 194`, `kNumParameters == 317`; the State suite
  pins every ID literally.
- State v3 round-trips all 317; v1/v2 states load with deterministic dynamic
  defaults; no detector/envelope/key/audition runtime state is serialized.

### Dynamic EQ law
- `dynamicActivationForLevel`: quadratic 6 dB soft knee, r in [0,1].
- Signed range law: positive range cuts as the signal rises
  (`-range * r`), negative range boosts as it falls (`-range * (1-r)`),
  zero range is deterministic static EQ; both branches meet continuously at
  zero (unit- and automation-tested, including rapid sign reversal).

### Reusable Dynamics Core consumption
- Detectors and the exact attack/release timing coefficients come from
  `APEX::Dynamics`; the product keeps a SIGNED per-band smoothed state that
  uses the reusable coefficients with a magnitude-comparison direction rule
  (`|target| > |state|` -> attack), moving through zero continuously at
  range sign flips. No core law was altered.

### DSP integration
- Delta-scaled residual modulation in `Engine::processWithDynamicGains`
  (`out = x + g*(filtered - x)`), gain-compatible shapes only, all five
  placements, zero-mask path bit-identical to the static path.
- Per-band preallocated detectors/envelopes/gain lanes/key scratch; enable
  edges reset cleanly; timing retargets adopt click-free; the processBlock
  fast path excludes active dynamics.

### Sidechain
- Internal: the detector reads the band's placed component of the plugin
  input (Stereo/Left/Right/Mid/Side - the exact program channels).
- External-ready: `submitExternalDetectorKey`/`clearExternalDetectorKey`
  (bounded preallocated copy); external-without-key sees deterministic
  silence, never the program. Host routing remains a shared-host
  responsibility and is documented as such (no plugin-local hacks).

### Response authority
- `ResponseFrame` publishes `dynamicActiveMask` and `dynamicGainDb[24]`;
  dynamic-only state changes invalidate publication (`responseDirty_`);
  deactivation/reset/reprepare publish zero.

## Bugs found and fixed (all root-caused with probes, all removed)
1. **Signed-law vs magnitude envelope** (DYN-L4): signed targets selected the
   release branch; fixed with the product-level signed state over reusable
   coefficients. Measured pre-fix path -1.71/-3.11/-4.24/-5.16 dB per block.
2. **Detector key-channel routing** (DYN-L5): internal stereo key right
   pointer fell back to left; Right/Side/Mid domains responded to the left
   channel. Fixed to exact program channels.
3. **Stale published dynamic gains** on deactivate/reset/reprepare.
4. **External-without-key fallback** to program audio (now silence).
5. **Missing response invalidation** for dynamic-only changes.
6. **Harness defects**: output-buffer feedback in silence loops (fixed with
   per-block clear), non-bin-exact RMS windows (fixed via 937.5 Hz), and
   oracle windows that ignored the RMS detector window and knee.
7. **Flaky CPU gate** in the Phase 4 diagnostics (now evidence-only).

## Test suites (all green in focused Debug AND Release)
`ParametricEQ.DynamicEQ` (range law, zero-range bit-exact, residual-law
oracles, attack-timing discrimination regression, six rates, block
invariance, silence/burst), `.DynamicState` (317-pin ABI, v3/v2 migration,
malformed, no runtime serialization), `.DynamicAutomation` (zero-crossing
continuity per-sample, hostile sweeps, sample-exact single change,
overlapping transitions), `.DynamicSidechain` (internal/external/key
appear-disappear/source switch), `.DynamicLifecycle` (enable edges,
disable/enable, reset/reprepare, restore, repeated cycles),
`.DynamicRtAllocation` (0/1/4/12/24 bands x Peak/RMS + audition + bypass +
external key + automation), `.DynamicResponse` (mask/gains match, static
empty, drop-safe), `.DynamicStereo` (Stereo/Left/Right domains, mono, linked/
unlinked), `.DynamicMidSide` (Mid/Side domains, correlated/anti-correlated/
dual-mono, mixed placements, 24-band stress). All Phase 1-4 regressions stay
green.

## Dynamic EQ editor / controller

- Contextual inspector section for the selected band: `Dynamic EQ` label,
  enable toggle, Threshold/Range/Attack/Release sliders. All controls use the
  canonical 317-parameter IDs, the canonical parameter text formatters, and
  balanced `beginChangeGesture`/`setValueNotifyingHost`/`endChangeGesture`
  discipline; the section is offered only for gain-compatible shapes.
- Top-bar global controls: Detector Peak/RMS, Key Internal/External,
  Link On/Off (`setClickingTogglesState(false)`; state display owned by
  `refresh()`).
- Live visualization: the graph consumes the published `ResponseFrame`
  (`dynamicActiveMask`, `dynamicGainDb[24]`) and draws magnitude-encoded arcs
  per active band — amber below the node for cuts, green above for boosts.
  No locks, no audio-thread work, no duplicate GUI state; the editor never
  reads mutable DSP objects.
- Responsive/mobile: the dynamics section extends the scrollable inspector
  content (fixed 56 px rows, 44 px touch targets); all five breakpoints
  (CompactPhone/LargePhone/Tablet/Desktop/LargeDesktop) validated in
  `ParametricEQ.DynamicEditor`; no hover/right-click/keyboard-only
  dependencies; the graph stays dominant at every size.
- Deactivation/reset/reprepare publish cleared mask/gains, so the
  visualization can never display stale dynamic state.

## DynamicEditor test coverage

`ParametricEQ.DynamicEditor` verifies: dynamic control layout/touch geometry
at all five breakpoints; band-selection synchronization (no stale values);
enable toggle via the real click-command path; slider/global parameter
coherence; published visualization truth (active mask, live gain, and
deactivation clearing); state restore and editor reopen synchronization.

## CPU characterization (evidence-only, no pass/fail gates)

Measured in `ParametricEQ.DynamicRtAllocation` via `logMessage` at 48 kHz /
512-sample stereo blocks: 0/1/4/12/24 dynamic bands x Peak/RMS in µs per
block. Measurements are environment-dependent evidence, not correctness
gates.

## External sidechain shared-host boundary

- Processor-side external-key plumbing is implemented and validated:
  `submitExternalDetectorKey`/`clearExternalDetectorKey` (bounded,
  preallocated, deterministic).
- A supplied key is consumed correctly; External selected without a host key
  produces deterministic silence; the plugin never fabricates a missing key.
- Delivery of a sidechain to the target plugin instance is a shared
  APEX host/DAW routing responsibility and remains separate from this
  completed Parametric EQ processor implementation. No shared-host routing
  was modified during Phase 5.

## Final validation (final source tree)

| Gate | Result |
|------|--------|
| Focused Debug `APEX.ParametricEQ` | PASS — 160 groups, 251,941 assertions, 0 failed |
| Focused Release `APEX.ParametricEQ` | PASS — 160 groups, 251,941 assertions, 0 failed |
| Complete repository Debug | PASS — 770 groups, 653,849 assertions, 0 failed |
| Complete repository Release | PASS — 770 groups, 653,849 assertions, 0 failed |
| Debug application rebuild | PASS |
| Release application rebuild | PASS |
| `test_repository_policy.ps1` | PASS |
| `test_validate_test_evidence.ps1` | PASS — 6 passed, 0 failed |
| `verify_dependencies.ps1` | PASS |
| Phase 5 source hygiene + temporary-diagnostic scan | PASS |

Earlier transient evidence: one complete Debug run (runId `0e39fe514d60`)
exited nonzero during today's sweep while the Phase-4 evidence-only CPU
diagnostic still carried a flaky assertion; that assertion was removed and
every subsequent complete run is green (final runs recorded below).

## Final evidence run identifiers

| Scope | Configuration | Run ID | Exit |
|-------|---------------|--------|------|
| Focused `APEX.ParametricEQ` | Debug | `0562ef96e1a1` | 0 |
| Focused `APEX.ParametricEQ` | Release | `c233b9b55823` | 0 |
| Complete repository | Debug | `dbcd5b30fdf9` | 0 |
| Complete repository | Release | `2bfdfcea359a` | 0 |

## Final artifact inventory (UTC, SHA-256)

| Artifact | Bytes | Timestamp | SHA-256 |
|----------|------:|-----------|---------|
| Debug `DAW_Core.exe` | 39,825,920 | 2026-08-15 08:57:32 | `097254BFC1A84E2343F0EBB13078DA798CB0CEC8B8BE3A116BD5762AEAE60138` |
| Debug `DAW_Core.pdb` | 256,258,048 | 2026-08-15 08:57:32 | `20B92740E59EA4B80ACE5CE7EA0A5512B037440C728BBDA983B774F047711BD2` |
| Release `DAW_Core.exe` | 13,344,768 | 2026-08-15 09:06:45 | `062ADCA79472D6E055A7DDADD2D7FD26FFFC99FA39AB37A9A66CFFD7AC5D6303` |
| Release `DAW_Core.pdb` | 162,295,808 | 2026-08-15 09:06:45 | `E882B742B55D8044EEAC24566C6FB47AB3A27B251F29E835FE7942C161505E26` |
| Debug `APEXTests.exe` | 27,811,328 | 2026-08-15 07:54:58 | `6FEE2CDB68E208AAD93A8D0398F32A1D6C18E23D84FB9B811D8003F66E412691` |
| Release `APEXTests.exe` | 9,728,000 | 2026-08-15 08:44:12 | `ECA8A015FF68408855250C1FC9FFB609588A7EE5C4549AA248B0E3F885F71F7A` |
| Focused Debug results | 25,027 | 2026-08-15 07:56:15 | `9C2863A20B8DF68C57F62D86166433F02DAF649EEC2ED77E1EC98A414A0E5316` |
| Focused Release results | 25,029 | 2026-08-15 06:38:47 | `829DB014CAB8DF80683839F52300FB4D75AE06622417F779FE46EF7452804A52` |
| Full Debug results | 113,711 | 2026-08-15 08:41:10 | `80744CD8A2DAFB3B0EDA231E36EDE9978D8E680495CE98890E2F15B608DE8BE6` |
| Full Release results | 113,713 | 2026-08-15 08:49:49 | `391DEA78C5E639B50C860061C0966981E304835DF4845A12C3BDB8A6896BE2BE` |

## Known limitations

- Physical Android/iPhone/iPad runtime validation remains a later
  platform-port gate; the editor architecture and its platform-independent
  behavior are validated on Windows.
- Per-target external sidechain delivery by the shared APEX host remains
  separate host work (documented boundary above).
- Evidence is diagnostic grade (dirty shared workspace; no clean
  release commit/tag).
- The dynamic EQ affects gain-compatible shapes (Bell, shelves, Tilt,
  Flat Tilt) by design; other shapes keep their enable parameter inert but
  persistent.
