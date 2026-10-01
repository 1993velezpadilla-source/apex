[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Manifest
)

$ErrorActionPreference = 'Stop'

$resolvedManifestPath = [System.IO.Path]::GetFullPath($Manifest)
$manifestDir = [System.IO.Path]::GetDirectoryName($resolvedManifestPath)
$vFailures = @()

function Add-VFail {
    param([Parameter(Mandatory = $true)][string] $Message)
    $script:vFailures += $Message
}

function Test-RequiredField {
    param(
        [Parameter(Mandatory = $true)] $Object,
        [Parameter(Mandatory = $true)][string] $Name
    )
    if ($null -eq $Object.PSObject.Properties[$Name]) {
        Add-VFail "missing required field: $Name"
        return $false
    }
    return $true
}

function Test-NonEmptyString {
    param(
        [Parameter(Mandatory = $true)] $Object,
        [Parameter(Mandatory = $true)][string] $Name
    )
    if (-not (Test-RequiredField -Object $Object -Name $Name)) { return }
    $val = [string] $Object.$Name
    if ([string]::IsNullOrEmpty($val)) {
        Add-VFail "field $Name must be a non-empty string"
    }
}

function Test-NonNegativeInt {
    param(
        [Parameter(Mandatory = $true)] $Object,
        [Parameter(Mandatory = $true)][string] $Name
    )
    if (-not (Test-RequiredField -Object $Object -Name $Name)) { return }
    if ($Object.$Name -lt 0) {
        Add-VFail "field $Name must be >= 0"
    }
}

function Test-Enum {
    param(
        [Parameter(Mandatory = $true)] $Object,
        [Parameter(Mandatory = $true)][string] $Name,
        [Parameter(Mandatory = $true)][string[]] $Allowed
    )
    if (-not (Test-RequiredField -Object $Object -Name $Name)) { return }
    $val = [string] $Object.$Name
    if ($Allowed -notcontains $val) {
        Add-VFail "field $Name must be one of [$($Allowed -join ', ')], got '$val'"
    }
}

function Test-Pattern {
    param(
        [Parameter(Mandatory = $true)] $Object,
        [Parameter(Mandatory = $true)][string] $Name,
        [Parameter(Mandatory = $true)][string] $Pattern
    )
    if (-not (Test-RequiredField -Object $Object -Name $Name)) { return }
    if ([string] $Object.$Name -notmatch $Pattern) {
        Add-VFail "field $Name does not match pattern $Pattern"
    }
}

function Test-Bool {
    param(
        [Parameter(Mandatory = $true)] $Object,
        [Parameter(Mandatory = $true)][string] $Name
    )
    if (-not (Test-RequiredField -Object $Object -Name $Name)) { return }
    if ($Object.$Name -isnot [bool]) {
        Add-VFail "field $Name must be a boolean"
    }
}

function Test-StringOrNullable {
    param(
        [Parameter(Mandatory = $true)] $Object,
        [Parameter(Mandatory = $true)][string] $Name
    )
    if (-not (Test-RequiredField -Object $Object -Name $Name)) { return }
    if ($null -ne $Object.$Name -and $Object.$Name -isnot [string]) {
        Add-VFail "field $Name must be a string or null"
    }
}

function Test-NumberOrNullable {
    param(
        [Parameter(Mandatory = $true)] $Object,
        [Parameter(Mandatory = $true)][string] $Name
    )
    if (-not (Test-RequiredField -Object $Object -Name $Name)) { return }
    if ($null -ne $Object.$Name -and $Object.$Name -isnot [double] -and $Object.$Name -isnot [int]) {
        Add-VFail "field $Name must be a number or null"
    }
}

function Test-IntegerOrNullable {
    param(
        [Parameter(Mandatory = $true)] $Object,
        [Parameter(Mandatory = $true)][string] $Name
    )
    if (-not (Test-RequiredField -Object $Object -Name $Name)) { return }
    if ($null -ne $Object.$Name -and $Object.$Name -isnot [int]) {
        Add-VFail "field $Name must be an integer or null"
    }
}

if (-not (Test-Path -LiteralPath $resolvedManifestPath -PathType Leaf)) {
    Write-Host "[FAIL] manifest file not found: $resolvedManifestPath"
    exit 1
}

$data = Get-Content -LiteralPath $resolvedManifestPath -Raw | ConvertFrom-Json

$knownTop = @('schemaVersion', 'evidenceGrade', 'runId', 'suiteId', 'source', 'build', 'dependencies', 'environment', 'execution', 'results', 'artifacts')
foreach ($prop in $data.PSObject.Properties) {
    if ($knownTop -notcontains $prop.Name) {
        Add-VFail "unknown top-level property: $($prop.Name)"
    }
}

foreach ($f in $knownTop) {
    Test-RequiredField -Object $data -Name $f | Out-Null
}

