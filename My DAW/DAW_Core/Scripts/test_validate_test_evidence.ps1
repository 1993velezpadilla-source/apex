[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$validatorScript = Join-Path $PSScriptRoot 'validate_test_evidence.ps1'
$validFixture = Join-Path $repositoryRoot 'evidence\fixtures\valid-minimal.json'
$invalidFixture = Join-Path $repositoryRoot 'evidence\fixtures\invalid-missing-source.json'
$invalidBadGrade = Join-Path $repositoryRoot 'evidence\fixtures\invalid-bad-evidenceGrade.json'
$invalidBadHash = Join-Path $repositoryRoot 'evidence\fixtures\invalid-bad-commit-hash.json'
$invalidExtraProps = Join-Path $repositoryRoot 'evidence\fixtures\invalid-extra-properties.json'
$powerShellExe = Join-Path $PSHOME 'powershell.exe'
$script:passed = 0
$script:failed = 0

function Invoke-Validator {
    param(
        [Parameter(Mandatory = $true)]
        [string] $ManifestPath
    )

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & $powerShellExe -NoProfile -ExecutionPolicy Bypass -File $validatorScript -Manifest $ManifestPath 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    return [pscustomobject]@{
        ExitCode = $exitCode
        Output   = ($output | Out-String)
    }
}

function Assert-Equal {
    param(
        [AllowNull()] $Expected,
        [AllowNull()] $Actual,
        [Parameter(Mandatory = $true)] [string] $Message
    )
    if ($Expected -ne $Actual) {
        throw "[FAIL] $Message (expected '$Expected', got '$Actual')"
    }
}

function Assert-True {
    param(
        [Parameter(Mandatory = $true)] [bool] $Condition,
        [Parameter(Mandatory = $true)] [string] $Message
    )
    if (-not $Condition) {
        throw "[FAIL] $Message"
    }
}

function Invoke-Test {
    param(
        [Parameter(Mandatory = $true)] [string] $Name,
        [Parameter(Mandatory = $true)] [scriptblock] $Body
    )
    try {
        & $Body
        $script:passed++
        Write-Host "[PASS] $Name"
    }
    catch {
        $script:failed++
        Write-Host "[FAIL] $Name - $($_.Exception.Message)"
    }
}

Invoke-Test 'validator script exists' {
    Assert-True (Test-Path -LiteralPath $validatorScript -PathType Leaf) "validator script missing: $validatorScript"
}

Invoke-Test 'valid fixture exits 0' {
    $result = Invoke-Validator -ManifestPath $validFixture
    Assert-Equal 0 $result.ExitCode "expected exit 0, got $($result.ExitCode). Output: $($result.Output)"
    Assert-True ($result.Output -match '\[PASS\]') "expected PASS in output: $($result.Output)"
}

Invoke-Test 'invalid-missing-source exits nonzero and names source' {
    $result = Invoke-Validator -ManifestPath $invalidFixture
    Assert-True ($result.ExitCode -ne 0) "expected nonzero exit, got $($result.ExitCode)"
    Assert-True ($result.Output -match 'source') "expected source mentioned in output: $($result.Output)"
}

Invoke-Test 'invalid evidenceGrade value exits nonzero' {
    $result = Invoke-Validator -ManifestPath $invalidBadGrade
    Assert-True ($result.ExitCode -ne 0) "expected nonzero exit, got $($result.ExitCode)"
    Assert-True ($result.Output -match 'evidenceGrade') "expected evidenceGrade mentioned in output: $($result.Output)"
}

Invoke-Test 'bad commit hash pattern exits nonzero' {
    $result = Invoke-Validator -ManifestPath $invalidBadHash
    Assert-True ($result.ExitCode -ne 0) "expected nonzero exit, got $($result.ExitCode)"
    Assert-True ($result.Output -match 'outerCommit') "expected outerCommit mentioned in output: $($result.Output)"
}

Invoke-Test 'extra unknown top-level property exits nonzero' {
    $result = Invoke-Validator -ManifestPath $invalidExtraProps
    Assert-True ($result.ExitCode -ne 0) "expected nonzero exit, got $($result.ExitCode)"
    Assert-True ($result.Output -match 'unknown top-level property') "expected unknown property error in output: $($result.Output)"
}

Write-Host ""
Write-Host "Results: $script:passed passed, $script:failed failed"
if ($script:failed -gt 0) { exit 1 }
exit 0
