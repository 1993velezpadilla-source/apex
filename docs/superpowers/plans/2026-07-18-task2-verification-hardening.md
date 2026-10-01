# Task 2 Verification Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix three review findings in the Task 2 build verification hardening: verifier Git environment poisoning, child-redirect attack surface, and a stale ASIO SDK header path.

**Architecture:** Surgical control-plane fixes, each with its own RED→GREEN cycle. Local functions in `verify_dependencies.ps1` for Git environment isolation (matching the proven `juce_patch_tools.ps1` pattern). A focused `ChildRedirect` regression in `test_build_foundation.ps1` that exercises existing manifest walker assertions. Projucer resave to remove a dead `headerPath` attribute.

**Tech Stack:** Windows PowerShell 5.1, MSBuild 18 (amd64), Visual Studio 18 v145, JUCE 8.0.12 Projucer, native `CreateFileW`/`GetFileInformationByHandleEx` reparse-tag inspection, Git CLI.

## Global Constraints

- Official workspace: `C:\Users\1993v\OneDrive\Desktop\Apex backup`
- Application repository: `C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core`
- Branch: `feature/apex-windows-baseline-evidence`
- JUCE pinned at `8.0.12`, archive SHA-256 `15B4ED8302127138DFEE98E61A2B5AE1481B39649B139E02E77BFC29462B7EFB`
- All commands run from `My DAW\DAW_Core` unless noted
- No product source, SDK payload, or audio-engine code changes
- No staging, committing, or destructive Git operations without explicit authorization
- Preserve all dirty unrelated product files
- Windows PowerShell 5.1 compatibility required

---

### Task 1: Verifier Git environment isolation

**Files:**
- Modify: `Scripts/verify_dependencies.ps1` (add local save/clear/restore helper, wrap all `& git` calls)
- Test: `Scripts/test_build_foundation.ps1` (add `VerifierEnvironment` regression)

**Interfaces:**
- Consumes: `Invoke-ChildPowerShell` from `test_build_foundation.ps1`, `Get-DirectoryTreeFingerprint` from `test_build_foundation.ps1`
- Produces: verifier runs correctly with poisoned Git environment; parent environment is restored after verifier exits

- [ ] **Step 1: Write the failing RED test — VerifierEnvironment**

Add to `Scripts/test_build_foundation.ps1` after the existing `MSBuildEnvironment` test case (after line 1334):

