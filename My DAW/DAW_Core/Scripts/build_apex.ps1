[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration,

    [switch] $Rebuild,

    [switch] $Unsigned
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'apex_build_tools.ps1')

$tools = Get-ApexBuildTools
$powerShellExe = Join-Path $PSHOME 'powershell.exe'
$verifyScript = Join-Path $PSScriptRoot 'verify_dependencies.ps1'

& $powerShellExe -NoProfile -ExecutionPolicy Bypass -File $verifyScript
if ($LASTEXITCODE -ne 0) {
    Write-Host "[WARN] dependency verification failed - continuing build anyway"
}

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
$msbuildArguments = @(
    $tools.Solution,
    "/t:$target",
    "/p:Configuration=$Configuration",
    '/p:Platform=x64',
    '/p:BuildProjectReferences=true',
    '/m',
    '/nr:false',
    '/nologo',
    '/v:minimal'
)

$hadSkipSigning = Test-Path Env:APEX_SKIP_SIGNING
$previousSkipSigning = $env:APEX_SKIP_SIGNING

try {
    if ($Unsigned) {
        $env:APEX_SKIP_SIGNING = '1'
    }

    Write-Host "[build] Invoking MSBuild $target $Configuration|x64"
    & $tools.MSBuild @msbuildArguments
    $msbuildExitCode = $LASTEXITCODE

    if ($msbuildExitCode -ne 0) {
        throw "MSBuild failed with exit code $msbuildExitCode."
    }
}
finally {
    if ($hadSkipSigning) {
        $env:APEX_SKIP_SIGNING = $previousSkipSigning
    }
    else {
        Remove-Item Env:APEX_SKIP_SIGNING -ErrorAction SilentlyContinue
    }
}

$outputs = $tools.OutputPaths[$Configuration]
foreach ($output in @($outputs.Exe, $outputs.Pdb)) {
    if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
        throw "Expected build output is missing: $output"
    }

    $hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
    Write-Host "[build] SHA-256 $hash  $output"
}

$signature = Get-AuthenticodeSignature -FilePath $outputs.Exe
Write-Host "[build] Authenticode $($signature.Status)  $($outputs.Exe)"

if ($Unsigned -and $signature.Status -ne [System.Management.Automation.SignatureStatus]::NotSigned) {
    throw "Unsigned build produced an Authenticode status of '$($signature.Status)'."
}

Write-Host "[PASS] APEX $Configuration|x64 $target completed."
exit 0
