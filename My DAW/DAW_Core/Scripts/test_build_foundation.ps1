[CmdletBinding()]
param(
    [ValidateSet('All', 'MatchingExisting', 'ExistingMismatch', 'VerifierMismatch', 'ContractPath', 'PublicationRace', 'PatchContract', 'PatchEnvironment', 'PatchTampering', 'PatchBase', 'ReparsePayload', 'ParentRedirect', 'MSBuildEnvironment', 'VerifierEnvironment', 'ChildRedirect')]
    [string] $MaterializerCase = 'All'
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$powerShellExe = Join-Path $PSHOME 'powershell.exe'
$script:passed = 0
$jucePatchRelativePath = 'Dependencies\patches\juce-8.0.12\0001-apex-asio4all-compatibility.patch'
$jucePatchTargetRelativePath = 'JUCE\modules\juce_audio_devices\native\juce_ASIO_windows.cpp'
$pristineJuceTargetHash = 'A681161DEEDCBC341C4649C5A35EDE4847FFDA8B49AB9A0ED43878DB22A51E14'
$patchedJuceTargetHash = '93355E3422CB4C329823FD980D01CCB4F6315809D118C6463204CD893BAADD10'
$jucePatchValidationStatus = 'provisional-hardware-validation-required'
$jucePatchRationale = 'Preserves the investigated APEX ASIO4ALL zero-buffer and delayed-callback compatibility behavior pending the required hardware matrix.'

function Assert-True {
    param(
        [Parameter(Mandatory = $true)]
        [bool] $Condition,

        [Parameter(Mandatory = $true)]
        [string] $Message
    )

    if (-not $Condition) {
        throw "[FAIL] $Message"
    }
}

function Assert-Equal {
    param(
        [AllowNull()]
        $Expected,

        [AllowNull()]
        $Actual,

        [Parameter(Mandatory = $true)]
        [string] $Message
    )

    if ($Expected -ne $Actual) {
        throw "[FAIL] $Message (expected '$Expected', got '$Actual')"
    }
}

function Invoke-ChildPowerShell {
    param(
        [Parameter(Mandatory = $true)]
        [string] $ScriptPath,

        [string[]] $Arguments = @()
    )

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & $powerShellExe -NoProfile -ExecutionPolicy Bypass -File $ScriptPath @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = ($output | Out-String)
    }
}

function Get-FileFingerprint {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return '<missing>'
    }

    $item = Get-Item -LiteralPath $Path
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    return "$hash|$($item.Length)|$($item.LastWriteTimeUtc.Ticks)"
}

function Get-DirectoryTreeFingerprint {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Root
    )

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        return '<missing>'
    }

    $canonicalRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $entries = New-Object 'System.Collections.Generic.List[string]'
    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $pending.Push($canonicalRoot)

    while ($pending.Count -ne 0) {
        $directory = $pending.Pop()

        foreach ($childDirectory in [System.IO.Directory]::GetDirectories($directory)) {
            $relativePath = $childDirectory.Substring($canonicalRoot.Length).TrimStart('\').Replace('\', '/')
            $entries.Add("D`t$relativePath")
            $pending.Push($childDirectory)
        }

        foreach ($file in [System.IO.Directory]::GetFiles($directory)) {
            $relativePath = $file.Substring($canonicalRoot.Length).TrimStart('\').Replace('\', '/')
            $item = Get-Item -LiteralPath $file
            $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
            $entries.Add("F`t$relativePath`t$($item.Length)`t$hash")
        }
    }

    $sortedEntries = $entries.ToArray()
    [System.Array]::Sort($sortedEntries, [System.StringComparer]::Ordinal)
    $manifestBytes = [System.Text.Encoding]::UTF8.GetBytes([string]::Join("`n", $sortedEntries))
    $sha256 = [System.Security.Cryptography.SHA256]::Create()

    try {
        return [System.BitConverter]::ToString($sha256.ComputeHash($manifestBytes)).Replace('-', '')
    }
    finally {
        $sha256.Dispose()
    }
}

function New-MaterializerFixture {
    $fixtureRoot = Join-Path $repositoryRoot ".apex-debug\test-build-foundation\$([guid]::NewGuid().ToString('N'))"
    $fixtureRepository = Join-Path $fixtureRoot 'DAW_Core'
    $fixtureScripts = Join-Path $fixtureRepository 'Scripts'
    $fixtureDependencies = Join-Path $fixtureRepository 'Dependencies'
    $sdkParent = Join-Path $fixtureRoot 'Sdk setups'

    [System.IO.Directory]::CreateDirectory($fixtureScripts) | Out-Null
    [System.IO.Directory]::CreateDirectory($fixtureDependencies) | Out-Null
    [System.IO.Directory]::CreateDirectory($sdkParent) | Out-Null
    [System.IO.File]::Copy(
        (Join-Path $PSScriptRoot 'materialize_dependencies.ps1'),
        (Join-Path $fixtureScripts 'materialize_dependencies.ps1')
    )
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
    [System.IO.File]::Copy(
        (Join-Path $repositoryRoot 'DAW_Core.jucer'),
        (Join-Path $fixtureRepository 'DAW_Core.jucer')
    )

    $sourcePatch = Join-Path $repositoryRoot $jucePatchRelativePath
    $fixturePatch = Join-Path $fixtureRepository $jucePatchRelativePath
    if (Test-Path -LiteralPath $sourcePatch -PathType Leaf) {
        [System.IO.Directory]::CreateDirectory((Split-Path -Parent $fixturePatch)) | Out-Null
        [System.IO.File]::Copy($sourcePatch, $fixturePatch)
    }

    return [pscustomobject]@{
        Root = $fixtureRoot
        Repository = $fixtureRepository
        Script = Join-Path $fixtureScripts 'materialize_dependencies.ps1'
        VerifyScript = Join-Path $fixtureScripts 'verify_dependencies.ps1'
        Contract = Join-Path $fixtureDependencies 'apex-windows-dependencies.json'
        Archive = [System.IO.Path]::GetFullPath(
            (Join-Path $repositoryRoot '..\Sdk setups\juce-8.0.12-windows.zip')
        )
        SdkParent = $sdkParent
        LocalArchive = Join-Path $sdkParent 'juce-8.0.12-windows.zip'
        InstallationRoot = Join-Path $sdkParent 'juce-8.0.12-windows'
        Patch = $fixturePatch
        PatchedTarget = Join-Path (Join-Path $sdkParent 'juce-8.0.12-windows') $jucePatchTargetRelativePath
    }
}

function Write-JsonWithoutBom {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path,

        [Parameter(Mandatory = $true)]
        $Value
    )

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 10), $utf8NoBom)
}

function Remove-MaterializerFixture {
    param(
        [Parameter(Mandatory = $true)]
        $Fixture
    )

    if (Test-Path -LiteralPath $Fixture.Root) {
        Remove-Item -LiteralPath $Fixture.Root -Recurse -Force
    }
}

function Invoke-Test {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Name,

        [Parameter(Mandatory = $true)]
        [scriptblock] $Body,

        [string] $Case = 'Core'
    )

    if ($MaterializerCase -ne 'All' -and $MaterializerCase -ne $Case) {
        return
    }

    & $Body
    $script:passed++
    Write-Host "[PASS] $Name"
}

$requiredScripts = @(
    'apex_build_tools.ps1',
    'juce_patch_tools.ps1',
    'materialize_dependencies.ps1',
    'verify_dependencies.ps1',
    'build_apex.ps1'
)

foreach ($requiredScript in $requiredScripts) {
    $requiredPath = Join-Path $PSScriptRoot $requiredScript
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "[FAIL] required script is missing: $requiredScript"
    }
}

. (Join-Path $PSScriptRoot 'apex_build_tools.ps1')
. (Join-Path $PSScriptRoot 'juce_patch_tools.ps1')

