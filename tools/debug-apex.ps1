#Requires -Version 5.1
<#
.SYNOPSIS
    Builds APEX (DAW_Core) as x64 Debug and launches it under Microsoft CDB.
.DESCRIPTION
    1. Builds DAW_Core.sln as Debug|x64 using MSBuild.
    2. Verifies the executable and PDB exist.
    3. Locates cdb.exe (Debugging Tools for Windows).
    4. Launches the executable under CDB with symbol paths.
    5. Catches unhandled native C++ exceptions and access violations.
    6. On crash, runs .exr -1, .ecxr, kb, kv, lm, r.
    7. Writes complete output to crash-reports\APEX_CDB_CRASH_<timestamp>.txt.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Configuration ──────────────────────────────────────────────────────────────

$WorkspaceRoot = Split-Path -Parent $PSScriptRoot
$SolutionDir   = Join-Path $WorkspaceRoot 'My DAW\DAW_Core\Builds\VisualStudio2026'
$SolutionFile  = Join-Path $SolutionDir 'DAW_Core.sln'
$AppProject    = Join-Path $SolutionDir 'DAW_Core_App.vcxproj'

$Configuration = 'Debug'
$Platform      = 'x64'
$TargetName    = 'DAW_Core'
$OutputRelDir  = 'x64\Debug\App'
$ExeDir        = Join-Path $SolutionDir $OutputRelDir
$ExePath       = Join-Path $ExeDir "$TargetName.exe"
$PdbPath       = Join-Path $ExeDir "$TargetName.pdb"

$CrashReportsDir = Join-Path $WorkspaceRoot 'crash-reports'

# ── Helper Functions ───────────────────────────────────────────────────────────

function Write-Header {
    param([string]$Message)
    Write-Host "`n=== $Message ===" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "  [OK] $Message" -ForegroundColor Green
}

function Write-Fail {
    param([string]$Message)
    Write-Host "  [FAIL] $Message" -ForegroundColor Red
}

function Write-Info {
    param([string]$Message)
    Write-Host "  [INFO] $Message" -ForegroundColor Yellow
}

function Find-Cdb {
    # Search order for cdb.exe (x64 only)
    $searchPaths = @()

    # 1. Windows Kits Debugging Tools (classic install)
    foreach ($base in @('C:\Program Files (x86)\Windows Kits', 'C:\Program Files\Windows Kits')) {
        if (Test-Path $base) {
            Get-ChildItem $base -Recurse -Filter 'cdb.exe' -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match '\\x64\\' -and $_.FullName -match 'Debuggers' } |
                ForEach-Object { $searchPaths += $_.FullName }
        }
    }

    # 2. WinDbg Preview (Microsoft Store)
    $windbgPreviewPaths = @(
        'C:\Program Files\WindowsApps\Microsoft.WinDbg*\x64\cdb.exe',
        'C:\Program Files\WindowsApps\Microsoft.WinDbg*\cdb.exe'
    )
    foreach ($pattern in $windbgPreviewPaths) {
        Get-Item $pattern -ErrorAction SilentlyContinue |
            ForEach-Object { $searchPaths += $_.FullName }
    }

    # 3. PATH environment variable
    $pathCdb = Get-Command 'cdb.exe' -ErrorAction SilentlyContinue
    if ($pathCdb) { $searchPaths += $pathCdb.Source }

    # 4. Common manual install locations
    $manualPaths = @(
        'C:\Debugging Tools for Windows (x64)\cdb.exe',
        'C:\Program Files\Debugging Tools for Windows (x64)\cdb.exe'
    )
    foreach ($p in $manualPaths) {
        if (Test-Path $p) { $searchPaths += $p }
    }

    # Return first found
    if ($searchPaths.Count -gt 0) {
        return $searchPaths[0]
    }

    return $null
}

# ── Step 1: Verify Solution Exists ─────────────────────────────────────────────

Write-Header 'Step 1: Verifying solution and project files'

if (-not (Test-Path $SolutionFile)) {
    Write-Fail "Solution not found: $SolutionFile"
    exit 1
}
Write-Success "Solution: $SolutionFile"

if (-not (Test-Path $AppProject)) {
    Write-Fail "App project not found: $AppProject"
    exit 1
}
Write-Success "App project: $AppProject"

# ── Step 2: Find MSBuild ───────────────────────────────────────────────────────

Write-Header 'Step 2: Locating MSBuild'

$msbuild = $null

# Try VS2026 / VS2022 / VS2019 paths
$vsVersions = @('18', '17', '16')
foreach ($ver in $vsVersions) {
    foreach ($edition in @('Community', 'Professional', 'Enterprise', 'BuildTools')) {
        $candidate = "C:\Program Files\Microsoft Visual Studio\$ver\$edition\MSBuild\Current\Bin\amd64\MSBuild.exe"
        if (Test-Path $candidate) {
            $msbuild = $candidate
            break
        }
        $candidate = "C:\Program Files\Microsoft Visual Studio\$ver\$edition\MSBuild\Current\Bin\MSBuild.exe"
        if (Test-Path $candidate) {
            $msbuild = $candidate
            break
        }
    }
    if ($msbuild) { break }
}

