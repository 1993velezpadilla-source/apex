[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string] $JuceArchive
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'apex_build_tools.ps1')
. (Join-Path $PSScriptRoot 'juce_patch_tools.ps1')

$repositoryRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
$contractPath = Join-Path $repositoryRoot 'Dependencies\apex-windows-dependencies.json'
$expectedArchiveHash = '15B4ED8302127138DFEE98E61A2B5AE1481B39649B139E02E77BFC29462B7EFB'
$expectedArchiveFileName = 'juce-8.0.12-windows.zip'
$expectedJuceVersion = '8.0.12'
$expectedUpstreamCommit = '29396c22c93392d6738e021b83196283d6e4d850'
$expectedRootRelativeToRepository = '../Sdk setups/juce-8.0.12-windows/JUCE'
$expectedPatchRelativePath = 'Dependencies/patches/juce-8.0.12/0001-apex-asio4all-compatibility.patch'
$expectedPatchHash = '88C03F5BE28A026FC074B51A3E0E9E57840C57435ECEEE700235B0BEF77E257E'
$expectedPatchTargetRelativePath = 'JUCE/modules/juce_audio_devices/native/juce_ASIO_windows.cpp'
$expectedPristineTargetHash = 'A681161DEEDCBC341C4649C5A35EDE4847FFDA8B49AB9A0ED43878DB22A51E14'
$expectedPatchedTargetHash = '93355E3422CB4C329823FD980D01CCB4F6315809D118C6463204CD893BAADD10'
$expectedPatchValidationStatus = 'provisional-hardware-validation-required'
$expectedPatchRationale = 'Preserves the investigated APEX ASIO4ALL zero-buffer and delayed-callback compatibility behavior pending the required hardware matrix.'
$expectedPatch2RelativePath = 'Dependencies/patches/juce-8.0.12/0002-apex-vst3-null-editcontroller-guard.patch'
$expectedPatch2Hash = '04209CF9DF38633A9667D63B3E34A90E9B400B7C21486BF3A01EA794BEA0FD32'
$expectedPatch2TargetRelativePath = 'JUCE/modules/juce_audio_processors_headless/format_types/juce_VST3PluginFormatImpl.h'
$expectedPatch2PristineTargetHash = 'DC03C6BD92E1373C3EE0B183847B867FD1A5A131DF6A7C46FCEA0DFE2BFDAD64'
$expectedPatch2PatchedTargetHash = '7209A49408B06E5CFF8B413420A42ABC13C89B4FE476DD2915413BB3973B9562'
$expectedPatch2ValidationStatus = 'verified-by-audit-2026-07-27'
$expectedPatch2Rationale = 'Guards against null editController in VST3 plugin view creation. Prevents AV crash when plugins like Antares Auto-Tune EFX+ fail to fully initialise.'
$expectedPatch3RelativePath = 'Dependencies/patches/juce-8.0.12/0003-apex-string-null-holder-guard.patch'
$expectedPatch3Hash = 'F2E8617B9907E5206E7393557E42EA1B782B296A883C25E59958CB64DA83C029'
$expectedPatch3TargetRelativePath = 'JUCE/modules/juce_core/text/juce_String.cpp'
$expectedPatch3PristineTargetHash = '8FEDFC941C4E1073EA096B6AFE06118D42571294FAEF96015278B00C8F67A047'
$expectedPatch3PatchedTargetHash = 'F8F8D9A78459808B292E58BBB00F9AC8B3AF08A177536611E8E61C0BBD804852'
$expectedPatch3ValidationStatus = 'verified-by-cdb-2026-07-28'
$expectedPatch3Rationale = 'Prevents null String holder pointers from being converted into invalid buffer headers during retain or release after observed lifetime corruption.'