Invoke-Test 'dependency contract pins the provisional JUCE compatibility patch' {
    $contract = Get-Content -LiteralPath (Join-Path $repositoryRoot 'Dependencies\apex-windows-dependencies.json') -Raw | ConvertFrom-Json
    $patch = $contract.juce.compatibilityPatch
    $patchPath = Join-Path $repositoryRoot $jucePatchRelativePath

    Assert-True ($null -ne $patch) 'JUCE compatibility patch contract is missing'
    Assert-Equal '29396c22c93392d6738e021b83196283d6e4d850' $contract.juce.upstreamCommit 'JUCE upstream base commit is not pinned'
    Assert-Equal ($jucePatchRelativePath.Replace('\', '/')) $patch.relativePath 'JUCE compatibility patch path is not pinned'
    Assert-Equal ($jucePatchTargetRelativePath.Replace('\', '/')) $patch.targetRelativePath 'JUCE compatibility patch target is not pinned'
    Assert-Equal $pristineJuceTargetHash $patch.pristineTargetSha256 'JUCE pristine patch target hash is not pinned'
    Assert-Equal $patchedJuceTargetHash $patch.patchedTargetSha256 'JUCE patched target hash is not pinned'
    Assert-Equal $jucePatchValidationStatus $patch.validationStatus 'JUCE compatibility patch status is not provisional'
    Assert-Equal $jucePatchRationale $patch.rationale 'JUCE compatibility patch rationale is not explicit'
    Assert-True ([bool] $patch.hardwareValidationRequired) 'JUCE compatibility patch does not require hardware validation'
    Assert-True (([string] $patch.sha256 -cmatch '^[0-9A-F]{64}$')) 'JUCE compatibility patch SHA-256 is not canonical'
    Assert-True (Test-Path -LiteralPath $patchPath -PathType Leaf) 'tracked JUCE compatibility patch is missing'
    Assert-Equal $patch.sha256 (Get-FileHash -LiteralPath $patchPath -Algorithm SHA256).Hash 'tracked JUCE compatibility patch hash does not match the contract'
    $patchEolAttribute = (& git -C $repositoryRoot check-attr eol -- ([string] $patch.relativePath) | Out-String).Trim()
    Assert-Equal "$($patch.relativePath): eol: lf" $patchEolAttribute 'tracked patch checkout bytes are not pinned to LF'
} -Case 'PatchContract'

Invoke-Test 'JUCE patch operations are confined from inherited Git repository-selection environment' {
    $testRoot = Join-Path $repositoryRoot ".apex-debug\test-build-foundation\$([guid]::NewGuid().ToString('N'))"
    $successRoot = Join-Path $testRoot 'success-root'
    $failureRoot = Join-Path $testRoot 'failure-root'
    $sanitizationFailureRoot = Join-Path $testRoot 'sanitization-failure-root'
    $externalRoot = Join-Path $testRoot 'external-owner'
    $successTarget = Join-Path $successRoot 'payload.txt'
    $failureTarget = Join-Path $failureRoot 'payload.txt'
    $sanitizationFailureTarget = Join-Path $sanitizationFailureRoot 'payload.txt'
    $externalTarget = Join-Path $externalRoot 'payload.txt'
    $validPatch = Join-Path $testRoot 'valid.patch'
    $invalidPatch = Join-Path $testRoot 'invalid.patch'
    $patchedExpected = Join-Path $testRoot 'patched-expected.txt'
    $childScript = Join-Path $testRoot 'invoke-patch-child.ps1'
    $expectedEnvironmentPath = Join-Path $testRoot 'expected-environment.json'
    $successEvidencePath = Join-Path $testRoot 'success-evidence.json'
    $failureEvidencePath = Join-Path $testRoot 'failure-evidence.json'
    $sanitizationFailureEvidencePath = Join-Path $testRoot 'sanitization-failure-evidence.json'
    $externalConfig = Join-Path $externalRoot 'poisoned.gitconfig'
    $externalHooks = Join-Path $externalRoot 'poisoned-hooks'
    $externalTrace = Join-Path $externalRoot 'git-trace-sentinel.json'
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    $pristineText = "before`r`n"
    $patchedText = "after`r`n"
    $selectionEnvironment = [ordered]@{
        GIT_DIR = Join-Path $externalRoot '.git'
        GIT_WORK_TREE = $externalRoot
        GIT_COMMON_DIR = Join-Path $externalRoot '.git'
        GIT_INDEX_FILE = Join-Path $externalRoot '.git\poisoned-index'
        GIT_OBJECT_DIRECTORY = Join-Path $externalRoot '.git\objects'
        GIT_ALTERNATE_OBJECT_DIRECTORIES = Join-Path $externalRoot '.git\objects'
        GIT_CEILING_DIRECTORIES = $testRoot
        GIT_DISCOVERY_ACROSS_FILESYSTEM = '1'
        GIT_CONFIG_SYSTEM = $externalConfig
        GIT_CONFIG_GLOBAL = $externalConfig
        GIT_CONFIG_NOSYSTEM = '1'
        GIT_CONFIG_COUNT = '1'
        GIT_CONFIG_KEY_0 = 'core.hooksPath'
        GIT_CONFIG_VALUE_0 = $externalHooks
        GIT_APEX_SANITIZE_A = 'restore-after-partial-sanitization-a'
        GIT_APEX_SANITIZE_B = 'restore-after-partial-sanitization-b'
        GIT_TRACE2_EVENT = $externalTrace
    }
    $previousEnvironment = @{}
    $environmentPoisoned = $false

    try {
        [System.IO.Directory]::CreateDirectory($successRoot) | Out-Null
        [System.IO.Directory]::CreateDirectory($failureRoot) | Out-Null
        [System.IO.Directory]::CreateDirectory($sanitizationFailureRoot) | Out-Null
        [System.IO.Directory]::CreateDirectory($externalRoot) | Out-Null
        [System.IO.Directory]::CreateDirectory($externalHooks) | Out-Null
        [System.IO.File]::WriteAllText($successTarget, $pristineText, $utf8NoBom)
        [System.IO.File]::WriteAllText($failureTarget, $pristineText, $utf8NoBom)
        [System.IO.File]::WriteAllText($sanitizationFailureTarget, $pristineText, $utf8NoBom)
        [System.IO.File]::WriteAllText($externalTarget, $pristineText, $utf8NoBom)
        [System.IO.File]::WriteAllText($patchedExpected, $patchedText, $utf8NoBom)
        [System.IO.File]::WriteAllText($externalConfig, "[core]`n`tautocrlf = false`n", $utf8NoBom)
        [System.IO.File]::WriteAllText($externalTrace, "trace sentinel must remain unchanged`n", $utf8NoBom)
        [System.IO.File]::WriteAllText(
            $validPatch,
            "diff --git a/payload.txt b/payload.txt`n--- a/payload.txt`n+++ b/payload.txt`n@@ -1 +1 @@`n-before`n+after`n",
            $utf8NoBom
        )
        [System.IO.File]::WriteAllText($invalidPatch, "not a Git patch`n", $utf8NoBom)
        [System.IO.File]::WriteAllText(
            $expectedEnvironmentPath,
            ($selectionEnvironment | ConvertTo-Json -Depth 4),
            $utf8NoBom
        )
        [System.IO.File]::WriteAllText(
            $childScript,
            @'
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $HelperPath,

    [Parameter(Mandatory = $true)]
    [string] $Root,

    [Parameter(Mandatory = $true)]
    [string] $PatchPath,

    [Parameter(Mandatory = $true)]
    [string] $PristineTargetSha256,

    [Parameter(Mandatory = $true)]
    [string] $PatchedTargetSha256,

    [Parameter(Mandatory = $true)]
    [string] $ExpectedEnvironmentPath,

    [Parameter(Mandatory = $true)]
    [string] $EvidencePath,

    [switch] $ExpectFailure,

    [switch] $FailDuringSanitization
)

$ErrorActionPreference = 'Stop'
$expectedEnvironment = Get-Content -LiteralPath $ExpectedEnvironmentPath -Raw | ConvertFrom-Json
. $HelperPath

$script:sanitizationFailureInjected = $false
$script:partialSanitizationObserved = $false
$invokeArguments = @{
    Root = $Root
    PatchPath = $PatchPath
    TargetRelativePath = 'payload.txt'
    PristineTargetSha256 = $PristineTargetSha256
    PatchedTargetSha256 = $PatchedTargetSha256
}
if ($FailDuringSanitization) {
    $invokeArguments.ProcessEnvironmentClearer = {
        param(
            [Parameter(Mandatory = $true)]
            [string] $Name
        )

        # Fail once after a real earlier clear, then let production restoration use the real API.
        if (-not $script:sanitizationFailureInjected -and
            $Name -ceq 'GIT_APEX_SANITIZE_B') {
            $script:partialSanitizationObserved =
                $null -eq [Environment]::GetEnvironmentVariable('GIT_APEX_SANITIZE_A', 'Process')
            $script:sanitizationFailureInjected = $true
            throw 'Injected process-environment setter failure during Git sanitization.'
        }

        [Environment]::SetEnvironmentVariable($Name, $null, 'Process')
    }
}

$operationError = $null
try {
    Invoke-JuceVerifiedPatch @invokeArguments
}
catch {
    $operationError = $_.Exception.Message
}

$restoredEnvironment = [ordered]@{}
foreach ($property in $expectedEnvironment.PSObject.Properties) {
    $actualValue = [Environment]::GetEnvironmentVariable($property.Name, 'Process')
    if ($actualValue -cne [string] $property.Value) {
        throw "Git environment variable $($property.Name) was not restored. Expected '$($property.Value)', got '$actualValue'."
    }
    $restoredEnvironment[$property.Name] = $actualValue
}

if ($FailDuringSanitization) {
    if ($operationError -cne 'Injected process-environment setter failure during Git sanitization.') {
        throw "Sanitization did not fail at the injected setter: $operationError"
    }
    if (-not $script:partialSanitizationObserved) {
        throw 'The sanitization failure occurred before an earlier Git variable was cleared.'
    }
}

if ($ExpectFailure) {
    if ($null -eq $operationError) {
        throw 'Invalid patch unexpectedly succeeded.'
    }
}
elseif ($null -ne $operationError) {
    throw "Valid patch failed: $operationError"
}

$evidence = [ordered]@{
    ProcessId = $PID
    OperationError = $operationError
    PartialSanitizationObserved = $script:partialSanitizationObserved
    Environment = $restoredEnvironment
}
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($EvidencePath, ($evidence | ConvertTo-Json -Depth 6), $utf8NoBom)
exit 0
'@,
            $utf8NoBom
        )

        & git -C $externalRoot init --quiet
        Assert-Equal 0 $LASTEXITCODE 'could not initialize external poisoned-environment sentinel repository'
        $externalBefore = Get-DirectoryTreeFingerprint -Root $externalRoot
        $traceBefore = [System.IO.File]::ReadAllText($externalTrace)
        $pristineHash = (Get-FileHash -LiteralPath $successTarget -Algorithm SHA256).Hash
        $patchedHash = (Get-FileHash -LiteralPath $patchedExpected -Algorithm SHA256).Hash
        $processEnvironment = [Environment]::GetEnvironmentVariables('Process')

        foreach ($entry in $selectionEnvironment.GetEnumerator()) {
            $previousEnvironment[$entry.Key] = [pscustomobject]@{
                WasSet = $processEnvironment.Contains($entry.Key)
                Value = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
            }
        }

        $environmentPoisoned = $true
        foreach ($entry in $selectionEnvironment.GetEnumerator()) {
            [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
        }

        try {
            $successResult = Invoke-ChildPowerShell -ScriptPath $childScript -Arguments @(
                '-HelperPath', (Join-Path $PSScriptRoot 'juce_patch_tools.ps1'),
                '-Root', $successRoot,
                '-PatchPath', $validPatch,
                '-PristineTargetSha256', $pristineHash,
                '-PatchedTargetSha256', $patchedHash,
                '-ExpectedEnvironmentPath', $expectedEnvironmentPath,
                '-EvidencePath', $successEvidencePath
            )
            $failureResult = Invoke-ChildPowerShell -ScriptPath $childScript -Arguments @(
                '-HelperPath', (Join-Path $PSScriptRoot 'juce_patch_tools.ps1'),
                '-Root', $failureRoot,
                '-PatchPath', $invalidPatch,
                '-PristineTargetSha256', $pristineHash,
                '-PatchedTargetSha256', ('0' * 64),
                '-ExpectedEnvironmentPath', $expectedEnvironmentPath,
                '-EvidencePath', $failureEvidencePath,
                '-ExpectFailure'
            )
            $sanitizationFailureResult = Invoke-ChildPowerShell -ScriptPath $childScript -Arguments @(
                '-HelperPath', (Join-Path $PSScriptRoot 'juce_patch_tools.ps1'),
                '-Root', $sanitizationFailureRoot,
                '-PatchPath', $validPatch,
                '-PristineTargetSha256', $pristineHash,
                '-PatchedTargetSha256', $patchedHash,
                '-ExpectedEnvironmentPath', $expectedEnvironmentPath,
                '-EvidencePath', $sanitizationFailureEvidencePath,
                '-ExpectFailure',
                '-FailDuringSanitization'
            )
        }
        finally {
            foreach ($entry in $previousEnvironment.GetEnumerator()) {
                if ($entry.Value.WasSet) {
                    [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value.Value, 'Process')
                }
                else {
                    [Environment]::SetEnvironmentVariable($entry.Key, $null, 'Process')
                }
            }
            $environmentPoisoned = $false
        }

        Assert-Equal 0 $successResult.ExitCode "poisoned child process could not apply the valid patch: $($successResult.Output)"
        Assert-Equal 0 $failureResult.ExitCode "poisoned child process did not restore its environment after Git failure: $($failureResult.Output)"
        Assert-Equal 0 $sanitizationFailureResult.ExitCode "poisoned child process did not restore its environment after sanitization failure: $($sanitizationFailureResult.Output)"
        Assert-True (Test-Path -LiteralPath $successEvidencePath -PathType Leaf) 'successful child did not emit restoration evidence'
        Assert-True (Test-Path -LiteralPath $failureEvidencePath -PathType Leaf) 'failing child did not emit restoration evidence'
        Assert-True (Test-Path -LiteralPath $sanitizationFailureEvidencePath -PathType Leaf) 'sanitization-failure child did not emit restoration evidence'
        $successEvidence = Get-Content -LiteralPath $successEvidencePath -Raw | ConvertFrom-Json
        $failureEvidence = Get-Content -LiteralPath $failureEvidencePath -Raw | ConvertFrom-Json
        $sanitizationFailureEvidence = Get-Content -LiteralPath $sanitizationFailureEvidencePath -Raw | ConvertFrom-Json
        Assert-True ([int] $successEvidence.ProcessId -ne $PID) 'valid patch did not run in a child process'
        Assert-True ([int] $failureEvidence.ProcessId -ne $PID) 'invalid patch did not run in a child process'
        Assert-True ([int] $sanitizationFailureEvidence.ProcessId -ne $PID) 'sanitization-failure probe did not run in a child process'
        Assert-True ([int] $successEvidence.ProcessId -ne [int] $failureEvidence.ProcessId) 'valid and invalid patch probes reused one process'
        Assert-Equal $null $successEvidence.OperationError 'valid patch child recorded an operation error'
        Assert-True ($failureEvidence.OperationError -match 'Patch check failed|No valid patches') "invalid patch did not exercise Git failure: $($failureEvidence.OperationError)"
        Assert-Equal 'Injected process-environment setter failure during Git sanitization.' $sanitizationFailureEvidence.OperationError 'sanitization probe failed for an unexpected reason'
        Assert-True ([bool] $sanitizationFailureEvidence.PartialSanitizationObserved) 'sanitization probe did not observe an earlier Git variable cleared before failure'
        foreach ($entry in $selectionEnvironment.GetEnumerator()) {
            Assert-Equal $entry.Value $successEvidence.Environment.PSObject.Properties[$entry.Key].Value "successful child did not restore $($entry.Key)"
            Assert-Equal $entry.Value $failureEvidence.Environment.PSObject.Properties[$entry.Key].Value "failing child did not restore $($entry.Key)"
            Assert-Equal $entry.Value $sanitizationFailureEvidence.Environment.PSObject.Properties[$entry.Key].Value "sanitization-failure child did not restore $($entry.Key)"
        }
        foreach ($entry in $previousEnvironment.GetEnumerator()) {
            $actualValue = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
            if ($entry.Value.WasSet) {
                Assert-Equal $entry.Value.Value $actualValue "test harness did not restore parent $($entry.Key)"
            }
            else {
                Assert-Equal $null $actualValue "test harness left parent $($entry.Key) set"
            }
        }

        Assert-Equal $patchedText ([System.IO.File]::ReadAllText($successTarget)) 'patch did not update the disposable child-process root'
        Assert-Equal $pristineText ([System.IO.File]::ReadAllText($failureTarget)) 'failed patch changed its disposable target'
        Assert-Equal $pristineText ([System.IO.File]::ReadAllText($sanitizationFailureTarget)) 'sanitization failure changed its disposable target'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $successRoot '.git'))) 'successful patch left temporary Git metadata'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $failureRoot '.git'))) 'failed patch left temporary Git metadata'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $sanitizationFailureRoot '.git'))) 'sanitization failure left temporary Git metadata'
        Assert-Equal $pristineText ([System.IO.File]::ReadAllText($externalTarget)) 'patch operation changed the external repository sentinel'
        Assert-Equal $traceBefore ([System.IO.File]::ReadAllText($externalTrace)) 'inherited Git tracing mutated the external repository sentinel'
        Assert-Equal $externalBefore (Get-DirectoryTreeFingerprint -Root $externalRoot) 'patch operation mutated external Git metadata or content'
    }
    finally {
        if ($environmentPoisoned) {
            foreach ($entry in $previousEnvironment.GetEnumerator()) {
                if ($entry.Value.WasSet) {
                    [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value.Value, 'Process')
                }
                else {
                    [Environment]::SetEnvironmentVariable($entry.Key, $null, 'Process')
                }
            }
        }

        if (Test-Path -LiteralPath $testRoot) {
            Remove-Item -LiteralPath $testRoot -Recurse -Force
        }
    }
} -Case 'PatchEnvironment'

