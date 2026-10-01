# APEX Windows Baseline and Evidence Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish a clean, reproducibly buildable Windows x64 APEX source baseline and the first permanent correctness, realtime-audit, recording-integrity, and machine-readable evidence foundation.

**Architecture:** Preserve the current two-repository layout: the outer workspace repository owns canonical architecture and workspace tooling, while `My DAW/DAW_Core` remains the application repository. Add one independent JUCE console test runner and small testable seams; do not rewrite the audio engine, recording pipeline, plug-in host, or Blade tool. Characterize current Blade behavior first and change production behavior only when a failing regression proves a violation.

**Tech Stack:** Windows 10/11 x64, PowerShell 5.1, Git, Visual Studio 2026/MSBuild 18, MSVC v145, C++17, JUCE 8.0.12, JUCE `UnitTest`, JSON evidence manifests.

## Global Constraints

- Official workspace: `C:\Users\1993v\OneDrive\Desktop\Apex backup`.
- Application repository: `C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core`.
- Do not inspect or modify another APEX copy.
- Do not create a Git worktree or isolated APEX copy unless the user explicitly authorizes an exception to the official-workspace rule.
- Do not use `git reset --hard`, `git clean`, force-push, history rewriting, or deletion of unreviewed source.
- Preserve the outer repository and application repository as separate repositories in this project.
- Keep SDK payloads, generated builds, recordings, caches, logs, crash reports, dumps, test runs, and secrets out of Git.
- Keep `Builds/VisualStudio2026/ArrangementEditor` tracked; it contains active production source.
- Pin JUCE to version `8.0.12` and archive SHA-256 `15B4ED8302127138DFEE98E61A2B5AE1481B39649B139E02E77BFC29462B7EFB`.
- Keep the current application language level at C++17 in this project; a language-standard migration requires a separate decision.
- Build x64 Debug and x64 Release. Build verification must skip signing; signing remains a separate publication operation.
- No callback allocation, deallocation, blocking, file/console/GUI I/O, formatted logging, plug-in lifecycle, graph rebuilding, device reopening, or unbounded retry.
- UI/Bubblegum cables are not realtime authority; `RoutingGraph` or a validated successor remains authoritative.
- Preserve one-owner dry/wet monitoring, offline-export isolation, previous valid save/export artifacts, and stable persistence IDs.
- The prior Blade Debug/Release root cause is Unknown. This plan must not label a speculative change as its fix.
- Every test assertion must remain active in Release; do not use `assert`, `jassert`, or debug-only logging as the oracle.
- Generated evidence is diagnostic while either repository is dirty. Release-grade evidence requires clean identified commits.
- Commit steps are gated: execute them only after the user explicitly authorizes commits during implementation.
- Do not stage files or run Git commands that modify the index until commit authorization has been granted for that task.

## File Structure

### Outer workspace repository

- Create `.gitignore` — excludes nested application/SDK payloads and local workspace output.
- Create `.gitattributes` — stable text/binary and line-ending policy.
- Track `tools/debug-apex.ps1` and `tools/analyze-latest-crash.md` — canonical crash workflow, excluding generated CDB commands/reports.
- Track `docs/superpowers/specs/2026-07-17-apex-windows-stability-program-design.md` — approved design.
- Track this plan — execution contract.

### Application repository

- Modify `.gitignore` — excludes generated Visual Studio Bridge output, crash output, audit output, and evidence runs.
- Create `.gitattributes` — application line-ending/binary policy.
- Modify `.gitmodules` — one managed SignalSmith Stretch dependency and one managed SignalSmith Linear dependency.
- Create `Dependencies/apex-windows-dependencies.json` — pinned dependency/toolchain contract.
- Create `Scripts/materialize_dependencies.ps1` — verifies and expands the pinned JUCE archive outside Git.
- Create `Scripts/apex_build_tools.ps1` — shared MSBuild/Projucer discovery functions.
- Create `Scripts/verify_dependencies.ps1` — dependency and source-layout gate.
- Create `Scripts/build_apex.ps1` — one unsigned Debug/Release build interface.
- Modify `Scripts/sign_build.ps1` — explicit `APEX_SKIP_SIGNING=1` no-op path.
- Create `Tests/APEXTests.jucer` — independent test project authority.
- Create `Tests/Source/Main.cpp` — deterministic JUCE test runner with nonzero failure exit.
- Create `Tests/Source/Smoke/RunnerSmokeTests.cpp` — runner discovery regression.
- Create `Scripts/run_apex_tests.ps1` — builds/runs Debug and Release and writes evidence.
- Create `evidence/schemas/apex-test-evidence-v1.schema.json` — evidence contract.
- Create `evidence/fixtures/valid-minimal.json` and `evidence/fixtures/invalid-missing-source.json` — validator tests.
- Create `Scripts/validate_test_evidence.ps1` and `Scripts/test_validate_test_evidence.ps1` — evidence validation.
- Create `Builds/VisualStudio2026/ArrangementEditor/BladeSplitPlanCore.h` — pure extraction of Blade split calculations, with no mutation or I/O.
- Modify `Builds/VisualStudio2026/ArrangementEditor/ArrangementViewCore.cpp` — consume the tested split plan while preserving current mutation order.
- Modify `Source/AudioEngineCore/AudioFileManager.h` only if the nested-share regression fails.
- Create Blade tests under `Tests/Source/Arrangement/`.
- Create `Source/DiagnosticsCore/CallbackAuditCore.h` — fixed-size callback facts, SPSC ring, and bounded histogram.
- Modify `Source/MainComponent.h` and `Source/MainComponent.cpp` — optional outer-callback timing publication and non-realtime drain.
- Create `Tests/Source/Diagnostics/CallbackAuditCoreTests.cpp`.
- Create recording fakes/tests under `Tests/Source/Recording/` without changing the production recording pipeline.

---

### Task 1: Establish repository safety and ignore policies

**Files:**
- Create: `.gitignore`
- Create: `.gitattributes`
- Modify: `My DAW/DAW_Core/.gitignore`
- Create: `My DAW/DAW_Core/.gitattributes`
- Create: `My DAW/DAW_Core/Scripts/test_repository_policy.ps1`

**Interfaces:**
- Consumes: current two-repository layout and measured generated-output paths.
- Produces: `test_repository_policy.ps1`, returning `0` only when required paths are ignored and active arrangement source remains visible to Git.

- [ ] **Step 1: Write the failing repository-policy test**

Create `My DAW/DAW_Core/Scripts/test_repository_policy.ps1` with checks equivalent to:

```powershell
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Assert-Ignored([string] $Path) {
    & git -C $repo check-ignore --quiet -- $Path
    if ($LASTEXITCODE -ne 0) { throw "[FAIL] expected ignored: $Path" }
}

function Assert-NotIgnored([string] $Path) {
    & git -C $repo check-ignore --quiet -- $Path
    if ($LASTEXITCODE -eq 0) { throw "[FAIL] active source is ignored: $Path" }
}

Assert-Ignored 'Builds/VisualStudio2026/crash-reports/probe.dmp'
Assert-Ignored 'Builds/VisualStudio2026/dumpbin_release.txt'
Assert-Ignored 'Builds/VisualStudio2026/PerformanceTests2/probe/x64/Debug/PerformanceTests2.pch'
Assert-Ignored 'tools/VisualStudioBridge/bin/Release/probe.exe'
Assert-Ignored 'tools/VisualStudioBridge/obj/probe.cache'
Assert-Ignored '.apex-debug/probe.txt'
Assert-Ignored 'evidence/runs/probe/manifest.json'
Assert-NotIgnored 'Builds/VisualStudio2026/ArrangementEditor/ArrangementViewCore.cpp'
Assert-NotIgnored 'Tests/Source/Main.cpp'

'[PASS] application repository policy'
exit 0
```

- [ ] **Step 2: Run the policy test and verify RED**