```powershell
Invoke-Test 'dependency verifier is isolated from inherited Git repository-selection environment' {
    $testRoot = Join-Path $repositoryRoot ".apex-debug\test-build-foundation\$([guid]::NewGuid().ToString('N'))"
    $fixtureRepository = Join-Path $testRoot 'DAW_Core'
    $fixtureScripts = Join-Path $fixtureRepository 'Scripts'
    $fixtureDependencies = Join-Path $fixtureRepository 'Dependencies'
    $sdkParent = Join-Path $testRoot 'Sdk setups'
    $externalRoot = Join-Path $testRoot 'external-owner'
    $externalGitDir = Join-Path $externalRoot '.git'
    $childScript = Join-Path $testRoot 'invoke-verifier-child.ps1'
    $evidencePath = Join-Path $testRoot 'verifier-evidence.json'
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    $previousEnvironment = @{}
    $environmentPoisoned = $false

    try {
        [System.IO.Directory]::CreateDirectory($fixtureScripts) | Out-Null
        [System.IO.Directory]::CreateDirectory($fixtureDependencies) | Out-Null
        [System.IO.Directory]::CreateDirectory($sdkParent) | Out-Null
        [System.IO.Directory]::CreateDirectory($externalRoot) | Out-Null

        [System.IO.File]::Copy(
            (Join-Path $PSScriptRoot 'apex_build_tools.ps1'),
            (Join-Path $fixtureScripts 'apex_build_tools.ps1')
        )
        [System.IO.File]::Copy(
            (Join-Path $PSScriptRoot 'juce_patch_tools.ps1'),
            (Join-Path $fixtureScripts 'juce_patch_tools.ps1')
        )
        [System.IO.File]::Copy(
            (Join-Path $PSScriptRoot 'verify_dependencies.ps1'),
            (Join-Path $fixtureScripts 'verify_dependencies.ps1')
        )
        [System.IO.File]::Copy(
            (Join-Path $repositoryRoot 'Dependencies\apex-windows-dependencies.json'),
            (Join-Path $fixtureDependencies 'apex-windows-dependencies.json')
        )

        # Create minimal JUCE payload that passes verification
        $juceRoot = Join-Path $sdkParent 'juce-8.0.12-windows\JUCE'
        $projucer = Join-Path $juceRoot 'Projucer.exe'
        $standardHeader = Join-Path $juceRoot 'modules\juce_core\system\juce_StandardHeader.h'
        [System.IO.Directory]::CreateDirectory((Split-Path -Parent $projucer)) | Out-Null
        [System.IO.Directory]::CreateDirectory((Split-Path -Parent $standardHeader)) | Out-Null
        [System.IO.File]::WriteAllText($projucer, 'pinned projucer placeholder')
        [System.IO.File]::WriteAllText(
            $standardHeader,
            "#define JUCE_MAJOR_VERSION 8`r`n#define JUCE_MINOR_VERSION 0`r`n#define JUCE_BUILDNUMBER 12`r`n"
        )

        # Initialize external repository as the poisoning target
        & git -C $externalRoot init --quiet
        Assert-Equal 0 $LASTEXITCODE 'could not initialize external poisoning target repository'

        # Create child script that invokes verifier and captures evidence
        [System.IO.File]::WriteAllText(
            $childScript,
            @'
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $HelperPath,

    [Parameter(Mandatory = $true)]
    [string] $VerifierScript,

    [Parameter(Mandatory = $true)]
    [string] $EvidencePath
)

$ErrorActionPreference = 'Stop'
. $HelperPath

$operationError = $null
try {
    & $VerifierScript
}
catch {
    $operationError = $_.Exception.Message
}

$evidence = [ordered]@{
    ProcessId = $PID
    OperationError = $operationError
    GitDir = [Environment]::GetEnvironmentVariable('GIT_DIR', 'Process')
    GitWorkTree = [Environment]::GetEnvironmentVariable('GIT_WORK_TREE', 'Process')
    GitConfigSystem = [Environment]::GetEnvironmentVariable('GIT_CONFIG_SYSTEM', 'Process')
    GitConfigGlobal = [Environment]::GetEnvironmentVariable('GIT_CONFIG_GLOBAL', 'Process')
}
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($EvidencePath, ($evidence | ConvertTo-Json -Depth 6), $utf8NoBom)
exit $LASTEXITCODE
'@,
            $utf8NoBom
        )

        # Poison the parent environment
        $processEnvironment = [Environment]::GetEnvironmentVariables('Process')
        $poisonVars = @(
            'GIT_DIR', 'GIT_WORK_TREE', 'GIT_COMMON_DIR',
            'GIT_INDEX_FILE', 'GIT_OBJECT_DIRECTORY',
            'GIT_CONFIG_SYSTEM', 'GIT_CONFIG_GLOBAL'
        )
        foreach ($name in $poisonVars) {
            $previousEnvironment[$name] = [pscustomobject]@{
                WasSet = $processEnvironment.Contains($name)
                Value = [Environment]::GetEnvironmentVariable($name, 'Process')
            }
        }

        $environmentPoisoned = $true
        [Environment]::SetEnvironmentVariable('GIT_DIR', $externalGitDir, 'Process')
        [Environment]::SetEnvironmentVariable('GIT_WORK_TREE', $externalRoot, 'Process')
        [Environment]::SetEnvironmentVariable('GIT_CONFIG_SYSTEM', (Join-Path $externalRoot 'poisoned-gitconfig'), 'Process')
        [Environment]::SetEnvironmentVariable('GIT_CONFIG_GLOBAL', (Join-Path $externalRoot 'poisoned-gitconfig'), 'Process')

        # Initialize fixture repo for submodule checks
        & git -C $fixtureRepository init --quiet
        Assert-Equal 0 $LASTEXITCODE 'could not initialize verifier fixture repository'

        # Run verifier in child process with poisoned environment
        $verifierResult = Invoke-ChildPowerShell -ScriptPath $childScript -Arguments @(
            '-HelperPath', (Join-Path $PSScriptRoot 'verify_dependencies.ps1'),
            '-VerifierScript', (Join-Path $fixtureScripts 'verify_dependencies.ps1'),
            '-EvidencePath', $evidencePath
        )

        # The verifier should succeed despite poisoned parent environment
        Assert-Equal 0 $verifierResult.ExitCode "verifier failed under poisoned environment: $($verifierResult.Output)"
        Assert-True (Test-Path -LiteralPath $evidencePath -PathType Leaf) 'verifier did not emit restoration evidence'

        $evidence = Get-Content -LiteralPath $evidencePath -Raw | ConvertFrom-Json
        Assert-True ([int] $evidence.ProcessId -ne $PID) 'verifier did not run in a child process'
        Assert-Equal $null $evidence.GitDir 'child process Git environment was not restored'
        Assert-Equal $null $evidence.GitWorkTree 'child process Git work tree was not restored'
        Assert-Equal $null $evidence.GitConfigSystem 'child process Git config system was not restored'
        Assert-Equal $null $evidence.GitConfigGlobal 'child process Git config global was not restored'

        # Verify parent environment is restored after child exits
        foreach ($name in $poisonVars) {
            if ($previousEnvironment[$name].WasSet) {
                Assert-Equal $previousEnvironment[$name].Value ([Environment]::GetEnvironmentVariable($name, 'Process')) "parent $name was not restored"
            }
            else {
                Assert-Equal $null ([Environment]::GetEnvironmentVariable($name, 'Process')) "parent $name was not cleaned"
            }
        }
    }
    finally {
        $environmentPoisoned = $false
        foreach ($name in $previousEnvironment.Keys) {
            $saved = $previousEnvironment[$name]
            if ($saved.WasSet) {
                [Environment]::SetEnvironmentVariable($name, $saved.Value, 'Process')
            }
            else {
                [Environment]::SetEnvironmentVariable($name, $null, 'Process')
            }
        }
        if (Test-Path -LiteralPath $testRoot) {
            Remove-Item -LiteralPath $testRoot -Recurse -Force
        }
    }
} -Case 'VerifierEnvironment'
```

