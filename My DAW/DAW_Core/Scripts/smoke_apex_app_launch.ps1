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

# Use the real DAW executable, NOT APEXTests. Each isolated process must
# initialize its full MainComponent and JUCE message loop and quit cleanly.
# First probe: production window startup. Second: actual ProjectManager
# new/save/new/load roundtrip inside the running application. No user project
# is touched by the CI-only persistence probe.
$smokes = @(
    @{ Flag = '--apex-ci-launch-smoke'; Label = 'real GUI startup' },
    @{ Flag = '--apex-ci-startup-panel'; Label = 'visible and interactive startup panel' },
    @{ Flag = '--apex-ci-project-roundtrip'; Label = 'real project new/save/new/load' },
    @{ Flag = '--apex-ci-mixer-automation-ui'; Label = 'real MixerStrip automation mode and broadcast UI' }
)

foreach ($smoke in $smokes) {
    $process = $null
    try {
        Write-Host "[app-smoke] Launching $Configuration|x64 $($smoke.Label): $exe"
        $process = Start-Process -FilePath $exe `
                                 -ArgumentList $smoke.Flag `
                                 -WorkingDirectory (Split-Path -Parent $exe) `
                                 -PassThru

        if (-not $process.WaitForExit(90000)) {
            throw "$($smoke.Label) did not exit within the 90-second safety deadline."
        }

        if ($process.ExitCode -ne 0) {
            throw "$($smoke.Label) failed with exit code $($process.ExitCode)."
        }

        Write-Host "[PASS] APEX $Configuration|x64: $($smoke.Label) completed cleanly."

        # Surface real app start-page timings in CI logs, including timings
        # from Debug where the compiled app has diagnostics enabled.
        # Not an arbitrary pass/fail timing budget: cloud runners vary.
        if ($smoke.Flag -eq '--apex-ci-startup-panel') {
            $tracePath = Join-Path $env:APPDATA 'DAW_Core\startup_trace.log'
            if (Test-Path -LiteralPath $tracePath -PathType Leaf) {
                Write-Host "[startup-profile] $Configuration measured ApplicationCore/MainComponent stages:"
                Get-Content -LiteralPath $tracePath -Tail 65 |
                    Where-Object { $_ -match '\[(ApplicationCore|MainComponent)\]' } |
                    ForEach-Object { Write-Host "[startup-profile] $_" }
            } else {
                Write-Warning "[startup-profile] No startup_trace.log produced by the application."
            }
        }
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
}