function Test-ApexJucePayloadShape {
    param(
        [Parameter(Mandatory = $true)]
        [string] $InstallationRoot
    )

    $projucer = Join-Path $InstallationRoot 'JUCE\Projucer.exe'
    $standardHeader = Join-Path $InstallationRoot 'JUCE\modules\juce_core\system\juce_StandardHeader.h'

    if (-not (Test-Path -LiteralPath $projucer -PathType Leaf) -or
        -not (Test-Path -LiteralPath $standardHeader -PathType Leaf)) {
        return $false
    }

    $headerText = [System.IO.File]::ReadAllText($standardHeader)
    return (
        $headerText -match '(?m)^\s*#define\s+JUCE_MAJOR_VERSION\s+8\s*$' -and
        $headerText -match '(?m)^\s*#define\s+JUCE_MINOR_VERSION\s+0\s*$' -and
        $headerText -match '(?m)^\s*#define\s+JUCE_BUILDNUMBER\s+12\s*$'
    )
}

if (-not (Test-Path -LiteralPath $contractPath -PathType Leaf)) {
    throw "Dependency contract is missing: $contractPath"
}

$contract = Get-Content -LiteralPath $contractPath -Raw | ConvertFrom-Json
if ([string] $contract.juce.version -cne $expectedJuceVersion) {
    throw 'Dependency contract does not contain the approved JUCE version.'
}
if ([string] $contract.juce.upstreamCommit -cne $expectedUpstreamCommit) {
    throw 'Dependency contract does not contain the approved JUCE upstream commit.'
}
if ([string] $contract.juce.archiveFileName -cne $expectedArchiveFileName) {
    throw 'Dependency contract does not contain the approved JUCE archive file name.'
}
if ([string] $contract.juce.archiveSha256 -cne $expectedArchiveHash) {
    throw 'Dependency contract does not contain the approved JUCE archive SHA-256.'
}
if ([string] $contract.juce.rootRelativeToRepository -cne $expectedRootRelativeToRepository) {
    throw "juce.rootRelativeToRepository must equal the approved value '$expectedRootRelativeToRepository'."
}
if ([string] $contract.juce.compatibilityPatch.relativePath -cne $expectedPatchRelativePath) {
    throw 'juce.compatibilityPatch.relativePath does not contain the approved value.'
}
if ([string] $contract.juce.compatibilityPatch.sha256 -cne $expectedPatchHash) {
    throw 'juce.compatibilityPatch.sha256 does not contain the approved value.'
}
if ([string] $contract.juce.compatibilityPatch.targetRelativePath -cne $expectedPatchTargetRelativePath) {
    throw 'juce.compatibilityPatch.targetRelativePath does not contain the approved value.'
}
if ([string] $contract.juce.compatibilityPatch.pristineTargetSha256 -cne $expectedPristineTargetHash) {
    throw 'juce.compatibilityPatch.pristineTargetSha256 does not contain the approved value.'
}
if ([string] $contract.juce.compatibilityPatch.patchedTargetSha256 -cne $expectedPatchedTargetHash) {
    throw 'juce.compatibilityPatch.patchedTargetSha256 does not contain the approved value.'
}
if ([string] $contract.juce.compatibilityPatch.validationStatus -cne $expectedPatchValidationStatus) {
    throw 'juce.compatibilityPatch.validationStatus does not contain the approved provisional status.'
}
if ([string] $contract.juce.compatibilityPatch.rationale -cne $expectedPatchRationale) {
    throw 'juce.compatibilityPatch.rationale does not contain the approved rationale.'
}
if ($contract.juce.compatibilityPatch.hardwareValidationRequired -isnot [bool] -or
    -not [bool] $contract.juce.compatibilityPatch.hardwareValidationRequired) {
    throw 'juce.compatibilityPatch.hardwareValidationRequired must be true.'
}
if ([string] $contract.juce.compatibilityPatch2.relativePath -cne $expectedPatch2RelativePath -or
    [string] $contract.juce.compatibilityPatch2.sha256 -cne $expectedPatch2Hash -or
    [string] $contract.juce.compatibilityPatch2.targetRelativePath -cne $expectedPatch2TargetRelativePath -or
    [string] $contract.juce.compatibilityPatch2.pristineTargetSha256 -cne $expectedPatch2PristineTargetHash -or
    [string] $contract.juce.compatibilityPatch2.patchedTargetSha256 -cne $expectedPatch2PatchedTargetHash -or
    [string] $contract.juce.compatibilityPatch2.validationStatus -cne $expectedPatch2ValidationStatus -or
    [string] $contract.juce.compatibilityPatch2.rationale -cne $expectedPatch2Rationale -or
    $contract.juce.compatibilityPatch2.hardwareValidationRequired -isnot [bool] -or
    [bool] $contract.juce.compatibilityPatch2.hardwareValidationRequired) {
    throw 'juce.compatibilityPatch2 does not contain the approved VST3 editor safety patch contract.'
}
if ([string] $contract.juce.compatibilityPatch3.relativePath -cne $expectedPatch3RelativePath -or
    [string] $contract.juce.compatibilityPatch3.sha256 -cne $expectedPatch3Hash -or
    [string] $contract.juce.compatibilityPatch3.targetRelativePath -cne $expectedPatch3TargetRelativePath -or
    [string] $contract.juce.compatibilityPatch3.pristineTargetSha256 -cne $expectedPatch3PristineTargetHash -or
    [string] $contract.juce.compatibilityPatch3.patchedTargetSha256 -cne $expectedPatch3PatchedTargetHash -or
    [string] $contract.juce.compatibilityPatch3.validationStatus -cne $expectedPatch3ValidationStatus -or
    [string] $contract.juce.compatibilityPatch3.rationale -cne $expectedPatch3Rationale -or
    $contract.juce.compatibilityPatch3.hardwareValidationRequired -isnot [bool] -or
    [bool] $contract.juce.compatibilityPatch3.hardwareValidationRequired) {
    throw 'juce.compatibilityPatch3 does not contain the approved String holder safety patch contract.'
}