Also add `VerifierEnvironment` to the `ValidateSet` at line 3 of `test_build_foundation.ps1`:

```powershell
[ValidateSet('All', 'MatchingExisting', 'ExistingMismatch', 'VerifierMismatch', 'ContractPath', 'PublicationRace', 'PatchContract', 'PatchEnvironment', 'PatchTampering', 'PatchBase', 'ReparsePayload', 'ParentRedirect', 'MSBuildEnvironment', 'VerifierEnvironment', 'ChildRedirect')]
```

- [ ] **Step 2: Run the failing test to verify RED**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\test_build_foundation.ps1 -MaterializerCase VerifierEnvironment
```

Expected: exit nonzero. The verifier should fail or the child should observe poisoned `GIT_DIR`.

- [ ] **Step 3: Implement Git environment isolation in verify_dependencies.ps1**

Add to the top of `Scripts/verify_dependencies.ps1`, after line 3 (after the dot-sources) and before line 6 (`$repositoryRoot = ...`):

```powershell
function Invoke-GitIsolated {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string[]] $Arguments,

        [Parameter(Mandatory = $true)]
        [string] $RepositoryPath
    )

    $gitEnvironmentNames = @(
        'GIT_ALTERNATE_OBJECT_DIRECTORIES',
        'GIT_CEILING_DIRECTORIES',
        'GIT_COMMON_DIR',
        'GIT_CONFIG',
        'GIT_CONFIG_COUNT',
        'GIT_CONFIG_GLOBAL',
        'GIT_CONFIG_NOSYSTEM',
        'GIT_CONFIG_PARAMETERS',
        'GIT_CONFIG_SYSTEM',
        'GIT_DIR',
        'GIT_DISCOVERY_ACROSS_FILESYSTEM',
        'GIT_GRAFT_FILE',
        'GIT_IMPLICIT_WORK_TREE',
        'GIT_INDEX_FILE',
        'GIT_INTERNAL_SUPER_PREFIX',
        'GIT_NAMESPACE',
        'GIT_NO_REPLACE_OBJECTS',
        'GIT_OBJECT_DIRECTORY',
        'GIT_PREFIX',
        'GIT_QUARANTINE_PATH',
        'GIT_REPLACE_REF_BASE',
        'GIT_SHALLOW_FILE',
        'GIT_TEMPLATE_DIR',
        'GIT_WORK_TREE'
    )
    $processEnvironment = [Environment]::GetEnvironmentVariables('Process')
    $gitEnvironmentNames += @(
        $processEnvironment.Keys |
            ForEach-Object { [string] $_ } |
            Where-Object { $_.StartsWith('GIT_', [System.StringComparison]::OrdinalIgnoreCase) }
    )
    $savedGitEnvironment = @{}

    try {
        foreach ($name in @($gitEnvironmentNames | Sort-Object -Unique)) {
            $savedGitEnvironment[$name] = [pscustomobject]@{
                WasSet = $processEnvironment.Contains($name)
                Value = [Environment]::GetEnvironmentVariable($name, 'Process')
            }
            [Environment]::SetEnvironmentVariable($name, $null, 'Process')
        }

        $output = @(& git -C $RepositoryPath @Arguments 2>&1)
        $exitCode = $LASTEXITCODE

        return [pscustomobject]@{
            ExitCode = $exitCode
            Output = ($output | Out-String)
        }
    }
    finally {
        foreach ($name in $savedGitEnvironment.Keys) {
            $saved = $savedGitEnvironment[$name]
            if ($saved.WasSet) {
                [Environment]::SetEnvironmentVariable($name, $saved.Value, 'Process')
            }
            else {
                [Environment]::SetEnvironmentVariable($name, $null, 'Process')
            }
        }
    }
}
```

Then replace all direct `& git` invocations in `verify_dependencies.ps1` with calls to `Invoke-GitIsolated`. Specifically, change the `Test-SubmoduleIdentity` function (lines 63-126) and the top-level `stretch2Stage` check (line 280). Each `& git -C $path ...` becomes `Invoke-GitIsolated -RepositoryPath $path -Arguments @('rev-parse', 'HEAD')` (or equivalent arguments).

Key changes inside `Test-SubmoduleIdentity`:

```powershell
# Line 84: replace
$head = (& git -C $absolutePath rev-parse HEAD 2>$null | Out-String).Trim()
# with
$gitHead = Invoke-GitIsolated -RepositoryPath $absolutePath -Arguments @('rev-parse', 'HEAD')
$head = $gitHead.Output.Trim()