# Try vswhere as fallback
if (-not $msbuild) {
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
        if ($installPath) {
            $amd64Candidate = Join-Path $installPath 'MSBuild\Current\Bin\amd64\MSBuild.exe'
            if (Test-Path $amd64Candidate) {
                $msbuild = $amd64Candidate
            } else {
                $msbuild = Join-Path $installPath 'MSBuild\Current\Bin\MSBuild.exe'
            }
        }
    }
}

if (-not $msbuild -or -not (Test-Path $msbuild)) {
    Write-Fail "MSBuild not found. Install Visual Studio with C++ Desktop workload."
    exit 1
}
Write-Success "MSBuild: $msbuild"

# ── Step 3: Build APEX ────────────────────────────────────────────────────────

Write-Header "Step 3: Building APEX ($Configuration|$Platform)"

# Create crash-reports directory if it does not exist
if (-not (Test-Path $CrashReportsDir)) {
    New-Item -ItemType Directory -Path $CrashReportsDir -Force | Out-Null
}

# Build the full solution — the project is registered as "DAW_Core - App" in the .sln,
# and /t:DAW_Core_App does not match any MSBuild target.
$buildArgs = @(
    $SolutionFile
    "/p:Configuration=$Configuration"
    "/p:Platform=$Platform"
    "/p:BuildProjectReferences=true"
    "/m"
    "/v:minimal"
)