Run from the application repository:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\test_repository_policy.ps1
```

Expected: nonzero exit with `[FAIL] expected ignored:` for at least `crash-reports`, `VisualStudioBridge/bin`, or `evidence/runs`.

- [ ] **Step 3: Add the outer ignore policy**

Create the outer `.gitignore` with:

```gitignore
/My DAW/
/New folder/
/Daw Brain/
/Cerebro/
/.claude/
/.superpowers/
/crash-reports/
/recaps/
/evidence/runs/
/tools/cdb_commands.txt
/*.obj
*.dmp
*.log
```

This intentionally ignores the nested application repository from the outer repository; it does not ignore files inside the application repository itself.

- [ ] **Step 4: Extend the application ignore policy**

Append this exact block to `My DAW/DAW_Core/.gitignore`:

```gitignore

# APEX generated diagnostics and test evidence
.apex-debug/
Builds/VisualStudio2026/crash-reports/
Builds/VisualStudio2026/*_results.txt
evidence/runs/
TestResults/
*.trx
*.dmp
*.pch
Builds/VisualStudio2026/dumpbin_release.txt

# Runtime project media and recovery output
Recordings/
**/Recordings/
.apex_autosaves/
**/.apex_autosaves/
.backups/
**/.backups/

# Local secrets and signing material
.env
.env.*
*.pfx
*.p12
*.pem
*.key

# Generated .NET bridge outputs
tools/VisualStudioBridge/bin/
tools/VisualStudioBridge/obj/

# Canonical DAW Brain lives at the official workspace root
/DAW_BRAIN.md
```

- [ ] **Step 5: Add stable attributes without renormalizing the whole tree**

Create both `.gitattributes` files with:

```gitattributes
* text=auto
*.md text eol=lf
*.json text eol=lf
*.jucer text eol=lf
*.ps1 text eol=crlf
*.cpp text eol=crlf
*.h text eol=crlf
*.sln text eol=crlf
*.vcxproj text eol=crlf
*.filters text eol=crlf
*.png binary
*.jpg binary
*.jpeg binary
*.wav binary
*.flac binary
*.dll binary
*.exe binary
*.pdb binary
*.dmp binary
```

Do not run `git add --renormalize .` in this project.

- [ ] **Step 6: Run policy and whitespace checks**

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\test_repository_policy.ps1
git diff --check
```

Expected: `[PASS] application repository policy`; no whitespace diagnostics.

Verify the three known generated artifacts are currently tracked and scheduled for index-only removal in the authorized commit step:

```powershell
git ls-files --error-unmatch -- Builds/VisualStudio2026/dumpbin_release.txt Builds/VisualStudio2026/PerformanceTests2/Performa.38EB6EFF/x64/Debug/PerformanceTests2.pch Builds/VisualStudio2026/PerformanceTests2/Performa.38EB6EFF/x64/Release/PerformanceTests2.pch
```

- [ ] **Step 7: Inspect both repositories before staging**

```powershell
git -C "C:\Users\1993v\OneDrive\Desktop\Apex backup" status --short --untracked-files=all
git -C "C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core" status --short --untracked-files=all
```

Expected: generated dumps/bridge output no longer appear; human-authored files remain visible for review.

- [ ] **Step 8: Commit only with explicit authorization**

Application repository:

```powershell
git rm --cached -- Builds/VisualStudio2026/dumpbin_release.txt Builds/VisualStudio2026/PerformanceTests2/Performa.38EB6EFF/x64/Debug/PerformanceTests2.pch Builds/VisualStudio2026/PerformanceTests2/Performa.38EB6EFF/x64/Release/PerformanceTests2.pch
git add -- .gitignore .gitattributes Scripts/test_repository_policy.ps1
git diff --cached --check
git commit -m "chore: define repository output policy"
```

Outer repository:

```powershell
git add -- .gitignore .gitattributes
git diff --cached --check
git commit -m "chore: define workspace repository policy"
```

If commits are not authorized, leave a staging report and do not commit.

---

### Task 2: Pin dependencies and create one unsigned build interface

**Files:**
- Modify: `My DAW/DAW_Core/.gitmodules`
- Modify: `My DAW/DAW_Core/DAW_Core.jucer:245-261,991-1015`
- Modify: generated `My DAW/DAW_Core/Builds/VisualStudio2026/DAW_Core_App.vcxproj`
- Create: `My DAW/DAW_Core/Dependencies/apex-windows-dependencies.json`
- Create: `My DAW/DAW_Core/Scripts/materialize_dependencies.ps1`
- Create: `My DAW/DAW_Core/Scripts/apex_build_tools.ps1`
- Create: `My DAW/DAW_Core/Scripts/verify_dependencies.ps1`
- Create: `My DAW/DAW_Core/Scripts/build_apex.ps1`
- Create: `My DAW/DAW_Core/Scripts/test_build_foundation.ps1`
- Modify: `My DAW/DAW_Core/Scripts/sign_build.ps1:1-18`
- Create: `My DAW/DAW_Core/docs/BUILDING_WINDOWS.md`

**Interfaces:**
- Produces: `Get-ApexBuildTools`, returning resolved `MSBuild`, `Projucer`, repository root, and expected output paths.
- Produces: `materialize_dependencies.ps1 -JuceArchive "..\Sdk setups\juce-8.0.12-windows.zip"`, which installs only the verified JUCE payload at the ignored sibling SDK path.
- Produces: `verify_dependencies.ps1`, exit `0` only for pinned dependency identities and required files.
- Produces: `build_apex.ps1 -Configuration Debug|Release -Rebuild -Unsigned`.

- [ ] **Step 1: Write the failing build-foundation harness and verify RED**

Create `Scripts/test_build_foundation.ps1` before the scripts it exercises. It must:

```text
fail when apex_build_tools.ps1, materialize_dependencies.ps1, verify_dependencies.ps1, or build_apex.ps1 is absent
after implementation, verify Get-ApexBuildTools resolves the application root, pinned Projucer, and amd64 MSBuild
verify materialize_dependencies.ps1 rejects a deliberately wrong archive hash without replacing the installed SDK
verify build_apex.ps1 rejects an invalid configuration before invoking MSBuild
verify APEX_SKIP_SIGNING=1 exits sign_build.ps1 before certificate lookup or target inspection
```

Run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\test_build_foundation.ps1
```

Expected: nonzero exit naming the first missing script.

- [ ] **Step 2: Create the dependency contract before changing Git links**

Create `Dependencies/apex-windows-dependencies.json`:

```json
{
  "schemaVersion": 1,
  "platform": "windows-x64",
  "languageStandard": "c++17",
  "toolchain": {
    "visualStudioMajor": 18,
    "platformToolset": "v145",
    "windowsSdkProperty": "10.0"
  },
  "juce": {
    "version": "8.0.12",
    "archiveFileName": "juce-8.0.12-windows.zip",
    "archiveSha256": "15B4ED8302127138DFEE98E61A2B5AE1481B39649B139E02E77BFC29462B7EFB",
    "rootRelativeToRepository": "../Sdk setups/juce-8.0.12-windows/JUCE"
  },
  "signalsmithStretch": {
    "url": "https://github.com/Signalsmith-Audio/signalsmith-stretch.git",
    "commit": "57b93f4e9206a089a45387eaa39bdc9f310d3308",
    "path": "Source/ThirdParty/signalsmith-stretch"
  },
  "signalsmithLinear": {
    "url": "https://github.com/Signalsmith-Audio/linear.git",
    "commit": "88c701ce8d581946de5ee587848cde4a572ed6b5",
    "path": "Source/ThirdParty/signalsmith-linear"
  }
}
```

- [ ] **Step 3: Add deterministic JUCE materialization outside Git**

`Scripts/materialize_dependencies.ps1` must:

```text
require -JuceArchive
resolve the archive without relying on the current directory
require SHA-256 15B4ED8302127138DFEE98E61A2B5AE1481B39649B139E02E77BFC29462B7EFB
expand into a temporary sibling directory
require JUCE/Projucer.exe and JUCE/modules/juce_core/system/juce_StandardHeader.h
rename the verified temporary directory to ../Sdk setups/juce-8.0.12-windows
leave an existing verified installation unchanged
refuse to replace an existing mismatched installation
```

No SDK file is staged. Verify the local archive with:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\materialize_dependencies.ps1 -JuceArchive "..\Sdk setups\juce-8.0.12-windows.zip"
```

Expected: exit `0` and the pinned Projucer/module paths exist.

- [ ] **Step 4: Write and run the dependency verifier in RED state**

The verifier must check:

```text
JUCE StandardHeader version == 8.0.12
JUCE archive SHA-256 == pinned hash when the archive is present
Projucer.exe exists
signalsmith-stretch HEAD == pinned commit
signalsmith-linear HEAD == pinned commit at the declared sibling path
signalsmith-stretch/signalsmith-linear is absent so it cannot shadow the declared sibling include path
signalsmith-stretch2 is absent from git ls-files --stage
PluginEditorWindowWin32Patch.h is not referenced by DAW_Core.jucer
```

Run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\verify_dependencies.ps1
```

Expected: nonzero exit because Linear is not at the declared sibling path and `signalsmith-stretch2` is still an unmanaged gitlink.

- [ ] **Step 5: Normalize SignalSmith topology without deleting the local duplicate**

From the application repository, preserve the dirty nested Linear diff inside the ignored `.apex-debug/dependency-backups` workspace, then:

Do not execute the following index-changing commands until the Task 2 commit is authorized.

```powershell
$backupRoot = Join-Path (Get-Location) ".apex-debug\dependency-backups"
if (-not (Test-Path -LiteralPath (Get-Location))) { throw "Application repository root is unavailable" }
New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
git -C Source/ThirdParty/signalsmith-stretch/signalsmith-linear diff -- tests/main.cpp > "$backupRoot\signalsmith-linear-tests-main-20260717.patch"
Move-Item -LiteralPath "Source/ThirdParty/signalsmith-stretch/signalsmith-linear" -Destination (Join-Path $backupRoot "signalsmith-linear.local-backup")
git submodule absorbgitdirs Source/ThirdParty/signalsmith-stretch
git rm --cached -- Source/ThirdParty/signalsmith-stretch2
git submodule add https://github.com/Signalsmith-Audio/linear.git Source/ThirdParty/signalsmith-linear
git -C Source/ThirdParty/signalsmith-linear checkout 88c701ce8d581946de5ee587848cde4a572ed6b5
```

Do not delete the local `signalsmith-stretch2` directory in this task. Add `/Source/ThirdParty/signalsmith-stretch2/` to `.gitignore` after removing its gitlink. The clean clone authority becomes the two declared submodules.

- [ ] **Step 6: Correct Projucer inputs**

In `DAW_Core.jucer`:

1. Remove only the stale `PH_H006` entry for nonexistent `PluginEditorWindowWin32Patch.h`; keep `PluginEditorWindowWin32Patch.cpp`.
2. Add `/I"..\..\Source\ThirdParty"` to both Debug and Release `extraCompilerFlags`, so `signalsmith-linear/stft.h` resolves from the sibling submodule.

Regenerate with the pinned Projucer:

```powershell
& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave DAW_Core.jucer --lf
```

Expected: regenerated Visual Studio files reference no missing header and retain x64 Debug/Release, v145, and C++17.

- [ ] **Step 7: Add an explicit unsigned signing bypass**

Insert before certificate lookup in `Scripts/sign_build.ps1`:

```powershell
if ($env:APEX_SKIP_SIGNING -eq '1') {
    Write-Host '[sign] APEX_SKIP_SIGNING=1 - unsigned build requested.'
    exit 0
}
```

Normal builds retain current signing behavior. Verification builds set the environment variable before invoking MSBuild and restore its previous value afterward.

- [ ] **Step 8: Implement shared tool discovery and build scripts**

`Scripts/apex_build_tools.ps1` must use `vswhere.exe` to find Visual Studio major 18 and return the amd64 MSBuild path. `Scripts/build_apex.ps1` must:

```text
validate Configuration is Debug or Release
run verify_dependencies.ps1
set APEX_SKIP_SIGNING=1 when -Unsigned is present
invoke DAW_Core.sln /t:Build or /t:Rebuild with Configuration set to Debug or Release, Platform=x64, BuildProjectReferences=true, /m, /nologo, and /v:minimal
restore APEX_SKIP_SIGNING
require DAW_Core.exe and DAW_Core.pdb
print SHA-256 for both files
fail if Get-AuthenticodeSignature is Signed during an unsigned rebuild
```

Expected output paths:

```text
Builds/VisualStudio2026/x64/Debug/App/DAW_Core.exe
Builds/VisualStudio2026/x64/Debug/App/DAW_Core.pdb
Builds/VisualStudio2026/x64/Release/App/DAW_Core.exe
Builds/VisualStudio2026/x64/Release/App/DAW_Core.pdb
```

- [ ] **Step 9: Verify scripts, dependencies, and both configurations unsigned**

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\test_build_foundation.ps1
powershell -ExecutionPolicy Bypass -File Scripts\verify_dependencies.ps1
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned
```

Expected: all commands exit `0`; EXE/PDB pairs exist; both EXEs report `NotSigned`; hashes are printed. Compilation is not behavioral validation.

> **2026-07-18 verification ratification:** Archive verification was ratified as mandatory for both materializer and verifier. The stale ASIO SDK header path was removed from `DAW_Core.jucer` and the vcxproj was regenerated. `Invoke-GitIsolated` was added to `verify_dependencies.ps1` for subprocess git isolation. A `VerifierEnvironment` test was added to `test_build_foundation.ps1` to prove the verifier clears inherited Git repository-selection environment. A `ChildRedirect` test was added to prove the materializer and verifier reject a junction inside the approved SDK parent before child access.

- [ ] **Step 10: Commit only with explicit authorization**

```powershell
git add -- .gitignore .gitmodules DAW_Core.jucer Builds/VisualStudio2026/DAW_Core_App.vcxproj Builds/VisualStudio2026/DAW_Core_App.vcxproj.filters Dependencies Scripts/apex_build_tools.ps1 Scripts/materialize_dependencies.ps1 Scripts/verify_dependencies.ps1 Scripts/build_apex.ps1 Scripts/test_build_foundation.ps1 Scripts/sign_build.ps1 docs/BUILDING_WINDOWS.md Source/ThirdParty/signalsmith-stretch Source/ThirdParty/signalsmith-linear
git diff --cached --check
git diff --cached --submodule=log
git commit -m "build: pin Windows dependencies and unsigned builds"
```

---

### Task 3: Capture the current intended APEX application baseline

**Files:**
- Review and stage: the 34 currently modified tracked application files reported by `git diff --name-only`
- Modify within that reviewed set: `Source/Main.cpp:172-178` to remove the machine-specific splash path.
- Track after review: `.opencode/commands/*.md`, `.opencode/tools/visual-studio.ts`, `tools/VisualStudioBridge/Program.cs`, `tools/VisualStudioBridge/VisualStudioBridge.csproj`, `tools/Watch-VisualStudioCrash.ps1`
- Track after authority review: `.opencode/agents/daw-brain.md`, `AGENTS.md`, `opencode.json`
- Move and preserve: `Builds/VisualStudio2026/automation_test.cpp`, `Builds/VisualStudio2026/oversampling_test.cpp` to `Tests/Legacy/`
- Create: `Tests/Legacy/README.md`
- Track in the outer repository: `AGENTS.md`, `tools/debug-apex.ps1`, `tools/analyze-latest-crash.md`, the approved specification, and this plan.

**Interfaces:**
- Produces: one identified local application baseline containing current intended source, with generated/sensitive output excluded.

- [ ] **Step 1: Verify the measured source set has not drifted**

After Task 2 is committed, run:

```powershell
$changed = @(git diff --name-only -- Source Builds/VisualStudio2026/ArrangementEditor)
if ($changed.Count -ne 34) { throw "Expected 34 pre-existing tracked source changes; found $($changed.Count). Stop for review." }
git diff --check -- Source Builds/VisualStudio2026/ArrangementEditor
```

Expected: exactly 34 paths and no whitespace diagnostics. A different count is a review stop, not permission to stage a broader set.

- [ ] **Step 2: Review security- and architecture-sensitive diffs**

Inspect every changed file, with explicit attention to:

```text
AudioEngine.h realtime callback work
AudioFileManager.h ownership/cache behavior
MainComponent.cpp monitoring and callback paths
ProjectManager.cpp save/publication behavior
TransportController.cpp transport ownership
ArrangementViewCore.cpp Blade mutation and diagnostics
plugin state/lifecycle headers
step sequencer snapshot publication
```