if ($null -ne $data.PSObject.Properties['schemaVersion']) {
    if ([int] $data.schemaVersion -ne 1) {
        Add-VFail "schemaVersion must be 1, got $($data.schemaVersion)"
    }
}

Test-Enum -Object $data -Name 'evidenceGrade' -Allowed @('diagnostic', 'release')
Test-NonEmptyString -Object $data -Name 'runId'
Test-NonEmptyString -Object $data -Name 'suiteId'

if ($null -ne $data.PSObject.Properties['source']) {
    $src = $data.source
    $requiredSource = @('outerCommit', 'outerDirty', 'applicationCommit', 'applicationDirty')
    foreach ($f in $requiredSource) {
        Test-RequiredField -Object $src -Name $f | Out-Null
    }
    Test-Pattern -Object $src -Name 'outerCommit' -Pattern '^[0-9a-fA-F]{40}$'
    Test-Pattern -Object $src -Name 'applicationCommit' -Pattern '^[0-9a-fA-F]{40}$'
    Test-Bool -Object $src -Name 'outerDirty'
    Test-Bool -Object $src -Name 'applicationDirty'
}

if ($null -ne $data.PSObject.Properties['build']) {
    $bld = $data.build
    $requiredBuild = @('configuration', 'platform', 'toolset', 'binarySha256')
    foreach ($f in $requiredBuild) {
        Test-RequiredField -Object $bld -Name $f | Out-Null
    }
    Test-Enum -Object $bld -Name 'configuration' -Allowed @('Debug', 'Release')
    if ($null -ne $bld.PSObject.Properties['platform']) {
        if ([string] $bld.platform -ne 'x64') { Add-VFail "build.platform must be 'x64'" }
    }
    if ($null -ne $bld.PSObject.Properties['toolset']) {
        if ([string] $bld.toolset -ne 'v145') { Add-VFail "build.toolset must be 'v145'" }
    }
    Test-Pattern -Object $bld -Name 'binarySha256' -Pattern '^[0-9a-fA-F]{64}$'
}

if ($null -ne $data.PSObject.Properties['dependencies']) {
    $deps = $data.dependencies
    Test-RequiredField -Object $deps -Name 'juceVersion' | Out-Null
    if ($null -ne $deps.PSObject.Properties['juceVersion']) {
        if ([string] $deps.juceVersion -ne '8.0.12') { Add-VFail "dependencies.juceVersion must be '8.0.12'" }
    }
}

if ($null -ne $data.PSObject.Properties['environment']) {
    $envObj = $data.environment
    $requiredEnv = @('os', 'cpu', 'powerMode', 'locale')
    foreach ($f in $requiredEnv) {
        Test-NonEmptyString -Object $envObj -Name $f
    }
    if ($null -ne $envObj.PSObject.Properties['audioDriver']) {
        Test-StringOrNullable -Object $envObj -Name 'audioDriver'
    }
    if ($null -ne $envObj.PSObject.Properties['audioDevice']) {
        Test-StringOrNullable -Object $envObj -Name 'audioDevice'
    }
    if ($null -ne $envObj.PSObject.Properties['sampleRate']) {
        Test-NumberOrNullable -Object $envObj -Name 'sampleRate'
        if ($null -ne $envObj.sampleRate -and [double] $envObj.sampleRate -le 0) {
            Add-VFail "environment.sampleRate must be > 0"
        }
    }
    if ($null -ne $envObj.PSObject.Properties['blockSize']) {
        Test-IntegerOrNullable -Object $envObj -Name 'blockSize'
        if ($null -ne $envObj.blockSize -and [int] $envObj.blockSize -lt 1) {
            Add-VFail "environment.blockSize must be >= 1"
        }
    }
}

if ($null -ne $data.PSObject.Properties['execution']) {
    $exec = $data.execution
    $requiredExec = @('command', 'startedUtc', 'seed', 'exitCode', 'durationMs')
    foreach ($f in $requiredExec) {
        Test-RequiredField -Object $exec -Name $f | Out-Null
    }
    if ($null -ne $exec.command) {
        if ($exec.command -isnot [System.Array]) {
            Add-VFail "execution.command must be an array"
        }
        elseif ($exec.command.Count -lt 1) {
            Add-VFail "execution.command must have at least 1 item"
        }
        else {
            foreach ($item in $exec.command) {
                if ($item -isnot [string]) {
                    Add-VFail "execution.command items must be strings"
                    break
                }
            }
        }
    }
    if ($null -ne $exec.PSObject.Properties['startedUtc']) {
        try {
            [System.DateTimeOffset]::Parse($exec.startedUtc) | Out-Null
        }
        catch {
            Add-VFail "execution.startedUtc is not a valid date-time"
        }
    }
    if ($null -ne $exec.PSObject.Properties['seed']) {
        $seedVal = $exec.seed
        if ($seedVal -isnot [int] -and $seedVal -isnot [long]) {
            Add-VFail "execution.seed must be an integer"
        }
    }
    if ($null -ne $exec.PSObject.Properties['exitCode']) {
        if ($exec.exitCode -isnot [int]) {
            Add-VFail "execution.exitCode must be an integer"
        }
    }
    if ($null -ne $exec.PSObject.Properties['durationMs']) {
        if ($exec.durationMs -isnot [int]) {
            Add-VFail "execution.durationMs must be an integer"
        }
        elseif ([int] $exec.durationMs -lt 0) {
            Add-VFail "execution.durationMs must be >= 0"
        }
    }
}