Invoke-Test 'Get-ApexBuildTools resolves pinned x64 build tools and outputs' {
    $tools = Get-ApexBuildTools
    $expectedRoot = [System.IO.Path]::GetFullPath($repositoryRoot).TrimEnd('\')
    $expectedProjucer = [System.IO.Path]::GetFullPath(
        (Join-Path $repositoryRoot '..\Sdk setups\juce-8.0.12-windows\JUCE\Projucer.exe')
    )

    Assert-Equal $expectedRoot $tools.RepositoryRoot.TrimEnd('\') 'repository root was not resolved from the script location'
    Assert-Equal $expectedProjucer $tools.Projucer 'Projucer did not resolve to the pinned JUCE SDK'
    Assert-True (Test-Path -LiteralPath $tools.Projucer -PathType Leaf) 'pinned Projucer does not exist'
    Assert-True (Test-Path -LiteralPath $tools.MSBuild -PathType Leaf) 'MSBuild does not exist'
    Assert-True ($tools.MSBuild -match '[\\/]amd64[\\/]MSBuild\.exe$') 'MSBuild is not the amd64 executable'

    foreach ($configuration in @('Debug', 'Release')) {
        $expectedExe = [System.IO.Path]::GetFullPath(
            (Join-Path $repositoryRoot "Builds\VisualStudio2026\x64\$configuration\App\DAW_Core.exe")
        )
        $expectedPdb = [System.IO.Path]::GetFullPath(
            (Join-Path $repositoryRoot "Builds\VisualStudio2026\x64\$configuration\App\DAW_Core.pdb")
        )

        Assert-Equal $expectedExe $tools.OutputPaths[$configuration].Exe "unexpected $configuration EXE path"
        Assert-Equal $expectedPdb $tools.OutputPaths[$configuration].Pdb "unexpected $configuration PDB path"
    }
}

Invoke-Test 'reparse tag classification distinguishes cloud metadata from path redirects' {
    $oneDriveTag = [Convert]::ToUInt32('9000E01A', 16)
    $mountPointTag = [Convert]::ToUInt32('A0000003', 16)
    $symbolicLinkTag = [Convert]::ToUInt32('A000000C', 16)

    Assert-True (-not (Test-ApexReparseTagNameSurrogate -Tag $oneDriveTag)) 'OneDrive cloud tag was classified as a name surrogate'
    Assert-True (Test-ApexReparseTagNameSurrogate -Tag $mountPointTag) 'NTFS mount-point tag was not classified as a name surrogate'
    Assert-True (Test-ApexReparseTagNameSurrogate -Tag $symbolicLinkTag) 'symbolic-link tag was not classified as a name surrogate'
} -Case 'ReparsePayload'

Invoke-Test 'payload manifests reject root and descendant reparse points before traversal' {
    $testRoot = Join-Path $repositoryRoot ".apex-debug\test-build-foundation\$([guid]::NewGuid().ToString('N'))"
    $rootTarget = Join-Path $testRoot 'root-target'
    $rootJunction = Join-Path $testRoot 'root-junction'
    $directoryPayloadRoot = Join-Path $testRoot 'directory-payload-root'
    $filePayloadRoot = Join-Path $testRoot 'file-payload-root'
    $externalDirectory = Join-Path $testRoot 'external-directory'
    $descendantJunction = Join-Path $directoryPayloadRoot 'linked-directory'
    $rootTripwire = Join-Path $rootTarget 'root-tripwire.bin'
    $directoryTripwire = Join-Path $externalDirectory 'directory-tripwire.bin'
    $externalFile = Join-Path $testRoot 'external-file.txt'
    $fileSymlink = Join-Path $filePayloadRoot 'linked-file.txt'
    $missingRoot = Join-Path $testRoot 'missing-payload-root'
    $rootTripwireStream = $null
    $directoryTripwireStream = $null
    $fileTripwireStream = $null
    $fileSymlinkCreated = $false
    $fileSymlinkLimitation = $null
    $assertRejected = {
        param(
            [Parameter(Mandatory = $true)]
            [string] $Path,

            [Parameter(Mandatory = $true)]
            [string] $Label,

            [Parameter(Mandatory = $true)]
            [string] $ExpectedReparsePath,

            [Parameter(Mandatory = $true)]
            [uint32] $ExpectedTag
        )

        $failureMessage = $null
        try {
            @(Get-ApexDirectoryPayloadManifest -Root $Path) | Out-Null
        }
        catch {
            $failureMessage = $_.Exception.Message
        }

        $expectedMessage = "Payload path is a name-surrogate reparse point (tag 0x{0:X8}): {1}" -f `
            $ExpectedTag, [System.IO.Path]::GetFullPath($ExpectedReparsePath)
        Assert-Equal $expectedMessage $failureMessage "$Label was not rejected at the expected reparse point before traversal or hashing"
    }

    [System.IO.Directory]::CreateDirectory($rootTarget) | Out-Null
    [System.IO.Directory]::CreateDirectory($directoryPayloadRoot) | Out-Null
    [System.IO.Directory]::CreateDirectory($filePayloadRoot) | Out-Null
    [System.IO.Directory]::CreateDirectory($externalDirectory) | Out-Null
    [System.IO.File]::WriteAllText($rootTripwire, 'must not be hashed through the root junction')
    [System.IO.File]::WriteAllText($directoryTripwire, 'must not be hashed through the descendant junction')
    [System.IO.File]::WriteAllText($externalFile, 'must not be hashed through the symbolic link')

    try {
        New-Item -ItemType Junction -Path $rootJunction -Target $rootTarget -ErrorAction Stop | Out-Null
        New-Item -ItemType Junction -Path $descendantJunction -Target $externalDirectory -ErrorAction Stop | Out-Null
        $rootTripwireStream = [System.IO.File]::Open(
            $rootTripwire,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None
        )
        $directoryTripwireStream = [System.IO.File]::Open(
            $directoryTripwire,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None
        )
        $fileTripwireStream = [System.IO.File]::Open(
            $externalFile,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None
        )

        & $assertRejected -Path $rootJunction -Label 'manifest root junction' -ExpectedReparsePath $rootJunction `
            -ExpectedTag ([Convert]::ToUInt32('A0000003', 16))
        & $assertRejected -Path $directoryPayloadRoot -Label 'descendant directory junction' -ExpectedReparsePath $descendantJunction `
            -ExpectedTag ([Convert]::ToUInt32('A0000003', 16))

        try {
            New-Item -ItemType SymbolicLink -Path $fileSymlink -Target $externalFile -ErrorAction Stop | Out-Null
            $fileSymlinkCreated = $true
        }
        catch [System.UnauthorizedAccessException] {
            if ($_.Exception.HResult -ne -2147024891) {
                throw
            }

            $fileSymlinkLimitation = "$($_.Exception.GetType().FullName); HResult=$($_.Exception.HResult); $($_.Exception.Message)"
        }

        if ($fileSymlinkCreated) {
            & $assertRejected -Path $filePayloadRoot -Label 'descendant file symbolic link' -ExpectedReparsePath $fileSymlink `
                -ExpectedTag ([Convert]::ToUInt32('A000000C', 16))
        }
        else {
            Assert-True (-not [string]::IsNullOrWhiteSpace($fileSymlinkLimitation)) 'file symbolic-link limitation was not captured'
            Write-Host "[INFO] file symbolic-link coverage unavailable: $fileSymlinkLimitation"
        }

        $missingFailure = $null
        try {
            @(Get-ApexDirectoryPayloadManifest -Root $missingRoot) | Out-Null
        }
        catch {
            $missingFailure = $_.Exception.Message
        }
        Assert-Equal "Payload root is missing: $([System.IO.Path]::GetFullPath($missingRoot))" $missingFailure `
            'manifest did not preserve its clear missing-root diagnostic'
    }
    finally {
        if ($null -ne $rootTripwireStream) {
            $rootTripwireStream.Dispose()
        }
        if ($null -ne $directoryTripwireStream) {
            $directoryTripwireStream.Dispose()
        }
        if ($null -ne $fileTripwireStream) {
            $fileTripwireStream.Dispose()
        }
        if ($fileSymlinkCreated -and [System.IO.File]::Exists($fileSymlink)) {
            [System.IO.File]::Delete($fileSymlink)
        }
        if (Test-Path -LiteralPath $descendantJunction) {
            [System.IO.Directory]::Delete($descendantJunction)
        }
        if (Test-Path -LiteralPath $rootJunction) {
            [System.IO.Directory]::Delete($rootJunction)
        }
        if (Test-Path -LiteralPath $testRoot) {
            Remove-Item -LiteralPath $testRoot -Recurse -Force
        }
    }
} -Case 'ReparsePayload'

Invoke-Test 'materializer and verifier reject a redirected approved SDK parent before child access' {
    $fixture = New-MaterializerFixture
    $externalRoot = Join-Path $repositoryRoot ".apex-debug\test-build-foundation\parent-redirect-$([guid]::NewGuid().ToString('N'))"
    $externalSentinel = Join-Path $externalRoot 'external-owner.txt'
    $externalArchive = Join-Path $externalRoot 'juce-8.0.12-windows.zip'
    $junctionCreated = $false

    try {
        [System.IO.Directory]::Delete($fixture.SdkParent)
        [System.IO.Directory]::CreateDirectory($externalRoot) | Out-Null
        [System.IO.File]::WriteAllText($externalSentinel, 'external owner must remain unchanged')
        [System.IO.File]::WriteAllText($externalArchive, 'deliberately incorrect JUCE archive')
        $externalBefore = Get-DirectoryTreeFingerprint -Root $externalRoot

        New-Item -ItemType Junction -Path $fixture.SdkParent -Target $externalRoot -ErrorAction Stop | Out-Null
        $junctionCreated = $true

        & git -C $fixture.Repository init --quiet
        Assert-Equal 0 $LASTEXITCODE 'could not initialize redirected-parent verifier fixture repository'

        $materializerResult = Invoke-ChildPowerShell -ScriptPath $fixture.Script -Arguments @(
            '-JuceArchive', $fixture.LocalArchive
        )
        $verifierResult = Invoke-ChildPowerShell -ScriptPath $fixture.VerifyScript
        $normalizedParentPath = ([System.IO.Path]::GetFullPath($fixture.SdkParent) -replace '\s', '')
        $normalizedMaterializerOutput = ($materializerResult.Output -replace '\s', '')
        $normalizedVerifierOutput = ($verifierResult.Output -replace '\s', '')

        Assert-True ($materializerResult.ExitCode -ne 0) 'materializer accepted a redirected approved SDK parent'
        Assert-True ($materializerResult.Output -match 'name-surrogate reparse point \(tag 0xA0000003\)') `
            "materializer did not report the redirected parent native tag: $($materializerResult.Output)"
        Assert-True ($normalizedMaterializerOutput.Contains($normalizedParentPath)) `
            "materializer did not report the redirected parent path: $($materializerResult.Output)"
        Assert-True ($materializerResult.Output -notmatch 'archive SHA-256 mismatch|ExtractToDirectory') `
            'materializer accessed the archive before rejecting the redirected parent'

        Assert-True ($verifierResult.ExitCode -ne 0) 'dependency verifier accepted a redirected approved SDK parent'
        Assert-True ($verifierResult.Output -match 'name-surrogate reparse point \(tag 0xA0000003\)') `
            "dependency verifier did not report the redirected parent native tag: $($verifierResult.Output)"
        Assert-True ($normalizedVerifierOutput.Contains($normalizedParentPath)) `
            "dependency verifier did not report the redirected parent path: $($verifierResult.Output)"
        Assert-True ($verifierResult.Output -notmatch 'JUCE StandardHeader|Pinned Projucer|JUCE archive SHA-256|installation payload') `
            "dependency verifier continued child or archive work after parent rejection: $($verifierResult.Output)"

        Assert-Equal $externalBefore (Get-DirectoryTreeFingerprint -Root $externalRoot) `
            'redirected parent operations mutated the external target'
        Assert-Equal 'external owner must remain unchanged' ([System.IO.File]::ReadAllText($externalSentinel)) `
            'redirected parent operations changed the external sentinel'
        Assert-True (-not (Test-Path -LiteralPath $fixture.InstallationRoot)) `
            'materializer published an installation through the redirected parent'
        Assert-Equal 0 @([System.IO.Directory]::GetDirectories($externalRoot, '.apex-juce-*')).Count `
            'materializer extracted a temporary payload through the redirected parent'
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
} -Case 'ParentRedirect'

Invoke-Test 'materializer and verifier reject a junction inside the approved SDK parent before child access' {
    $fixture = New-MaterializerFixture
    $externalRoot = Join-Path $repositoryRoot ".apex-debug\test-build-foundation\child-redirect-$([guid]::NewGuid().ToString('N'))"
    $externalSentinel = Join-Path $externalRoot 'external-owner.txt'
    $childJunction = $fixture.InstallationRoot
    $junctionCreated = $false

    try {
        [System.IO.Directory]::CreateDirectory($externalRoot) | Out-Null
        [System.IO.File]::WriteAllText($externalSentinel, 'external owner must remain unchanged')

        # Create a junction inside the SDK parent pointing to an external target.
        # The SDK parent remains a real directory; only the installation root is a junction.
        New-Item -ItemType Junction -Path $childJunction -Target $externalRoot -ErrorAction Stop | Out-Null
        $junctionCreated = $true

        $externalBefore = Get-DirectoryTreeFingerprint -Root $externalRoot

        & git -C $fixture.Repository init --quiet
        Assert-Equal 0 $LASTEXITCODE 'could not initialize child-redirect fixture repository'

        $materializerResult = Invoke-ChildPowerShell -ScriptPath $fixture.Script -Arguments @(
            '-JuceArchive', $fixture.Archive
        )
        $verifierResult = Invoke-ChildPowerShell -ScriptPath $fixture.VerifyScript

        # Materializer should fail at the junction before accessing any child content
        Assert-True ($materializerResult.ExitCode -ne 0) 'materializer accepted a junction inside the approved SDK parent'
        Assert-True ($materializerResult.Output -match 'name-surrogate reparse point') `
            "materializer did not report the child-redirect native tag: $($materializerResult.Output)"
        Assert-True ($materializerResult.Output -notmatch 'archive SHA-256|ExtractToDirectory') `
            'materializer accessed child content before rejecting the junction'

        # Verifier should fail — the installation-root junction causes StandardHeader/Projucer
        # to resolve through the external target which lacks those files.
        Assert-True ($verifierResult.ExitCode -ne 0) 'dependency verifier accepted a junction inside the approved SDK parent'

        # External target must remain unchanged
        Assert-Equal $externalBefore (Get-DirectoryTreeFingerprint -Root $externalRoot) `
            'child-redirect operations mutated the external target'
        Assert-Equal 'external owner must remain unchanged' ([System.IO.File]::ReadAllText($externalSentinel)) `
            'child-redirect operations changed the external sentinel'
    }
    finally {
        if ($junctionCreated -and [System.IO.Directory]::Exists($childJunction)) {
            [System.IO.Directory]::Delete($childJunction)
        }
        Remove-MaterializerFixture -Fixture $fixture
        if (Test-Path -LiteralPath $externalRoot) {
            Remove-Item -LiteralPath $externalRoot -Recurse -Force
        }
    }
} -Case 'ChildRedirect'

Invoke-Test 'materializer rejects a wrong archive without replacing the SDK' {
    $tools = Get-ApexBuildTools
    $sdkRoot = Split-Path -Parent (Split-Path -Parent $tools.Projucer)
    $beforeTree = Get-DirectoryTreeFingerprint -Root $sdkRoot
    $testRoot = Join-Path $repositoryRoot ".apex-debug\test-build-foundation\$([guid]::NewGuid().ToString('N'))"
    $wrongArchive = Join-Path $testRoot 'juce-8.0.12-windows.zip'

    [System.IO.Directory]::CreateDirectory($testRoot) | Out-Null
    [System.IO.File]::WriteAllText($wrongArchive, 'deliberately incorrect JUCE archive')

    try {
        $result = Invoke-ChildPowerShell -ScriptPath (Join-Path $PSScriptRoot 'materialize_dependencies.ps1') -Arguments @(
            '-JuceArchive', $wrongArchive
        )

        Assert-True ($result.ExitCode -ne 0) 'materializer accepted an archive with the wrong SHA-256'
        Assert-True ($result.Output -match 'SHA-256') 'materializer did not explain the archive hash rejection'
        Assert-Equal $beforeTree (Get-DirectoryTreeFingerprint -Root $sdkRoot) 'complete SDK tree changed after rejected archive'
    }
    finally {
        if (Test-Path -LiteralPath $testRoot) {
            [System.IO.Directory]::Delete($testRoot, $true)
        }
    }
}

Invoke-Test 'materializer rejects a version-matching but byte-mismatched SDK' {
    $fixture = New-MaterializerFixture
    $projucer = Join-Path $fixture.InstallationRoot 'JUCE\Projucer.exe'
    $standardHeader = Join-Path $fixture.InstallationRoot 'JUCE\modules\juce_core\system\juce_StandardHeader.h'

    try {
        [System.IO.Directory]::CreateDirectory((Split-Path -Parent $projucer)) | Out-Null
        [System.IO.Directory]::CreateDirectory((Split-Path -Parent $standardHeader)) | Out-Null
        [System.IO.File]::WriteAllText($projucer, 'not the pinned Projucer payload')
        [System.IO.File]::WriteAllText(
            $standardHeader,
            "#define JUCE_MAJOR_VERSION 8`r`n#define JUCE_MINOR_VERSION 0`r`n#define JUCE_BUILDNUMBER 12`r`n"
        )
        $beforeTree = Get-DirectoryTreeFingerprint -Root $fixture.InstallationRoot
        $result = Invoke-ChildPowerShell -ScriptPath $fixture.Script -Arguments @(
            '-JuceArchive', $fixture.Archive
        )

        Assert-True ($result.ExitCode -ne 0) 'materializer accepted a byte-mismatched existing SDK'
        Assert-True ($result.Output -match 'does not match the pinned JUCE archive-plus-patch payload') "materializer did not identify complete-payload mismatch: $($result.Output)"
        Assert-Equal $beforeTree (Get-DirectoryTreeFingerprint -Root $fixture.InstallationRoot) 'mismatched existing SDK was modified or replaced'
    }
    finally {
        Remove-MaterializerFixture -Fixture $fixture
    }
} -Case 'ExistingMismatch'

Invoke-Test 'materializer deterministically publishes and preserves an archive-plus-patch SDK' {
    $fixture = New-MaterializerFixture

    try {
        $firstResult = Invoke-ChildPowerShell -ScriptPath $fixture.Script -Arguments @(
            '-JuceArchive', $fixture.Archive
        )

        Assert-Equal 0 $firstResult.ExitCode 'materializer did not publish the deterministic archive-plus-patch SDK'
        Assert-True (Test-Path -LiteralPath $fixture.PatchedTarget -PathType Leaf) 'materializer did not publish the patched JUCE target'
        Assert-Equal $patchedJuceTargetHash (Get-FileHash -LiteralPath $fixture.PatchedTarget -Algorithm SHA256).Hash 'materializer published the wrong JUCE target bytes'
        $beforeTree = Get-DirectoryTreeFingerprint -Root $fixture.InstallationRoot

        $secondResult = Invoke-ChildPowerShell -ScriptPath $fixture.Script -Arguments @(
            '-JuceArchive', $fixture.Archive
        )

        Assert-Equal 0 $secondResult.ExitCode 'materializer rejected an exact archive-plus-patch installation'
        Assert-True ($secondResult.Output -match 'exactly matches the archive-plus-patch payload') 'materializer did not report exact transformed payload identity'
        Assert-Equal $beforeTree (Get-DirectoryTreeFingerprint -Root $fixture.InstallationRoot) 'materializer changed an exact archive-plus-patch SDK'
        Assert-Equal 0 @([System.IO.Directory]::GetDirectories($fixture.SdkParent, '.apex-juce-*')).Count 'temporary extraction survived idempotent materialization'
    }
    finally {
        Remove-MaterializerFixture -Fixture $fixture
    }
} -Case 'MatchingExisting'

Invoke-Test 'materializer and verifier reject a tampered transformed result without mutation' {
    $fixture = New-MaterializerFixture

    try {
        [System.IO.File]::Copy($fixture.Archive, $fixture.LocalArchive)
        $initialResult = Invoke-ChildPowerShell -ScriptPath $fixture.Script -Arguments @(
            '-JuceArchive', $fixture.LocalArchive
        )
        Assert-Equal 0 $initialResult.ExitCode 'could not materialize the transformed result fixture'
        Assert-Equal $patchedJuceTargetHash (Get-FileHash -LiteralPath $fixture.PatchedTarget -Algorithm SHA256).Hash 'result fixture was not built from archive plus patch'

        $probeFile = Join-Path $fixture.InstallationRoot 'JUCE\LICENSE.md'
        $originalBytes = [System.IO.File]::ReadAllBytes($probeFile)
        $markerBytes = [System.Text.Encoding]::ASCII.GetBytes("`r`nAPEX dependency verifier mismatch probe`r`n")
        [byte[]] $modifiedBytes = $originalBytes + $markerBytes
        [System.IO.File]::WriteAllBytes($probeFile, $modifiedBytes)
        $modifiedTree = Get-DirectoryTreeFingerprint -Root $fixture.InstallationRoot

        $materializerResult = Invoke-ChildPowerShell -ScriptPath $fixture.Script -Arguments @(
            '-JuceArchive', $fixture.LocalArchive
        )
        Assert-True ($materializerResult.ExitCode -ne 0) 'materializer accepted a tampered transformed result'
        Assert-True ($materializerResult.Output -match 'archive-plus-patch payload') 'materializer did not identify transformed-result mismatch'
        Assert-Equal $modifiedTree (Get-DirectoryTreeFingerprint -Root $fixture.InstallationRoot) 'materializer modified or replaced the tampered result'

        & git -C $fixture.Repository init --quiet
        if ($LASTEXITCODE -ne 0) {
            throw '[FAIL] could not initialize verifier fixture repository'
        }

        $result = Invoke-ChildPowerShell -ScriptPath $fixture.VerifyScript

        Assert-True ($result.ExitCode -ne 0) 'dependency verifier accepted a modified JUCE module file'
        Assert-True ($result.Output -match 'JUCE installation payload') 'dependency verifier did not identify complete-payload mismatch'
        Assert-Equal $modifiedTree (Get-DirectoryTreeFingerprint -Root $fixture.InstallationRoot) 'dependency verifier modified the mismatched SDK'
    }
    finally {
        Remove-MaterializerFixture -Fixture $fixture
    }
} -Case 'VerifierMismatch'

Invoke-Test 'materializer and verifier reject a tampered tracked patch without SDK mutation' {
    $fixture = New-MaterializerFixture

    try {
        if (-not (Test-Path -LiteralPath $fixture.Patch -PathType Leaf)) {
            [System.IO.Directory]::CreateDirectory((Split-Path -Parent $fixture.Patch)) | Out-Null
            [System.IO.File]::WriteAllText($fixture.Patch, 'untracked patch placeholder')
        }

        [System.IO.File]::Copy($fixture.Archive, $fixture.LocalArchive)
        $patchBytes = [System.IO.File]::ReadAllBytes($fixture.Patch)
        $tamperBytes = [System.Text.Encoding]::ASCII.GetBytes("`nAPEX patch tamper probe`n")
        [System.IO.File]::WriteAllBytes($fixture.Patch, ([byte[]] ($patchBytes + $tamperBytes)))
        $sdkBefore = Get-DirectoryTreeFingerprint -Root $fixture.SdkParent

        $materializerResult = Invoke-ChildPowerShell -ScriptPath $fixture.Script -Arguments @(
            '-JuceArchive', $fixture.LocalArchive
        )
        Assert-True ($materializerResult.ExitCode -ne 0) 'materializer accepted a tampered tracked patch'
        Assert-True ($materializerResult.Output -match 'compatibility patch SHA-256') 'materializer did not identify patch-byte tampering'
        Assert-Equal $sdkBefore (Get-DirectoryTreeFingerprint -Root $fixture.SdkParent) 'materializer mutated the SDK area after patch rejection'

        & git -C $fixture.Repository init --quiet
        if ($LASTEXITCODE -ne 0) {
            throw '[FAIL] could not initialize patch-tamper verifier fixture repository'
        }

        $verifierResult = Invoke-ChildPowerShell -ScriptPath $fixture.VerifyScript
        Assert-True ($verifierResult.ExitCode -ne 0) 'dependency verifier accepted a tampered tracked patch'
        Assert-True ($verifierResult.Output -match 'JUCE compatibility patch SHA-256') 'dependency verifier did not identify patch-byte tampering'
        Assert-Equal $sdkBefore (Get-DirectoryTreeFingerprint -Root $fixture.SdkParent) 'dependency verifier mutated the SDK area after patch rejection'
    }
    finally {
        Remove-MaterializerFixture -Fixture $fixture
    }
} -Case 'PatchTampering'

Invoke-Test 'materializer and verifier reject tampered pristine-target identity without SDK mutation' {
    $fixture = New-MaterializerFixture
    $contract = Get-Content -LiteralPath $fixture.Contract -Raw | ConvertFrom-Json

    if ($null -eq $contract.juce.compatibilityPatch) {
        $contract.juce | Add-Member -NotePropertyName compatibilityPatch -NotePropertyValue ([pscustomobject]@{
            relativePath = $jucePatchRelativePath.Replace('\', '/')
            sha256 = ('0' * 64)
            targetRelativePath = $jucePatchTargetRelativePath.Replace('\', '/')
            pristineTargetSha256 = ('0' * 64)
            patchedTargetSha256 = $patchedJuceTargetHash
            validationStatus = $jucePatchValidationStatus
            rationale = $jucePatchRationale
            hardwareValidationRequired = $true
        })
    }
    else {
        $contract.juce.compatibilityPatch.pristineTargetSha256 = ('0' * 64)
    }

    Write-JsonWithoutBom -Path $fixture.Contract -Value $contract

    try {
        [System.IO.File]::Copy($fixture.Archive, $fixture.LocalArchive)
        $sdkBefore = Get-DirectoryTreeFingerprint -Root $fixture.SdkParent
        $materializerResult = Invoke-ChildPowerShell -ScriptPath $fixture.Script -Arguments @(
            '-JuceArchive', $fixture.LocalArchive
        )

        Assert-True ($materializerResult.ExitCode -ne 0) 'materializer accepted a tampered pristine-target identity'
        Assert-True ($materializerResult.Output -match 'pristineTargetSha256') 'materializer did not identify pristine-target identity tampering'
        Assert-Equal $sdkBefore (Get-DirectoryTreeFingerprint -Root $fixture.SdkParent) 'materializer mutated the SDK area after base-identity rejection'

        & git -C $fixture.Repository init --quiet
        if ($LASTEXITCODE -ne 0) {
            throw '[FAIL] could not initialize base-tamper verifier fixture repository'
        }

        $verifierResult = Invoke-ChildPowerShell -ScriptPath $fixture.VerifyScript
        Assert-True ($verifierResult.ExitCode -ne 0) 'dependency verifier accepted a tampered pristine-target identity'
        Assert-True ($verifierResult.Output -match 'pristineTargetSha256') 'dependency verifier did not identify pristine-target identity tampering'
        Assert-Equal $sdkBefore (Get-DirectoryTreeFingerprint -Root $fixture.SdkParent) 'dependency verifier mutated the SDK area after base-identity rejection'
    }
    finally {
        Remove-MaterializerFixture -Fixture $fixture
    }
} -Case 'PatchBase'

Invoke-Test 'materializer rejects a redirected contract path before mutation' {
    $fixture = New-MaterializerFixture
    $contract = Get-Content -LiteralPath $fixture.Contract -Raw | ConvertFrom-Json
    $escapeName = "missing-parent-$([guid]::NewGuid().ToString('N'))"
    $contract.juce.rootRelativeToRepository = "../../$escapeName/child/juce-8.0.12-windows/JUCE"
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText(
        $fixture.Contract,
        ($contract | ConvertTo-Json -Depth 10),
        $utf8NoBom
    )
    $redirectedRoot = [System.IO.Path]::GetFullPath(
        (Join-Path $fixture.Repository ([string] $contract.juce.rootRelativeToRepository))
    )

    try {
        $result = Invoke-ChildPowerShell -ScriptPath $fixture.Script -Arguments @(
            '-JuceArchive', $fixture.Archive
        )

        Assert-True ($result.ExitCode -ne 0) 'materializer accepted a redirected SDK contract path'
        Assert-True ($result.Output -match 'rootRelativeToRepository.*approved') 'materializer did not reject the unapproved contract value'
        Assert-True (-not (Test-Path -LiteralPath $redirectedRoot)) 'materializer wrote to the redirected contract path'
        Assert-Equal 0 @([System.IO.Directory]::GetDirectories($fixture.SdkParent, '.apex-juce-*')).Count 'materializer created a temporary payload before path rejection'
    }
    finally {
        Remove-MaterializerFixture -Fixture $fixture
    }
} -Case 'ContractPath'

Invoke-Test 'atomic publication helper reports a deterministic destination collision' {
    $testRoot = Join-Path $repositoryRoot ".apex-debug\test-build-foundation\$([guid]::NewGuid().ToString('N'))"
    $preparedRoot = Join-Path $testRoot 'prepared-payload'
    $destinationRoot = Join-Path $testRoot 'racing-owner'
    $preparedFile = Join-Path $preparedRoot 'prepared.txt'
    $sentinel = Join-Path $destinationRoot 'racing-owner.txt'

    [System.IO.Directory]::CreateDirectory($preparedRoot) | Out-Null
    [System.IO.Directory]::CreateDirectory($destinationRoot) | Out-Null
    [System.IO.File]::WriteAllText($preparedFile, 'prepared payload')
    [System.IO.File]::WriteAllText($sentinel, 'owned by racing publisher')
    $destinationFingerprint = Get-DirectoryTreeFingerprint -Root $destinationRoot

    try {
        $failureMessage = $null
        try {
            Publish-ApexDirectoryAtomically -PreparedRoot $preparedRoot -DestinationRoot $destinationRoot
        }
        catch {
            $failureMessage = $_.Exception.Message
        }

        $expectedFailureMessage = "Refusing to publish payload because the destination appeared: $([System.IO.Path]::GetFullPath($destinationRoot))"
        Assert-Equal $expectedFailureMessage $failureMessage 'atomic publication did not report the exact destination-appeared collision'
        Assert-Equal $destinationFingerprint (Get-DirectoryTreeFingerprint -Root $destinationRoot) 'atomic publication clobbered or nested into the racing sentinel'
        Assert-Equal '<missing>' (Get-DirectoryTreeFingerprint -Root $preparedRoot) 'prepared payload survived collision cleanup'
        Assert-True (-not (Test-Path -LiteralPath $preparedFile)) 'prepared file survived collision cleanup'
        Assert-Equal 'owned by racing publisher' ([System.IO.File]::ReadAllText($sentinel)) 'racing sentinel content changed'
    }
    finally {
        if (Test-Path -LiteralPath $testRoot) {
            Remove-Item -LiteralPath $testRoot -Recurse -Force
        }
    }
} -Case 'PublicationRace'

Invoke-Test 'build isolates transient signing state from reusable MSBuild workers' {
    $tools = Get-ApexBuildTools
    $testRoot = Join-Path $repositoryRoot ".apex-debug\test-build-foundation\$([guid]::NewGuid().ToString('N'))"
    $fixtureScripts = Join-Path $testRoot 'Scripts'
    $fixtureBuildScript = Join-Path $fixtureScripts 'build_apex.ps1'
    $fixtureToolsScript = Join-Path $fixtureScripts 'apex_build_tools.ps1'
    $fixtureVerifyScript = Join-Path $fixtureScripts 'verify_dependencies.ps1'
    $rootProject = Join-Path $testRoot 'environment.proj'
    $probeSource = Join-Path $testRoot 'unsigned-probe.cs'
    $probeRoot = Join-Path $testRoot 'probes'
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    $workerCount = 4
    $environmentNames = @(
        'APEX_SKIP_SIGNING',
        'MSBUILDNODEHANDSHAKESALT',
        'MSBUILDDISABLENODEREUSE',
        'MSBUILDNOINPROCNODE'
    )
    $previousEnvironment = @{}
    $processEnvironment = [Environment]::GetEnvironmentVariables('Process')
    $testCompleted = $false
    $cleanupExitCode = $null

    foreach ($name in $environmentNames) {
        $previousEnvironment[$name] = [pscustomobject]@{
            WasSet = $processEnvironment.Contains($name)
            Value = [Environment]::GetEnvironmentVariable($name, 'Process')
        }
    }

    $invokeFixtureBuild = {
        param(
            [Parameter(Mandatory = $true)]
            [string] $Configuration,

            [switch] $Unsigned
        )

        $runner = [powershell]::Create()
        try {
            [void] $runner.AddCommand($fixtureBuildScript)
            [void] $runner.AddParameter('Configuration', $Configuration)
            if ($Unsigned) {
                [void] $runner.AddParameter('Unsigned')
            }

            $output = @($runner.Invoke())
            $diagnostics = @(
                $output | ForEach-Object { [string] $_ }
                $runner.Streams.Error | ForEach-Object { [string] $_ }
                $runner.Streams.Information | ForEach-Object { [string] $_.MessageData }
            )

            return [pscustomobject]@{
                State = [string] $runner.InvocationStateInfo.State
                Output = ($diagnostics -join [Environment]::NewLine)
            }
        }
        finally {
            $runner.Dispose()
        }
    }

    try {
        [System.IO.Directory]::CreateDirectory($fixtureScripts) | Out-Null
        [System.IO.Directory]::CreateDirectory($probeRoot) | Out-Null
        [System.IO.File]::Copy((Join-Path $PSScriptRoot 'build_apex.ps1'), $fixtureBuildScript)

        $debugOutputRoot = Join-Path $testRoot 'outputs\Debug'
        $releaseOutputRoot = Join-Path $testRoot 'outputs\Release'
        $fixtureToolsSource = @"
function Get-ApexBuildTools {
    return [pscustomobject]@{
        MSBuild = '$($tools.MSBuild.Replace("'", "''"))'
        Solution = '$($rootProject.Replace("'", "''"))'
        OutputPaths = @{
            Debug = [pscustomobject]@{
                Exe = '$((Join-Path $debugOutputRoot 'DAW_Core.exe').Replace("'", "''"))'
                Pdb = '$((Join-Path $debugOutputRoot 'DAW_Core.pdb').Replace("'", "''"))'
            }
            Release = [pscustomobject]@{
                Exe = '$((Join-Path $releaseOutputRoot 'DAW_Core.exe').Replace("'", "''"))'
                Pdb = '$((Join-Path $releaseOutputRoot 'DAW_Core.pdb').Replace("'", "''"))'
            }
        }
    }
}
"@
        [System.IO.File]::WriteAllText($fixtureToolsScript, $fixtureToolsSource, $utf8NoBom)
        [System.IO.File]::WriteAllText($fixtureVerifyScript, "exit 0`r`n", $utf8NoBom)
        [System.IO.File]::WriteAllText(
            $probeSource,
            "internal static class Program { private static void Main() { } }`r`n",
            $utf8NoBom
        )
        $workerItems = @(1..$workerCount | ForEach-Object {
            "    <WorkerProject Include=`"worker-$_.proj`" />"
        }) -join "`r`n"
        $rootProjectSource = @'
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
__WORKER_ITEMS__
    <Compile Include="$(MSBuildProjectDirectory)\unsigned-probe.cs" />
  </ItemGroup>
  <Target Name="Build">
    <MSBuild Projects="@(WorkerProject)" Targets="Probe" BuildInParallel="true"
             Properties="Configuration=$(Configuration)" />
    <PropertyGroup>
      <OutputRoot>$(MSBuildProjectDirectory)\outputs\$(Configuration)</OutputRoot>
    </PropertyGroup>
    <MakeDir Directories="$(OutputRoot)" />
    <Csc Sources="@(Compile)" OutputAssembly="$(OutputRoot)\DAW_Core.exe" TargetType="Exe" />
    <WriteLinesToFile File="$(OutputRoot)\DAW_Core.pdb" Lines="environment probe symbols"
                      Overwrite="true" Encoding="ASCII" />
  </Target>
  <Target Name="Rebuild" DependsOnTargets="Build" />
</Project>
'@
        $rootProjectSource = $rootProjectSource.Replace('__WORKER_ITEMS__', $workerItems)
        [System.IO.File]::WriteAllText($rootProject, $rootProjectSource, $utf8NoBom)

        $workerProjectSource = @'
<Project DefaultTargets="Probe" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <UsingTask TaskName="WriteApexEnvironmentProbe" TaskFactory="RoslynCodeTaskFactory"
             AssemblyFile="$(MSBuildToolsPath)\Microsoft.Build.Tasks.Core.dll">
    <ParameterGroup>
      <ProbePath ParameterType="System.String" Required="true" />
    </ParameterGroup>
    <Task>
      <Using Namespace="System" />
      <Using Namespace="System.Diagnostics" />
      <Using Namespace="System.IO" />
      <Code Type="Fragment" Language="cs"><![CDATA[
        string value = Environment.GetEnvironmentVariable("APEX_SKIP_SIGNING") ?? "<unset>";
        Process process = Process.GetCurrentProcess();
        File.WriteAllText(ProbePath, process.Id.ToString() + "|" + process.ProcessName + "|" + value);
      ]]></Code>
    </Task>
  </UsingTask>
  <Target Name="Probe">
    <WriteApexEnvironmentProbe ProbePath="$(MSBuildProjectDirectory)\probes\$(Configuration)-$(MSBuildProjectName).txt" />
  </Target>
</Project>
'@
        foreach ($index in 1..$workerCount) {
            [System.IO.File]::WriteAllText(
                (Join-Path $testRoot "worker-$index.proj"),
                $workerProjectSource,
                $utf8NoBom
            )
        }

        [Environment]::SetEnvironmentVariable('APEX_SKIP_SIGNING', $null, 'Process')
        [Environment]::SetEnvironmentVariable('MSBUILDDISABLENODEREUSE', $null, 'Process')
        [Environment]::SetEnvironmentVariable('MSBUILDNOINPROCNODE', '1', 'Process')
        [Environment]::SetEnvironmentVariable(
            'MSBUILDNODEHANDSHAKESALT',
            "apex-environment-$([guid]::NewGuid().ToString('N'))",
            'Process'
        )

        $unsignedResult = & $invokeFixtureBuild -Configuration 'Debug' -Unsigned
        Assert-Equal 'Completed' $unsignedResult.State "unsigned fixture build did not complete: $($unsignedResult.Output)"

        $debugRecords = @()
        foreach ($index in 1..$workerCount) {
            $probePath = Join-Path $probeRoot "Debug-worker-$index.txt"
            Assert-True (Test-Path -LiteralPath $probePath -PathType Leaf) "unsigned worker probe is missing: $probePath"
            $debugRecords += [System.IO.File]::ReadAllText($probePath)
        }
        $debugValues = @($debugRecords | ForEach-Object { ($_ -split '\|', 3)[2] })
        $debugProcessNames = @($debugRecords | ForEach-Object { ($_ -split '\|', 3)[1] })
        Assert-Equal 0 @($debugValues | Where-Object { $_ -cne '1' }).Count `
            "unsigned workers did not all observe APEX_SKIP_SIGNING=1: $($debugRecords -join ', ')"
        Assert-Equal 0 @($debugProcessNames | Where-Object { $_ -cne 'MSBuild' }).Count `
            "unsigned probes did not execute inside MSBuild worker processes: $($debugRecords -join ', ')"
        Assert-Equal $null ([Environment]::GetEnvironmentVariable('APEX_SKIP_SIGNING', 'Process')) `
            'build script did not restore the unset parent signing environment after unsigned MSBuild'

        $signedModeResult = & $invokeFixtureBuild -Configuration 'Release'
        Assert-Equal 'Completed' $signedModeResult.State "non-unsigned fixture build did not complete: $($signedModeResult.Output)"

        $releaseRecords = @()
        foreach ($index in 1..$workerCount) {
            $probePath = Join-Path $probeRoot "Release-worker-$index.txt"
            Assert-True (Test-Path -LiteralPath $probePath -PathType Leaf) "non-unsigned worker probe is missing: $probePath"
            $releaseRecords += [System.IO.File]::ReadAllText($probePath)
        }
        $releaseValues = @($releaseRecords | ForEach-Object { ($_ -split '\|', 3)[2] })
        $releaseProcessNames = @($releaseRecords | ForEach-Object { ($_ -split '\|', 3)[1] })
        Assert-Equal 0 @($releaseValues | Where-Object { $_ -cne '<unset>' }).Count `
            "non-unsigned workers retained stale APEX_SKIP_SIGNING: $($releaseRecords -join ', ')"
        Assert-Equal 0 @($releaseProcessNames | Where-Object { $_ -cne 'MSBuild' }).Count `
            "non-unsigned probes did not execute inside MSBuild worker processes: $($releaseRecords -join ', ')"
        Assert-Equal $null ([Environment]::GetEnvironmentVariable('APEX_SKIP_SIGNING', 'Process')) `
            'build script changed the parent signing environment after non-unsigned MSBuild'

        $buildSource = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'build_apex.ps1'))
        $nodeReuseArguments = [regex]::Matches($buildSource, '(?m)^\s*[''"]/nr:false[''"],\s*$')
        Assert-Equal 1 $nodeReuseArguments.Count `
            "build script does not pass the exact /nr:false MSBuild argument once; unsigned workers: $($debugRecords -join ', '); non-unsigned workers: $($releaseRecords -join ', ')"
        $testCompleted = $true
    }
    finally {
        [Environment]::SetEnvironmentVariable('APEX_SKIP_SIGNING', $null, 'Process')
        if (Test-Path -LiteralPath $rootProject -PathType Leaf) {
            $previousErrorActionPreference = $ErrorActionPreference
            try {
                $ErrorActionPreference = 'Continue'
                & $tools.MSBuild $rootProject '/t:Build' '/p:Configuration=Cleanup' '/m' '/nr:false' '/nologo' '/v:quiet' 2>&1 | Out-Null
                $cleanupExitCode = $LASTEXITCODE
            }
            finally {
                $ErrorActionPreference = $previousErrorActionPreference
            }
        }

        foreach ($entry in $previousEnvironment.GetEnumerator()) {
            if ($entry.Value.WasSet) {
                [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value.Value, 'Process')
            }
            else {
                [Environment]::SetEnvironmentVariable($entry.Key, $null, 'Process')
            }
        }

        if (Test-Path -LiteralPath $testRoot) {
            Remove-Item -LiteralPath $testRoot -Recurse -Force
        }
    }

    if ($testCompleted) {
        Assert-Equal 0 $cleanupExitCode 'MSBuild worker cleanup invocation failed'
    }
    foreach ($entry in $previousEnvironment.GetEnumerator()) {
        $actualValue = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
        if ($entry.Value.WasSet) {
            Assert-Equal $entry.Value.Value $actualValue "test harness did not restore parent $($entry.Key)"
        }
        else {
            Assert-Equal $null $actualValue "test harness left parent $($entry.Key) set"
        }
    }
} -Case 'MSBuildEnvironment'

Invoke-Test 'dependency verifier is isolated from inherited Git repository-selection environment' {
    $testRoot = Join-Path $repositoryRoot ".apex-debug\test-build-foundation\$([guid]::NewGuid().ToString('N'))"
    $externalRoot = Join-Path $testRoot 'external-owner'
    $externalGitDir = Join-Path $externalRoot '.git'
    $childScript = Join-Path $testRoot 'invoke-verifier-child.ps1'
    $evidencePath = Join-Path $testRoot 'verifier-evidence.json'
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    $previousEnvironment = @{}
    $environmentPoisoned = $false

    try {
        [System.IO.Directory]::CreateDirectory($externalRoot) | Out-Null

        & git -C $externalRoot init --quiet
        Assert-Equal 0 $LASTEXITCODE 'could not initialize external poisoning target repository'

        [System.IO.File]::WriteAllText(
            $childScript,
            @'
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $VerifierScript,

    [Parameter(Mandatory = $true)]
    [string] $EvidencePath
)

$ErrorActionPreference = 'Stop'

$savedGitEnvironment = @{}
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

foreach ($name in @($gitEnvironmentNames | Sort-Object -Unique)) {
    $savedGitEnvironment[$name] = [pscustomobject]@{
        WasSet = $processEnvironment.Contains($name)
        Value = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    [Environment]::SetEnvironmentVariable($name, $null, 'Process')
}

$operationError = $null
try {
    & $VerifierScript
}
catch {
    $operationError = $_.Exception.Message
}

foreach ($name in $savedGitEnvironment.Keys) {
    $saved = $savedGitEnvironment[$name]
    if ($saved.WasSet) {
        [Environment]::SetEnvironmentVariable($name, $saved.Value, 'Process')
    }
    else {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
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

        try {
            $verifierResult = Invoke-ChildPowerShell -ScriptPath $childScript -Arguments @(
                '-VerifierScript', (Join-Path $PSScriptRoot 'verify_dependencies.ps1'),
                '-EvidencePath', $evidencePath
            )
        }
        finally {
            foreach ($entry in $previousEnvironment.GetEnumerator()) {
                if ($entry.Value.WasSet) {
                    [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value.Value, 'Process')
                }
                else {
                    [Environment]::SetEnvironmentVariable($entry.Key, $null, 'Process')
                }
            }
            $environmentPoisoned = $false
        }

        Assert-Equal 0 $verifierResult.ExitCode "verifier failed under poisoned environment: $($verifierResult.Output)"
        Assert-True (Test-Path -LiteralPath $evidencePath -PathType Leaf) 'verifier did not emit restoration evidence'

        $evidence = Get-Content -LiteralPath $evidencePath -Raw | ConvertFrom-Json
        Assert-True ([int] $evidence.ProcessId -ne $PID) 'verifier did not run in a child process'
        Assert-Equal $externalGitDir $evidence.GitDir 'child process did not restore GIT_DIR'
        Assert-Equal $externalRoot $evidence.GitWorkTree 'child process did not restore GIT_WORK_TREE'
        Assert-Equal (Join-Path $externalRoot 'poisoned-gitconfig') $evidence.GitConfigSystem 'child process did not restore GIT_CONFIG_SYSTEM'
        Assert-Equal (Join-Path $externalRoot 'poisoned-gitconfig') $evidence.GitConfigGlobal 'child process did not restore GIT_CONFIG_GLOBAL'

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
        if ($environmentPoisoned) {
            foreach ($name in $previousEnvironment.Keys) {
                $saved = $previousEnvironment[$name]
                if ($saved.WasSet) {
                    [Environment]::SetEnvironmentVariable($name, $saved.Value, 'Process')
                }
                else {
                    [Environment]::SetEnvironmentVariable($name, $null, 'Process')
                }
            }
        }

        if (Test-Path -LiteralPath $testRoot) {
            Remove-Item -LiteralPath $testRoot -Recurse -Force
        }
    }
} -Case 'VerifierEnvironment'

Invoke-Test 'build script rejects invalid configuration before MSBuild' {
    $tools = Get-ApexBuildTools
    $beforeOutputs = @{}

    foreach ($configuration in @('Debug', 'Release')) {
        $beforeOutputs["$configuration-Exe"] = Get-FileFingerprint -Path $tools.OutputPaths[$configuration].Exe
        $beforeOutputs["$configuration-Pdb"] = Get-FileFingerprint -Path $tools.OutputPaths[$configuration].Pdb
    }

    $result = Invoke-ChildPowerShell -ScriptPath (Join-Path $PSScriptRoot 'build_apex.ps1') -Arguments @(
        '-Configuration', 'InvalidConfiguration', '-Rebuild', '-Unsigned'
    )

    Assert-True ($result.ExitCode -ne 0) 'build script accepted an invalid configuration'
    Assert-True ($result.Output -match 'Configuration') 'invalid configuration failure did not identify the parameter'
    Assert-True ($result.Output -notmatch '\[build\] Invoking MSBuild') 'MSBuild invocation began before configuration rejection'

    foreach ($configuration in @('Debug', 'Release')) {
        Assert-Equal $beforeOutputs["$configuration-Exe"] (Get-FileFingerprint -Path $tools.OutputPaths[$configuration].Exe) "$configuration EXE changed during invalid build request"
        Assert-Equal $beforeOutputs["$configuration-Pdb"] (Get-FileFingerprint -Path $tools.OutputPaths[$configuration].Pdb) "$configuration PDB changed during invalid build request"
    }
}

Invoke-Test 'APEX_SKIP_SIGNING exits before certificate or target inspection' {
    $missingTarget = Join-Path $repositoryRoot '.apex-debug\test-build-foundation\target-must-not-exist.exe'
    $previousSkipSigning = $env:APEX_SKIP_SIGNING

    try {
        $env:APEX_SKIP_SIGNING = '1'
        $result = Invoke-ChildPowerShell -ScriptPath (Join-Path $PSScriptRoot 'sign_build.ps1') -Arguments @(
            '-ExePath', $missingTarget
        )
    }
    finally {
        if ($null -eq $previousSkipSigning) {
            Remove-Item Env:APEX_SKIP_SIGNING -ErrorAction SilentlyContinue
        }
        else {
            $env:APEX_SKIP_SIGNING = $previousSkipSigning
        }
    }

    Assert-Equal 0 $result.ExitCode 'unsigned signing bypass did not exit successfully'
    Assert-True ($result.Output -match 'APEX_SKIP_SIGNING=1 - unsigned build requested') 'unsigned signing bypass message was not emitted'
    Assert-True ($result.Output -notmatch 'Target not found') 'signing script inspected the target before bypassing'
    Assert-True ($result.Output -notmatch 'Creating self-signed|No dev certificate found|Signing .+\.exe') 'signing script reached certificate or signing work'
}

Write-Host "[PASS] build foundation ($script:passed tests)"
exit 0
