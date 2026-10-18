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
if ($header -match 'TranslateTransform\(0\.0f,\s*-32\.0f\)' -or
    $header -match 'Gdiplus::Region upper\(covered\)') {
    throw 'Reintroduced translated surprised eyebrow or opaque closed oval shutter.'
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
    @{ Name = 'Eyebrow safely excluded from full iris opening'; Expression = 'g\.SetClip\(&fullEye, Gdiplus::CombineModeExclude\)' },
    @{ Name = 'Independent upper/lower closure shutters'; Expression = 'static void drawShutters' },
    @{ Name = 'Moving eyelid seam separate from eyebrow'; Expression = 'static void drawLidRims' },
    @{ Name = 'Angular angry inner glabella'; Expression = 'P\(583,244\)' },
    @{ Name = 'Twin frowning upper-lid curves'; Expression = 'static void appendUpperLid' },
    @{ Name = 'Matched lower-lid curves'; Expression = 'static void appendLowerLid' },
    @{ Name = 'A single closed chevron seam'; Expression = 'if \(openness < 0\.035f\)' },
    @{ Name = 'No solid closed eye oval'; Expression = 'if \(strength < 0\.003f\)' },
    @{ Name = 'Chamber fades to background on blink'; Expression = '242\.0f \* material' },
    @{ Name = 'Electric iris strands'; Expression = 'for \(int i = 0; i < 78;' },
    @{ Name = 'Handcrafted APEX letterforms'; Expression = 'brand.AddLine\(P\(435, 615\)' },
    @{ Name = 'Curved layered steel ornaments'; Expression = 'static void sculptedArmour' },
    @{ Name = 'Code-drawn metallic bevels'; Expression = 'static void sculptedPlate' },
    @{ Name = 'Electric arc strands'; Expression = 'filament\[9\]' },
    @{ Name = 'Exact official tagline'; Expression = 'L"AUDIO PRODUCTION ENGINEERING"' },
    @{ Name = 'Live fade to StartupPanel'; Expression = 'WaitForSingleObject\(stop_, 0\)' },
    @{ Name = 'Independent Win32 render loop'; Expression = 'static DWORD WINAPI runThread' }
)
foreach ($feature in $features) {
    if ($header -notmatch $feature.Expression) {
        throw "Procedural boot GUI feature missing: $($feature.Name)"
    }
}

# Prevent the old regression: drawing upper armour AFTER the iris
# occludes the opening, and merging the two draws gives a red horizontal bar
# with overlapping static eyebrows in the blinked-shut frame.
$renderStart = $header.IndexOf('static void drawFrame(G& g, float seconds)')
$renderEnd = $header.IndexOf('DWORD run()', $renderStart)
if ($renderStart -lt 0 -or $renderEnd -lt $renderStart) {
    throw 'Cannot inspect drawFrame z-order'
}
$frameBody = $header.Substring($renderStart, $renderEnd - $renderStart)
$requiredOrder = @(
    'g.SetClip(&fullEye, Gdiplus::CombineModeExclude)',
    'plating(g)',
    'g.FillPath(&chamber, &fullEye)',
    'drawShutters(g, openness)',
    'iris(g, seconds, openness)',
    'drawLidRims(g, openness)',
    'identity(g, seconds)'
)
$position = -1
foreach ($call in $requiredOrder) {
    $found = $frameBody.IndexOf($call, $position + 1, [StringComparison]::Ordinal)
    if ($found -lt 0) {
        throw "Evo Eye z-order violated at: $call"
    }
    $position = $found
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
Write-Host "[PASS] EVO EYE $Configuration source-rendered C++ GUI verified: $($features.Count) features; no JPEG/PNG, no RCDATA 27182. SHA-256 $hash"
