[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'

# Focused research-only runner: unlike run_apex_tests.ps1, the Opto JUCE
# boundary suite uses NO VST3 fixture and must not build six fixture bundles.
# Requires prepare_ci.ps1 to materialize the pinned JUCE and resave Jucer.
$core = Split-Path -Parent $PSScriptRoot
$solution = Join-Path $core 'Tests\Builds\VisualStudio2026\APEXTests.sln'
if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) {
    throw "APEXTests solution not generated. Run Scripts/prepare_ci.ps1 first."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'Cannot find VS installation discovery tool.'
}
$vsHome = @(& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -version '[18.0,19.0)' -property installationPath)
if ($LASTEXITCODE -ne 0 -or $vsHome.Count -ne 1) {
    throw 'Visual Studio 2026 MSBuild installation was not resolved uniquely.'
}
$msbuild = Join-Path $vsHome[0] 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) {
    throw "MSBuild.exe missing: $msbuild"
}
Write-Host "[opto-juce] Building focused APEXTests ($Configuration) — no VST3 fixtures."
& $msbuild $solution '/t:Build' "/p:Configuration=$Configuration" '/p:Platform=x64' '/m' '/nr:false' '/nologo' '/v:minimal'
if ($LASTEXITCODE -ne 0) {
    throw "Opto JUCE research MSBuild failed: $LASTEXITCODE"
}
$exe = Join-Path $core "Tests\Builds\VisualStudio2026\x64\$Configuration\ConsoleApp\APEXTests.exe"
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "APEXTests output missing: $exe"
}
Write-Host '[opto-juce] Running only APEX.OptoResearchHost and returning real exit code.'
& $exe '--category=APEX.OptoResearchHost' '--seed=0xA20C108'
if ($LASTEXITCODE -ne 0) {
    throw "Opto JUCE host suite failed: $LASTEXITCODE"
}
Write-Host '[PASS] JUCE research-only Opto host latency / dry / bypass regression.'