if ($null -ne $data.PSObject.Properties['results']) {
    $res = $data.results
    $requiredResults = @('resultGroups', 'assertionsPassed', 'assertionsFailed')
    foreach ($f in $requiredResults) {
        Test-RequiredField -Object $res -Name $f | Out-Null
    }
    Test-NonNegativeInt -Object $res -Name 'resultGroups'
    Test-NonNegativeInt -Object $res -Name 'assertionsPassed'
    Test-NonNegativeInt -Object $res -Name 'assertionsFailed'
}

if ($null -ne $data.PSObject.Properties['artifacts']) {
    if ($data.artifacts -isnot [System.Array]) {
        Add-VFail "artifacts must be an array"
    }
    else {
        $idx = 0
        foreach ($artifact in $data.artifacts) {
            if ($artifact -isnot [PSCustomObject]) {
                Add-VFail "artifacts[$idx] must be an object"
                $idx++
                continue
            }
            $requiredArt = @('path', 'bytes', 'sha256')
            foreach ($f in $requiredArt) {
                Test-RequiredField -Object $artifact -Name $f | Out-Null
            }
            Test-NonEmptyString -Object $artifact -Name 'path'
            if ($null -ne $artifact.PSObject.Properties['bytes']) {
                if ($artifact.bytes -isnot [int] -or [int] $artifact.bytes -lt 0) {
                    Add-VFail "artifacts[$idx].bytes must be a non-negative integer"
                }
            }
            Test-Pattern -Object $artifact -Name 'sha256' -Pattern '^[0-9a-fA-F]{64}$'

            if ($null -ne $artifact.PSObject.Properties['path']) {
                $artPath = [string] $artifact.path
                if ($artPath.Contains('..')) {
                    Add-VFail "artifacts[$idx].path must not contain '..'"
                }
                elseif ([System.IO.Path]::IsPathRooted($artPath)) {
                    Add-VFail "artifacts[$idx].path must be a relative path"
                }
                else {
                    $resolvedArtPath = [System.IO.Path]::GetFullPath(
                        (Join-Path $manifestDir $artPath)
                    )
                    if (-not (Test-Path -LiteralPath $resolvedArtPath -PathType Leaf)) {
                        Add-VFail "artifacts[$idx].path does not exist: $resolvedArtPath"
                    }
                    elseif ($null -ne $artifact.PSObject.Properties['bytes'] -and $null -ne $artifact.PSObject.Properties['sha256']) {
                        $actualBytes = (Get-Item -LiteralPath $resolvedArtPath).Length
                        if ([int] $artifact.bytes -ne $actualBytes) {
                            Add-VFail "artifacts[$idx].bytes mismatch: expected $($artifact.bytes), got $actualBytes"
                        }
                        $actualHash = (Get-FileHash -LiteralPath $resolvedArtPath -Algorithm SHA256).Hash
                        if ([string] $artifact.sha256 -cne $actualHash) {
                            Add-VFail "artifacts[$idx].sha256 mismatch: expected $($artifact.sha256), got $actualHash"
                        }
                    }
                }
            }
            $idx++
        }
    }
}

if ($null -ne $data.execution -and $null -ne $data.results) {
    if ([int] $data.execution.exitCode -eq 0) {
        if ([int] $data.results.assertionsFailed -ne 0) {
            Add-VFail "assertionsFailed must be 0 when exitCode is 0"
        }
    }
}

if ($null -ne $data.PSObject.Properties['source']) {
    if ([string] $data.evidenceGrade -eq 'release') {
        if ([bool] $data.source.outerDirty -ne $false) {
            Add-VFail "source.outerDirty must be false for release evidenceGrade"
        }
        if ([bool] $data.source.applicationDirty -ne $false) {
            Add-VFail "source.applicationDirty must be false for release evidenceGrade"
        }
    }
}

if ($vFailures.Count -gt 0) {
    foreach ($f in $vFailures) {
        Write-Host "[FAIL] $f"
    }
    exit 1
}

Write-Host "[PASS] evidence manifest $($data.runId)"
exit 0
