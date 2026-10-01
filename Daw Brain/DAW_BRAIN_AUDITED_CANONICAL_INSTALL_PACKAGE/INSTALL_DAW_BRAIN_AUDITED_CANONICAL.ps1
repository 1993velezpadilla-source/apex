param(
    [string]$RepoRoot = ".",
    [string]$CandidatePath = "DAW_BRAIN_AUDITED_INTEGRATION_CANDIDATE.md"
)

$ErrorActionPreference = "Stop"

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

$expected = "ce92abd24f6ed246004047a374cbf791dedc8fe5b2c2ee98d7154cfa6bc9a7ca"
$repo = (Resolve-Path -LiteralPath $RepoRoot).Path
$candidate = (Resolve-Path -LiteralPath $CandidatePath).Path
$target = Join-Path $repo "DAW_BRAIN.md"
$backupDir = Join-Path $repo ".apex_brain_backups"
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$backup = Join-Path $backupDir ("DAW_BRAIN.pre_audited_integration." + $timestamp + ".md")
$installManifest = Join-Path $repo "DAW_BRAIN.install_manifest.json"

$actual = Get-Sha256 $candidate
if ($actual -ne $expected) {
    throw "Candidate SHA-256 mismatch. Expected $expected but got $actual"
}

New-Item -ItemType Directory -Force -Path $backupDir | Out-Null

$previousHash = $null
if (Test-Path -LiteralPath $target) {
    $previousHash = Get-Sha256 $target
    Copy-Item -LiteralPath $target -Destination $backup -Force
}

Copy-Item -LiteralPath $candidate -Destination $target -Force

$installedHash = Get-Sha256 $target
if ($installedHash -ne $expected) {
    throw "Installed DAW_BRAIN.md hash mismatch after copy."
}

$record = [ordered]@{
    installed_at = (Get-Date).ToString("o")
    repo_root = $repo
    target = $target
    installed_sha256 = $installedHash
    source_candidate = $candidate
    source_candidate_sha256 = $actual
    previous_sha256 = $previousHash
    backup = $(if (Test-Path -LiteralPath $backup) { $backup } else { $null })
    repository_feature_state_verified = $false
    note = "Canonical knowledge file installed. Live APEX repository/runtime verification remains separate."
}

$record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $installManifest -Encoding UTF8

Write-Host ""
Write-Host "DAW_BRAIN.md installed successfully."
Write-Host "SHA-256: $installedHash"
if ($record.backup) { Write-Host "Backup: $($record.backup)" }
Write-Host "Install manifest: $installManifest"
Write-Host ""
Write-Host "IMPORTANT: Installed canonical knowledge != current repository feature verification."