Write-Info "Running: & `"$msbuild`" $($buildArgs -join ' ')"
& $msbuild @buildArgs

if ($LASTEXITCODE -ne 0) {
    Write-Fail "Build failed with exit code $LASTEXITCODE"
    exit 1
}
Write-Success "Build succeeded"

# ── Step 4: Verify EXE and PDB ────────────────────────────────────────────────

Write-Header 'Step 4: Verifying executable and PDB'

if (-not (Test-Path $ExePath)) {
    Write-Fail "Executable not found: $ExePath"
    exit 1
}
$exeSize = (Get-Item $ExePath).Length / 1MB
Write-Success "EXE: $ExePath ($([math]::Round($exeSize, 1)) MB)"

if (-not (Test-Path $PdbPath)) {
    Write-Fail "PDB not found: $PdbPath"
    exit 1
}
$pdbSize = (Get-Item $PdbPath).Length / 1MB
Write-Success "PDB: $PdbPath ($([math]::Round($pdbSize, 1)) MB)"

# ── Step 5: Locate CDB ────────────────────────────────────────────────────────

Write-Header 'Step 5: Locating CDB (Debugging Tools for Windows)'

$cdbPath = Find-Cdb

if (-not $cdbPath -or -not (Test-Path $cdbPath)) {
    Write-Fail "cdb.exe not found on this system."
    Write-Host ""
    Write-Host "  MISSING COMPONENT: Debugging Tools for Windows" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Install via one of these methods:" -ForegroundColor Yellow
    Write-Host "    1. Windows SDK: install 'Debugging Tools for Windows' from"
    Write-Host "       https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/"
    Write-Host "    2. WinDbg Preview: install from the Microsoft Store"
    Write-Host "       (includes cdb.exe at: C:\Program Files\WindowsApps\Microsoft.WinDbg*\cdb.exe)"
    Write-Host ""
    exit 1
}
Write-Success "CDB: $cdbPath"

# ── Step 6: Create CDB Command Script ─────────────────────────────────────────

Write-Header 'Step 6: Creating CDB command script'

$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$crashReportPath = Join-Path $CrashReportsDir "APEX_CDB_CRASH_$timestamp.txt"
$crashDumpPath   = Join-Path $CrashReportsDir "APEX_CDB_CRASH_$timestamp.dmp"
$temporaryDumpPath = "C:\Users\1993v\AppData\Local\Temp\opencode\APEX_CDB_CRASH_$timestamp.dmp"
# The dump command is embedded inside CDB's quoted `sxd -c2` command string.
# Escape path separators for that parser; unescaped backslashes were consumed
# and produced flattened filenames in the workspace root.
$cdbTemporaryDumpPath = $temporaryDumpPath.Replace('\', '\\')

# Symbol path: APEX exe dir, PDB dir, Microsoft public symbol server
$symbolCache = Join-Path $env:LOCALAPPDATA 'SymbolCache'
$symPath = "srv*$symbolCache*https://msdl.microsoft.com/download/symbols"
# Symbol paths contain directories, not individual PDB files.  CDB finds
# DAW_Core.pdb beside the executable once the output directory is listed.
$symPath += ";$ExeDir"

$fatalCommands = ".echo; .echo === FATAL EXCEPTION ===; .exr -1; .ecxr; kb; kv; lm; r; .dump /ma $cdbTemporaryDumpPath; .echo === END CRASH REPORT ===; q"
$breakpointPolicy = if ($env:APEX_CDB_TRACE_BREAKPOINTS -eq '1') {
    'sxe -c ".echo; .echo === BREAKPOINT TRACE ===; .exr -1; kb; .echo === END BREAKPOINT TRACE ===; g" 80000003'
} else {
    'sxi 80000003'
}

$cdbScript = @"
.sympath $symPath
.reload /f

$breakpointPolicy
sxi 80000004

sxd -c2 "$fatalCommands" c0000005
sxd -c2 "$fatalCommands" e06d7363
sxd -c2 "$fatalCommands" c00000fd
sxd -c2 "$fatalCommands" c000001d
sxd -c2 "$fatalCommands" c0000374
sxd -c2 "$fatalCommands" c0000409

.echo
.echo ============================================
.echo   APEX CRASH DEBUGGER
.echo   $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
.echo   Crash report: $crashReportPath
.echo ============================================
.echo
.echo Instructions:
.echo   - APEX is now running under CDB
.echo   - If APEX crashes, CDB will break automatically
.echo   - At the (CDB) prompt, type 'q' to quit
.echo   - After quitting, the crash report will be saved
.echo   - For handled exceptions (plugins), type 'gn' to continue
.echo
g
.echo === TARGET EXITED WITHOUT FATAL EXCEPTION ===
q
"@

$cdbScriptPath = Join-Path $PSScriptRoot 'cdb_commands.txt'
$cdbScript | Out-File -FilePath $cdbScriptPath -Encoding ASCII -Force
Write-Success "CDB script: $cdbScriptPath"

# ── Step 7: Launch Under CDB ──────────────────────────────────────────────────

Write-Header 'Step 7: Launching APEX under CDB'

Write-Info "Symbol path:   $symPath"
Write-Info "Executable:    $ExePath"
Write-Info "Working dir:   $ExeDir"
Write-Info "Crash report:  $crashReportPath"
Write-Host ""
Write-Host "  APEX will launch under CDB." -ForegroundColor Green
Write-Host "  If a crash occurs, CDB will break and display the exception." -ForegroundColor Green
Write-Host "  Type 'q' to quit CDB after a crash." -ForegroundColor Yellow
Write-Host "  For handled plugin exceptions, type 'gn' to continue." -ForegroundColor DarkGray
Write-Host ""

# Set _NT_SYMBOL_PATH for CDB
$env:_NT_SYMBOL_PATH = $symPath

# Launch CDB with the command script.
# -cf is CDB's command-file option.  The old `-c "cmds < file"` form was
# parsed as a debugger command and failed at the initial loader breakpoint,
# so APEX never actually started under the debugger.
# -o debugs child processes as well as the launched executable.
# -y sets symbol path
# -lines shows source lines when symbols are available
$cdbArgs = @(
    '-cf', $cdbScriptPath
    '-o'
    '-logo', $crashReportPath
    '-y', $symPath
    '-lines'
    $ExePath
)

Write-Info "Launching: `"$cdbPath`" $($cdbArgs -join ' ')"
Write-Host ""

# CDB blocks here until the user quits it
& $cdbPath @cdbArgs

$cdbExitCode = $LASTEXITCODE

if (Test-Path -LiteralPath $temporaryDumpPath) {
    Move-Item -LiteralPath $temporaryDumpPath -Destination $crashDumpPath -Force
}

# ── Step 8: Post-Session ──────────────────────────────────────────────────────

Write-Host ""
Write-Header 'Step 8: CDB session ended'
Write-Info "CDB exit code: $cdbExitCode"

# Check if any new crash report files exist
$latestReport = Get-ChildItem $CrashReportsDir -Filter 'APEX_CDB_CRASH_*.txt' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if ($latestReport) {
    Write-Host ""
    Write-Host "  Crash report found: $($latestReport.Name)" -ForegroundColor Yellow
    Write-Host "  Full path: $($latestReport.FullName)" -ForegroundColor White
    Write-Host ""
    Write-Host "  To analyze this crash, ask OpenCode:" -ForegroundColor Yellow
    Write-Host "    'debug APEX crash report'" -ForegroundColor White
} else {
    Write-Host ""
    Write-Host "  No crash reports were generated." -ForegroundColor DarkGray
    Write-Host "  (APEX may have exited cleanly or CDB did not capture a crash)" -ForegroundColor DarkGray
}

Write-Host ""
Write-Host "  To re-run: powershell -ExecutionPolicy Bypass -File tools\debug-apex.ps1" -ForegroundColor DarkGray
Write-Host ""
