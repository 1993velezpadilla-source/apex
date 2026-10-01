param(
    [string]$RepoRoot = "."
)

$ErrorActionPreference = "Stop"
$target = Join-Path (Resolve-Path -LiteralPath $RepoRoot).Path "DAW_BRAIN.md"
$expected = "ce92abd24f6ed246004047a374cbf791dedc8fe5b2c2ee98d7154cfa6bc9a7ca"

if (-not (Test-Path -LiteralPath $target)) {
    throw "DAW_BRAIN.md not found at $target"
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $target).Hash.ToLowerInvariant()
$content = Get-Content -LiteralPath $target -Raw

$checks = [ordered]@{
    sha256_match = ($hash -eq $expected)
    has_authority_contract = $content.Contains("AUDITED_SUPPORTING_AUTHORITY != CURRENT_APEX_REPOSITORY_VERIFIED")
    has_v6_core = $content.Contains("## 1. System Architecture")
    has_v6_end = $content.Contains("## 38. ARA and Deep Audio-Editing Integration")
    has_ai43 = $content.Contains("## 43. APEX Cognitive Engineering Runtime and Autonomous Brain Evolution")
    has_volume13 = $content.Contains("# VOLUME 13 —")
    has_volume46 = $content.Contains("# VOLUME 46 —")
    has_end_marker = $content.Contains("# END AUDITED CANONICAL INTEGRATION CANDIDATE")
    no_v7_full_snapshot = (-not $content.Contains("BEGIN DAW BRAIN V7 COMPLETE"))
    no_single_file_frankenstein_layer = (-not $content.Contains("# END SINGLE-FILE SELF-CONTAINED RUNTIME LAYER"))
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value })

$checks.GetEnumerator() | ForEach-Object {
    $status = if ($_.Value) { "PASS" } else { "FAIL" }
    Write-Host ("{0} = {1}" -f $_.Key, $status)
}

Write-Host "SHA-256 = $hash"

if ($failed.Count -gt 0) {
    throw ("Verification failed: " + (($failed | ForEach-Object { $_.Key }) -join ", "))
}

Write-Host ""
Write-Host "Canonical-file verification PASS."
Write-Host "This does NOT verify current APEX repository features or runtime behavior."
