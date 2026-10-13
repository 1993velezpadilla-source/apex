[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Debug','Release')]
    [string] $Configuration
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'apex_build_tools.ps1')
$tools = Get-ApexBuildTools
$core = Split-Path -Parent $PSScriptRoot
$testSolution = Join-Path $core 'Tests\Builds\VisualStudio2026\APEXTests.sln'
$testExe = Join-Path $core "Tests\Builds\VisualStudio2026\x64\$Configuration\ConsoleApp\APEXTests.exe"

if (-not (Test-Path -LiteralPath $testSolution -PathType Leaf)) {
    throw "APEXTests.sln missing; run Scripts/prepare_ci.ps1 first: $testSolution"
}
$arguments = @(
    $testSolution, '/t:Build', "/p:Configuration=$Configuration",
    '/p:Platform=x64', '/m:2', '/nr:false', '/nologo', '/v:minimal'
)
Write-Host "[TuneLive] Building ACTUAL APEXTests suite ($Configuration) without unrelated VST3 fault fixtures"
& $tools.MSBuild @arguments
if ($LASTEXITCODE -ne 0) {
    throw "APEXTests MSBuild failed (exit $LASTEXITCODE)"
}
if (-not (Test-Path -LiteralPath $testExe -PathType Leaf)) {
    throw "Missing APEXTests.exe: $testExe"
}

# All three suites are existing REAL APEX host tests. New TuneLive assertions:
#  - scanner cache/browser fourth intrinsic plugin
#  - actual track PluginChain insert -> AudioPluginFormatManager
#  - all 11 ID-based automation bindings, linked wet audio, DAW track state
#  - exact 40ms PDC, independent reopen, existing G10/C4/EQ compatibility.
# No VST3 test fixture is referenced by any of these focused suites.
$names = @('G10.Format.Host','C4.FormatHost','ParametricEQ.FormatHost')
foreach ($name in $names) {
    Write-Host "[TuneLive] $Configuration real APEX host suite: $name"
    & $testExe "--name=$name" '--seed=0xA9E12026'
    if ($LASTEXITCODE -ne 0) {
        throw "Real APEX $Configuration host suite $name FAILED (exit $LASTEXITCODE)"
    }
}
Write-Host "[PASS] Real APEX native TuneLive + legacy G10/C4/EQ host suites ($Configuration)"
