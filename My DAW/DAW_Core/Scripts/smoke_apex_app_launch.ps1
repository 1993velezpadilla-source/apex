[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'apex_build_tools.ps1')
$tools = Get-ApexBuildTools
$exe = $tools.OutputPaths[$Configuration].Exe

if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Production APEX executable is missing: $exe"
}

# This exercises DAW_Core.exe, NOT the APEXTests test runner. The executable
# constructs MainComponent and its actual JUCE window, lets the message loop
# pump for four seconds, checks readiness, then quits with an explicit status.
# Use a hard process deadline so a hung initialization cannot stall CI forever.
$process = $null
try {
    Write-Host "[app-smoke] Launching real DAW $Configuration|x64: $exe"
    $process = Start-Process -FilePath $exe `
                             -ArgumentList '--apex-ci-launch-smoke' `
                             -WorkingDirectory (Split-Path -Parent $exe) `
                             -PassThru

    if (-not $process.WaitForExit(90000)) {
        throw "DAW GUI startup did not exit within the 90-second safety deadline."
    }

    # WaitForExit() has completed; refresh ExitCode from the process handle.
    if ($process.ExitCode -ne 0) {
        throw "DAW GUI initialization smoke failed with exit code $($process.ExitCode)."
    }

    Write-Host "[PASS] Real APEX DAW GUI initialized, message loop ran, and app exited cleanly ($Configuration)."
}
finally {
    if ($null -ne $process) {
        if (-not $process.HasExited) {
            Write-Warning "[app-smoke] Terminating hung DAW process $($process.Id)"
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
        $process.Dispose()
    }
}
