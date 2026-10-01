#Requires -Version 5.1
<#
.SYNOPSIS
    Monitors Visual Studio for native C++ exception crashes and launches daw-brain analysis.
.DESCRIPTION
    Polls the running Visual Studio instance via EnvDTE COM interop. When the debugger
    transitions from Run Mode to Break Mode due to an unhandled exception, captures
    full crash context and invokes the OpenCode daw-brain agent to analyze it.
.NOTES
    This script never modifies APEX source code or interferes with the debugged process.
.EXAMPLE
    pwsh -File tools\Watch-VisualStudioCrash.ps1
#>

param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

$ErrorActionPreference = 'Stop'

# --- Singleton guard ---
$mutexName = 'Global\APEX_VS_CrashWatcher'
$mutexCreated = $false
$mutex = [System.Threading.Mutex]::new($false, $mutexName, [ref]$mutexCreated)
if (-not $mutexCreated) {
    Write-Error 'Another Watch-VisualStudioCrash instance is already running.'
    exit 1
}

# --- Directories ---
$debugDir = Join-Path $RepoRoot '.apex-debug'
if (-not (Test-Path -LiteralPath $debugDir)) {
    New-Item -ItemType Directory -Path $debugDir -Force | Out-Null
}
$latestCrashFile   = Join-Path $debugDir 'latest-crash.txt'
$previousCrashFile = Join-Path $debugDir 'previous-crash.txt'
$analysisFile      = Join-Path $debugDir 'latest-analysis.md'

# --- EnvDTE debugger mode constants (integer values avoid assembly-load issues) ---
$MODE_BREAK = 2  # dbgBreakMode
$MODE_RUN   = 1  # dbgRunMode