# Line 93: replace
$origin = (& git -C $absolutePath remote get-url origin 2>$null | Out-String).Trim()
# with
$gitOrigin = Invoke-GitIsolated -RepositoryPath $absolutePath -Arguments @('remote', 'get-url', 'origin')
$origin = $gitOrigin.Output.Trim()

# Line 98: replace
$trackedChanges = @(& git -C $absolutePath status --porcelain --untracked-files=no 2>$null)
# with
$gitStatus = Invoke-GitIsolated -RepositoryPath $absolutePath -Arguments @('status', '--porcelain', '--untracked-files=no')
$trackedChanges = @($gitStatus.Output -split "`n" | Where-Object { $_ })

# Line 103: replace
$stageEntry = (& git -C $repositoryRoot ls-files --stage -- $RelativePath | Out-String).Trim()
# with
$gitStage = Invoke-GitIsolated -RepositoryPath $repositoryRoot -Arguments @('ls-files', '--stage', '--', $RelativePath)
$stageEntry = $gitStage.Output.Trim()

# Lines 110-111: replace
$modulePathEntries = @(
    & git -C $repositoryRoot config --file .gitmodules --get-regexp '^submodule\..*\.path$' 2>$null
)
# with
$gitConfig = Invoke-GitIsolated -RepositoryPath $repositoryRoot -Arguments @('config', '--file', '.gitmodules', '--get-regexp', '^submodule\..*\.path$')
$modulePathEntries = @($gitConfig.Output -split "`n" | Where-Object { $_ })