Reject credentials, private keys, tokens, generated dumps, absolute user paths, and unexplained diagnostics before staging.

- [ ] **Step 3: Remove the machine-specific splash path**

Before moving the harnesses, replace the absolute Downloads splash path in `Source/Main.cpp` with deterministic installed/development candidates:

```cpp
const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
const auto installedLogo = executable.getSiblingFile ("logo").getChildFile ("1000166327(2).png");
const auto repositoryLogo = executable.getParentDirectory()
                                      .getParentDirectory()
                                      .getParentDirectory()
                                      .getParentDirectory()
                                      .getParentDirectory()
                                      .getParentDirectory()
                                      .getChildFile ("logo")
                                      .getChildFile ("1000166327(2).png");
const auto logoFile = installedLogo.existsAsFile() ? installedLogo : repositoryLogo;
splashScreen_ = logoFile.existsAsFile() ? DAWSplashScreen::launch (logoFile) : nullptr;
```

Document that installation packaging must place `logo/1000166327(2).png` beside the executable.

- [ ] **Step 4: Preserve the two ad-hoc harnesses without treating them as active tests**

Move both source files into `Tests/Legacy/` and add a README stating:

```text
These sources are preserved historical harnesses.
They are not built, not release gates, and not evidence of passing behavior.
Their scenarios may be migrated into APEXTests only through a failing JUCE UnitTest first.
```

Remove their hardcoded `C:\Users\1993v\...` output paths during the move; if retained as callable utilities, accept an output path argument.

- [ ] **Step 5: Build the Visual Studio bridge before tracking its source**

```powershell
dotnet build tools\VisualStudioBridge\VisualStudioBridge.csproj -c Release
```

Expected: exit `0`; `bin/` and `obj/` remain ignored. This validates compilation only; the bridge remains outside the APEX audio runtime.

- [ ] **Step 6: Stage only reviewed human-authored files**

```powershell
git add -u -- Source Builds/VisualStudio2026/ArrangementEditor
git add -- .opencode/commands .opencode/tools/visual-studio.ts tools/VisualStudioBridge/Program.cs tools/VisualStudioBridge/VisualStudioBridge.csproj tools/Watch-VisualStudioCrash.ps1 docs/superpowers/specs/2026-07-16-vs-crash-bridge-design.md Tests/Legacy
```

Update app-scoped `AGENTS.md` and `.opencode/agents/daw-brain.md` to state that the canonical Brain is `../../DAW_BRAIN.md` in the official workspace, then stage those files with `opencode.json`. Do not track the duplicate app-root `DAW_BRAIN.md`.

Before editing `.opencode/` or `opencode.json`, load the required `customize-opencode` skill and validate the resulting configuration with its prescribed workflow.

```powershell
git add -- AGENTS.md opencode.json .opencode/agents/daw-brain.md
```

