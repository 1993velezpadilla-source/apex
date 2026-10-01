$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'apex_build_tools.ps1')
. (Join-Path $PSScriptRoot 'juce_patch_tools.ps1')

$savedTopLevelGitEnvironment = @{}
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
    $savedTopLevelGitEnvironment[$name] = [pscustomobject]@{
        WasSet = $processEnvironment.Contains($name)
        Value = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    [Environment]::SetEnvironmentVariable($name, $null, 'Process')
}

try {

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
            Output = ($output -join "`n")
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

$repositoryRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
$contractPath = Join-Path $repositoryRoot 'Dependencies\apex-windows-dependencies.json'
$failures = New-Object 'System.Collections.Generic.List[string]'

$expected = [ordered]@{
    SchemaVersion = 1
    Platform = 'windows-x64'
    LanguageStandard = 'c++17'
    VisualStudioMajor = 18
    PlatformToolset = 'v145'
    WindowsSdkProperty = '10.0'
    JuceVersion = '8.0.12'
    JuceUpstreamCommit = '29396c22c93392d6738e021b83196283d6e4d850'
    JuceArchiveFileName = 'juce-8.0.12-windows.zip'
    JuceArchiveSha256 = '15B4ED8302127138DFEE98E61A2B5AE1481B39649B139E02E77BFC29462B7EFB'
    JuceRoot = '../Sdk setups/juce-8.0.12-windows/JUCE'
    JucePatchPath = 'Dependencies/patches/juce-8.0.12/0001-apex-asio4all-compatibility.patch'
    JucePatchSha256 = '88C03F5BE28A026FC074B51A3E0E9E57840C57435ECEEE700235B0BEF77E257E'
    JucePatchTarget = 'JUCE/modules/juce_audio_devices/native/juce_ASIO_windows.cpp'
    JucePristineTargetSha256 = 'A681161DEEDCBC341C4649C5A35EDE4847FFDA8B49AB9A0ED43878DB22A51E14'
    JucePatchedTargetSha256 = '93355E3422CB4C329823FD980D01CCB4F6315809D118C6463204CD893BAADD10'
    JucePatchValidationStatus = 'provisional-hardware-validation-required'
    JucePatchRationale = 'Preserves the investigated APEX ASIO4ALL zero-buffer and delayed-callback compatibility behavior pending the required hardware matrix.'
    JucePatch2Path = 'Dependencies/patches/juce-8.0.12/0002-apex-vst3-null-editcontroller-guard.patch'
    JucePatch2Sha256 = '04209CF9DF38633A9667D63B3E34A90E9B400B7C21486BF3A01EA794BEA0FD32'
    JucePatch2Target = 'JUCE/modules/juce_audio_processors_headless/format_types/juce_VST3PluginFormatImpl.h'
    JucePatch2PristineTargetSha256 = 'DC03C6BD92E1373C3EE0B183847B867FD1A5A131DF6A7C46FCEA0DFE2BFDAD64'
    JucePatch2PatchedTargetSha256 = '7209A49408B06E5CFF8B413420A42ABC13C89B4FE476DD2915413BB3973B9562'
    JucePatch2ValidationStatus = 'verified-by-audit-2026-07-27'
    JucePatch2Rationale = 'Guards against null editController in VST3 plugin view creation. Prevents AV crash when plugins like Antares Auto-Tune EFX+ fail to fully initialise.'
    JucePatch3Path = 'Dependencies/patches/juce-8.0.12/0003-apex-string-null-holder-guard.patch'
    JucePatch3Sha256 = 'F2E8617B9907E5206E7393557E42EA1B782B296A883C25E59958CB64DA83C029'
    JucePatch3Target = 'JUCE/modules/juce_core/text/juce_String.cpp'
    JucePatch3PristineTargetSha256 = '8FEDFC941C4E1073EA096B6AFE06118D42571294FAEF96015278B00C8F67A047'
    JucePatch3PatchedTargetSha256 = 'F8F8D9A78459808B292E58BBB00F9AC8B3AF08A177536611E8E61C0BBD804852'
    JucePatch3ValidationStatus = 'verified-by-cdb-2026-07-28'
    JucePatch3Rationale = 'Prevents null String holder pointers from being converted into invalid buffer headers during retain or release after observed lifetime corruption.'
    StretchUrl = 'https://github.com/Signalsmith-Audio/signalsmith-stretch.git'
    StretchCommit = '57b93f4e9206a089a45387eaa39bdc9f310d3308'
    StretchPath = 'Source/ThirdParty/signalsmith-stretch'
    LinearUrl = 'https://github.com/Signalsmith-Audio/linear.git'
    LinearCommit = '88c701ce8d581946de5ee587848cde4a572ed6b5'
    LinearPath = 'Source/ThirdParty/signalsmith-linear'
}

function Add-DependencyFailure {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Message
    )

    $script:failures.Add($Message)
}

