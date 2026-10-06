function Get-ApexWasapiPatch {
    param([Parameter(Mandatory = $true)][string] $RepositoryRoot)
    $expected = @{
        relativePath = 'Dependencies/patches/juce-8.0.12/0004-apex-wasapi-shared-buffer.patch'
        sha256 = '8565917FC95A7E83B9184FB94685B2950B2458E34E2F7BDE16A3A17FC716BF43'
        targetRelativePath = 'JUCE/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp'
        pristineTargetSha256 = 'C5367D92BA534779CC8ACCEDD20424F7E3B6DAA7CE597D33CA8858451AB36759'
        patchedTargetSha256 = 'B0D577BCC9D4F03721A0AF594753EDF739A39048E43E38BF24B1EC4DA7A77696'
    }
    $contract = Get-Content -LiteralPath (Join-Path $RepositoryRoot 'Dependencies\apex-windows-dependencies.json') -Raw | ConvertFrom-Json
    foreach ($key in $expected.Keys) {
        if ([string] $contract.juce.compatibilityPatch4.$key -cne $expected[$key]) {
            throw "juce.compatibilityPatch4.$key does not match the pinned WASAPI patch."
        }
    }
    $patch = Join-Path $RepositoryRoot $expected.relativePath
    if ((Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash -cne $expected.sha256) {
        throw 'WASAPI patch SHA-256 mismatch.'
    }
    return $expected
}

function Invoke-ApexWasapiPatch {
    param([Parameter(Mandatory = $true)][string] $Root,
          [Parameter(Mandatory = $true)][string] $RepositoryRoot)
    $patch = Get-ApexWasapiPatch -RepositoryRoot $RepositoryRoot
    Invoke-JuceVerifiedPatch -Root $Root `
        -PatchPath (Join-Path $RepositoryRoot $patch.relativePath) `
        -TargetRelativePath $patch.targetRelativePath `
        -PristineTargetSha256 $patch.pristineTargetSha256 `
        -PatchedTargetSha256 $patch.patchedTargetSha256
}

function Invoke-JuceVerifiedPatch {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $Root,

        [Parameter(Mandatory = $true)]
        [string] $PatchPath,

        [Parameter(Mandatory = $true)]
        [string] $TargetRelativePath,

        [Parameter(Mandatory = $true)]
        [string] $PristineTargetSha256,

        [Parameter(Mandatory = $true)]
        [string] $PatchedTargetSha256,

        [Parameter(DontShow = $true)]
        [scriptblock] $ProcessEnvironmentClearer = {
            param(
                [Parameter(Mandatory = $true)]
                [string] $Name
            )

            [Environment]::SetEnvironmentVariable($Name, $null, 'Process')
        }
    )

    $canonicalRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $targetPath = Join-Path $canonicalRoot $TargetRelativePath
    if (-not (Test-Path -LiteralPath $targetPath -PathType Leaf)) {
        throw "Pristine patch target is missing: $targetPath"
    }

    $pristineTargetHash = (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash
    if ($pristineTargetHash -cne $PristineTargetSha256) {
        throw "Pristine patch target SHA-256 mismatch. Expected $PristineTargetSha256, got $pristineTargetHash."
    }

    # Prevent an enclosing repository from filtering patch paths outside its worktree prefix.
    $gitMetadataPath = Join-Path $canonicalRoot '.git'
    if (Test-Path -LiteralPath $gitMetadataPath) {
        throw "Patch root unexpectedly contains Git metadata: $gitMetadataPath"
    }

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
            & $ProcessEnvironmentClearer -Name $name
        }

        $gitInitOutput = @(
            & git -C $canonicalRoot --git-dir=$gitMetadataPath --work-tree=$canonicalRoot init --quiet 2>&1
        )
        $initExitCode = $LASTEXITCODE
        if ($initExitCode -ne 0) {
            throw "Temporary patch repository initialization failed with exit code $initExitCode`: $($gitInitOutput -join [Environment]::NewLine)"
        }

        $topLevelOutput = @(
            & git -C $canonicalRoot --git-dir=$gitMetadataPath --work-tree=$canonicalRoot rev-parse --show-toplevel 2>&1
        )
        $topLevelExitCode = $LASTEXITCODE
        if ($topLevelExitCode -ne 0) {
            throw "Temporary patch repository root verification failed with exit code $topLevelExitCode`: $($topLevelOutput -join [Environment]::NewLine)"
        }

        $actualTopLevel = [System.IO.Path]::GetFullPath(($topLevelOutput | Out-String).Trim()).TrimEnd('\')
        if (-not [System.StringComparer]::OrdinalIgnoreCase.Equals($actualTopLevel, $canonicalRoot)) {
            throw "Temporary patch repository top level is '$actualTopLevel'; expected '$canonicalRoot'."
        }

        $gitCheckOutput = @(
            & git -C $canonicalRoot --git-dir=$gitMetadataPath --work-tree=$canonicalRoot apply --check --whitespace=nowarn -- $PatchPath 2>&1
        )
        $checkExitCode = $LASTEXITCODE
        if ($checkExitCode -ne 0) {
            throw "Patch check failed with exit code $checkExitCode`: $($gitCheckOutput -join [Environment]::NewLine)"
        }

        $gitApplyOutput = @(
            & git -C $canonicalRoot --git-dir=$gitMetadataPath --work-tree=$canonicalRoot apply --whitespace=nowarn -- $PatchPath 2>&1
        )
        $applyExitCode = $LASTEXITCODE
        if ($applyExitCode -ne 0) {
            throw "Patch apply failed with exit code $applyExitCode`: $($gitApplyOutput -join [Environment]::NewLine)"
        }
    }
    finally {
        try {
            if (Test-Path -LiteralPath $gitMetadataPath -PathType Container) {
                [System.IO.Directory]::Delete($gitMetadataPath, $true)
            }
        }
        finally {
            foreach ($name in $savedGitEnvironment.Keys) {
                $savedValue = $savedGitEnvironment[$name]
                if ($savedValue.WasSet) {
                    [Environment]::SetEnvironmentVariable($name, $savedValue.Value, 'Process')
                }
                else {
                    [Environment]::SetEnvironmentVariable($name, $null, 'Process')
                }
            }
        }
    }

    $patchedTargetHash = (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash
    if ($patchedTargetHash -cne $PatchedTargetSha256) {
        throw "Patched target SHA-256 mismatch. Expected $PatchedTargetSha256, got $patchedTargetHash."
    }
}