$repositoryPrefix = $repositoryRoot + [System.IO.Path]::DirectorySeparatorChar
$patchPath = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot ([string] $contract.juce.compatibilityPatch.relativePath))
)
if (-not $patchPath.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "JUCE compatibility patch resolves outside the repository: $patchPath"
}
if (-not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
    throw "JUCE compatibility patch is missing: $patchPath"
}

$actualPatchHash = (Get-FileHash -LiteralPath $patchPath -Algorithm SHA256).Hash
if ($actualPatchHash -cne $expectedPatchHash) {
    throw "JUCE compatibility patch SHA-256 mismatch. Expected $expectedPatchHash, got $actualPatchHash."
}

$patch2Path = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot ([string] $contract.juce.compatibilityPatch2.relativePath))
)
$patch3Path = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot ([string] $contract.juce.compatibilityPatch3.relativePath))
)
foreach ($additionalPatch in @(
    [pscustomobject]@{ Number = 2; Path = $patch2Path; Hash = $expectedPatch2Hash },
    [pscustomobject]@{ Number = 3; Path = $patch3Path; Hash = $expectedPatch3Hash }
)) {
    if (-not $additionalPatch.Path.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "JUCE compatibility patch $($additionalPatch.Number) resolves outside the repository: $($additionalPatch.Path)"
    }
    if (-not (Test-Path -LiteralPath $additionalPatch.Path -PathType Leaf)) {
        throw "JUCE compatibility patch $($additionalPatch.Number) is missing: $($additionalPatch.Path)"
    }
    $actualAdditionalHash = (Get-FileHash -LiteralPath $additionalPatch.Path -Algorithm SHA256).Hash
    if ($actualAdditionalHash -cne $additionalPatch.Hash) {
        throw "JUCE compatibility patch $($additionalPatch.Number) SHA-256 mismatch. Expected $($additionalPatch.Hash), got $actualAdditionalHash."
    }
}

$approvedSdkParent = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot '..\Sdk setups')
).TrimEnd('\')
$approvedJuceRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $approvedSdkParent 'juce-8.0.12-windows\JUCE')
)
$contractJuceRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot ([string] $contract.juce.rootRelativeToRepository))
)
$approvedPrefix = $approvedSdkParent + [System.IO.Path]::DirectorySeparatorChar

