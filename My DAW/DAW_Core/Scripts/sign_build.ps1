param(
    [Parameter(Mandatory=$true)]
    [string]$ExePath
)

# sign_build.ps1 - Self-signed code signing for DAW_Core dev builds
# Called as a post-build step: powershell -ExecutionPolicy Bypass -File "sign_build.ps1" "path\to\exe"

$ErrorActionPreference = 'Continue'
$global:LASTEXITCODE = 0

if ($env:APEX_SKIP_SIGNING -eq '1') {
    Write-Host '[sign] APEX_SKIP_SIGNING=1 - unsigned build requested.'
    exit 0
}

$CertSubject = "CN=DAW_Core Dev"
$CertStore   = "Cert:\CurrentUser\My"

if (-not (Test-Path $ExePath)) {
    Write-Host "[sign] Target not found: $ExePath - skipping."
    exit 0
}

# Find or create the self-signed certificate
$cert = Get-ChildItem $CertStore -CodeSigningCert | Where-Object { $_.Subject -eq $CertSubject } | Select-Object -First 1

if (-not $cert) {
    Write-Host "[sign] No dev certificate found. Creating self-signed cert..."
    $cert = New-SelfSignedCertificate `
        -Subject $CertSubject `
        -Type CodeSigningCert `
        -CertStoreLocation $CertStore `
        -KeyExportPolicy Exportable `
        -NotAfter (Get-Date).AddYears(5)

    # Trust in CurrentUser
    foreach ($storeName in @("TrustedPublisher", "Root")) {
        $s = New-Object System.Security.Cryptography.X509Certificates.X509Store($storeName, "CurrentUser")
        $s.Open("ReadWrite"); $s.Add($cert); $s.Close()
    }

    # Trust in LocalMachine — REQUIRED for Application Control / Wibu DLL loading
    # (needs elevation; silently skips if not admin)
    foreach ($storeName in @("TrustedPublisher", "Root")) {
        try {
            $s = New-Object System.Security.Cryptography.X509Certificates.X509Store($storeName, "LocalMachine")
            $s.Open("ReadWrite"); $s.Add($cert); $s.Close()
            Write-Host "[sign] Trusted in LocalMachine\$storeName (fixes AppControl/Wibu)"
        } catch {
            Write-Host "[sign] Note: run as Admin once to trust in LocalMachine (fixes plugin AppControl blocks)"
        }
    }

    Write-Host "[sign] Certificate created and trusted: $($cert.Thumbprint)"
}

# Unblock the EXE (remove Zone.Identifier NTFS stream from Downloads)
Unblock-File -Path $ExePath -ErrorAction SilentlyContinue

# Locate signtool.exe from Windows SDK
$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "x64" } |
    Sort-Object { $_.Directory.Name } -Descending |
    Select-Object -First 1

if (-not $signtool) {
    Write-Host "[sign] signtool.exe not found - install Windows SDK. Skipping."
    exit 0
}

Write-Host "[sign] Signing $ExePath ..."
try {
    & $signtool.FullName sign /sha1 $cert.Thumbprint /fd SHA256 /t http://timestamp.digicert.com "$ExePath"

    if ($LASTEXITCODE -eq 0) {
        Write-Host "[sign] Signed successfully."
    } else {
        Write-Host "[sign] Signing failed (exit $LASTEXITCODE) - build continues."
    }
} catch {
    Write-Host "[sign] Signing threw an exception: $($_.Exception.Message) - build continues."
}

exit 0