function Test-PinnedValue {
    param(
        [AllowNull()]
        $Actual,

        [AllowNull()]
        $ExpectedValue,

        [Parameter(Mandatory = $true)]
        [string] $Label
    )

    if ([string] $Actual -cne [string] $ExpectedValue) {
        Add-DependencyFailure "$Label is '$Actual'; expected '$ExpectedValue'."
    }
}

function Test-SubmoduleIdentity {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Label,

        [Parameter(Mandatory = $true)]
        [string] $RelativePath,

        [Parameter(Mandatory = $true)]
        [string] $Commit,

        [Parameter(Mandatory = $true)]
        [string] $Url
    )

    $absolutePath = Join-Path $repositoryRoot $RelativePath
    if (-not (Test-Path -LiteralPath $absolutePath -PathType Container)) {
        Add-DependencyFailure "$Label is missing from declared path: $RelativePath"
        return
    }

    $gitHead = Invoke-GitIsolated -RepositoryPath $absolutePath -Arguments @('rev-parse', 'HEAD')
    $head = $gitHead.Output.Trim()
    if ($gitHead.ExitCode -ne 0) {
        Add-DependencyFailure "$Label is not a readable Git worktree: $RelativePath"
        return
    }
    if ($head -cne $Commit) {
        Add-DependencyFailure "$Label HEAD is '$head'; expected '$Commit'."
    }

    $gitOrigin = Invoke-GitIsolated -RepositoryPath $absolutePath -Arguments @('remote', 'get-url', 'origin')
    $origin = $gitOrigin.Output.Trim()
    if ($gitOrigin.ExitCode -ne 0 -or $origin -cne $Url) {
        Add-DependencyFailure "$Label origin is '$origin'; expected '$Url'."
    }

    $gitStatus = Invoke-GitIsolated -RepositoryPath $absolutePath -Arguments @('status', '--porcelain', '--untracked-files=no')
    $trackedChanges = @($gitStatus.Output -split "`n" | Where-Object { $_ })
    if ($gitStatus.ExitCode -ne 0 -or $trackedChanges.Count -ne 0) {
        Add-DependencyFailure "$Label has tracked worktree changes."
    }

    $gitStage = Invoke-GitIsolated -RepositoryPath $repositoryRoot -Arguments @('ls-files', '--stage', '--', $RelativePath)
    $stageEntry = $gitStage.Output.Trim()
    $expectedStagePrefix = "160000 $Commit 0"
    if (-not $stageEntry.StartsWith($expectedStagePrefix, [System.StringComparison]::Ordinal)) {
        Add-DependencyFailure "$Label gitlink is not pinned to '$Commit'."
    }

    $gitConfig = Invoke-GitIsolated -RepositoryPath $repositoryRoot -Arguments @('config', '--file', '.gitmodules', '--get-regexp', '^submodule\..*\.path$')
    $modulePathEntries = @($gitConfig.Output -split "`n" | Where-Object { $_ })
    $matchingEntry = @(
        $modulePathEntries | Where-Object { ([string] $_) -match "\s$([regex]::Escape($RelativePath))$" }
    )
    if ($matchingEntry.Count -ne 1) {
        Add-DependencyFailure "$Label does not have exactly one .gitmodules path entry."
        return
    }

    $pathKey = (([string] $matchingEntry[0]) -split '\s+', 2)[0]
    $urlKey = $pathKey -replace '\.path$', '.url'
    $gitModuleUrl = Invoke-GitIsolated -RepositoryPath $repositoryRoot -Arguments @('config', '--file', '.gitmodules', '--get', $urlKey)
    $moduleUrl = $gitModuleUrl.Output.Trim()
    if ($gitModuleUrl.ExitCode -ne 0 -or $moduleUrl -cne $Url) {
        Add-DependencyFailure "$Label .gitmodules URL is '$moduleUrl'; expected '$Url'."
    }
}

