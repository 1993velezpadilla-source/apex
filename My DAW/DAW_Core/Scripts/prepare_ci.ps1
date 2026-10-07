[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$core = Split-Path -Parent $PSScriptRoot
$contract = Get-Content -LiteralPath (Join-Path $core 'Dependencies/apex-windows-dependencies.json') -Raw | ConvertFrom-Json
$sdkDirectory = [IO.Path]::GetFullPath((Join-Path $core '../Sdk setups'))
$archive = Join-Path $sdkDirectory $contract.juce.archiveFileName
$downloadUrl = 'https://github.com/juce-framework/JUCE/releases/download/8.0.12/juce-8.0.12-windows.zip'
New-Item -ItemType Directory -Path $sdkDirectory -Force | Out-Null
if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
    $temporary = $archive + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $downloadUrl -OutFile $temporary -UseBasicParsing
        $hash = (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash
        if ($hash -cne [string]$contract.juce.archiveSha256) { throw 'Downloaded JUCE archive does not match the locked SHA-256.' }
        Move-Item -LiteralPath $temporary -Destination $archive
    }
    finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -cne [string]$contract.juce.archiveSha256) {
    throw 'JUCE archive does not match the locked SHA-256.'
}
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'materialize_dependencies.ps1') -JuceArchive $archive
if ($LASTEXITCODE -ne 0) { throw "Dependency materialization failed with exit code $LASTEXITCODE." }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'verify_dependencies.ps1')
if ($LASTEXITCODE -ne 0) { throw "Dependency verification failed with exit code $LASTEXITCODE." }
$juceRoot = [IO.Path]::GetFullPath((Join-Path $core $contract.juce.rootRelativeToRepository))
$projucer = Join-Path $juceRoot 'Projucer.exe'
# Regenerate test projects using their explicit pinned module paths, not a
# developer's global Projucer settings or stale machine-relative paths.
foreach ($project in Get-ChildItem -LiteralPath (Join-Path $core 'Tests') -Recurse -Filter '*.jucer' -File) {
    & $projucer --resave $project.FullName
    if ($LASTEXITCODE -ne 0) { throw "Projucer failed for $($project.FullName)." }
}
Write-Host '[PASS] CI dependencies match the desktop dependency contract.'
