# Visual Studio Crash Bridge — Design Spec

## Purpose

Automatically capture Visual Studio native C++ exception crashes during APEX F5 debugging and launch the `daw-brain` OpenCode agent to analyze root causes.

## File

`tools\Watch-VisualStudioCrash.ps1` — single self-contained PowerShell script.

## Architecture

### Singleton Enforcement
- Named mutex `Global\APEX_VS_CrashWatcher` prevents multiple watcher instances.
- On startup, attempt to create the mutex. If it already exists, print error and exit.

### Visual Studio Discovery
- Enumerate the Running Object Table (ROT) via `System.Runtime.InteropServices.Marshal.GetRunningObjectTable()`.
- Filter for entries whose moniker contains `!VisualStudio.DTE` (any version suffix).
- Cast the COM object to `EnvDTE.DTE`. Cache the reference.
- If no VS instance is found, print error and exit.
- If the VS process exits, reconnect on next poll cycle.

### Polling Loop
- 1-second `Start-Sleep` interval.
- On each iteration, check:
  - `debugger.CurrentProgram IsNot Nothing` — a program is being debugged.
  - `debugger.CurrentMode -eq [EnvDTE.dbgDebugMode]::dbgBreakMode` — VS is in break mode.
  - `debugger.LastBreakReason -eq [EnvDTE.dbgDebuggerExceptions]::dbgException` — break was caused by exception.

### Exception Filtering (Ignore Rules)
The watcher triggers ONLY when all conditions are met:
1. VS transitioned from Run Mode to Break Mode.
2. `LastBreakReason` is `dbgException`.

The watcher ignores:
- `dbgBreakpoint` — normal breakpoints.
- `dbgStep` — stepping (F10/F11).
- `dbgStop` — program exit / Stop debugging.
- Manual Pause (`dbgBreak` without exception).
- Any break event whose timestamp matches the previously processed event (dedup).

### Crash Information Capture
Extract everything EnvDTE exposes:

| Field | Source |
|-------|--------|
| Timestamp | `Get-Date -Format "o"` |
| Break reason | `debugger.LastBreakReason` |
| Exception type | `debugger.CurrentProgram.ExceptionFields` or exception dialog |
| Exception message | Same source |
| Exception address | `debugger.CurrentProgram.ExceptionFields` (if available) |
| Debugged executable | `debugger.CurrentProgram.Name` |
| Process ID | `debugger.CurrentProgram.ProcessID` |
| Active thread | `debugger.CurrentProgram.SelectedThread` |
| Current stack frame | `debugger.CurrentProgram.CurrentFrame` |
| Full call stack | Iterate `debugger.CurrentProgram.CurrentFrame.Chain` |
| Function names | Each frame's `FunctionName` |
| Source files + lines | Each frame's `Location` (file + line) |
| Debug Output window | `dte.Windows.Item("Output").Object.OutputWindow panes("Debug")` |
| Build Output window | `dte.Windows.Item("Output").Object.OutputWindow panes("Build")` |
| Active configuration | `dte.Solution.SolutionBuild.ActiveConfiguration` |
| Platform | `dte.Solution.SolutionBuild.ActiveConfiguration` (platform portion) |

### File Rotation
1. If `.apex-debug\latest-crash.txt` exists, copy it to `.apex-debug\previous-crash.txt`.
2. Write new crash data to `.apex-debug\latest-crash.txt`.

### OpenCode Invocation
- Use `cmd /c opencode run --agent daw-brain --dir "<repo-root>" "<prompt>"` to avoid PowerShell execution policy issues.
- The prompt instructs daw-brain to:
  - Read `.apex-debug\latest-crash.txt`
  - Inspect every referenced APEX source location
  - Identify most likely root cause
  - Distinguish crash location from original corruption source
  - Provide exact file, symbol, and line evidence
  - Inspect ownership, lifetime, threads, invalid memory risks
  - Propose smallest safe correction
  - Provide validation plan
  - NOT edit code automatically
- Capture stdout to `.apex-debug\latest-analysis.md`.

### Continue Watching
After analysis completes, wait for VS to resume (`dbgRunMode`), then continue monitoring for the next exception.

## Error Conditions

| Condition | Message |
|-----------|---------|
| No VS instance in ROT | "Error: No running Visual Studio instance found. Start Visual Studio and open a solution first." |
| ROT cast fails | "Error: Cannot access Visual Studio EnvDTE. Ensure no elevation mismatch." |
| No call stack available | "Warning: No call stack available. The crash may have occurred outside managed/native boundary." |
| opencode not in PATH | "Error: 'opencode' not found in PATH. Install OpenCode CLI first." |
| daw-brain agent missing | "Error: 'daw-brain' agent not found. Run 'opencode agent list' to verify." |

## Status Messages

```
Waiting for Visual Studio...
Visual Studio connected.
Waiting for an exception...
Crash detected.
DAW Brain analysis started.
Analysis saved to .apex-debug\latest-analysis.md
Visual Studio resumed. Waiting for another exception.
```

## Constraints

- Never inject code into APEX or the debugged process.
- Never create a Windows service, VSIX, or MCP server.
- Never modify APEX source code.
- Single file only: `tools\Watch-VisualStudioCrash.ps1`.
