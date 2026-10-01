# Testing APEX on Windows

Run all commands below from the `DAW_Core` application repository root.

## Test Runner

The APEX test runner is a console executable built from `Tests/Source/Main.cpp`. It
executes always-on assertion suites for blade split algebra, cache sharing, callback
audit, and recording writer integrity.

## Build and Run Tests

Build and run the test binary in each configuration:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Seed 0xA9E12026
```

The script builds `APEXTests.exe` from `Tests/Builds/VisualStudio2026/APEXTests_ConsoleApp.vcxproj`,
runs it with the specified seed, and publishes an evidence manifest to
`evidence/runs/runner-smoke/`.

### Expected Output (Debug)

```text
Random seed: 0xa9e12026
All tests completed successfully.
[PASS] evidence manifest <runId>
[PASS] APEXTests Debug completed.
```

### Expected Output (Release)

```text
Random seed: 0xa9e12026
All tests completed successfully.
[PASS] evidence manifest <runId>
[PASS] APEXTests Release completed.
```

Both configurations must exit 0 and produce an evidence manifest with
`assertionsFailed: 0`.

## Test Suites

| Suite | Coverage |
|-------|----------|
| `runner.discovery.v1` | Always-on assertion execution check |
| `clip-split-algebra.v1` | Same-rate and cross-rate splits, fades, timePitch, three repeated splits, invalid split rejection |
| `blade-split-plan.v1` | makePlan validity, boundary rejection, cross-rate source/device |
| `blade-undo-persistence.v1` | Split/undo/redo round-trip, getState/restoreState round-trip |
| `audio-cache.shared-source-chain.v1` | Shared cache chain A->B->C frame counts and sample identity |
| `blade-source-render-contract.v1` | Source window contiguity at 44100/48000, cross-rate, NaN/infinity rejection, source boundary declaration |
| `callback-audit-ring.v1` | FIFO order, drop-newest, monotonic overflow, wraparound, empty drain |
| `callback-audit-accumulator.v1` | Deadline miss classification, percentile histogram, reset, overflow passthrough, max consecutive misses |
| `callback-audit-no-allocation.v1` | Allocation-free ring/accumulator/snapshot verification |
| `reading.writer-integrity.v1` | FIFO durability, queue-full, write failure, flush failure, stop-drain, prefix after failure, counter width, normal roundtrip, seekable stream failure, WAV header prefix |

## Evidence Manifests

Published manifests are at:

```text
evidence/runs/runner-smoke/<timestamp>-<Configuration>-<commit>/
  manifest.json
  stdout.txt
```

Each manifest records source commit, build configuration, toolset, binary SHA-256,
environment, seed, exit code, duration, and assertion counts.

## Repository Policy

Run the repository policy gate to verify that excluded paths are clean:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\test_repository_policy.ps1
```

Expected: `[PASS] application repository policy`

## Dependency Verification

Run the dependency gate to verify pinned SDKs and toolchain:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\verify_dependencies.ps1
```

Expected: `[PASS] pinned Windows dependencies verified`

## Evidence Validator

Verify the evidence validator works correctly:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\test_validate_test_evidence.ps1
```

Expected: `6 passed, 0 failed`

## What Tests Do NOT Prove

Compilation and lower-level tests are not proof of live hardware behavior.
Blade runtime validation, callback hardware campaigns, and GUI interaction
testing require manual verification with actual audio devices.