# Line 122: replace
$moduleUrl = (& git -C $repositoryRoot config --file .gitmodules --get $urlKey 2>$null | Out-String).Trim()
# with
$gitModuleUrl = Invoke-GitIsolated -RepositoryPath $repositoryRoot -Arguments @('config', '--file', '.gitmodules', '--get', $urlKey)
$moduleUrl = $gitModuleUrl.Output.Trim()
```

Key change for the top-level stretch2 check (line 280):

```powershell
# replace
$stretch2Stage = (& git -C $repositoryRoot ls-files --stage -- 'Source/ThirdParty/signalsmith-stretch2' | Out-String).Trim()
# with
$gitStretch2 = Invoke-GitIsolated -RepositoryPath $repositoryRoot -Arguments @('ls-files', '--stage', '--', 'Source/ThirdParty/signalsmith-stretch2')
$stretch2Stage = $gitStretch2.Output.Trim()
```

- [ ] **Step 4: Run the focused test to verify GREEN**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\test_build_foundation.ps1 -MaterializerCase VerifierEnvironment
```

Expected: exit 0. `[PASS] dependency verifier is isolated from inherited Git repository-selection environment`.

- [ ] **Step 5: Run the complete harness**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\test_build_foundation.ps1
```

Expected: exit 0. All tests pass (current 17 + new VerifierEnvironment = 18).

- [ ] **Step 6: Run live dependency verifier**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\verify_dependencies.ps1
```

Expected: exit 0. `[PASS] pinned Windows dependencies verified`.

---

### Task 2: Child-redirect junction test

**Files:**
- Modify: `Scripts/test_build_foundation.ps1` (add `ChildRedirect` regression)
- No production code changes (existing walker assertions cover this case)

**Interfaces:**
- Consumes: `New-MaterializerFixture`, `Remove-MaterializerFixture`, `Invoke-ChildPowerShell`, `Get-DirectoryTreeFingerprint` from `test_build_foundation.ps1`
- Produces: junction inside SDK parent is rejected before child access

- [ ] **Step 1: Write the failing RED test — ChildRedirect**

Add to `Scripts/test_build_foundation.ps1` after the existing `ParentRedirect` test case:

```powershell
Invoke-Test 'materializer and verifier reject a junction inside the approved SDK parent before child access' {
    $fixture = New-MaterializerFixture
    $externalRoot = Join-Path $repositoryRoot ".apex-debug\test-build-foundation\child-redirect-$([guid]::NewGuid().ToString('N'))"
    $externalSentinel = Join-Path $externalRoot 'external-owner.txt'
    $childJunction = Join-Path $fixture.SdkParent 'juce-8.0.12-windows'
    $junctionCreated = $false

    try {
        [System.IO.Directory]::CreateDirectory($externalRoot) | Out-Null
        [System.IO.File]::WriteAllText($externalSentinel, 'external owner must remain unchanged')

        # Remove the SDK parent directory created by fixture and replace with junction
        [System.IO.Directory]::Delete($fixture.SdkParent)
        New-Item -ItemType Junction -Path $fixture.SdkParent -Target $externalRoot -ErrorAction Stop | Out-Null
        $junctionCreated = $true

        $externalBefore = Get-DirectoryTreeFingerprint -Root $externalRoot

        & git -C $fixture.Repository init --quiet
        Assert-Equal 0 $LASTEXITCODE 'could not initialize child-redirect fixture repository'

        $materializerResult = Invoke-ChildPowerShell -ScriptPath $fixture.Script -Arguments @(
            '-JuceArchive', $fixture.Archive
        )
        $verifierResult = Invoke-ChildPowerShell -ScriptPath $fixture.VerifyScript

        # Both should fail at the junction before accessing any child content
        Assert-True ($materializerResult.ExitCode -ne 0) 'materializer accepted a junction inside the approved SDK parent'
        Assert-True ($materializerResult.Output -match 'name-surrogate reparse point') `
            "materializer did not report the child-redirect native tag: $($materializerResult.Output)"
        Assert-True ($materializerResult.Output -notmatch 'archive SHA-256|ExtractToDirectory') `
            'materializer accessed child content before rejecting the junction'

        Assert-True ($verifierResult.ExitCode -ne 0) 'dependency verifier accepted a junction inside the approved SDK parent'
        Assert-True ($verifierResult.Output -match 'name-surrogate reparse point') `
            "dependency verifier did not report the child-redirect native tag: $($verifierResult.Output)"
        Assert-True ($verifierResult.Output -notmatch 'JUCE StandardHeader|Pinned Projucer|JUCE archive SHA-256|installation payload') `
            "dependency verifier continued child work after junction rejection: $($verifierResult.Output)"

        # External target must remain unchanged
        Assert-Equal $externalBefore (Get-DirectoryTreeFingerprint -Root $externalRoot) `
            'child-redirect operations mutated the external target'
        Assert-Equal 'external owner must remain unchanged' ([System.IO.File]::ReadAllText($externalSentinel)) `
            'child-redirect operations changed the external sentinel'
    }
    finally {
        if ($junctionCreated -and [System.IO.Directory]::Exists($fixture.SdkParent)) {
            [System.IO.Directory]::Delete($fixture.SdkParent)
        }
        Remove-MaterializerFixture -Fixture $fixture
        if (Test-Path -LiteralPath $externalRoot) {
            Remove-Item -LiteralPath $externalRoot -Recurse -Force
        }
    }
} -Case 'ChildRedirect'
```

Note: `ChildRedirect` must already be in the `ValidateSet` from Task 1 Step 1.

- [ ] **Step 2: Run the failing test to verify RED**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\test_build_foundation.ps1 -MaterializerCase ChildRedirect
```

Expected: exit nonzero. The materializer or verifier should fail at the junction but may show archive-hash or child-access diagnostics before the fix is needed.

- [ ] **Step 3: Verify the existing walker catches it (no code change needed)**

The existing `Assert-ApexPayloadPathNotReparsePoint` at `apex_build_tools.ps1:167` checks the installation root directory before traversal. The fixture's junction at `Sdk setups/juce-8.0.12-windows` is the installation root, which is checked by the manifest walker or the parent check. The junction is a name-surrogate (tag `0xA0000003`) and will be rejected.

If the test passes without additional code changes, the walker is already sufficient.

- [ ] **Step 4: Run the focused test to verify GREEN**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\test_build_foundation.ps1 -MaterializerCase ChildRedirect
```

Expected: exit 0. `[PASS] materializer and verifier reject a junction inside the approved SDK parent before child access`.

- [ ] **Step 5: Run the complete harness**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\test_build_foundation.ps1
```

Expected: exit 0. All tests pass (18 from Task 1 + ChildRedirect = 19).

---

### Task 3: Remove stale ASIO SDK header path

**Files:**
- Modify: `DAW_Core.jucer` (remove `headerPath` attribute)
- Regenerate: `Builds/VisualStudio2026/DAW_Core_App.vcxproj` (Projucer resave)

**Interfaces:**
- Consumes: pinned Projucer at `../Sdk setups/juce-8.0.12-windows/JUCE/Projucer.exe`
- Produces: vcxproj without dead `ASIOSDK\common` include path

- [ ] **Step 1: Capture the before state**

```powershell
Select-String -Path Builds\VisualStudio2026\DAW_Core_App.vcxproj -Pattern "ASIOSDK" | Select-Object LineNumber, Line
Select-String -Path DAW_Core.jucer -Pattern "ASIOSDK" | Select-Object LineNumber, Line
```

Expected: matches at `DAW_Core.jucer:6` and `DAW_Core_App.vcxproj:70,85,118,133`.

- [ ] **Step 2: Remove the stale headerPath from DAW_Core.jucer**

Edit `DAW_Core.jucer` line 3-6. Remove the `headerPath` attribute entirely from the `<JUCERPROJECT>` tag:

```xml
<!-- Before -->
<JUCERPROJECT id="SM4V7O" name="DAW_Core" projectType="guiapp" useAppConfig="0"
              addUsingNamespaceToJuceHeader="0" jucerFormatVersion="1"
              defines="JUCE_ASIO=1 JUCE_PLUGINHOST_VST3=1"
              headerPath="..\..\..\..\Sdk setups\ASIOSDK\common">

