param(
    [Parameter(Mandatory = $true)] [string] $Cdb,
    [Parameter(Mandatory = $true)] [string] $Gflags,
    [Parameter(Mandatory = $true)] [string] $Executable,
    [Parameter(Mandatory = $true)] [string] $PluginFile,
    [Parameter(Mandatory = $true)] [string] $PluginName,
    [Parameter(Mandatory = $true)] [string] $CrashReportsDirectory,
    [switch] $SkipPageHeap
)

$ErrorActionPreference = 'Stop'
foreach ($requiredPath in @($Cdb, $Gflags, $Executable, $PluginFile)) {
    if (-not [IO.File]::Exists($requiredPath)) { throw "Required file does not exist: $requiredPath" }
}
if (-not [IO.Directory]::Exists($CrashReportsDirectory)) {
    throw "Crash-report directory does not exist: $CrashReportsDirectory"
}

$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$report = Join-Path $CrashReportsDirectory "APEX_CDB_PAGEHEAP_PLUGIN_FORENSIC_$timestamp.txt"
$commands = Join-Path $CrashReportsDirectory "APEX_CDB_PAGEHEAP_PLUGIN_FORENSIC_$timestamp.commands.txt"
$symbolCache = Join-Path $env:LOCALAPPDATA 'SymbolCache'
$exeDirectory = Split-Path -Parent $Executable
$symbolPath = "srv*$symbolCache*https://msdl.microsoft.com/download/symbols;$exeDirectory"
$registryPath = 'HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\DAW_Core.exe'

$firstInvalid = '.echo; .echo === FIRST INVALID MEMORY OPERATION ===; .exr -1; .ecxr; kb; kv; lm; r; !heap -p -a @rcx; .echo === END FIRST INVALID MEMORY OPERATION ===; q'
$fatal = '.echo; .echo === FATAL EXCEPTION ===; .exr -1; .ecxr; kb; kv; lm; r; .echo === END FATAL EXCEPTION ===; q'
$commandText = @"
.sympath $symbolPath
.reload /f
sxi 80000003
sxi 80000004
sxe -c "$firstInvalid" c0000005
sxe -c "$firstInvalid" c0000374
sxd -c2 "$fatal" e06d7363
sxd -c2 "$fatal" c00000fd
sxd -c2 "$fatal" c000001d
sxd -c2 "$fatal" c0000409
.echo === CDB plugin forensic full-page-heap session started ===
g
.echo === TARGET EXITED WITHOUT INVALID MEMORY OPERATION ===
q
"@
[IO.File]::WriteAllText($commands, $commandText, [Text.Encoding]::ASCII)

$cdbProcess = $null
$existingTargetIds = @{}
foreach ($existing in @(Get-Process -Name DAW_Core -ErrorAction SilentlyContinue)) {
    $existingTargetIds[$existing.Id] = $true
}
try {
    if (-not $SkipPageHeap) {
        & $Gflags /p /enable DAW_Core.exe /full
        if ($LASTEXITCODE -ne 0) { throw "gflags enable failed: exit=$LASTEXITCODE" }
        Start-Sleep -Milliseconds 500
        $pageHeapState = @(& reg.exe query $registryPath 2>&1) -join "`n"
        if ($LASTEXITCODE -ne 0 -or
            $pageHeapState -notmatch 'GlobalFlag\s+REG_SZ\s+0x02000000' -or
            $pageHeapState -notmatch 'PageHeapFlags\s+REG_SZ\s+0x3') {
            throw "Full page heap is not enabled:`n$pageHeapState"
        }
        'PAGE_HEAP=full_confirmed_before_launch'
    }
    else {
        'PAGE_HEAP=skipped_debugger_only_run'
    }

    $arguments = @(
        '-cf', "`"$commands`"",
        '-logo', "`"$report`"",
        '-y', "`"$symbolPath`"",
        '-lines',
        "`"$Executable`"",
        '--plugin-forensic-vst3', "`"$PluginFile`"",
        '--plugin-forensic-name', "`"$PluginName`"")
    $cdbProcess = Start-Process -FilePath $Cdb -ArgumentList $arguments -PassThru
    "CDB_PID=$($cdbProcess.Id)"
    "CDB_REPORT=$report"
    if (-not $cdbProcess.WaitForExit(120000)) {
        throw 'CDB plugin forensic session exceeded its bounded 120-second timeout'
    }
    "CDB_EXIT=$($cdbProcess.ExitCode)"
    if ($cdbProcess.ExitCode -ne 0) { throw "CDB exited with code $($cdbProcess.ExitCode)" }

    $reportText = [IO.File]::ReadAllText($report)
    if ($reportText -match '(?m)^=== FIRST INVALID MEMORY OPERATION ===\r?$') {
        throw "CDB captured the first invalid memory operation: $report"
    }
    if ($reportText -match '(?m)^=== FATAL EXCEPTION ===\r?$') {
        throw "CDB captured a fatal exception: $report"
    }
    if ($reportText -notmatch '(?m)^=== TARGET EXITED WITHOUT INVALID MEMORY OPERATION ===\r?$') {
        throw 'CDB normal-exit marker is missing'
    }
    if (-not $SkipPageHeap -and
        $reportText -notmatch 'Page heap: pid 0x[0-9a-f]+: page heap enabled with flags 0x3\.') {
        throw 'CDB did not report full page heap for APEX'
    }
    'CDB_NORMAL_EXIT=confirmed'
}
finally {
    if ($null -ne $cdbProcess -and -not $cdbProcess.HasExited) {
        Stop-Process -Id $cdbProcess.Id -Force -ErrorAction SilentlyContinue
    }
    foreach ($target in @(Get-Process -Name DAW_Core -ErrorAction SilentlyContinue)) {
        if (-not $existingTargetIds.ContainsKey($target.Id)) {
            Stop-Process -Id $target.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if (-not $SkipPageHeap) {
        try { & $Gflags /p /disable DAW_Core.exe | Out-Null } catch {}
        Start-Sleep -Milliseconds 500
        $after = @(& reg.exe query $registryPath 2>&1) -join "`n"
        if ($after -match 'GlobalFlag\s+REG_SZ\s+0x02000000' -or
            $after -match 'PageHeapFlags\s+REG_SZ\s+0x3') {
            & reg.exe delete $registryPath /v GlobalFlag /f | Out-Null
            & reg.exe delete $registryPath /v PageHeapFlags /f | Out-Null
        }
        'PAGE_HEAP=disabled_after_run'
    }
}