- [ ] **Step 7: Rebuild from the staged source**

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned
git diff --cached --check
git diff --cached --stat
git status --short --untracked-files=all
```

Expected: builds exit `0`; no generated output is staged; only deliberately excluded local artifacts remain untracked/ignored.

- [ ] **Step 8: Commit the application baseline only with explicit authorization**

```powershell
git commit -m "chore: capture current APEX application baseline"
```

After the commit, require `git status --short --untracked-files=all` to be empty in the application repository before calling the local baseline clean.

- [ ] **Step 9: Capture the outer workspace baseline only with explicit authorization**

From the outer repository, stage only canonical workflow and design files:

```powershell
git add -- AGENTS.md tools/debug-apex.ps1 tools/analyze-latest-crash.md docs/superpowers/specs/2026-07-17-apex-windows-stability-program-design.md docs/superpowers/plans/2026-07-17-apex-windows-baseline-evidence-foundation.md
git diff --cached --check
git diff --cached --stat
git commit -m "docs: establish APEX stability baseline"
```

Require both repository status commands to be empty before evidence can be graded release-quality.

---

### Task 4: Add the independent APEX test runner

**Files:**
- Create: `My DAW/DAW_Core/Tests/APEXTests.jucer`
- Create: `My DAW/DAW_Core/Tests/Source/Main.cpp`
- Create: `My DAW/DAW_Core/Tests/Source/Smoke/RunnerSmokeTests.cpp`
- Generate and track: `My DAW/DAW_Core/Tests/JuceLibraryCode/*`
- Generate and track: `My DAW/DAW_Core/Tests/Builds/VisualStudio2026/APEXTests.sln` and project files
- Create: `My DAW/DAW_Core/Scripts/run_apex_tests.ps1`

**Interfaces:**
- Produces: `APEXTests.exe` options `--category`, `--name`, `--seed`, and `--results-json`, as exercised by the exact commands below.
- Exit codes: `0` all selected tests pass, `1` assertion failure, `2` invalid arguments or no selected tests.
- Stable default seed: `0xA9E12026`.

- [ ] **Step 1: Write the smoke test before the runner exists**

Create a JUCE test registered as:

```cpp
class RunnerSmokeTests final : public juce::UnitTest
{
public:
    RunnerSmokeTests() : juce::UnitTest ("runner.discovery.v1", "APEX.Smoke") {}

    void runTest() override
    {
        beginTest ("always-on assertion executes");
        expectEquals (2 + 2, 4);
    }
};

static RunnerSmokeTests runnerSmokeTests;
```

- [ ] **Step 2: Verify RED because no runner project exists**

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Category APEX.Smoke
```

Expected: nonzero exit identifying missing `Tests/APEXTests.jucer` or generated solution.

- [ ] **Step 3: Create the minimal Projucer console project**

`APEXTests.jucer` must define:

```text
projectType=consoleapp
VS2026 exporter only
targetName=APEXTests
Debug and Release x64
C++17
JUCE_ENABLE_ALLOCATION_HOOKS=1
JUCE_STRICT_REFCOUNTEDPOINTER=1
modules: juce_core, juce_events, juce_data_structures, juce_graphics, juce_gui_basics, juce_audio_basics, juce_audio_formats
module path: ../../Sdk setups/juce-8.0.12-windows/JUCE/modules
```

Do not define `JUCE_UNIT_TESTS=1`; that would register JUCE’s vendored test corpus rather than only APEX tests.

- [ ] **Step 4: Implement the deterministic console runner**

Adapt the pinned JUCE `extras/UnitTestRunner/Source/Main.cpp` with these required differences:

```cpp
constexpr juce::int64 defaultSeed = 0xA9E12026;
runner.setAssertOnFailure (false);
runner.setPassesAreLogged (false);

// Run category, name, or all registered APEX tests.
// Count TestResult entries, assertions, and failures.
// Write JSON atomically when --results-json is supplied.
// Return 2 if zero TestResult entries were produced.
// Return 1 if any TestResult::failures is nonzero.
// Return 0 otherwise.
```

The result JSON contract is:

```json
{
  "schemaVersion": 1,
  "seed": 2850103334,
  "configuration": "Debug",
  "resultGroups": 1,
  "assertionsPassed": 1,
  "assertionsFailed": 0,
  "durationMs": 0,
  "tests": []
}
```

Set `configuration` through an `APEX_TEST_CONFIGURATION` preprocessor definition in each Projucer configuration.

- [ ] **Step 5: Generate, build, and run Debug**

```powershell
& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave Tests\APEXTests.jucer --lf
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Category APEX.Smoke -Seed 0xA9E12026
```

Expected: one test group, zero failures, exit `0`.

- [ ] **Step 6: Build and run Release**

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Category APEX.Smoke -Seed 0xA9E12026
```

Expected: the same always-on assertion executes in Release and exits `0`.

- [ ] **Step 7: Commit only with explicit authorization**

```powershell
git add -- Tests/APEXTests.jucer Tests/Source Tests/JuceLibraryCode Tests/Builds/VisualStudio2026 Scripts/run_apex_tests.ps1
git diff --cached --check
git commit -m "test: add deterministic APEX test runner"
```

---

### Task 5: Add machine-readable evidence validation

**Files:**
- Create: `My DAW/DAW_Core/evidence/schemas/apex-test-evidence-v1.schema.json`
- Create: `My DAW/DAW_Core/evidence/fixtures/valid-minimal.json`
- Create: `My DAW/DAW_Core/evidence/fixtures/invalid-missing-source.json`
- Create: `My DAW/DAW_Core/Scripts/validate_test_evidence.ps1`
- Create: `My DAW/DAW_Core/Scripts/test_validate_test_evidence.ps1`
- Modify: `My DAW/DAW_Core/Scripts/run_apex_tests.ps1`

**Interfaces:**
- Produces: `validate_test_evidence.ps1 -Manifest evidence\fixtures\valid-minimal.json` and equivalent run-manifest validation.
- Produces: per-run `evidence/runs/{suite-id}/{run-id}/manifest.json`, `results.json`, and `stdout.txt`.

- [ ] **Step 1: Create valid and invalid fixtures first**

The valid fixture must include:

```json
{
  "schemaVersion": 1,
  "evidenceGrade": "diagnostic",
  "runId": "fixture-valid",
  "suiteId": "runner-smoke",
  "source": {
    "outerCommit": "0000000000000000000000000000000000000000",
    "outerDirty": true,
    "applicationCommit": "0000000000000000000000000000000000000000",
    "applicationDirty": true
  },
  "build": {
    "configuration": "Debug",
    "platform": "x64",
    "toolset": "v145",
    "binarySha256": "0000000000000000000000000000000000000000000000000000000000000000"
  },
  "dependencies": { "juceVersion": "8.0.12" },
  "environment": { "os": "fixture", "cpu": "fixture", "powerMode": "fixture", "locale": "en-US" },
  "execution": {
    "command": ["APEXTests.exe", "--category=APEX.Smoke"],
    "startedUtc": "2026-07-17T00:00:00.0000000Z",
    "seed": 2850103334,
    "exitCode": 0,
    "durationMs": 1
  },
  "results": { "resultGroups": 1, "assertionsPassed": 1, "assertionsFailed": 0 },
  "artifacts": []
}
```

The invalid fixture must omit `source`.

Create `apex-test-evidence-v1.schema.json` with this complete minimum schema:

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://apex.local/schemas/apex-test-evidence-v1.schema.json",
  "title": "APEX test evidence v1",
  "type": "object",
  "additionalProperties": false,
  "required": ["schemaVersion", "evidenceGrade", "runId", "suiteId", "source", "build", "dependencies", "environment", "execution", "results", "artifacts"],
  "properties": {
    "schemaVersion": { "const": 1 },
    "evidenceGrade": { "enum": ["diagnostic", "release"] },
    "runId": { "type": "string", "minLength": 1 },
    "suiteId": { "type": "string", "minLength": 1 },
    "source": {
      "type": "object",
      "additionalProperties": false,
      "required": ["outerCommit", "outerDirty", "applicationCommit", "applicationDirty"],
      "properties": {
        "outerCommit": { "type": "string", "pattern": "^[0-9a-fA-F]{40}$" },
        "outerDirty": { "type": "boolean" },
        "applicationCommit": { "type": "string", "pattern": "^[0-9a-fA-F]{40}$" },
        "applicationDirty": { "type": "boolean" }
      }
    },
    "build": {
      "type": "object",
      "additionalProperties": false,
      "required": ["configuration", "platform", "toolset", "binarySha256"],
      "properties": {
        "configuration": { "enum": ["Debug", "Release"] },
        "platform": { "const": "x64" },
        "toolset": { "const": "v145" },
        "compiler": { "type": "string" },
        "windowsSdk": { "type": "string" },
        "binaryPath": { "type": "string" },
        "binarySha256": { "type": "string", "pattern": "^[0-9a-fA-F]{64}$" }
      }
    },
    "dependencies": {
      "type": "object",
      "additionalProperties": false,
      "required": ["juceVersion"],
      "properties": { "juceVersion": { "const": "8.0.12" } }
    },
    "environment": {
      "type": "object",
      "additionalProperties": false,
      "required": ["os", "cpu", "powerMode", "locale"],
      "properties": {
        "os": { "type": "string", "minLength": 1 },
        "cpu": { "type": "string", "minLength": 1 },
        "powerMode": { "type": "string", "minLength": 1 },
        "locale": { "type": "string", "minLength": 1 },
        "audioDriver": { "type": ["string", "null"] },
        "audioDevice": { "type": ["string", "null"] },
        "sampleRate": { "type": ["number", "null"], "exclusiveMinimum": 0 },
        "blockSize": { "type": ["integer", "null"], "minimum": 1 }
      }
    },
    "execution": {
      "type": "object",
      "additionalProperties": false,
      "required": ["command", "startedUtc", "seed", "exitCode", "durationMs"],
      "properties": {
        "command": { "type": "array", "minItems": 1, "items": { "type": "string" } },
        "startedUtc": { "type": "string", "format": "date-time" },
        "seed": { "type": "integer" },
        "exitCode": { "type": "integer" },
        "durationMs": { "type": "integer", "minimum": 0 }
      }
    },
    "results": {
      "type": "object",
      "additionalProperties": false,
      "required": ["resultGroups", "assertionsPassed", "assertionsFailed"],
      "properties": {
        "resultGroups": { "type": "integer", "minimum": 0 },
        "assertionsPassed": { "type": "integer", "minimum": 0 },
        "assertionsFailed": { "type": "integer", "minimum": 0 }
      }
    },
    "artifacts": {
      "type": "array",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "required": ["path", "bytes", "sha256"],
        "properties": {
          "path": { "type": "string", "minLength": 1 },
          "bytes": { "type": "integer", "minimum": 0 },
          "sha256": { "type": "string", "pattern": "^[0-9a-fA-F]{64}$" }
        }
      }
    }
  }
}
```

- [ ] **Step 2: Write the validator test and verify RED**

`test_validate_test_evidence.ps1` must require:

```text
valid-minimal.json exits 0
invalid-missing-source.json exits nonzero and names source
```

Run before creating the validator:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\test_validate_test_evidence.ps1
```

Expected: nonzero exit because the validator script is absent.

- [ ] **Step 3: Implement the PowerShell 5.1 validator**

Validate all required objects, enum values, nonnegative counts, and these relations:

```text
assertionsFailed == 0 when execution.exitCode == 0
evidenceGrade is diagnostic or release
release requires source.outerDirty == false and source.applicationDirty == false
every artifact path is relative to the manifest directory
every artifact exists
every artifact byte count and SHA-256 match
```

Reject `..`, rooted paths, and missing hashes in artifact entries. Print one `[FAIL]` line per violation and exit `1`; print `[PASS] evidence manifest {runId}` and exit `0` otherwise.

- [ ] **Step 4: Make the runner publish evidence atomically**

`run_apex_tests.ps1` must write into a temporary run directory, validate the complete manifest, then rename the directory using `{UTC}-{configuration}-{short-commit}`. It must record both repository commits and dirty flags, exact command arguments, test binary hash, JUCE version, OS, CPU, locale, power scheme, start UTC, duration, seed, and exit code.

- [ ] **Step 5: Run validator and runner tests**

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\test_validate_test_evidence.ps1
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Category APEX.Smoke -Seed 0xA9E12026
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Category APEX.Smoke -Seed 0xA9E12026
```

Expected: validator fixture tests pass; both runs produce validated diagnostic manifests when the tree is dirty, or release-eligible manifests when both repositories are clean.

- [ ] **Step 6: Commit only with explicit authorization**

```powershell
git add -- evidence/schemas evidence/fixtures Scripts/validate_test_evidence.ps1 Scripts/test_validate_test_evidence.ps1 Scripts/run_apex_tests.ps1
git diff --cached --check
git commit -m "test: add APEX evidence manifests"
```

---

### Task 6: Add the permanent Blade overall-code regression

**Files:**
- Create: `My DAW/DAW_Core/Builds/VisualStudio2026/ArrangementEditor/BladeSplitPlanCore.h`
- Modify: `My DAW/DAW_Core/Builds/VisualStudio2026/ArrangementEditor/ArrangementViewCore.cpp:2105-2388`
- Modify: `My DAW/DAW_Core/Source/AudioEngineCore/AudioFileManager.h:79-92` only after RED proves the nested-share defect
- Create: `My DAW/DAW_Core/Tests/Source/Arrangement/ClipSplitCoreTests.cpp`
- Create: `My DAW/DAW_Core/Tests/Source/Arrangement/BladeSplitPlanCoreTests.cpp`
- Create: `My DAW/DAW_Core/Tests/Source/Arrangement/BladeUndoPersistenceTests.cpp`
- Create: `My DAW/DAW_Core/Tests/Source/Arrangement/AudioFileManagerShareTests.cpp`
- Create: `My DAW/DAW_Core/Tests/Source/Arrangement/BladeSourceRenderContractTests.cpp`
- Modify: `My DAW/DAW_Core/DAW_Core.jucer`
- Modify: `My DAW/DAW_Core/Tests/APEXTests.jucer`
- Compile in the test target: `Builds/VisualStudio2026/ArrangementEditor/ArrangementClipStateCore.cpp`, `Source/ClipCore/Clip.cpp`, and `Source/MidiCore/MidiClip.cpp`

**Interfaces:**
- Consumes: current `ClipSplitCore::splitClip`, `AudioClip` `ValueTree` persistence, `ArrangementUndoCore`, `ApexSourceReadContractCore`, and `AudioFileManager` public cache methods.
- Produces: pure `BladeSplitPlanCore::makePlan(const BladeSplitPlanInput&)` used by `performSplit` for calculations only.
- Does not claim the prior Debug/Release root cause.

- [ ] **Step 1: Write characterization tests for current split algebra**

Register stable IDs under `APEX.Arrangement` and cover this matrix:

```text
source/device rates: 44100/44100, 48000/48000, 44100/48000, 48000/44100
nonzero source start and offset
split inside a block-sized interval
left/right timeline contiguity
left/right source contiguity
outer fades preserved; cut-edge fades zero
time/pitch copied exactly
invalid start/end split rejected
three repeated splits preserve the original union
```

Use exact integer source-bound assertions and an epsilon only for second-domain calculations.

- [ ] **Step 2: Run the algebra tests before extraction**

```powershell
& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave Tests\APEXTests.jucer --lf
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Name arrangement.blade.split-algebra.v1
```

Expected: characterization tests either pass against current `ClipSplitCore` or expose a precise existing mismatch. Do not modify production behavior for a mismatch until the failing input and expected contract are reviewed.

- [ ] **Step 3: Write split-plan tests against the intended interface and verify RED**

Write `BladeSplitPlanCoreTests.cpp` against this intended interface before creating the production header:

```cpp
struct BladeSplitPlanInput
{
    ArrangementClipModel visual;
    juce::ValueTree engineState;
    DAW::SamplePosition engineStart = 0;
    DAW::SamplePosition engineLength = 0;
    DAW::SamplePosition processedTimelineLength = 0;
    DAW::SamplePosition sourceOffset = 0;
    int64_t sourceTotalSamples = 0;
    double sourceSampleRate = 0.0;
    double deviceSampleRate = 0.0;
    double splitTime = 0.0;
};

struct BladeSplitPlan
{
    bool valid = false;
    ArrangementClipModel leftVisual;
    ArrangementClipModel rightVisual;
    juce::ValueTree leftEngineState;
    juce::ValueTree rightEngineState;
    DAW::SamplePosition sourceBoundary = 0;
};

class BladeSplitPlanCore final
{
public:
    static BladeSplitPlan makePlan (const BladeSplitPlanInput& input);
};
```

Regenerate the test project and run the focused test:

```powershell
& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave Tests\APEXTests.jucer --lf
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Name arrangement.blade.split-plan.v1 -Seed 0xA9E12026
```

Expected: compile failure naming missing `BladeSplitPlanCore.h`.

- [ ] **Step 4: Extract and test only the engine-state calculation from `performSplit`**

Implement `makePlan` with the calculations currently at `ArrangementViewCore.cpp:2193-2235`. Its `ValueTree` copies and property writes are explicitly control-plane allocations; it performs no manager mutation, logging, or file I/O. Assert exact `length`, `startPosition`, `sourceOffset`, `sourceStartSample`, `sourceEndSample`, and fade properties in both output trees, then make `performSplit` consume the passing plan without changing its mutation order.

- [ ] **Step 5: Write the repeated cache-share test and verify RED**

The test must:

```text
write a deterministic temporary stereo WAV
load it for clip A
share A snapshot to B
share B snapshot to C
assert A, B, and C expose equal nonzero frame counts and identical selected samples
```

Expected current failure: clip C resolves an empty local buffer because `CachedAudio::getBufferRef()` dereferences only one `source_` level.

Run the focused RED case:

```powershell
& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave Tests\APEXTests.jucer --lf
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Name audio-cache.shared-source-chain.v1 -Seed 0xA9E12026
```

- [ ] **Step 6: Apply the smallest proven cache correction**

Change only these accessors:

```cpp
return source_ ? source_->getBufferRef() : buffer;
return (! rawMin.empty() || ! source_) ? rawMin : source_->getRawMinRef();
return (! rawMax.empty() || ! source_) ? rawMax : source_->getRawMaxRef();
```

Run the cache test again. Expected: PASS in Debug and Release.

- [ ] **Step 7: Add undo/redo and persistence regressions below the GUI**

Use `ArrangementClipStateCore` plus `ArrangementUndoCore` to apply one `Split Clip` command, undo to the original model, and redo to the same left/right semantic state. Use `AudioClip::getState()`, XML round-trip, and `AudioClip::restoreState()` to prove both halves preserve engine timeline/source/fade properties. Do not require regenerated engine IDs to equal pre-undo IDs.

- [ ] **Step 8: Add a deterministic source-read continuity oracle**

Generate non-silent, frame-varying stereo source data. Use `ApexSourceReadContractCore` with the original plan and both split plans, concatenate the left/right source windows, and compare their mapping against the unsplit window:

```text
same frame count
no gap or overlap at sourceBoundary
bitwise equality for same-rate source windows
monotonic source positions at 44100/48000 and 48000/44100
the first right source position equals the declared source boundary
no NaN or infinity in calculated source positions
```

This is a production mapping regression below the GUI. A real APEX Debug/Release Blade smoke remains required because this project does not certify the complete callback, plug-in, or Component path.

- [ ] **Step 9: Run the complete Blade matrix in both builds**

```powershell
& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave DAW_Core.jucer --lf
& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave Tests\APEXTests.jucer --lf
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Category APEX.Arrangement -Seed 0xA9E12026
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Category APEX.Arrangement -Seed 0xA9E12026
```

Expected: all split algebra, plan, repeated-share, undo/redo, persistence, and source-read continuity tests pass in both builds.

- [ ] **Step 10: Run focused application validation**

Build both application configurations unsigned, then manually run the same deterministic WAV through Blade in each binary. Confirm waveform and playback on both halves, repeated split, undo/redo, save/reopen, and differing project/source rates. Record this as runtime evidence, not an automated-test substitute.

- [ ] **Step 11: Commit only with explicit authorization**

```powershell
git add -- Builds/VisualStudio2026/ArrangementEditor/BladeSplitPlanCore.h Builds/VisualStudio2026/ArrangementEditor/ArrangementViewCore.cpp Source/AudioEngineCore/AudioFileManager.h DAW_Core.jucer Builds/VisualStudio2026/DAW_Core_App.vcxproj Builds/VisualStudio2026/DAW_Core_App.vcxproj.filters Tests/Source/Arrangement Tests/APEXTests.jucer Tests/Builds/VisualStudio2026
git diff --cached --check
git commit -m "test: add permanent Blade regressions"
```

---

### Task 7: Add callback timing and allocation-audit foundations

**Files:**
- Create: `My DAW/DAW_Core/Source/DiagnosticsCore/CallbackAuditCore.h`
- Modify: `My DAW/DAW_Core/Source/MainComponent.h`
- Modify: `My DAW/DAW_Core/Source/MainComponent.cpp:5960-6013,6298-6346`
- Create: `My DAW/DAW_Core/Tests/Source/Diagnostics/CallbackAuditCoreTests.cpp`
- Modify: `My DAW/DAW_Core/DAW_Core.jucer`
- Modify: `My DAW/DAW_Core/Tests/APEXTests.jucer`

**Interfaces:**
- Produces: `CallbackAuditRecord`, `CallbackAuditRing<Capacity>`, `CallbackAuditAccumulator`, and `CallbackAuditSnapshot`.
- Runtime enable switch: process environment `APEX_CALLBACK_AUDIT=1`, read before audio starts.
- Optional output: for example, `APEX_CALLBACK_AUDIT_OUTPUT=C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core\evidence\runs\callback-audit\callback-summary.json`, read before audio starts and written only by the non-realtime drain.

- [ ] **Step 1: Write ring and histogram tests first**

Cover:

```text
FIFO order
drop-newest on full ring
monotonic overflow count
wraparound
empty drain
deadline miss classification
p50/p95/p99/p99.9/p99.99/max from fixed histogram
zero new/delete calls during push and pop with juce::UnitTestAllocationChecker
```

Regenerate and run `APEX.Diagnostics` before the header exists:

```powershell
& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave Tests\APEXTests.jucer --lf
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Category APEX.Diagnostics -Seed 0xA9E12026
```

Expected: compile failure naming `CallbackAuditCore.h`.

- [ ] **Step 2: Implement fixed-size realtime facts and SPSC publication**

Use this record surface:

```cpp
struct CallbackAuditRecord
{
    uint64_t sequence = 0;
    int64_t startTicks = 0;
    int64_t durationTicks = 0;
    int64_t periodTicks = 0;
    int32_t numSamples = 0;
    uint32_t streamGeneration = 0;
    uint8_t deadlineMiss = 0;
};

struct CallbackAuditSnapshot
{
    uint64_t callbacks = 0;
    uint64_t deadlineMisses = 0;
    uint64_t maximumConsecutiveMisses = 0;
    uint64_t ringOverflows = 0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double p999 = 0.0;
    double p9999 = 0.0;
    double maximum = 0.0;
};

template <size_t Capacity>
class CallbackAuditRing final
{
public:
    bool tryPush (const CallbackAuditRecord& record) noexcept;
    bool tryPop (CallbackAuditRecord& record) noexcept;
    uint64_t getOverflowCount() const noexcept;
};

class CallbackAuditAccumulator final
{
public:
    void add (const CallbackAuditRecord& record) noexcept;
    CallbackAuditSnapshot snapshot (uint64_t ringOverflows) const noexcept;
    void reset() noexcept;
};
```

All percentile fields are callback-duration/period ratios; `1.0` is one complete deadline period.

The ring owns `std::array<CallbackAuditRecord, Capacity>` and atomic monotonic read/write indices. `tryPush` performs bounded copies and increments an atomic overflow counter when full. The accumulator uses a fixed utilization histogram and retains no per-callback vector.

- [ ] **Step 3: Verify the audit core has no new/delete in measured operations**

Construct the ring and records before `juce::UnitTestAllocationChecker`. Measure only `tryPush`, `tryPop`, and accumulator insertion inside the checker scope. Expected: zero hook calls in Debug and Release.

- [ ] **Step 4: Instrument the complete outer callback conditionally**

When enabled, capture high-resolution ticks before line 5969 and after line 6012. Precompute `periodTicks` in `audioDeviceAboutToStart` from opened sample rate and block size. Publish one record after all input copy, `ApplicationCore`, step sequencer, and drum sampler work. Do not format or log from the callback.

- [ ] **Step 5: Drain and report outside realtime**

Drain from `inputWatchdogTick()`. At a bounded five-second interval, the message thread may log one summary containing p50/p95/p99/p99.9/p99.99/max, deadline misses, maximum consecutive misses, ring overflow, JUCE CPU usage, and `deviceManager.getXRunCount()`. When `APEX_CALLBACK_AUDIT_OUTPUT` is set, atomically replace that JSON file from the message thread with the same counters plus opened sample rate, block size, and stream generation.

- [ ] **Step 6: Run unit tests and focused application builds**

```powershell
& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave DAW_Core.jucer --lf
& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave Tests\APEXTests.jucer --lf
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Category APEX.Diagnostics -Seed 0xA9E12026
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Category APEX.Diagnostics -Seed 0xA9E12026
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned
```

Expected: unit tests and builds pass. This does not prove the full callback is allocation-free and does not replace hardware measurements.

- [ ] **Step 7: Collect hardware evidence separately**

Run with `APEX_CALLBACK_AUDIT=1` and a unique `APEX_CALLBACK_AUDIT_OUTPUT` path on Apollo Solo USB 48 kHz/64, Volt 1 48 kHz/64 and 128, and Realtek/WASAPI 48 kHz/128. Record opened settings, power mode, driver version, session identity, percentiles, maxima, deadline misses, xruns, and ring overflow. Any nonzero reproducible miss is investigated rather than hidden by a relaxed threshold.

- [ ] **Step 8: Commit only with explicit authorization**

```powershell
git add -- Source/DiagnosticsCore/CallbackAuditCore.h Source/MainComponent.h Source/MainComponent.cpp Tests/Source/Diagnostics DAW_Core.jucer Tests/APEXTests.jucer Builds/VisualStudio2026/DAW_Core_App.vcxproj Builds/VisualStudio2026/DAW_Core_App.vcxproj.filters Tests/Builds/VisualStudio2026
git diff --cached --check
git commit -m "test: add callback audit foundation"
```

---

### Task 8: Add recording writer-integrity contract tests

**Files:**
- Create: `My DAW/DAW_Core/Tests/Source/Recording/Fakes/ScriptedAudioFormatWriter.h`
- Create: `My DAW/DAW_Core/Tests/Source/Recording/Fakes/ScriptedSeekableOutputStream.h`
- Create: `My DAW/DAW_Core/Tests/Source/Recording/RecordingWriterIntegrityTests.cpp`
- Modify: `My DAW/DAW_Core/Tests/APEXTests.jucer`

**Interfaces:**
- Consumes: pinned JUCE `AudioFormatWriter::ThreadedWriter` public interface.
- Produces: deterministic characterization of FIFO acceptance, writer failure, flush failure, draining, and valid-prefix expectations.
- Does not modify `RecordingDiskWriterCore` or claim disk failures currently propagate to APEX.

- [ ] **Step 1: Write the scripted-writer tests before the fake exists**

Stable test IDs under `APEX.Recording`:

```text
recording.writer.fifo-not-durable.v1
recording.writer.queue-full-whole-block.v1
recording.writer.runtime-write-failure.v1
recording.writer.flush-failure.v1
recording.writer.stop-drains-before-destroy.v1
recording.writer.valid-prefix.v1
recording.writer.counter-width.v1
recording.writer.normal-roundtrip.v1
recording.writer.seekable-stream-failure.v1
recording.writer.wav-header-prefix.v1
```

```powershell
& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave Tests\APEXTests.jucer --lf
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Category APEX.Recording -Seed 0xA9E12026
```

Expected initial result: compile failure because the scripted writer is absent.

- [ ] **Step 2: Implement the deterministic `AudioFormatWriter` fake**

Use a shared state object that outlives writer deletion:

```cpp
struct ScriptedWriterState
{
    juce::WaitableEvent allowWrite;
    std::atomic<int64_t> attemptedFrames { 0 };
    std::atomic<int64_t> confirmedFrames { 0 };
    std::atomic<int64_t> firstFailedFrame { -1 };
    std::atomic<int> writeCalls { 0 };
    std::atomic<int> flushCalls { 0 };
    std::atomic<bool> destroyed { false };
    int failWriteCall = -1;
    int failFlushCall = -1;
    bool blockWrites = false;
};
```

`ScriptedAudioFormatWriter` derives from `juce::AudioFormatWriter`, owns a `juce::MemoryOutputStream`, records attempted/confirmed frames, blocks only on the disk thread when requested, and returns scripted write/flush results.

Its interface is:

```cpp
class ScriptedAudioFormatWriter final : public juce::AudioFormatWriter
{
public:
    explicit ScriptedAudioFormatWriter (std::shared_ptr<ScriptedWriterState> stateToUse);
    ~ScriptedAudioFormatWriter() override;
    bool write (const int** channels, int numSamples) override;
    bool flush() override;

private:
    std::shared_ptr<ScriptedWriterState> state;
};
```

`ScriptedSeekableOutputStream` derives from `juce::OutputStream` and implements all four required virtual methods:

```cpp
void flush() override;
bool setPosition (juce::int64 newPosition) override;
juce::int64 getPosition() override;
bool write (const void* data, size_t bytes) override;
```

It stores bytes in a pre-sized test buffer, scripts failure after an exact byte position, counts seek/flush attempts, and never touches the filesystem.

```cpp
class ScriptedSeekableOutputStream final : public juce::OutputStream
{
public:
    explicit ScriptedSeekableOutputStream (size_t capacityBytes, juce::int64 failAtByte = -1);
    void flush() override;
    bool setPosition (juce::int64 newPosition) override;
    juce::int64 getPosition() override;
    bool write (const void* data, size_t bytes) override;

    int getFlushCalls() const noexcept;
    int getSeekCalls() const noexcept;
    juce::int64 getFirstFailedByte() const noexcept;

private:
    std::vector<std::byte> storage;
    juce::int64 position = 0;
    juce::int64 failAt = -1;
    juce::int64 firstFailedByte = -1;
    int flushCalls = 0;
    int seekCalls = 0;
};
```

- [ ] **Step 3: Prove FIFO acceptance is distinct from writer success**

Use a `juce::TimeSliceThread`, enqueue a block successfully, script the underlying writer to return `false`, release the disk-thread gate, and destroy `ThreadedWriter`. Assert:

```text
ThreadedWriter::write returned true
underlying writer attempted the block
confirmedFrames remained zero
writer destruction occurred
```

This test passes by characterizing JUCE’s pinned behavior; it must not assert that APEX currently receives the failure.

- [ ] **Step 4: Prove whole-block overflow and destructor draining**

Use an eight-frame FIFO, block the disk writer with `WaitableEvent`, fill available capacity, and submit a block larger than remaining space. Assert the rejected block is not partially copied. Release the gate, destroy `ThreadedWriter`, and assert every accepted block was attempted before writer destruction.

- [ ] **Step 5: Add valid-prefix and 64-bit contract tests**

Mark each input block with deterministic frame values. On a scripted middle failure, compute the contiguous confirmed prefix and prove later attempted data cannot relabel the take complete. Exercise counters above `INT_MAX` through the shared-state accounting without allocating a multi-hour buffer.

- [ ] **Step 6: Add a real temporary WAV round trip**

Write deterministic stereo blocks through JUCE WAV output, close/finalize, reopen with `AudioFormatReader`, and assert channel count, sample rate, frame count, and selected sample values. Keep this as file-format evidence, distinct from stable-storage durability.

Also construct a JUCE WAV writer over `ScriptedSeekableOutputStream`, fail data writes at a declared frame boundary, and assert the writer returns `false`, no later frame is counted as confirmed, and final header rewrite/seek attempts are observable. This establishes the expected failure signal that JUCE `ThreadedWriter` currently discards.

- [ ] **Step 7: Run both configurations**

```powershell
& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave Tests\APEXTests.jucer --lf
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Category APEX.Recording -Seed 0xA9E12026
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Category APEX.Recording -Seed 0xA9E12026
```

Expected: all JUCE characterization and contract tests pass. The evidence report must retain the known gap: current APEX `RecordingDiskWriterCore` still counts FIFO acceptance and does not propagate actual writer/flush failure.

- [ ] **Step 8: Commit only with explicit authorization**

```powershell
git add -- Tests/Source/Recording Tests/APEXTests.jucer Tests/Builds/VisualStudio2026
git diff --cached --check
git commit -m "test: characterize recording writer integrity"
```

---

### Task 9: Verify the complete first-project gate and record remaining uncertainty

**Files:**
- Modify: `My DAW/DAW_Core/docs/BUILDING_WINDOWS.md`
- Create: `My DAW/DAW_Core/docs/TESTING_WINDOWS.md`
- Modify: outer `AGENTS.md` only to point to the proven build/test commands
- Create: outer `apex-workspace.json` containing the nested application path and canonical workflow entry points

**Interfaces:**
- Produces: one discoverable command set and a final evidence package for this project.

- [ ] **Step 1: Verify both repository states**

```powershell
git -C "C:\Users\1993v\OneDrive\Desktop\Apex backup" status --short --untracked-files=all
git -C "C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core" status --short --untracked-files=all
```

Expected for release-grade evidence: both empty. If commits were not authorized, label all output diagnostic and report the exact dirty paths.

- [ ] **Step 2: Run fresh dependency, policy, build, test, and evidence checks**

Run this block from the application repository:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\test_repository_policy.ps1
powershell -ExecutionPolicy Bypass -File Scripts\verify_dependencies.ps1
powershell -ExecutionPolicy Bypass -File Scripts\test_validate_test_evidence.ps1
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Seed 0xA9E12026
```

Expected: every command exits `0`; both application binaries and both test binaries exist; validated manifests contain zero failed assertions. Do not infer runtime correctness from the two successful application builds.

- [ ] **Step 3: Run Blade runtime validation in Debug and Release**

Use the deterministic test WAV and record:

```text
single split playback on both halves
left/right source bounds
three repeated splits
undo/redo cycles
save/reopen
source/device rate mismatch
first failing timeline frame if any
```

If Debug and Release differ, stop the release gate and preserve the project, WAV, logs, binaries, hashes, and exact steps.

- [ ] **Step 4: Run callback hardware campaigns required by available devices**

Collect the audit manifest for each available matrix entry. Missing hardware is recorded as `Unknown`, never converted into a pass.

- [ ] **Step 5: Record explicit remaining uncertainty**

The final report must state:

```text
Blade’s prior Debug/Release root cause remains Unknown unless reproduced and traced.
The lower-level Blade regression does not certify every GUI, plug-in, or live callback interaction.
The current Blade engine delete/recreate sequence is not converted into one immutable audio generation by this project; failure rollback and callback-visible atomicity remain unproven.
JUCE new/delete hooks do not intercept every malloc/realloc or third-party allocator.
Recording writer tests characterize hidden failures; APEX production propagation remains a separate correction.
Current recording stop/finalization thread behavior is measured but not redesigned by this project.
Hardware callback tails remain Unknown for devices not measured.
No compatibility claim extends beyond the pinned test matrix.
```

- [ ] **Step 6: Update canonical workflow documentation**

Document exact commands and expected outputs in `BUILDING_WINDOWS.md`, `TESTING_WINDOWS.md`, and outer `AGENTS.md`. `apex-workspace.json` must record:

```json
{
  "schemaVersion": 1,
  "applicationRepository": "My DAW/DAW_Core",
  "outerBranch": "master",
  "applicationBranch": "main",
  "buildScript": "My DAW/DAW_Core/Scripts/build_apex.ps1",
  "testScript": "My DAW/DAW_Core/Scripts/run_apex_tests.ps1"
}
```

- [ ] **Step 7: Final commits only with explicit authorization**

Commit application documentation in the application repository, then update and commit the outer manifest/spec/plan/workflow files in the outer repository. Inspect `git status`, `git diff`, and recent logs before each commit. Do not push or create a PR unless explicitly requested.

## Acceptance Criteria

- Both repository roles are explicit and no duplicate source copy becomes authority.
- SDKs, outputs, recordings, caches, logs, crashes, dumps, evidence runs, and secrets are excluded.
- JUCE and SignalSmith dependencies are pinned and verifiable from a clean checkout.
- One unsigned command builds each x64 configuration and verifies EXE/PDB outputs.
- Signing is separate and cannot mutate a verification build machine when bypassed.
- One APEX-owned test runner executes always-on assertions in Debug and Release with stable IDs/seeds.
- Blade split algebra, source/device-rate conversion, repeated cache sharing, undo/redo, persistence, and source-read continuity have permanent regressions.
- Callback audit publication is fixed-size and callback-safe; formatting/aggregation remains off-thread.
- Recording tests prove the distinction between offered, FIFO-accepted, writer-confirmed, and valid-prefix frames.
- Evidence manifests identify source, build, dependency, environment, command, seed, results, and artifact hashes.
- Compilation and lower-level tests are not reported as proof of live hardware behavior.

## Out of Scope for This Plan

- Prepared immutable audio-engine generation migration.
- Production recording state-machine and long-media streaming replacement.
- Native plug-in compatibility hardening, CLAP, scan helpers, or x86 bridge.
- Beat-making feature completion.
- Multicore audio scheduling.
- Public release signing, installer, updater, remote provisioning, or deployment.