if (-not $contractJuceRoot.StartsWith($approvedPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
    -not [System.StringComparer]::OrdinalIgnoreCase.Equals($contractJuceRoot, $approvedJuceRoot)) {
    throw "Resolved JUCE root is outside the approved sibling SDK path: $contractJuceRoot"
}

Assert-ApexPayloadPathNotReparsePoint -Path $approvedSdkParent
if (-not (Test-Path -LiteralPath $approvedSdkParent -PathType Container)) {
    throw "JUCE installation parent is missing: $approvedSdkParent"
}

if ([System.IO.Path]::IsPathRooted($JuceArchive)) {
    $archivePath = [System.IO.Path]::GetFullPath($JuceArchive)
}
else {
    $archivePath = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $JuceArchive))
}

if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    throw "JUCE archive is missing: $archivePath"
}

$actualArchiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
if ($actualArchiveHash -cne $expectedArchiveHash) {
    throw "JUCE archive SHA-256 mismatch. Expected $expectedArchiveHash, got $actualArchiveHash."
}

$installationRoot = Split-Path -Parent $approvedJuceRoot
$installationParent = Split-Path -Parent $installationRoot

if (-not [System.StringComparer]::OrdinalIgnoreCase.Equals($installationParent, $approvedSdkParent)) {
    throw "JUCE installation parent is not the approved sibling SDK root: $installationParent"
}

$temporaryRoot = Join-Path $installationParent ".apex-juce-8.0.12-windows-$([guid]::NewGuid().ToString('N'))"
$existingInstallationVerified = $false

try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $temporaryRoot)

    if (-not (Test-ApexJucePayloadShape -InstallationRoot $temporaryRoot)) {
        throw 'Verified JUCE archive did not contain JUCE/Projucer.exe and the JUCE 8.0.12 StandardHeader.'
    }

    Invoke-JuceVerifiedPatch `
        -Root $temporaryRoot `
        -PatchPath $patchPath `
        -TargetRelativePath $expectedPatchTargetRelativePath `
        -PristineTargetSha256 $expectedPristineTargetHash `
        -PatchedTargetSha256 $expectedPatchedTargetHash
    Invoke-JuceVerifiedPatch `
        -Root $temporaryRoot `
        -PatchPath $patch2Path `
        -TargetRelativePath $expectedPatch2TargetRelativePath `
        -PristineTargetSha256 $expectedPatch2PristineTargetHash `
        -PatchedTargetSha256 $expectedPatch2PatchedTargetHash
    Invoke-JuceVerifiedPatch `
        -Root $temporaryRoot `
        -PatchPath $patch3Path `
        -TargetRelativePath $expectedPatch3TargetRelativePath `
        -PristineTargetSha256 $expectedPatch3PristineTargetHash `
        -PatchedTargetSha256 $expectedPatch3PatchedTargetHash

    if (Test-Path -LiteralPath $installationRoot) {
        $difference = $null
        if (-not (Test-ApexDirectoryPayloadEquivalent -ReferenceRoot $temporaryRoot -CandidateRoot $installationRoot -Difference ([ref] $difference))) {
            throw "Existing JUCE installation does not match the pinned JUCE archive-plus-patch payload: $difference"
        }

        $existingInstallationVerified = $true
    }
    else {
        $temporaryVolume = [System.IO.Path]::GetPathRoot($temporaryRoot)
        $installationVolume = [System.IO.Path]::GetPathRoot($installationRoot)
        if (-not [System.StringComparer]::OrdinalIgnoreCase.Equals($temporaryVolume, $installationVolume)) {
            throw 'JUCE publication requires temporary and destination directories on the same volume.'
        }

        Publish-ApexDirectoryAtomically `
            -PreparedRoot $temporaryRoot `
            -DestinationRoot $installationRoot
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

if ($existingInstallationVerified) {
    Write-Host "[dependencies] Pinned JUCE installation exactly matches the archive-plus-patch payload: $installationRoot"
}
else {
    Write-Host "[dependencies] Materialized pinned JUCE 8.0.12 with the provisional APEX compatibility patch: $installationRoot"
}

exit 0
