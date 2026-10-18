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
$jpeg = Join-Path (Split-Path -Parent $PSScriptRoot) 'Assets\EvoEye\evo_eye.jpg'

if (-not (Test-Path -LiteralPath $jpeg -PathType Leaf)) {
    Write-Warning "[EVO EYE] Official JPEG source not committed yet. Resource test deferred on this draft branch."
    return
}

if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "APEX executable missing: $exe"
}

# Read Win32 RCDATA directly from the PE file without starting the EXE.
# Proves the exact source JPEG bytes were embedded, not copied as a sidecar.
if (-not ('ApexEvoEyeResourceAudit' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class ApexEvoEyeResourceAudit
{
    const uint LOAD_LIBRARY_AS_DATAFILE = 0x00000002;
    const uint LOAD_LIBRARY_AS_IMAGE_RESOURCE = 0x00000020;
    const int RT_RCDATA = 10;

    [DllImport("kernel32.dll", EntryPoint = "LoadLibraryExW", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr LoadLibraryEx(string filename, IntPtr reserved, uint flags);

    [DllImport("kernel32.dll", EntryPoint = "FindResourceW", SetLastError = true)]
    static extern IntPtr FindResource(IntPtr module, IntPtr resourceName, IntPtr resourceType);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint SizeofResource(IntPtr module, IntPtr resourceInfo);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr LoadResource(IntPtr module, IntPtr resourceInfo);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr LockResource(IntPtr loadedResource);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool FreeLibrary(IntPtr module);

    public static byte[] GetEvoEye(string executable)
    {
        var module = LoadLibraryEx(executable, IntPtr.Zero,
            LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (module == IntPtr.Zero)
            throw new InvalidOperationException("Cannot inspect APEX PE: " + Marshal.GetLastWin32Error());

        try
        {
            var resource = FindResource(module, new IntPtr(27182), new IntPtr(RT_RCDATA));
            if (resource == IntPtr.Zero)
                throw new InvalidOperationException("APEX PE is missing embedded Evo Eye RCDATA 27182");

            var size = SizeofResource(module, resource);
            var h = LoadResource(module, resource);
            var ptr = LockResource(h);
            if (size == 0 || h == IntPtr.Zero || ptr == IntPtr.Zero)
                throw new InvalidOperationException("Evo Eye resource is corrupt or empty");

            var bytes = new byte[checked((int) size)];
            Marshal.Copy(ptr, bytes, 0, bytes.Length);
            return bytes;
        }
        finally
        {
            FreeLibrary(module);
        }
    }
}
'@
}

$expected = [System.IO.File]::ReadAllBytes($jpeg)
$actual = [ApexEvoEyeResourceAudit]::GetEvoEye($exe)
$sha = [System.Security.Cryptography.SHA256]::Create()
try {
    $expectedHash = [System.BitConverter]::ToString($sha.ComputeHash($expected)).Replace('-', '')
    $actualHash = [System.BitConverter]::ToString($sha.ComputeHash($actual)).Replace('-', '')
}
finally {
    $sha.Dispose()
}

if ($actualHash -ne $expectedHash) {
    throw "Embedded Evo Eye resource differs from source JPEG! Source=$expectedHash PE=$actualHash"
}
Write-Host "[PASS] EVO EYE $Configuration in-EXE RCDATA 27182 matches exact official JPEG SHA-256 $actualHash"
