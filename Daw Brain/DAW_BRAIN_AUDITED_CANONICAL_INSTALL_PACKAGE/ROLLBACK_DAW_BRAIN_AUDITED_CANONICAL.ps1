param(
    [string]$RepoRoot = ".",
    [string]$BackupPath = ""
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path -LiteralPath $RepoRoot).Path
$target = Join-Path $repo "DAW_BRAIN.md"
$backupDir = Join-Path $repo ".apex_brain_backups"

if ([string]::IsNullOrWhiteSpace($BackupPath)) {
    if (-not (Test-Path -LiteralPath $backupDir)) {
        throw "Backup directory not found: $backupDir"
    }
    $latest = Get-ChildItem -LiteralPath $backupDir -Filter "DAW_BRAIN.pre_audited_integration.*.md" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $latest) {
        throw "No DAW Brain backup found."
    }
    $BackupPath = $latest.FullName
}

$backup = (Resolve-Path -LiteralPath $BackupPath).Path
Copy-Item -LiteralPath $backup -Destination $target -Force
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $target).Hash.ToLowerInvariant()

Write-Host "Rollback complete."
Write-Host "Restored: $backup"
Write-Host "DAW_BRAIN.md SHA-256: $hash"
