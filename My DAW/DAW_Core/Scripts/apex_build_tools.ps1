$script:ApexBuildToolsScriptRoot = $PSScriptRoot

if ($null -eq ('ApexBuildTools.FileSystemNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace ApexBuildTools
{
    public static class FileSystemNative
    {
        private const uint FileAttributeReparsePoint = 0x00000400;
        private const uint FileShareRead = 0x00000001;
        private const uint FileShareWrite = 0x00000002;
        private const uint FileShareDelete = 0x00000004;
        private const uint OpenExisting = 3;
        private const uint FileFlagBackupSemantics = 0x02000000;
        private const uint FileFlagOpenReparsePoint = 0x00200000;
        private const int FileAttributeTagInfoClass = 9;

        [StructLayout(LayoutKind.Sequential)]
        private struct FileAttributeTagInfo
        {
            public uint FileAttributes;
            public uint ReparseTag;
        }

        [DllImport("kernel32.dll", EntryPoint = "CreateFileW", CharSet = CharSet.Unicode,
            ExactSpelling = true, SetLastError = true)]
        private static extern SafeFileHandle CreateFile(
            string fileName,
            uint desiredAccess,
            uint shareMode,
            IntPtr securityAttributes,
            uint creationDisposition,
            uint flagsAndAttributes,
            IntPtr templateFile);

        [DllImport("kernel32.dll", ExactSpelling = true, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandleEx(
            SafeFileHandle file,
            int fileInformationClass,
            out FileAttributeTagInfo fileInformation,
            uint bufferSize);

        public static uint GetReparseTag(string path)
        {
            if (String.IsNullOrWhiteSpace(path))
                throw new ArgumentException("Payload path must not be empty.", "path");

            string nativePath = ToExtendedLengthPath(path);
            uint shareMode = FileShareRead | FileShareWrite | FileShareDelete;
            uint flags = FileFlagBackupSemantics | FileFlagOpenReparsePoint;

            using (SafeFileHandle handle = CreateFile(
                nativePath, 0, shareMode, IntPtr.Zero, OpenExisting, flags, IntPtr.Zero))
            {
                if (handle.IsInvalid)
                    throw CreateLastError("Opening reparse point metadata", path);

                FileAttributeTagInfo information;
                if (!GetFileInformationByHandleEx(
                    handle,
                    FileAttributeTagInfoClass,
                    out information,
                    (uint) Marshal.SizeOf(typeof(FileAttributeTagInfo))))
                {
                    throw CreateLastError("Reading reparse point metadata", path);
                }

                if ((information.FileAttributes & FileAttributeReparsePoint) == 0)
                    return 0;

                if (information.ReparseTag == 0)
                    throw new IOException("Reparse point metadata did not contain a tag: " + path);

                return information.ReparseTag;
            }
        }

        private static string ToExtendedLengthPath(string path)
        {
            if (path.StartsWith(@"\\?\", StringComparison.Ordinal))
                return path;

            if (path.StartsWith(@"\\", StringComparison.Ordinal))
                return @"\\?\UNC\" + path.Substring(2);

            return @"\\?\" + path;
        }

        private static Win32Exception CreateLastError(string operation, string path)
        {
            int error = Marshal.GetLastWin32Error();
            return new Win32Exception(
                error,
                operation + " failed for payload path '" + path + "' (Win32 error " + error + ").");
        }
    }
}
'@
}

function Test-ApexReparseTagNameSurrogate {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [uint32] $Tag
    )

    $nameSurrogateMask = [uint32] 0x20000000
    return (($Tag -band $nameSurrogateMask) -ne [uint32] 0)
}

function Assert-ApexPayloadPathNotReparsePoint {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path
    )

    $reparseTag = [ApexBuildTools.FileSystemNative]::GetReparseTag($Path)
    if (Test-ApexReparseTagNameSurrogate -Tag $reparseTag) {
        throw ("Payload path is a name-surrogate reparse point (tag 0x{0:X8}): {1}" -f $reparseTag, $Path)
    }
}

function Get-ApexDirectoryPayloadManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $Root
    )

    $canonicalRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    try {
        Assert-ApexPayloadPathNotReparsePoint -Path $canonicalRoot
    }
    catch {
        $nativeError = $_.Exception
        while ($null -ne $nativeError.InnerException) {
            $nativeError = $nativeError.InnerException
        }

        if ($nativeError -is [System.ComponentModel.Win32Exception] -and
            $nativeError.NativeErrorCode -in @(2, 3)) {
            throw "Payload root is missing: $canonicalRoot"
        }

        throw
    }

    if (-not (Test-Path -LiteralPath $canonicalRoot -PathType Container)) {
        throw "Payload root is missing: $canonicalRoot"
    }

    $entries = New-Object 'System.Collections.Generic.List[string]'
    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $pending.Push($canonicalRoot)

    while ($pending.Count -ne 0) {
        $directory = $pending.Pop()
        Assert-ApexPayloadPathNotReparsePoint -Path $directory

        foreach ($childDirectory in [System.IO.Directory]::GetDirectories($directory)) {
            Assert-ApexPayloadPathNotReparsePoint -Path $childDirectory
            $relativePath = $childDirectory.Substring($canonicalRoot.Length).TrimStart('\').Replace('\', '/')
            $entries.Add("D`t$relativePath")
            $pending.Push($childDirectory)
        }

        foreach ($file in [System.IO.Directory]::GetFiles($directory)) {
            Assert-ApexPayloadPathNotReparsePoint -Path $file
            $relativePath = $file.Substring($canonicalRoot.Length).TrimStart('\').Replace('\', '/')
            $item = Get-Item -LiteralPath $file
            $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
            $entries.Add("F`t$relativePath`t$($item.Length)`t$hash")
        }
    }

    $sortedEntries = $entries.ToArray()
    [System.Array]::Sort($sortedEntries, [System.StringComparer]::Ordinal)
    return $sortedEntries
}

function Test-ApexDirectoryPayloadEquivalent {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ReferenceRoot,

        [Parameter(Mandatory = $true)]
        [string] $CandidateRoot,

        [Parameter(Mandatory = $true)]
        [ref] $Difference
    )

    $Difference.Value = $null
    $referenceManifest = @(Get-ApexDirectoryPayloadManifest -Root $ReferenceRoot)
    $candidateManifest = @(Get-ApexDirectoryPayloadManifest -Root $CandidateRoot)

    if ($referenceManifest.Count -ne $candidateManifest.Count) {
        $Difference.Value = "entry count is $($candidateManifest.Count); expected $($referenceManifest.Count)"
        return $false
    }

    for ($index = 0; $index -lt $referenceManifest.Count; $index++) {
        if ($referenceManifest[$index] -cne $candidateManifest[$index]) {
            $Difference.Value = "entry differs at sorted index $index; expected '$($referenceManifest[$index])'; found '$($candidateManifest[$index])'"
            return $false
        }
    }

    return $true
}

function Publish-ApexDirectoryAtomically {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $PreparedRoot,

        [Parameter(Mandatory = $true)]
        [string] $DestinationRoot
    )

    $canonicalPreparedRoot = [System.IO.Path]::GetFullPath($PreparedRoot)
    $canonicalDestinationRoot = [System.IO.Path]::GetFullPath($DestinationRoot)

    try {
        [System.IO.Directory]::Move($canonicalPreparedRoot, $canonicalDestinationRoot)
    }
    catch {
        if (Test-Path -LiteralPath $canonicalDestinationRoot) {
            throw "Refusing to publish payload because the destination appeared: $canonicalDestinationRoot"
        }

        throw
    }
    finally {
        if (Test-Path -LiteralPath $canonicalPreparedRoot -PathType Container) {
            [System.IO.Directory]::Delete($canonicalPreparedRoot, $true)
        }
    }
}

function Get-ApexBuildTools {
    [CmdletBinding()]
    param()

    $repositoryRoot = [System.IO.Path]::GetFullPath(
        (Split-Path -Parent $script:ApexBuildToolsScriptRoot)
    ).TrimEnd('\')
    $contractPath = Join-Path $repositoryRoot 'Dependencies\apex-windows-dependencies.json'

    if (-not (Test-Path -LiteralPath $contractPath -PathType Leaf)) {
        throw "Dependency contract is missing: $contractPath"
    }

    $contract = Get-Content -LiteralPath $contractPath -Raw | ConvertFrom-Json
    if ([int] $contract.toolchain.visualStudioMajor -ne 18) {
        throw "Unsupported Visual Studio major in dependency contract: $($contract.toolchain.visualStudioMajor)"
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw "vswhere.exe is missing: $vswhere"
    }

    $vswhereArguments = @(
        '-latest',
        '-products', '*',
        '-requires', 'Microsoft.Component.MSBuild',
        '-version', '[18.0,19.0)',
        '-property', 'installationPath'
    )
    $installationOutput = @(& $vswhere @vswhereArguments 2>&1)
    $vswhereExitCode = $LASTEXITCODE

    if ($vswhereExitCode -ne 0) {
        throw "vswhere.exe failed with exit code $vswhereExitCode`: $($installationOutput -join [Environment]::NewLine)"
    }

    $installationPaths = @(
        $installationOutput |
            ForEach-Object { ([string] $_).Trim() } |
            Where-Object { $_ }
    )
    if ($installationPaths.Count -ne 1) {
        throw 'Visual Studio 18 with Microsoft.Component.MSBuild was not resolved uniquely.'
    }

    $visualStudio = [System.IO.Path]::GetFullPath($installationPaths[0])
    $msbuild = Join-Path $visualStudio 'MSBuild\Current\Bin\amd64\MSBuild.exe'
    if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) {
        throw "Visual Studio 18 amd64 MSBuild is missing: $msbuild"
    }

    $juceRoot = [System.IO.Path]::GetFullPath(
        (Join-Path $repositoryRoot ([string] $contract.juce.rootRelativeToRepository))
    )
    $projucer = Join-Path $juceRoot 'Projucer.exe'
    if (-not (Test-Path -LiteralPath $projucer -PathType Leaf)) {
        throw "Pinned Projucer is missing: $projucer"
    }

    $solution = Join-Path $repositoryRoot 'Builds\VisualStudio2026\DAW_Core.sln'
    if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) {
        throw "Generated Visual Studio solution is missing: $solution"
    }

    $outputPaths = @{}
    foreach ($configuration in @('Debug', 'Release')) {
        $outputRoot = Join-Path $repositoryRoot "Builds\VisualStudio2026\x64\$configuration\App"
        $outputPaths[$configuration] = [pscustomobject]@{
            Exe = [System.IO.Path]::GetFullPath((Join-Path $outputRoot 'DAW_Core.exe'))
            Pdb = [System.IO.Path]::GetFullPath((Join-Path $outputRoot 'DAW_Core.pdb'))
        }
    }

    return [pscustomobject]@{
        RepositoryRoot = $repositoryRoot
        DependencyContract = $contractPath
        VSWhere = [System.IO.Path]::GetFullPath($vswhere)
        VisualStudio = $visualStudio
        MSBuild = [System.IO.Path]::GetFullPath($msbuild)
        Projucer = [System.IO.Path]::GetFullPath($projucer)
        Solution = [System.IO.Path]::GetFullPath($solution)
        OutputPaths = $outputPaths
    }
}