if (-not (Test-Path -LiteralPath $contractPath -PathType Leaf)) {
    Add-DependencyFailure "Dependency contract is missing: $contractPath"
}
else {
    try {
        $contract = Get-Content -LiteralPath $contractPath -Raw | ConvertFrom-Json
        Test-PinnedValue $contract.schemaVersion $expected.SchemaVersion 'schemaVersion'
        Test-PinnedValue $contract.platform $expected.Platform 'platform'
        Test-PinnedValue $contract.languageStandard $expected.LanguageStandard 'languageStandard'
        Test-PinnedValue $contract.toolchain.visualStudioMajor $expected.VisualStudioMajor 'toolchain.visualStudioMajor'
        Test-PinnedValue $contract.toolchain.platformToolset $expected.PlatformToolset 'toolchain.platformToolset'
        Test-PinnedValue $contract.toolchain.windowsSdkProperty $expected.WindowsSdkProperty 'toolchain.windowsSdkProperty'
        Test-PinnedValue $contract.juce.version $expected.JuceVersion 'juce.version'
        Test-PinnedValue $contract.juce.upstreamCommit $expected.JuceUpstreamCommit 'juce.upstreamCommit'
        Test-PinnedValue $contract.juce.archiveFileName $expected.JuceArchiveFileName 'juce.archiveFileName'
        Test-PinnedValue $contract.juce.archiveSha256 $expected.JuceArchiveSha256 'juce.archiveSha256'
        Test-PinnedValue $contract.juce.rootRelativeToRepository $expected.JuceRoot 'juce.rootRelativeToRepository'
        Test-PinnedValue $contract.juce.compatibilityPatch.relativePath $expected.JucePatchPath 'juce.compatibilityPatch.relativePath'
        Test-PinnedValue $contract.juce.compatibilityPatch.sha256 $expected.JucePatchSha256 'juce.compatibilityPatch.sha256'
        Test-PinnedValue $contract.juce.compatibilityPatch.targetRelativePath $expected.JucePatchTarget 'juce.compatibilityPatch.targetRelativePath'
        Test-PinnedValue $contract.juce.compatibilityPatch.pristineTargetSha256 $expected.JucePristineTargetSha256 'juce.compatibilityPatch.pristineTargetSha256'
        Test-PinnedValue $contract.juce.compatibilityPatch.patchedTargetSha256 $expected.JucePatchedTargetSha256 'juce.compatibilityPatch.patchedTargetSha256'
        Test-PinnedValue $contract.juce.compatibilityPatch.validationStatus $expected.JucePatchValidationStatus 'juce.compatibilityPatch.validationStatus'
        Test-PinnedValue $contract.juce.compatibilityPatch.rationale $expected.JucePatchRationale 'juce.compatibilityPatch.rationale'
        if ($contract.juce.compatibilityPatch.hardwareValidationRequired -isnot [bool] -or
            -not [bool] $contract.juce.compatibilityPatch.hardwareValidationRequired) {
            Add-DependencyFailure 'juce.compatibilityPatch.hardwareValidationRequired must be true.'
        }
        Test-PinnedValue $contract.juce.compatibilityPatch2.relativePath $expected.JucePatch2Path 'juce.compatibilityPatch2.relativePath'
        Test-PinnedValue $contract.juce.compatibilityPatch2.sha256 $expected.JucePatch2Sha256 'juce.compatibilityPatch2.sha256'
        Test-PinnedValue $contract.juce.compatibilityPatch2.targetRelativePath $expected.JucePatch2Target 'juce.compatibilityPatch2.targetRelativePath'
        Test-PinnedValue $contract.juce.compatibilityPatch2.pristineTargetSha256 $expected.JucePatch2PristineTargetSha256 'juce.compatibilityPatch2.pristineTargetSha256'
        Test-PinnedValue $contract.juce.compatibilityPatch2.patchedTargetSha256 $expected.JucePatch2PatchedTargetSha256 'juce.compatibilityPatch2.patchedTargetSha256'
        Test-PinnedValue $contract.juce.compatibilityPatch2.validationStatus $expected.JucePatch2ValidationStatus 'juce.compatibilityPatch2.validationStatus'
        Test-PinnedValue $contract.juce.compatibilityPatch2.rationale $expected.JucePatch2Rationale 'juce.compatibilityPatch2.rationale'
        if ($contract.juce.compatibilityPatch2.hardwareValidationRequired -isnot [bool] -or
            [bool] $contract.juce.compatibilityPatch2.hardwareValidationRequired) {
            Add-DependencyFailure 'juce.compatibilityPatch2.hardwareValidationRequired must be false.'
        }
        Test-PinnedValue $contract.juce.compatibilityPatch3.relativePath $expected.JucePatch3Path 'juce.compatibilityPatch3.relativePath'
        Test-PinnedValue $contract.juce.compatibilityPatch3.sha256 $expected.JucePatch3Sha256 'juce.compatibilityPatch3.sha256'
        Test-PinnedValue $contract.juce.compatibilityPatch3.targetRelativePath $expected.JucePatch3Target 'juce.compatibilityPatch3.targetRelativePath'
        Test-PinnedValue $contract.juce.compatibilityPatch3.pristineTargetSha256 $expected.JucePatch3PristineTargetSha256 'juce.compatibilityPatch3.pristineTargetSha256'
        Test-PinnedValue $contract.juce.compatibilityPatch3.patchedTargetSha256 $expected.JucePatch3PatchedTargetSha256 'juce.compatibilityPatch3.patchedTargetSha256'
        Test-PinnedValue $contract.juce.compatibilityPatch3.validationStatus $expected.JucePatch3ValidationStatus 'juce.compatibilityPatch3.validationStatus'
        Test-PinnedValue $contract.juce.compatibilityPatch3.rationale $expected.JucePatch3Rationale 'juce.compatibilityPatch3.rationale'
        if ($contract.juce.compatibilityPatch3.hardwareValidationRequired -isnot [bool] -or
            [bool] $contract.juce.compatibilityPatch3.hardwareValidationRequired) {
            Add-DependencyFailure 'juce.compatibilityPatch3.hardwareValidationRequired must be false.'
        }
        Test-PinnedValue $contract.signalsmithStretch.url $expected.StretchUrl 'signalsmithStretch.url'
        Test-PinnedValue $contract.signalsmithStretch.commit $expected.StretchCommit 'signalsmithStretch.commit'
        Test-PinnedValue $contract.signalsmithStretch.path $expected.StretchPath 'signalsmithStretch.path'
        Test-PinnedValue $contract.signalsmithLinear.url $expected.LinearUrl 'signalsmithLinear.url'
        Test-PinnedValue $contract.signalsmithLinear.commit $expected.LinearCommit 'signalsmithLinear.commit'
        Test-PinnedValue $contract.signalsmithLinear.path $expected.LinearPath 'signalsmithLinear.path'
    }
    catch {
        Add-DependencyFailure "Dependency contract is not valid JSON: $($_.Exception.Message)"
    }
}