try {
    # --- Find Visual Studio via Running Object Table ---
    function Find-VisualStudioDTE {
        foreach ($ver in @('18.0','17.0','16.0','15.0','14.0')) {
            try {
                $dte = [System.Runtime.InteropServices.Marshal]::GetActiveObject("VisualStudio.DTE.$ver")
                if ($dte) { return $dte }
            } catch {}
        }
        try {
            return [System.Runtime.InteropServices.Marshal]::GetActiveObject('VisualStudio.DTE')
        } catch {}

        # Fallback: scan the Running Object Table
        try {
            $rot = [System.Runtime.InteropServices.Marshal]::GetRunningObjectTable()
            $enumIface = $rot.EnumRunning()
            $enumRef = [System.Runtime.InteropServices.Marshal]::GetComInterfaceForObject(
                $enumIface, [System.Runtime.InteropServices.ComTypes.IEnumMoniker]
            )
            $fetched = 0
            $monikerArray = New-Object object[] 1

            while ($true) {
                $enumRef.Next(1, $monikerArray, [ref]$fetched)
                if ($fetched -eq 0) { break }
                $mk = $monikerArray[0]
                if (-not $mk) { continue }

                $displayName = ''
                try { $mk.GetDisplayName($null, $null, [ref]$displayName) } catch { continue }

                if ($displayName -match 'VisualStudio\.DTE') {
                    $obj = $rot.GetObject($mk)
                    if ($obj) {
                        $vsType = [System.Runtime.InteropServices.Type]::GetTypeFromProgID('VisualStudio.DTE')
                        $dte = [System.Runtime.InteropServices.Marshal]::GetObjectForIUnknown($obj, $vsType)
                        if ($dte) { return $dte }
                    }
                }
            }
        } catch {}

        return $null
    }

    # --- Connect ---
    Write-Host 'Waiting for Visual Studio...'
    $dte = $null
    while ($true) {
        $dte = Find-VisualStudioDTE
        if ($dte) { break }
        Start-Sleep -Seconds 2
    }
    Write-Host 'Visual Studio connected.'

    $dbg = $null
    try { $dbg = $dte.Debugger } catch {}
    if (-not $dbg) {
        Write-Error 'Cannot access the Visual Studio debugger object.'
        exit 1
    }

    Write-Host 'Waiting for an exception...'

    # --- Polling loop ---
    $lastBreakKey = ''
    $analysisRunning = $false

    while ($true) {
        try {
            $mode = 0
            try { $mode = [int]$dbg.CurrentMode } catch { Start-Sleep -Milliseconds 500; continue }

            if ($mode -ne $MODE_BREAK) {
                if ($analysisRunning) {
                    $analysisRunning = $false
                    Write-Host 'Visual Studio resumed. Waiting for another exception.'
                }
                Start-Sleep -Milliseconds 500
                continue
            }

            # In break mode -- check for an active exception
            $hasException = $false
            try {
                $ex = $dbg.CurrentException
                if ($ex) { $hasException = $true }
            } catch {
                $hasException = $false
            }

            if (-not $hasException) {
                Start-Sleep -Milliseconds 500
                continue
            }

            # De-duplicate: key from process ID + exception type
            $breakKey = ''
            try {
                $procId = $dbg.CurrentProcess.ProcessID
                $exType = $dbg.CurrentException.Type
                $breakKey = "${procId}:${exType}"
            } catch {
                $breakKey = [guid]::NewGuid().ToString()
            }

            if ($breakKey -eq $lastBreakKey) {
                Start-Sleep -Milliseconds 500
                continue
            }

            Write-Host 'Crash detected.'
            $lastBreakKey = $breakKey

            # --- Rotate crash files ---
            if (Test-Path -LiteralPath $latestCrashFile) {
                Copy-Item -LiteralPath $latestCrashFile -Destination $previousCrashFile -Force
            }

            # --- Capture crash information ---
            $ts = Get-Date
            $sb = [System.Text.StringBuilder]::new()

            [void]$sb.AppendLine('=' * 72)
            [void]$sb.AppendLine('APEX CRASH REPORT')
            [void]$sb.AppendLine('=' * 72)
            [void]$sb.AppendLine()
            [void]$sb.AppendLine("Timestamp:       $($ts.ToString('yyyy-MM-dd HH:mm:ss.fff zzz'))")
            [void]$sb.AppendLine("Break Reason:    Exception")
            [void]$sb.AppendLine()

            # Exception details
            try {
                $ex = $dbg.CurrentException
                [void]$sb.AppendLine('--- Exception ---')
                [void]$sb.AppendLine("  Type:        $($ex.Type)")
                [void]$sb.AppendLine("  Description: $($ex.Description)")
                [void]$sb.AppendLine("  Code:        $($ex.Code)")
                try { [void]$sb.AppendLine("  HexCode:     $($ex.HexCode)") } catch {}
                [void]$sb.AppendLine()
            } catch {
                [void]$sb.AppendLine('Exception details not available.')
                [void]$sb.AppendLine()
            }

            # Debugged process
            try {
                $proc = $dbg.CurrentProcess
                [void]$sb.AppendLine('--- Process ---')
                [void]$sb.AppendLine("  Name:        $($proc.Name)")
                [void]$sb.AppendLine("  Process ID:  $($proc.ProcessID)")
                [void]$sb.AppendLine()
            } catch {
                [void]$sb.AppendLine('Process info not available.')
                [void]$sb.AppendLine()
            }

            # Active thread
            try {
                $thread = $dbg.CurrentThread
                [void]$sb.AppendLine('--- Active Thread ---')
                [void]$sb.AppendLine("  Thread ID:   $($thread.ID)")
                [void]$sb.AppendLine("  Thread Name: $(try { $thread.Name } catch { 'N/A' })")
                [void]$sb.AppendLine()
            } catch {
                [void]$sb.AppendLine('Thread info not available.')
                [void]$sb.AppendLine()
            }

            # All threads
            try {
                $allThreads = $dbg.CurrentProcess.Threads
                if ($allThreads -and $allThreads.Count -gt 1) {
                    [void]$sb.AppendLine('--- All Threads ---')
                    $activeId = try { $dbg.CurrentThread.ID } catch { -1 }
                    foreach ($t in $allThreads) {
                        $marker = if ($t.ID -eq $activeId) { ' <-- active' } else { '' }
                        [void]$sb.AppendLine("  Thread $($t.ID) ($($t.Name))$marker")
                    }
                    [void]$sb.AppendLine()
                }
            } catch {}

            # Current stack frame
            try {
                $frame = $dbg.CurrentStackFrame
                [void]$sb.AppendLine('--- Current Stack Frame ---')
                [void]$sb.AppendLine("  Function:   $($frame.FunctionName)")
                [void]$sb.AppendLine("  Language:   $($frame.Language)")
                try { [void]$sb.AppendLine("  File:       $($frame.FileName)") } catch {}
                try { [void]$sb.AppendLine("  Line:       $($frame.LineNumber)") } catch {}
                [void]$sb.AppendLine()
            } catch {
                [void]$sb.AppendLine('Current stack frame not available.')
                [void]$sb.AppendLine()
            }

            # Full call stack
            try {
                $topFrame = $dbg.CurrentStackFrame
                if ($topFrame) {
                    $allFrames = $topFrame.Collection
                    if ($allFrames -and $allFrames.Count -gt 0) {
                        [void]$sb.AppendLine('--- Full Call Stack ---')
                        for ($idx = 0; $idx -lt $allFrames.Count; $idx++) {
                            $f = $allFrames.Item($idx)
                            $fn = try { $f.FunctionName } catch { '<unknown>' }
                            $sf = try { $f.FileName }     catch { '' }
                            $ln = try { $f.LineNumber }   catch { 0 }
                            $line = "  [$idx] $fn"
                            if ($sf) { $line += "  ($($sf):$($ln))" }
                            [void]$sb.AppendLine($line)
                        }
                        [void]$sb.AppendLine()
                    } else {
                        [void]$sb.AppendLine('Call stack is empty.')
                        [void]$sb.AppendLine()
                    }
                }
            } catch {
                [void]$sb.AppendLine("Could not retrieve call stack: $($_.Exception.Message)")
                [void]$sb.AppendLine()
            }

            # Active configuration / platform
            try {
                $solBuild = $dte.Solution.SolutionBuild
                $activeCfg = $solBuild.ActiveConfiguration
                [void]$sb.AppendLine('--- Configuration ---')
                [void]$sb.AppendLine("  Configuration: $($activeCfg.Name)")
                [void]$sb.AppendLine("  Platform:      $(try { $activeCfg.PlatformName } catch { 'N/A' })")
                [void]$sb.AppendLine()
            } catch {}

            # Debug Output window
            try {
                $outWin = $dte.Windows.Item('Output')
                if ($outWin) {
                    $outputObj = $outWin.Object
                    $debugPane = $outputObj.Panes.Item('Debug')
                    if ($debugPane) {
                        $debugText = $debugPane.Text
                        if ($debugText -and $debugText.Length -gt 0) {
                            [void]$sb.AppendLine('--- Debug Output ---')
                            $maxLen = [Math]::Min($debugText.Length, 8192)
                            [void]$sb.AppendLine($debugText.Substring(0, $maxLen))
                            if ($debugText.Length -gt 8192) { [void]$sb.AppendLine('... (truncated)') }
                            [void]$sb.AppendLine()
                        }
                    }
                }
            } catch {}

            # Build Output window
            try {
                $outWin = $dte.Windows.Item('Output')
                if ($outWin) {
                    $outputObj = $outWin.Object
                    $buildPane = $outputObj.Panes.Item('Build')
                    if ($buildPane) {
                        $buildText = $buildPane.Text
                        if ($buildText -and $buildText.Length -gt 0) {
                            [void]$sb.AppendLine('--- Build Output (last 4096 chars) ---')
                            $start = [Math]::Max(0, $buildText.Length - 4096)
                            [void]$sb.AppendLine($buildText.Substring($start))
                            [void]$sb.AppendLine()
                        }
                    }
                }
            } catch {}

            [void]$sb.AppendLine('=' * 72)
            [void]$sb.AppendLine('END OF CRASH REPORT')
            [void]$sb.AppendLine('=' * 72)

            # --- Save crash report ---
            $crashText = $sb.ToString()
            [System.IO.File]::WriteAllText($latestCrashFile, $crashText, [System.Text.Encoding]::UTF8)

            # --- Verify opencode CLI ---
            $opencodeOk = $false
            try {
                $null = & cmd /c 'where opencode' 2>&1
                if ($LASTEXITCODE -eq 0) { $opencodeOk = $true }
            } catch {}

            if (-not $opencodeOk) {
                Write-Warning 'OpenCode CLI not found in PATH.'
                Write-Host "Crash saved to $latestCrashFile"
                Start-Sleep -Seconds 1
                continue
            }

            # --- Verify daw-brain agent ---
            try {
                $agentList = & cmd /c 'opencode agent list' 2>&1 | Out-String
                if ($agentList -notmatch 'daw-brain') {
                    Write-Warning "'daw-brain' agent not found. Run 'opencode agent list' to verify."
                    Write-Host "Crash saved to $latestCrashFile"
                    Start-Sleep -Seconds 1
                    continue
                }
            } catch {
                Write-Warning "Could not verify daw-brain agent: $($_.Exception.Message)"
            }

            # --- Launch daw-brain ---
            Write-Host 'DAW Brain analysis started.'

            $prompt = @"
Read the crash report at .apex-debug\latest-crash.txt.

Your task:
1. Read .apex-debug\latest-crash.txt in full.
2. Inspect every APEX source location referenced in the call stack.
3. Identify the most likely root cause of the crash.
4. Distinguish the crash location from the original corruption source.
5. Provide exact file, symbol, and line evidence for each finding.
6. Inspect ownership, lifetime, threads, and invalid memory risks at each frame.
7. Propose the smallest safe correction.
8. Provide a validation plan (what to test, how to verify the fix).
9. Do NOT edit any code. Analysis only.

Save your analysis to .apex-debug\latest-analysis.md.
"@

            try {
                $output = & opencode run --agent daw-brain --dir "$RepoRoot" --auto $prompt 2>&1 | Out-String
                if ($output -and $output.Trim()) {
                    [System.IO.File]::WriteAllText($analysisFile, $output, [System.Text.Encoding]::UTF8)
                    Write-Host 'Analysis saved to .apex-debug\latest-analysis.md'
                } else {
                    Write-Warning 'DAW Brain returned no output.'
                }
            } catch {
                Write-Warning "Failed to launch DAW Brain: $($_.Exception.Message)"
            }

            $analysisRunning = $true
            Write-Host 'Waiting for an exception...'

        } catch {
            Write-Warning "Monitoring error: $($_.Exception.Message)"
        }

        Start-Sleep -Milliseconds 500
    }

} catch {
    Write-Error "Fatal watcher error: $($_.Exception.Message)"
} finally {
    if ($mutex) {
        try { $mutex.ReleaseMutex() | Out-Null } catch {}
        try { $mutex.Dispose() } catch {}
    }
}
