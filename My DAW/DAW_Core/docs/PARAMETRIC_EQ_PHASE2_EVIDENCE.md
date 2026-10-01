# APEX Parametric EQ Phase 2 — Native Processor Integration (PASS)

Status: **PHASE 2 = PASS** — declared 2026-08-14 as a diagnostic working-tree
milestone. Phase 1 remains the static-DSP regression baseline. This is not a
canonical release freeze: the shared workspace is intentionally dirty, so the
retained run manifests are diagnostic grade and no clean milestone commit or
tag is claimed.

## Brain contract

Phase 2 implements the native-processor layer required by DAW Brain §§85–88:

- separable DSP, parameter/state, processor-adapter, and visualization layers
  (`DAW_BRAIN.md:54163-54202`);
- stable identity, layouts, lifecycle, state, latency, bypass, and validation
  contracts (`DAW_BRAIN.md:54206-54259`);
- no allocation, locks, I/O, GUI calls, waits, or unbounded work in
  `processBlock` (`DAW_BRAIN.md:54263-54291`);
- permanent parameter IDs and explicit mappings (`DAW_BRAIN.md:54295-54314`).

## Implemented architecture

- `ParametricEQProcessor.h/.cpp` exposes one intrinsic APEX processor with
  stable identity `APEX::ParametricEQ`, state schema version 1, matching mono
  or stereo main buses, zero latency/tail, and no editor in this phase.
- The permanent hosted ABI contains 170 lock-free atomic parameters: 24 fixed
  slots × 7 controls, `peq.design`, and `peq.bypass`. IDs span
  `peq.band01.enabled` through `peq.band24.slope`.
- Control/automation writers publish normalized atomics. Only the audio thread
  adopts settings into the DSP engines. Audible topology/coefficient changes
  use parallel current/target engines with a 10 ms sample-stepped transition.
- Bypass has one owner and converges to an exact dry wire. Fully bypassed
  hidden target changes adopt with cleared histories; un-bypass warms the wet
  path under the dry/wet transition.
- `ParametricEQResponseCore.h` sends coherent fixed-size response frames over
  SPSC storage. A consumer evaluates the exact nested current/target and
  wet/dry transfer without reading mutable `Engine` state.
- `ApexSpectrumAnalyzerCore.h` is observational: callback-side work is bounded
  SPSC copying, FFT/smoothing runs on a low-priority worker, and consumers pin
  immutable fixed-slot snapshots. The analyzer defaults closed.
- `ApexNativePluginFormat.h` remains the single `APEX Native` family format and
  now enumerates G10, C4, and Parametric EQ. Scanner cache seeding preserves
  deterministic family membership and uniqueness.

## Repository evidence

Primary implementation:

- `Source/ParametricEQCore/ParametricEQProcessor.h:15-197`
- `Source/ParametricEQCore/ParametricEQProcessor.cpp:201-375,393-473,489-710`
- `Source/ParametricEQCore/ParametricEQResponseCore.h:13-169`
- `Source/AnalysisCore/ApexSpectrumAnalyzerCore.h:14-338`
- `Source/PluginHostCore/ApexNativePluginFormat.h`
- `Source/PluginHostCore/PluginScannerCore.h`

Focused regression sources are under `Tests/Source/ParametricEQ/`. The
processor contract, including the 170-ID ABI, state tolerance, sample-order
invariance, exact dry bypass, and callback allocation audit, is exercised by
`ParametricEQProcessorTests.cpp:43-422`. Analyzer dormancy, transfer truth,
observational equivalence, allocation safety, pinned snapshots, and explicit
discontinuity are exercised by `ParametricEQAnalyzerTests.cpp:36-186`.

## Validation

All commands ran from `My DAW/DAW_Core` with seed `0xA9E12026`.

| Gate | Configuration | Result |
|------|---------------|--------|
| `run_apex_tests.ps1 -Category APEX.ParametricEQ` | Debug | PASS — 54 groups, 1,735 assertions, 0 failed |
| `run_apex_tests.ps1 -Category APEX.ParametricEQ` | Release | PASS — 54 groups, 1,735 assertions, 0 failed |
| `run_apex_tests.ps1` complete repository suite | Debug | PASS — 648 groups, 60,726 assertions, 0 failed |
| `run_apex_tests.ps1` complete repository suite | Release | PASS — 648 groups, 60,726 assertions, 0 failed |
| `build_apex.ps1 -Configuration Debug -Rebuild -Unsigned` | Debug | PASS — EXE and PDB present |
| `build_apex.ps1 -Configuration Release -Rebuild -Unsigned` | Release | PASS — EXE and PDB present |
| `test_repository_policy.ps1` | repository | PASS |
| `verify_dependencies.ps1` | repository | PASS |
| `test_validate_test_evidence.ps1` | repository | PASS — 6 passed, 0 failed |
| Four retained run manifests through `validate_test_evidence.ps1` | Debug/Release | PASS |
| Phase 2 tracked diff check and 23-file source hygiene scan | repository | PASS |