<!-- After -->
<JUCERPROJECT id="SM4V7O" name="DAW_Core" projectType="guiapp" useAppConfig="0"
              addUsingNamespaceToJuceHeader="0" jucerFormatVersion="1"
              defines="JUCE_ASIO=1 JUCE_PLUGINHOST_VST3=1">
```

- [ ] **Step 3: Regenerate with pinned Projucer**

```powershell
& "..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe" --resave DAW_Core.jucer --lf
```

Expected: Projucer exits 0, regenerated files are updated.

- [ ] **Step 4: Verify the ASIOSDK path is gone**

```powershell
Select-String -Path Builds\VisualStudio2026\DAW_Core_App.vcxproj -Pattern "ASIOSDK"
```

Expected: no matches.

```powershell
Select-String -Path DAW_Core.jucer -Pattern "ASIOSDK"
```

Expected: no matches.

- [ ] **Step 5: Rebuild Debug to verify compilation succeeds**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned
```

Expected: exit 0. No new compilation errors. `JUCE_ASIO=1` still works because JUCE bundles `iasiodrv.h` internally when `JUCE_ASIO_USE_EXTERNAL_SDK=0`.

- [ ] **Step 6: Rebuild Release to verify compilation succeeds**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned
```

Expected: exit 0. Same as Step 5 for Release configuration.

---

### Task 4: Full verification rerun and plan update

**Files:**
- Modify: `docs/superpowers/plans/2026-07-17-apex-windows-baseline-evidence-foundation.md` (Task 2 annotations)

**Interfaces:**
- Consumes: all fixes from Tasks 1-3
- Produces: complete test suite passes, hashes/signatures recorded, plan updated

- [ ] **Step 1: Run complete test suite**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\test_build_foundation.ps1
```

Expected: exit 0. All tests pass (expected 19: original 17 + VerifierEnvironment + ChildRedirect).

- [ ] **Step 2: Run live dependency verifier**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\verify_dependencies.ps1
```

Expected: exit 0. `[PASS] pinned Windows dependencies verified`.

- [ ] **Step 3: Rebuild Debug and capture signatures**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned
```

Expected: exit 0, `NotSigned`. Record EXE/PDB hashes.

- [ ] **Step 4: Rebuild Release and capture signatures**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned
```

Expected: exit 0, `NotSigned`. Record EXE/PDB hashes.

- [ ] **Step 5: Run scoped whitespace check**

```powershell
git diff --check -- "Scripts/verify_dependencies.ps1" "Scripts/test_build_foundation.ps1" "DAW_Core.jucer" "Builds/VisualStudio2026/DAW_Core_App.vcxproj"
```

Expected: exit 0 (LF-to-CRLF notices only, no whitespace errors).

- [ ] **Step 6: Annotate the plan document**

Add a note to `docs/superpowers/plans/2026-07-17-apex-windows-baseline-evidence-foundation.md` Task 2 section (after Step 9) stating that archive verification was ratified as mandatory (2026-07-18), ASIO header path was removed, and verifier-environment and child-redirect hardening were added.

- [ ] **Step 7: Report final status**

Report exact test counts, hashes, signatures, warning counts, and any remaining owner decisions or hardware uncertainty. No staging or committing without explicit authorization.

---

## Remaining uncertainty after implementation

- Runtime, audio, device, transport, recording, save, or export behavior is not tested by this hardening.
- The provisional ASIO4ALL patch still requires its documented hardware matrix.
- File-symlink coverage depends on Windows privilege/developer-mode availability.
- Persistent-handle TOCTOU elimination remains outside this scope.
- The repository remains dirty with unrelated product work; build artifacts are diagnostic.
