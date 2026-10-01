# ADelay PageHeap Validation — Final Report

Date: 2026-07-29

Workspace: `C:\Users\1993v\OneDrive\Desktop\Apex backup`

Application repository: `My DAW/DAW_Core`

Application baseline: `78b267dc2f6668c902f7c859ad0ffdf9096aa068` with a dirty working tree

## Verdict

**Partial**

The tested ADelay project-open path instantiated and prepared the expected VST3 plug-in under CDB with full PageHeap. APEX then emitted its clean-shutdown marker, and CDB reached its normal-target-exit marker without a captured second-chance fatal exception or an explicit heap-corruption report.

The run was not fully clean: JUCE reported leaked objects during shutdown. The harness also remains a manual diagnostic smoke probe rather than a self-contained permanent accessibility regression.

## Brain contract

The DAW Brain requires:

- a legally maintained plug-in corpus with vendor, version, hash, and redistribution constraints (`DAW_BRAIN.md:37753-37771`);
- scanner and runtime-failure coverage that attributes failures and validates recovery policy (`DAW_BRAIN.md:37795-37829`);
- diagnostic allocators and Windows verification tools without treating their timing as realtime performance evidence (`DAW_BRAIN.md:37848-37859`);
- semantic GUI automation for project-open and plug-in-scan workflows instead of pixel-only interaction (`DAW_BRAIN.md:38031-38043`);
- separate automated and manual accessibility validation for keyboard, focus, roles, states, screen readers, contrast, scaling, localization, and announcements (`DAW_BRAIN.md:38056-38070`).

## Repository evidence

### Harness

- `Scripts/run_adelay_accessibility_probe.ps1:1-564`
  - drives APEX through UI Automation and native file-dialog controls;
  - validates exact ADelay identity values;
  - requires a newly appended clean-shutdown marker.
- `Scripts/run_adelay_cdb_pageheap.ps1:1-158`
  - enables and verifies full PageHeap;
  - launches CDB and invokes the UI probe;
  - rejects fatal-exception and heap-corruption markers;
  - disables PageHeap in `finally`.
- `Source/PluginSafetyCore/PluginSafeLoadWrapperCore.h:58-61,69-71`
  - emits the plug-in creation success/failure records consumed by the probe.
- `Source/PluginHostCore/PluginScanStartupDialog.h:20-43,52-66,210-223`
  - owns the scan-dialog close, cached-load auto-close, and shared completion callback behavior used by the harness.

### Final observed CDB report

Artifact:

`..\..\..\..\..\crash-reports\APEX_CDB_PAGEHEAP_ADELAY_20260729_141527.txt`

Verified evidence:

- Full PageHeap active: line 64.
- Expected ADelay binary loaded: line 1761.
- Creation succeeded with `uniqueId=-19464422` and `deprecatedUid=-1457048775`: line 1765.
- Stereo layout negotiation succeeded: lines 1768-1770.
- Preparation occurred at 48 kHz with a 480-sample block: line 1771.
- APEX clean-shutdown marker: line 1790.
- CDB normal-target-exit marker: lines 1911-1913.

Contradictory/qualifying evidence:

- Two leaked `AutomationParameter` instances: lines 1872-1873.
- Ten leaked `AccessibilityNativeHandle` instances: lines 1884-1886.
- Numerous first-chance C++ and COM exceptions occurred; the report proves only that the configured second-chance fatal policy did not trigger.

## Technical analysis

The control path is outside the realtime callback:

```text
PowerShell UIA orchestration
→ StartupPanel Open Project
→ asynchronous project load
→ plug-in-chain restoration
→ PluginSafeLoadWrapperCore::createPluginInstance
→ ADelay layout negotiation and preparation
→ session-log identity oracle
→ controlled application close
→ CDB/PageHeap shutdown observation
```

This run proves successful instantiation, preparation, and normal process exit for the tested binary and fixture. It does not prove audio correctness, state round-trip correctness, editor lifecycle safety, repeated load/unload stability, leak freedom, broad plug-in compatibility, or complete accessibility.

