param(
    [Parameter(Mandatory = $true)] [string] $Cdb,
    [Parameter(Mandatory = $true)] [string] $Gflags,
    [Parameter(Mandatory = $true)] [string] $Executable,
    [Parameter(Mandatory = $true)] [string] $Fixture,
    [Parameter(Mandatory = $true)] [string] $ProbeScript,
    [Parameter(Mandatory = $true)] [string] $CrashReportsDirectory
)

$ErrorActionPreference = 'Stop'
foreach ($requiredPath in @($Cdb, $Gflags, $Executable, $Fixture, $ProbeScript)) {
    if (-not [IO.File]::Exists($requiredPath)) { throw "Required file does not exist: $requiredPath" }
}
if (-not [IO.Directory]::Exists($CrashReportsDirectory)) {
    throw "Crash-report directory does not exist: $CrashReportsDirectory"
}

$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$report = Join-Path $CrashReportsDirectory "APEX_CDB_PAGEHEAP_ADELAY_$timestamp.txt"
$commands = Join-Path $CrashReportsDirectory "APEX_CDB_PAGEHEAP_ADELAY_$timestamp.commands.txt"
$symbolCache = Join-Path $env:LOCALAPPDATA 'SymbolCache'
$exeDirectory = Split-Path -Parent $Executable
$symbolPath = "srv*$symbolCache*https://msdl.microsoft.com/download/symbols;$exeDirectory"
$registryPath = 'HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\DAW_Core.exe'

$fatal = '.echo; .echo === FATAL EXCEPTION ===; .exr -1; .ecxr; kb; kv; lm; r; .echo === END FATAL EXCEPTION ===; q'
$commandText = @"
.sympath $symbolPath
.reload /f
sxi 80000003
sxi 80000004
sxd -c2 "$fatal" c0000005
sxd -c2 "$fatal" e06d7363
sxd -c2 "$fatal" c00000fd
sxd -c2 "$fatal" c000001d
sxd -c2 "$fatal" c0000374
sxd -c2 "$fatal" c0000409
.echo === CDB ADelay full-page-heap session started ===
g
.echo === TARGET EXITED WITHOUT FATAL EXCEPTION ===
q
"@
[IO.File]::WriteAllText($commands, $commandText, [Text.Encoding]::ASCII)

$cdbProcess = $null
$cdbExit = -1
$existingTargetIds = @{}
foreach ($existing in @(Get-Process -Name DAW_Core -ErrorAction SilentlyContinue)) {
    $existingTargetIds[$existing.Id] = $true
}

try {
    & $Gflags /p /enable DAW_Core.exe /full
    $enableExit = $LASTEXITCODE
    if ($null -ne $enableExit -and $enableExit -ne 0) {
        throw "gflags failed to enable full page heap: exit=$enableExit"
    }
    Start-Sleep -Milliseconds 500

    $pageHeapState = @(& reg.exe query $registryPath 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0 -or
        $pageHeapState -notmatch 'GlobalFlag\s+REG_SZ\s+0x02000000' -or
        $pageHeapState -notmatch 'PageHeapFlags\s+REG_SZ\s+0x3') {
        throw "Full page heap is not enabled for DAW_Core.exe:`n$pageHeapState"
    }
    'PAGE_HEAP=full_confirmed_before_launch'

    $cdbArguments = @(
        '-cf', "`"$commands`"",
        '-logo', "`"$report`"",
        '-y', "`"$symbolPath`"",
        '-lines',
        "`"$Executable`"")
    $cdbProcess = Start-Process -FilePath $Cdb -ArgumentList $cdbArguments -PassThru
    "CDB_PID=$($cdbProcess.Id)"
    "CDB_REPORT=$report"

    $deadline = [DateTime]::UtcNow.AddMinutes(4)
    $target = $null
    while ([DateTime]::UtcNow -lt $deadline -and $null -eq $target) {
        $mainProcessIds = @(Get-CimInstance Win32_Process -Filter "Name = 'DAW_Core.exe'" -ErrorAction SilentlyContinue |
            Where-Object { $_.CommandLine -notmatch '--plugin-scan-worker' } |
            ForEach-Object { [int] $_.ProcessId })
        foreach ($candidateId in $mainProcessIds) {
            if ($existingTargetIds.ContainsKey($candidateId)) { continue }
            $candidate = Get-Process -Id $candidateId -ErrorAction SilentlyContinue
            if ($null -eq $candidate) { continue }
            $candidate.Refresh()
            if ($candidate.MainWindowHandle -ne 0) {
                $target = $candidate
                break
            }
        }
        if ($null -eq $target) {
            if ($cdbProcess.HasExited) {
                throw "CDB exited before APEX showed a window: $($cdbProcess.ExitCode)"
            }
            Start-Sleep -Milliseconds 100
        }
    }
    if ($null -eq $target) { throw 'Timed out waiting for the CDB-hosted APEX process' }
    "TARGET_PID=$($target.Id)"

    # Invoke in the already-running harness process. Spawning another
    # powershell.exe consumed most of the cached scan overlay's 750 ms lifetime
    # before UIA initialized, forcing an unnecessary full rescan fallback.
    try {
        & $ProbeScript -ProcessId $target.Id -Fixture $Fixture
        'PROBE_EXIT=0'
    }
    catch {
        throw "Accessibility probe failed: $($_.Exception.Message)"
    }

    if (-not $cdbProcess.WaitForExit(120000)) { throw 'CDB did not exit after target shutdown' }
    $cdbExit = $cdbProcess.ExitCode
    "CDB_EXIT=$cdbExit"
    if ($cdbExit -ne 0) { throw "CDB exited with code $cdbExit" }

    $reportText = [IO.File]::ReadAllText($report)
    if ($reportText -notmatch '(?m)^=== TARGET EXITED WITHOUT FATAL EXCEPTION ===\r?$') {
        throw 'CDB normal-exit marker is missing'
    }
    if ($reportText -match '(?m)^=== FATAL EXCEPTION ===\r?$') {
        throw 'CDB captured a fatal exception'
    }
    if ($reportText -match '(?i)heap corruption') {
        throw 'CDB report contains heap-corruption evidence'
    }
    if ($reportText -notmatch 'Page heap: pid 0x[0-9a-f]+: page heap enabled with flags 0x3\.') {
        throw 'CDB did not report full page heap for the debugged APEX process'
    }
    'CDB_NORMAL_EXIT=confirmed'
}
finally {
    $disableExit = $null
    try {
        & $Gflags /p /disable DAW_Core.exe
        $disableExit = $LASTEXITCODE
    }
    catch {
        Write-Warning "gflags disable invocation failed; applying registry-value fallback: $($_.Exception.Message)"
    }
    Start-Sleep -Milliseconds 500
    $after = @(& reg.exe query $registryPath 2>&1) -join "`n"
    if ($after -match 'GlobalFlag\s+REG_SZ\s+0x02000000' -or
        $after -match 'PageHeapFlags\s+REG_SZ\s+0x3') {
        & reg.exe delete $registryPath /v GlobalFlag /f | Out-Null
        & reg.exe delete $registryPath /v PageHeapFlags /f | Out-Null
        Start-Sleep -Milliseconds 250
        $after = @(& reg.exe query $registryPath 2>&1) -join "`n"
    }
    if ($after -match 'GlobalFlag\s+REG_SZ\s+0x02000000' -or
        $after -match 'PageHeapFlags\s+REG_SZ\s+0x3') {
        throw "Full page heap remained enabled after gflags and registry fallback cleanup (gflags exit=$disableExit):`n$after"
    }
    'PAGE_HEAP=disabled_after_run'
}
