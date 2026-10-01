# Task 2 Verification Hardening — Design Spec

**Date:** 2026-07-18
**Status:** Approved (user authorization via "i trust you")
**Scope:** Build control-plane hardening only. No product source, SDK payload, or audio-engine code changes.

## Problem

An independent final review of Task 2 after fresh verification found three technical issues:

1. **Verifier Git environment poisoning.** `verify_dependencies.ps1` invokes `git` directly without clearing inherited `GIT_DIR`, `GIT_WORK_TREE`, `GIT_CONFIG_*`, etc. Setting `GIT_DIR` to a nonexistent path made the verifier exit 1 with `fatal: not a git repository`, proving the environment can redirect or break dependency identity checks. Reproduction: set `GIT_DIR` then invoke verifier; exit 1. The patch application path (`juce_patch_tools.ps1`) already sanitizes these variables; the verifier does not.

2. **Child-redirect attack surface.** The approved SDK parent is native-tag checked before child/archive access (the `ParentRedirect` regression). However, a junction or symlink *inside* the verified SDK parent pointing to an external target would still be followed by `Test-Path`, file reads, and Projucer probes before the manifest walker reaches it. The manifest walker eventually rejects descendant reparse points, but only after external or network-backed target access.

3. **Stale ASIO SDK header path.** `DAW_Core.jucer:6` declares `headerPath="..\..\..\..\Sdk setups\ASIOSDK\common"` which resolves to a directory that does not exist on this machine. `JUCE_ASIO_USE_EXTERNAL_SDK` defaults to `0` (bundled headers), so the path is dead. The generated vcxproj propagates it into `AdditionalIncludeDirectories` for all configurations.

## Owner decisions

- Archive retention: **mandatory** for every verification/build (ratified).
- Stale ASIO header: **remove** now.
- Child redirect: **full coverage** with junction test.

## Design

### Fix 1: Verifier Git environment isolation

**Pattern:** identical to the proven `Invoke-JuceVerifiedPatch` pattern at `Scripts/juce_patch_tools.ps1:47-143`.

**Mechanism:**
- Before any `& git` invocation in `verify_dependencies.ps1`, enumerate all current-process variables starting with `GIT_`.
- Save each as `{ WasSet, Value }`.
- Set each to `$null` for the `Process` scope.
- Run all git commands in the sanitized scope.
- In a `finally` block, restore each variable to its saved state.

**Affected lines in current `verify_dependencies.ps1`:**
- `Test-SubmoduleIdentity` function: lines 84, 93, 98, 103, 110, 122 — six `& git` calls.
- Top-level stretch2 check: line 280 — one `& git` call.
- Total: seven git invocations that need environment isolation.

**Helper extraction:** add a shared `Invoke-GitWithIsolatedEnvironment` wrapper to `apex_build_tools.ps1` (or `juce_patch_tools.ps1`) that accepts the same arguments as `& git`, performs the save/clear/restore, and returns the exit code and output. This avoids duplicating the pattern across two scripts.

**Alternatively (simpler, lower risk):** add the save/clear/restore directly inside `verify_dependencies.ps1` as a local function, matching the self-contained style of the existing test harness. This avoids modifying the shared tools script for a verifier-only concern.

**Recommendation:** local function in `verify_dependencies.ps1`. Keeps the fix scoped, testable, and independent of `apex_build_tools.ps1` changes.

### Fix 2: Child-redirect junction test

**New test case:** `ChildRedirect` in `test_build_foundation.ps1`.

**Fixture:**
- Use `New-MaterializerFixture` to create a disposable SDK parent and repository.
- Inside the fixture's SDK parent, create a junction pointing from `juce-8.0.12-windows` (the installation root) to a separate disposable `.apex-debug` target containing a wrong archive.
- Initialize the fixture's repository with `git init`.
- Invoke the materializer in a child process.
- Assert: exit code nonzero, output mentions the child's native tag, no archive-hash/extraction diagnostic (rejection happens before archive access), the external target's sentinel is unchanged, no temporary or published installation through the junction.
- Invoke the verifier in a child process.
- Assert: exit code nonzero, output mentions the child's native tag, no verifier child/archive diagnostics after rejection.
- Cleanup: delete junction itself (non-recursive), then recursively delete both disposable directories.

**Implementation detail:** the manifest walker at `apex_build_tools.ps1:165-183` already calls `Assert-ApexPayloadPathNotReparsePoint` on every directory and file before traversal/hashing. The child junction will be caught at line 167 (parent directory) or 170 (child directory) before any `Test-Path`/read/probe on its contents. No new code is needed in the walker itself — only the regression proving it works.

### Fix 3: Remove stale ASIOSDK header path

**Files changed:**
- `DAW_Core.jucer:6` — remove the entire `headerPath` attribute (the value `..\..\..\..\Sdk setups\ASIOSDK\common`).
- Regenerate with pinned Projucer: `& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave DAW_Core.jucer --lf`.
- `Builds/VisualStudio2026/DAW_Core_App.vcxproj` — the regenerated file will no longer contain `..\..\..\..\Sdk setups\ASIOSDK\common` in `AdditionalIncludeDirectories`.

**Verification:** grep the regenerated vcxproj for `ASIOSDK`; confirm absent. Rebuild both configurations; confirm no compilation errors (no source file includes `iasiodrv.h` directly — JUCE bundles it internally when `JUCE_ASIO_USE_EXTERNAL_SDK=0`).

### Fix 4: Plan update

Update `docs/superpowers/plans/2026-07-17-apex-windows-baseline-evidence-foundation.md` Task 2 section:
- Note that archive verification is mandatory (ratified 2026-07-18).
- Note the ASIO header path removal.
- Note the child-redirect and verifier-environment hardening additions.

## Test strategy

Each fix follows test-driven development:

1. **VerifierEnvironment:** RED by setting `GIT_DIR` in the parent, invoking verifier in child, asserting nonzero exit and specific failure message. GREEN after adding save/clear/restore. Verify: focused case exits 0, full harness exits 0.

2. **ChildRedirect:** RED by creating a junction inside SDK parent, invoking materializer/verifier, asserting nonzero exit and native tag message without archive diagnostics. GREEN after junction is caught by existing manifest walker assertions. Verify: focused case exits 0, full harness exits 0.

3. **ASIO header removal:** RED by grepping vcxproj for `ASIOSDK` before resave (present), GREEN after Projucer resave removes it. Verify: rebuild both configurations, grep absent.

4. **Full suite rerun:** `test_build_foundation.ps1` (expected 18/18), `verify_dependencies.ps1`, Debug and Release unsigned rebuilds.

## Risk assessment

- **Verifier environment isolation:** low risk. Identical pattern already proven in `juce_patch_tools.ps1`. Only affects control-plane build scripts.
- **Child-redirect test:** low risk. No production code change; exercises existing walker assertions.
- **ASIO header removal:** low risk. Path is dead; `JUCE_ASIO_USE_EXTERNAL_SDK=0` means JUCE uses bundled headers. Compilation success proves correctness.
- **Plan update:** no code change.

## What this does NOT change

- Product source code, SDK payload, or audio-engine behavior.
- The existing check-then-use TOCTOU boundary (documented; would require larger redesign).
- The provisional ASIO4ALL patch status.
- Runtime, audio, device, transport, recording, save, or export behavior.
- Git index, HEAD, or commits (no staging/commit without explicit authorization).