The scan-complete marker is also weaker than its name suggests because the same callback is used for user dismissal, scanner completion, and cached-overlay timeout. The harness can therefore observe the marker without proving that a real scan completed.

## Validation

### CDB/PageHeap result

- ADelay identity: **PASS**
- Plug-in layout/preparation path: **PASS for the observed fixture**
- APEX clean-shutdown marker: **PASS**
- CDB second-chance fatal exception: **not observed**
- Explicit heap-corruption marker: **not observed**
- Leak-free shutdown: **FAIL**
- PageHeap cleanup after the run: reported by the invoking harness. A later independent registry query also found the full-PageHeap flags disabled.

### Seeded test manifests

The latest durable manifests record exit code 0 for the requested seed:

- Debug: `evidence/runs/runner-smoke/20260729T182612Z-Debug-78b267d/manifest.json:27-40`
- Release: `evidence/runs/runner-smoke/20260729T183106Z-Release-78b267d/manifest.json:27-40`

Both manifests report zero result groups and zero passed assertions. They prove process exit status and binary identity, but do not independently substantiate the prior claim that every individual suite assertion passed.

### Continuation verification

Commands were run from `My DAW/DAW_Core` unless noted otherwise:

- `powershell -ExecutionPolicy Bypass -File Scripts\test_repository_policy.ps1`
  - result: `[PASS] application repository policy`;
- `powershell -ExecutionPolicy Bypass -File Scripts\verify_dependencies.ps1`
  - first attempt exceeded the 120-second command timeout;
  - retry with a 600-second timeout completed with `[PASS] pinned Windows dependencies verified`;
- `powershell -ExecutionPolicy Bypass -File Scripts\test_validate_test_evidence.ps1`
  - result: 6 passed, 0 failed;
- PowerShell parser validation for both ADelay scripts
  - result: PASS;
- IFEO registry query for `DAW_Core.exe` from the workspace root
  - result: full-PageHeap flags disabled;
- process query for `DAW_Core` and `cdb`
  - result: no matching live processes;
- focused whitespace check for this report
  - result: PASS.

Repository-wide `git diff --check` is not clean because the unrelated existing modification `Dependencies/apex-windows-dependencies.json:18` contains trailing whitespace. No product code was changed during this continuation, so builds and application tests were not rerun.

## Cleanup performed

Removed only regenerable command-input artifacts:

- eleven `APEX_CDB_PAGEHEAP_ADELAY_*.commands.txt` files;
- `crash-reports/cdb_analysis_cmds.txt`;
- `crash-reports/cdb_stdin.txt`;
- `tools/cdb_commands.txt`;
- empty untracked `test_error.txt`.

Retained:

- the two ADelay harness scripts;
- successful and intermediate CDB reports;
- unique crash dumps, including the two malformed flattened dump paths;
- all product source and unrelated dirty-tree work;
- `test_output.txt`, because it records an earlier real test failure.

## Required action

1. Track the shutdown leaks as separate defects; do not describe this run as memory-clean.
2. Decide whether the ADelay scripts are temporary lab tools or a supported regression surface.
3. If promoted, add a pinned fixture and plug-in manifest with binary hash/version, remove geometry-derived control identity, separate scan completion from dialog dismissal, and guarantee process cleanup on probe failure.
4. Extend evidence generation so seeded test manifests contain actual suite/result-group and assertion counts.
5. Harden the PageHeap wrapper to preserve prior IFEO state and terminate debugger/target processes on exceptional paths before unattended use.

## Remaining uncertainty

- The repository does not contain the ADelay project fixture or plug-in binary used by this run.
- ADelay audio output and state restoration were not measured.
- Leak ownership and whether the UIA harness itself extends accessibility-object lifetimes are not established.
- The independent PageHeap recheck occurred after the original run rather than in the same atomic evidence package.
- The working tree contains extensive unrelated modifications, so this report does not certify the entire repository for release.