The first complete Debug run correctly exposed a stale G10 scanner assertion
that expected two intrinsic family entries. The assertion was updated to the
new three-member contract, its focused regression passed, and both complete
suites above were rerun afterward. No DSP behavior was changed for that fix.

## Retained run evidence

| Scope | Configuration | Run ID | Exit | Duration |
|-------|---------------|--------|------|----------|
| `APEX.ParametricEQ` | Debug | `95269cc140a8` | 0 | 3,874 ms |
| `APEX.ParametricEQ` | Release | `895a9203a50a` | 0 | 2,688 ms |
| Complete suite | Debug | `cac87ee94ef5` | 0 | 1,517,121 ms |
| Complete suite | Release | `1109b5310116` | 0 | 319,228 ms |

The manifests record application commit
`e4176c2569a5cd1f3cb5cf43e6367f5e0312a1a1`, JUCE 8.0.12, x64/v145,
Windows 11, and dirty source flags. `run_apex_tests.ps1` currently leaves the
manifest `results` object zero-initialized; therefore assertion totals are
proved by the separately retained runner JSON below, while process exit,
environment, and test-binary identity are proved by the validated manifests.
The two evidence layers must not be conflated.

## Artifact inventory

All timestamps are UTC; hashes are SHA-256.

| Artifact | Bytes | Timestamp | SHA-256 |
|----------|------:|-----------|---------|
| Debug `DAW_Core.exe` | 39,638,528 | 2026-08-14 07:24:30 | `F15A85C68D7810A3E15576D549F46E6788ECE81E1B40336A4656B4C42A380684` |
| Debug `DAW_Core.pdb` | 255,307,776 | 2026-08-14 07:24:30 | `8B2B1DF28715514964C58A91BD320CBE3ACB2EC2829055E5F2AE6C0DEF612A00` |
| Release `DAW_Core.exe` | 13,261,824 | 2026-08-14 07:33:00 | `ADEA2A71CAB2BB7D5E4A37CF0BCD92F8C76A4AB5925CEB7AD3A89854F44A5073` |
| Release `DAW_Core.pdb` | 161,599,488 | 2026-08-14 07:33:00 | `DF2B3E2A2EBE2E4C84CD366F054CBE8355CB93877BA6A1C30A79E558912226DF` |
| Debug `APEXTests.exe` | 26,951,680 | 2026-08-14 12:46:21 | `C458F433D59BEB4215458E97F65C14DE55CDC7BC3A2F56A8AFC0BA9C39CCAC9B` |
| Debug `APEXTests.pdb` | 201,109,504 | 2026-08-14 12:46:21 | `8F48E58F71715012C7DCBCBC43F471D136D556BE33030F60224F422B0956DAF6` |
| Release `APEXTests.exe` | 9,374,720 | 2026-08-14 13:15:20 | `15EEDBEA012CE1546568FF9AFE03DEAD44232B4BA0088E2AA553248690CF11A0` |
| Release `APEXTests.pdb` | 104,435,712 | 2026-08-14 13:15:20 | `7B916D3A46886CF2648C26D33A22B2B2AD95CCFC1A7BCAA55BC1993854B045FD` |

Runner JSON:

| File | Bytes | SHA-256 |
|------|------:|---------|
| `parametric-eq-phase2-debug-results.json` | 8,495 | `F608A79B736782C4032A62C1A1CFF0E7B59839D9EDB91080044742ED36429C2C` |
| `parametric-eq-phase2-release-results.json` | 8,497 | `3730892547A97935AE02959E1F0F9C6324C1C6C5282A1AA6EC5A49E0F0B580EE` |
| `parametric-eq-phase2-full-debug-results.json` | 94,883 | `DCD60CBA1370995482A29B7AFC0A8B4931D1D2DDB09817F7B762E4750F5B8ECD` |
| `parametric-eq-phase2-full-release-results.json` | 94,885 | `0DEC3EC25623592ED98024DDDF9B46E79B5CF3C7FC72FBD85CE1368E442E37FB` |

## Scope boundary and remaining uncertainty

- Phase 2 deliberately has no editor; no visual or human listening approval is
  claimed.
- Channel placement remains stereo-only in the hosted parameter surface.
- No external VST3/AU/AAX publication or third-party-host behavior is claimed.
- Evidence is diagnostic rather than release grade because unrelated working
  changes predate and coexist with this work. Repository-wide `git diff
  --check` also reports unrelated pre-existing whitespace defects; the
  Phase 2 tracked paths and all 23 Phase 2 source/test files pass their focused
  hygiene checks.
- A clean commit/tag, transactional publication, exact published-artifact
  reopen, and manual host validation remain later release gates.