$approvedSdkParent = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot '..\Sdk setups')
).TrimEnd('\')
try {
    Assert-ApexPayloadPathNotReparsePoint -Path $approvedSdkParent
    if (-not (Test-Path -LiteralPath $approvedSdkParent -PathType Container)) {
        throw "Approved SDK parent is missing: $approvedSdkParent"
    }
}
catch {
    Add-DependencyFailure "Approved SDK parent validation failed: $($_.Exception.Message)"
    foreach ($failure in $failures) {
        Write-Host "[FAIL] $failure"
    }

    Write-Host "[FAIL] dependency verification failed ($($failures.Count) issue(s))"
    exit 1
}

$juceRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $expected.JuceRoot))
$standardHeader = Join-Path $juceRoot 'modules\juce_core\system\juce_StandardHeader.h'
$projucer = Join-Path $juceRoot 'Projucer.exe'

if (-not (Test-Path -LiteralPath $standardHeader -PathType Leaf)) {
    Add-DependencyFailure "JUCE StandardHeader is missing: $standardHeader"
}
else {
    $headerText = [System.IO.File]::ReadAllText($standardHeader)
    if ($headerText -notmatch '(?m)^\s*#define\s+JUCE_MAJOR_VERSION\s+8\s*$' -or
        $headerText -notmatch '(?m)^\s*#define\s+JUCE_MINOR_VERSION\s+0\s*$' -or
        $headerText -notmatch '(?m)^\s*#define\s+JUCE_BUILDNUMBER\s+12\s*$') {
        Add-DependencyFailure 'JUCE StandardHeader does not identify version 8.0.12.'
    }
}

