$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Assert-Ignored([string] $Path) {
    & git -C $repo check-ignore --quiet --no-index -- $Path
    if ($LASTEXITCODE -ne 0) { throw "[FAIL] expected ignored: $Path" }
}

function Assert-NotIgnored([string] $Path) {
    & git -C $repo check-ignore --quiet --no-index -- $Path
    if ($LASTEXITCODE -eq 0) { throw "[FAIL] active source is ignored: $Path" }
}

Assert-Ignored 'Builds/VisualStudio2026/crash-reports/probe.dmp'
Assert-Ignored 'Builds/VisualStudio2026/dumpbin_release.txt'
Assert-Ignored 'Builds/VisualStudio2026/PerformanceTests2/probe/x64/Debug/PerformanceTests2.pch'
Assert-Ignored 'tools/VisualStudioBridge/bin/Release/probe.exe'
Assert-Ignored 'tools/VisualStudioBridge/obj/probe.cache'
Assert-Ignored '.apex-debug/probe.txt'
Assert-Ignored 'evidence/runs/probe/manifest.json'
Assert-NotIgnored 'Builds/VisualStudio2026/ArrangementEditor/ArrangementViewCore.cpp'
Assert-NotIgnored 'Tests/Source/Main.cpp'

'[PASS] application repository policy'
exit 0
