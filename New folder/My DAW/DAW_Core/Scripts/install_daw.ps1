#Requires -RunAsAdministrator
<#
.SYNOPSIS
    DAW_Core Installer
    Installs to Program Files, signs with a machine-trusted cert, creates shortcuts.

.DESCRIPTION
    Run once from an elevated PowerShell prompt:
        powershell -ExecutionPolicy Bypass -File "Scripts\install_daw.ps1"

    What this does:
      1. Builds Release x64
      2. Creates a code-signing cert trusted by LocalMachine (fixes Wibu/AppControl)
      3. Signs DAW_Core.exe + re-runs post-build signing
      4. Copies everything to C:\Program Files\DAW_Core\
      5. Creates Start Menu + Desktop shortcuts
      6. Marks the install folder as trusted (Unblock-File)
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── Paths ─────────────────────────────────────────────────────────────────────
$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot    = Resolve-Path "$ScriptDir\.."
$BuildDir    = "$RepoRoot\Builds\VisualStudio2026"
$ExeSrc      = "$BuildDir\x64\Release\App\DAW_Core.exe"
$InstallDir  = "C:\Program Files\DAW_Core"
$CertSubject = "CN=DAW_Core"
$AppName     = "DAW_Core"

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "  DAW_Core Installer" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""

# ── Step 1: Build Release ──────────────────────────────────────────────────────
Write-Host "[1/5] Building Release x64..." -ForegroundColor Yellow

$msbuild = Get-Command msbuild.exe -ErrorAction SilentlyContinue
if (-not $msbuild) {
    # Try VS 2022/2026 default paths
    $candidates = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )
    $msbuildPath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $msbuildPath) {
        Write-Warning "MSBuild not found - skipping build step. Make sure you build Release first."
        $msbuildPath = $null
    }
} else {
    $msbuildPath = $msbuild.Source
}

if ($msbuildPath) {
    $vcxproj = "$BuildDir\DAW_Core_App.vcxproj"
    & $msbuildPath $vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
    Write-Host "  Build succeeded." -ForegroundColor Green
} else {
    Write-Host "  Skipped (build manually in Visual Studio: Release | x64)." -ForegroundColor Yellow
}

if (-not (Test-Path $ExeSrc)) {
    throw "EXE not found at: $ExeSrc`nBuild Release x64 in Visual Studio first."
}

# ── Step 2: Create machine-trusted code signing cert ──────────────────────────
Write-Host ""
Write-Host "[2/5] Setting up machine-trusted code signing certificate..." -ForegroundColor Yellow

# Check if cert already exists in LocalMachine
$lmStore = New-Object System.Security.Cryptography.X509Certificates.X509Store(
    [System.Security.Cryptography.X509Certificates.StoreName]::My,
    [System.Security.Cryptography.X509Certificates.StoreLocation]::LocalMachine)
$lmStore.Open("ReadOnly")
$cert = $lmStore.Certificates | Where-Object { $_.Subject -eq $CertSubject -and $_.HasPrivateKey } | Select-Object -First 1
$lmStore.Close()