if (-not (Test-Path -LiteralPath $projucer -PathType Leaf)) {
    Add-DependencyFailure "Pinned Projucer is missing: $projucer"
}

$archiveIdentityVerified = $false
$archivePath = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot "..\Sdk setups\$($expected.JuceArchiveFileName)")
)
if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    Add-DependencyFailure "Pinned JUCE archive is required for complete payload verification: $archivePath"
}
else {
    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    if ($archiveHash -cne $expected.JuceArchiveSha256) {
        Add-DependencyFailure "JUCE archive SHA-256 is '$archiveHash'; expected '$($expected.JuceArchiveSha256)'."
    }
    else {
        $archiveIdentityVerified = $true
    }
}

$patchIdentityVerified = $false
$patchPath = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $expected.JucePatchPath))
$repositoryPrefix = $repositoryRoot + [System.IO.Path]::DirectorySeparatorChar
if (-not $patchPath.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    Add-DependencyFailure "JUCE compatibility patch resolves outside the repository: $patchPath"
}
elseif (-not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
    Add-DependencyFailure "JUCE compatibility patch is missing: $patchPath"
}
else {
    $patchHash = (Get-FileHash -LiteralPath $patchPath -Algorithm SHA256).Hash
    if ($patchHash -cne $expected.JucePatchSha256) {
        Add-DependencyFailure "JUCE compatibility patch SHA-256 is '$patchHash'; expected '$($expected.JucePatchSha256)'."
    }
    else {
        $patchIdentityVerified = $true
    }
}

$patch2IdentityVerified = $false
$patch2Path = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $expected.JucePatch2Path))
if (-not $patch2Path.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    Add-DependencyFailure "JUCE compatibility patch 2 resolves outside the repository: $patch2Path"
}
elseif (-not (Test-Path -LiteralPath $patch2Path -PathType Leaf)) {
    Add-DependencyFailure "JUCE compatibility patch 2 is missing: $patch2Path"
}
else {
    $patch2Hash = (Get-FileHash -LiteralPath $patch2Path -Algorithm SHA256).Hash
    if ($patch2Hash -cne $expected.JucePatch2Sha256) {
        Add-DependencyFailure "JUCE compatibility patch 2 SHA-256 is '$patch2Hash'; expected '$($expected.JucePatch2Sha256)'."
    }
    else {
        $patch2IdentityVerified = $true
    }
}

$patch3IdentityVerified = $false
$patch3Path = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $expected.JucePatch3Path))
if (-not $patch3Path.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    Add-DependencyFailure "JUCE compatibility patch 3 resolves outside the repository: $patch3Path"
}
elseif (-not (Test-Path -LiteralPath $patch3Path -PathType Leaf)) {
    Add-DependencyFailure "JUCE compatibility patch 3 is missing: $patch3Path"
}
else {
    $patch3Hash = (Get-FileHash -LiteralPath $patch3Path -Algorithm SHA256).Hash
    if ($patch3Hash -cne $expected.JucePatch3Sha256) {
        Add-DependencyFailure "JUCE compatibility patch 3 SHA-256 is '$patch3Hash'; expected '$($expected.JucePatch3Sha256)'."
    }
    else {
        $patch3IdentityVerified = $true
    }
}

$installationRoot = Split-Path -Parent $juceRoot
try {
    Assert-ApexPayloadPathNotReparsePoint -Path $installationRoot
}
catch {
    $nativeError = $_.Exception
    while ($null -ne $nativeError.InnerException) {
        $nativeError = $nativeError.InnerException
    }

    if ($nativeError -is [System.ComponentModel.Win32Exception] -and
        $nativeError.NativeErrorCode -in @(2, 3)) {
        Add-DependencyFailure "JUCE installation root is missing: $installationRoot"
    }
    else {
        Add-DependencyFailure "JUCE installation root validation failed: $($_.Exception.Message)"
    }
}

