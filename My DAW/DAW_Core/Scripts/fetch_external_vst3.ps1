# MIT open-source VST3 binaries for ephemeral CI only. Five effect categories; no redistributing.
# Pinned to release-asset SHA256 published by GitHub. Never execute installers.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Join-Path ([IO.Path]::GetTempPath()) 'apex-vst3-external'
[IO.Directory]::CreateDirectory($root) | Out-Null
$assets = @(
    [pscustomobject]@{
        Name = 'WetReverb'
        Url = 'https://github.com/yonie/WetReverb/releases/download/v1.2.1/WetReverb-v1.2.1.zip'
        Sha256 = '379fa843429fa4ed2726b9c4c56017bbd52ece0eb9de3ff1a1fd4e51b346a1cb'
    },
    [pscustomobject]@{
        Name = 'WetDelay'
        Url = 'https://github.com/yonie/WetDelay/releases/download/v1.3.1/WetDelay-v1.3.1.zip'
        Sha256 = '35689b1072d3e5f65b791cddf0dcdbe0390f2ae16ae4f368aef8792d8c87b775'
    },
    # Three NON-reverb/non-delay processors; hashes are the publisher's
    # exact GitHub release-asset SHA256 digests (checked 2026-10-12).
    [pscustomobject]@{
        Name = 'WetEQ'
        Url = 'https://github.com/yonie/WetEQ/releases/download/v1.2.1/WetEQ-v1.2.1.zip'
        Sha256 = '856c47c221ee07fbc25fcc481612de78e23b43dbb8e0bb96b555bc0cbedc244f'
    },
    [pscustomobject]@{
        Name = 'WetCompressor'
        Url = 'https://github.com/yonie/WetCompressor/releases/download/v1.1.1/WetCompressor-v1.1.1.zip'
        Sha256 = '72b13d1b974ad2aefda4f7d7a85c496529a2c0f5519cd81764e6bb28569acd83'
    },
    [pscustomobject]@{
        Name = 'WetChorus'
        Url = 'https://github.com/yonie/WetChorus/releases/download/v1.0.1/WetChorus-v1.0.1.zip'
        Sha256 = '19e821333b7047814cd463ba321fca37c6888b3abcb1449321ced2d30677eb43'
    }
)
foreach ($item in $assets) {
    $zip = Join-Path $root ($item.Name + '.zip')
    $dest = Join-Path $root $item.Name
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
    Write-Host "[external-vst3] downloading $($item.Name) from verified publisher release"
    Invoke-WebRequest -Uri $item.Url -OutFile $zip -UseBasicParsing -MaximumRedirection 10
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $item.Sha256) { throw "Refusing unverified $($item.Name): hash=$hash" }
    Expand-Archive -LiteralPath $zip -DestinationPath $dest
    # VST3 Windows bundles are directories; the host scans their outer paths.
    $bundles = @(Get-ChildItem -LiteralPath $dest -Recurse -Directory -Force |
        Where-Object { $_.Name -eq ($item.Name + '.vst3') })
    if ($bundles.Count -ne 1) {
        throw "Expected one $($item.Name).vst3 Windows bundle; found $($bundles.Count)"
    }
    $bundlePath = $bundles[0].FullName
    $modules = @(Get-ChildItem -LiteralPath $bundlePath -Recurse -File -Force |
        Where-Object { $_.Extension -in @('.vst3', '.dll') })
    if ($modules.Count -eq 0) { throw "No native binary in $bundlePath" }
    $envName = 'APEX_EXTERNAL_VST3_' + $item.Name.ToUpperInvariant()
    [Environment]::SetEnvironmentVariable($envName, $bundlePath, 'Process')
    if ($env:GITHUB_ENV) {
        [IO.File]::AppendAllText($env:GITHUB_ENV, "$envName=$bundlePath`n", (New-Object Text.UTF8Encoding($false)))
    }
    Write-Host "[external-vst3] verified SHA256 $hash; $envName=$bundlePath"
}
if ($env:GITHUB_ENV) {
    [IO.File]::AppendAllText($env:GITHUB_ENV, "APEX_EXTERNAL_VST3_REQUIRED=1`n", (New-Object Text.UTF8Encoding($false)))
}
[Environment]::SetEnvironmentVariable('APEX_EXTERNAL_VST3_REQUIRED', '1', 'Process')