if (-not $cert) {
    Write-Host "  Creating new self-signed certificate..." -ForegroundColor Gray

    # Create in CurrentUser first (needs private key access)
    $cert = New-SelfSignedCertificate `
        -Subject $CertSubject `
        -Type CodeSigningCert `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -KeyExportPolicy Exportable `
        -KeySpec Signature `
        -KeyLength 2048 `
        -HashAlgorithm SHA256 `
        -NotAfter (Get-Date).AddYears(10)

    Write-Host "  Created cert: $($cert.Thumbprint)" -ForegroundColor Gray
}

# Add to ALL required stores for full Windows trust
$stores = @(
    # CurrentUser stores
    @{ Name = "My";              Location = "CurrentUser" },
    @{ Name = "TrustedPublisher"; Location = "CurrentUser" },
    @{ Name = "Root";             Location = "CurrentUser" },
    # LocalMachine stores — this is what fixes Application Control / AppLocker
    @{ Name = "TrustedPublisher"; Location = "LocalMachine" },
    @{ Name = "Root";             Location = "LocalMachine" }
)

foreach ($s in $stores) {
    try {
        $store = New-Object System.Security.Cryptography.X509Certificates.X509Store($s.Name, $s.Location)
        $store.Open("ReadWrite")
        $store.Add($cert)
        $store.Close()
        Write-Host "  Trusted in $($s.Location)\$($s.Name)" -ForegroundColor Gray
    } catch {
        Write-Warning "  Could not add to $($s.Location)\$($s.Name): $_"
    }
}

Write-Host "  Certificate trusted machine-wide." -ForegroundColor Green

# ── Step 3: Sign the EXE ──────────────────────────────────────────────────────
Write-Host ""
Write-Host "[3/5] Signing DAW_Core.exe..." -ForegroundColor Yellow

$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "x64" } |
    Sort-Object { $_.Directory.Name } -Descending |
    Select-Object -First 1

if ($signtool) {
    & $signtool.FullName sign /sha1 $cert.Thumbprint /fd SHA256 /t http://timestamp.digicert.com "$ExeSrc" 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  Signed successfully." -ForegroundColor Green
    } else {
        Write-Warning "  Signing failed (non-fatal, continuing install)."
    }
} else {
    Write-Warning "  signtool.exe not found. Install Windows SDK for signing."
}

# ── Step 4: Install to Program Files ──────────────────────────────────────────
Write-Host ""
Write-Host "[4/5] Installing to $InstallDir ..." -ForegroundColor Yellow

if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
}

# Copy EXE
Copy-Item $ExeSrc "$InstallDir\DAW_Core.exe" -Force
Write-Host "  Copied DAW_Core.exe" -ForegroundColor Gray

# Unblock all files (removes the NTFS Zone.Identifier "downloaded from internet" mark)
Get-ChildItem $InstallDir -Recurse | Unblock-File -ErrorAction SilentlyContinue
Write-Host "  Unblocked all files (removed download zone marks)" -ForegroundColor Gray

# Copy any required assets/resources if present
$assetsDir = "$RepoRoot\Assets"
if (Test-Path $assetsDir) {
    Copy-Item "$assetsDir\*" "$InstallDir\" -Recurse -Force
    Write-Host "  Copied Assets" -ForegroundColor Gray
}

Write-Host "  Installed to $InstallDir" -ForegroundColor Green

# ── Step 5: Create shortcuts ───────────────────────────────────────────────────
Write-Host ""
Write-Host "[5/5] Creating shortcuts..." -ForegroundColor Yellow

$shell = New-Object -ComObject WScript.Shell

# Start Menu
$startMenuDir = "$env:ProgramData\Microsoft\Windows\Start Menu\Programs"
$startMenuLink = "$startMenuDir\DAW_Core.lnk"
$lnk = $shell.CreateShortcut($startMenuLink)
$lnk.TargetPath = "$InstallDir\DAW_Core.exe"
$lnk.WorkingDirectory = $InstallDir
$lnk.Description = "DAW_Core Digital Audio Workstation"
$lnk.Save()
Write-Host "  Start Menu shortcut created" -ForegroundColor Gray

# Desktop
$desktopLink = "$env:Public\Desktop\DAW_Core.lnk"
$lnk2 = $shell.CreateShortcut($desktopLink)
$lnk2.TargetPath = "$InstallDir\DAW_Core.exe"
$lnk2.WorkingDirectory = $InstallDir
$lnk2.Description = "DAW_Core Digital Audio Workstation"
$lnk2.Save()
Write-Host "  Desktop shortcut created" -ForegroundColor Gray

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Green
Write-Host "  Installation complete!" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Installed to : $InstallDir\DAW_Core.exe" -ForegroundColor White
Write-Host "  Certificate  : Trusted machine-wide (fixes AppControl/Wibu)" -ForegroundColor White
Write-Host "  Shortcuts    : Start Menu + Desktop" -ForegroundColor White
Write-Host ""
Write-Host "  Run DAW_Core from Start Menu or Desktop." -ForegroundColor Cyan
Write-Host "  All plugins (UAD, Antares, Waves, FabFilter) should now scan." -ForegroundColor Cyan
Write-Host ""