if ($archiveIdentityVerified -and $patchIdentityVerified -and $patch2IdentityVerified -and $patch3IdentityVerified -and (Test-Path -LiteralPath $installationRoot -PathType Container)) {
    $installationParent = Split-Path -Parent $installationRoot
    $temporaryRoot = Join-Path $installationParent ".apex-juce-verify-$([guid]::NewGuid().ToString('N'))"

    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $temporaryRoot)
        Invoke-JuceVerifiedPatch `
            -Root $temporaryRoot `
            -PatchPath $patchPath `
            -TargetRelativePath $expected.JucePatchTarget `
            -PristineTargetSha256 $expected.JucePristineTargetSha256 `
            -PatchedTargetSha256 $expected.JucePatchedTargetSha256
        Invoke-JuceVerifiedPatch `
            -Root $temporaryRoot `
            -PatchPath $patch2Path `
            -TargetRelativePath $expected.JucePatch2Target `
            -PristineTargetSha256 $expected.JucePatch2PristineTargetSha256 `
            -PatchedTargetSha256 $expected.JucePatch2PatchedTargetSha256
        Invoke-JuceVerifiedPatch `
            -Root $temporaryRoot `
            -PatchPath $patch3Path `
            -TargetRelativePath $expected.JucePatch3Target `
            -PristineTargetSha256 $expected.JucePatch3PristineTargetSha256 `
            -PatchedTargetSha256 $expected.JucePatch3PatchedTargetSha256
        $difference = $null
        if (-not (Test-ApexDirectoryPayloadEquivalent -ReferenceRoot $temporaryRoot -CandidateRoot $installationRoot -Difference ([ref] $difference))) {
            Add-DependencyFailure "JUCE installation payload does not match the pinned archive-plus-patch payload: $difference"
        }
    }
    catch {
        Add-DependencyFailure "JUCE installation payload verification failed: $($_.Exception.Message)"
    }
    finally {
        if (Test-Path -LiteralPath $temporaryRoot) {
            Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
        }
    }
}

Test-SubmoduleIdentity -Label 'signalsmith-stretch' -RelativePath $expected.StretchPath -Commit $expected.StretchCommit -Url $expected.StretchUrl
Test-SubmoduleIdentity -Label 'signalsmith-linear' -RelativePath $expected.LinearPath -Commit $expected.LinearCommit -Url $expected.LinearUrl

$shadowLinear = Join-Path $repositoryRoot "$($expected.StretchPath)\signalsmith-linear"
if (Test-Path -LiteralPath $shadowLinear) {
    Add-DependencyFailure "Nested Linear shadow path must be absent: $($expected.StretchPath)/signalsmith-linear"
}

$gitStretch2 = Invoke-GitIsolated -RepositoryPath $repositoryRoot -Arguments @('ls-files', '--stage', '--', 'Source/ThirdParty/signalsmith-stretch2')
$stretch2Stage = $gitStretch2.Output.Trim()
if ($stretch2Stage) {
    Add-DependencyFailure 'Source/ThirdParty/signalsmith-stretch2 remains in the Git index.'
}

$jucerPath = Join-Path $repositoryRoot 'DAW_Core.jucer'
if (-not (Test-Path -LiteralPath $jucerPath -PathType Leaf)) {
    Add-DependencyFailure "Projucer project is missing: $jucerPath"
}
elseif ([System.IO.File]::ReadAllText($jucerPath) -match 'PluginEditorWindowWin32Patch\.h') {
    Add-DependencyFailure 'DAW_Core.jucer still references PluginEditorWindowWin32Patch.h.'
}

if ($failures.Count -ne 0) {
    foreach ($failure in $failures) {
        Write-Host "[FAIL] $failure"
    }

    Write-Host "[FAIL] dependency verification failed ($($failures.Count) issue(s))"
    exit 1
}

Write-Host '[PASS] pinned Windows dependencies verified'
exit 0

}
finally {
    foreach ($name in $savedTopLevelGitEnvironment.Keys) {
        $saved = $savedTopLevelGitEnvironment[$name]
        if ($saved.WasSet) {
            [Environment]::SetEnvironmentVariable($name, $saved.Value, 'Process')
        }
        else {
            [Environment]::SetEnvironmentVariable($name, $null, 'Process')
        }
    }
}
