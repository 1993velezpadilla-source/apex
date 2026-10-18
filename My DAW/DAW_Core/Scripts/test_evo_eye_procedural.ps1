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
$root = Split-Path -Parent $PSScriptRoot
$headerPath = Join-Path $root 'Source\SplashScreen.h'
$rcPath = Join-Path $root 'Builds\VisualStudio2026\resources.rc'
$projectPath = Join-Path $root 'Builds\VisualStudio2026\DAW_Core_App.vcxproj'
$header = Get-Content -LiteralPath $headerPath -Raw
$resource = Get-Content -LiteralPath $rcPath -Raw
$project = Get-Content -LiteralPath $projectPath -Raw

if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "APEX $Configuration executable missing: $exe"
}
# The reference picture must NOT be compiled in or loaded from disk.
if (Test-Path -LiteralPath (Join-Path $root 'Assets\EvoEye\evo_eye.jpg')) {
    throw 'Obsolete eye JPEG exists in the source tree.'
}
if ($header -match 'Bitmap::From(File|Stream)|FindResourceW|SHCreateMemStream|\.png|\.jpe?g') {
    throw 'APEX splash still loads a raster image instead of drawing its own GUI.'
}
if ($resource -match '27182\s+RCDATA|APEX_EVO_EYE_EMBEDDED_ART' -or
    $project -match 'APEX_EVO_EYE_EMBEDDED_ART') {
    throw 'Windows build still embeds the old bootup picture.'
}

$features = @(
    @{ Name = 'Procedural eyelid graphics'; Expression = 'makeEyeContour' },
    @{ Name = 'Vector metal plating'; Expression = 'static void plating' },
    @{ Name = 'Live rendered eye'; Expression = 'static void iris' },
    @{ Name = 'Real frame-by-frame blink'; Expression = 'static float blink' },
    @{ Name = 'Electric iris strands'; Expression = 'for \(int i = 0; i < 78;' },
    @{ Name = 'APEX vector wordmark'; Expression = 'textPath.AddString\(L"APEX"' },
    @{ Name = 'Exact official tagline'; Expression = 'L"AUDIO PRODUCTION ENGINEERING"' },
    @{ Name = 'Live fade to StartupPanel'; Expression = 'WaitForSingleObject\(stop_, 0\)' },
    @{ Name = 'Independent Win32 render loop'; Expression = 'static DWORD WINAPI runThread' }
)
foreach ($feature in $features) {
    if ($header -notmatch $feature.Expression) {
        throw "Procedural boot GUI feature missing: $($feature.Name)"
    }
}

if (-not ('ApexProceduralEyeResourceAudit' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class ApexProceduralEyeResourceAudit
{
    [DllImport("kernel32.dll", EntryPoint="LoadLibraryExW", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr LoadLibraryEx(string name, IntPtr reserved, uint flags);

    [DllImport("kernel32.dll", EntryPoint="FindResourceW", SetLastError=true)]
    static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);

    [DllImport("kernel32.dll")]
    static extern bool FreeLibrary(IntPtr module);

    public static bool HasObsoleteEyeJpeg(string exe)
    {
        const uint LOAD_LIBRARY_AS_DATAFILE = 0x00000002;
        const uint LOAD_LIBRARY_AS_IMAGE_RESOURCE = 0x00000020;
        IntPtr module = LoadLibraryEx(exe, IntPtr.Zero,
                                      LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (module == IntPtr.Zero)
            throw new InvalidOperationException("Cannot inspect built EXE: " + Marshal.GetLastWin32Error());
        try
        {
            // The old raster image lived at Win32 RCDATA id 27182.
            return FindResource(module, new IntPtr(27182), new IntPtr(10)) != IntPtr.Zero;
        }
        finally { FreeLibrary(module); }
    }
}
'@
}
if ([ApexProceduralEyeResourceAudit]::HasObsoleteEyeJpeg($exe)) {
    throw 'Built DAW_Core.exe still contains the obsolete JPEG in RCDATA 27182.'
}
$hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
Write-Host "[PASS] EVO EYE $Configuration source-rendered C++ GUI verified: 9 features; no JPEG/PNG, no RCDATA 27182. SHA-256 $hash"
