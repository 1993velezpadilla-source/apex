[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration,

    [string] $Category,

    [string] $Name,

    [string] $Seed,

    [string] $ResultsJson
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$outerRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot '..'))
$testProjectDir = Join-Path $repositoryRoot 'Tests'
$jucerFile = Join-Path $testProjectDir 'APEXTests.jucer'
$solutionDir = Join-Path $testProjectDir 'Builds\VisualStudio2026'
$slnFile = Join-Path $solutionDir 'APEXTests.sln'
$fixtureProjectDir = Join-Path $testProjectDir 'Fixtures\VST3'
$fixtureJucerFile = Join-Path $fixtureProjectDir 'APEXTestVST3.jucer'
$fixtureSolution = Join-Path $fixtureProjectDir 'Builds\VisualStudio2026\APEXTestVST3.sln'
$needsDenseE2BFixture = ($Category -eq 'PluginSandboxPhaseE2B')
if ($Name -and $Name.StartsWith('plugin.sandbox.phase-e2b.')) {
    $needsDenseE2BFixture = $true
}
$denseE2BFixtureBundle = $null

$contractsPath = Join-Path $repositoryRoot 'Dependencies\apex-windows-dependencies.json'
if (-not (Test-Path -LiteralPath $contractsPath -PathType Leaf)) {
    Write-Error "Dependency contract is missing: $contractsPath"
    exit 2
}

$contractData = Get-Content -LiteralPath $contractsPath -Raw | ConvertFrom-Json
$juceRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot ([string] $contractData.juce.rootRelativeToRepository))
)
$projucer = Join-Path $juceRoot 'Projucer.exe'
if (-not (Test-Path -LiteralPath $projucer -PathType Leaf)) {
    Write-Error "Pinned Projucer is missing: $projucer"
    exit 2
}

if (-not (Test-Path -LiteralPath $jucerFile -PathType Leaf)) {
    Write-Error "Tests project is missing: $jucerFile"
    exit 2
}

if (-not (Test-Path -LiteralPath $slnFile -PathType Leaf)) {
    Write-Error "Generated solution is missing: $slnFile. Run Projucer --resave first."
    exit 2
}
if (-not (Test-Path -LiteralPath $fixtureJucerFile -PathType Leaf)) {
    Write-Error "Deterministic VST3 fixture project is missing: $fixtureJucerFile"
    exit 2
}
if (-not (Test-Path -LiteralPath $fixtureSolution -PathType Leaf)) {
    Write-Error "Generated VST3 fixture solution is missing: $fixtureSolution. Run Projucer --resave first."
    exit 2
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    Write-Error "vswhere.exe is missing: $vswhere"
    exit 2
}

$vswhereArguments = @(
    '-latest',
    '-products', '*',
    '-requires', 'Microsoft.Component.MSBuild',
    '-version', '[18.0,19.0)',
    '-property', 'installationPath'
)
$installationOutput = @(& $vswhere @vswhereArguments 2>&1)
if ($LASTEXITCODE -ne 0) {
    Write-Error "vswhere.exe failed with exit code $LASTEXITCODE"
    exit 2
}

$installationPaths = @(
    $installationOutput |
        ForEach-Object { ([string] $_).Trim() } |
        Where-Object { $_ }
)
if ($installationPaths.Count -ne 1) {
    Write-Error "Visual Studio 18 not resolved uniquely."
    exit 2
}

$visualStudio = [System.IO.Path]::GetFullPath($installationPaths[0])
$msbuild = Join-Path $visualStudio 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) {
    Write-Error "MSBuild is missing: $msbuild"
    exit 2
}

$target = 'Build'
$fixtureBuildArguments = @(
    $fixtureSolution,
    "/t:$target",
    "/p:Configuration=$Configuration",
    '/p:Platform=x64',
    '/m',
    '/nr:false',
    '/nologo',
    '/v:minimal'
)

Write-Host "[test] Building deterministic VST3 fixture $Configuration|x64"
& $msbuild @fixtureBuildArguments
if ($LASTEXITCODE -ne 0) {
    Write-Error "VST3 fixture MSBuild failed with exit code $LASTEXITCODE."
    exit 1
}

$fixtureBundle = Join-Path $fixtureProjectDir "Builds\VisualStudio2026\x64\$Configuration\VST3\APEXTestVST3.vst3"
$fixtureModule = Join-Path $fixtureBundle 'Contents\x86_64-win\APEXTestVST3.vst3'
if (-not (Test-Path -LiteralPath $fixtureBundle -PathType Container)) {
    Write-Error "VST3 fixture bundle is missing: $fixtureBundle"
    exit 1
}
if (-not (Test-Path -LiteralPath $fixtureModule -PathType Leaf)) {
    Write-Error "VST3 fixture module is missing: $fixtureModule"
    exit 1
}

# Deterministic mono fixture (distinct component identity from the stereo fixture).
$monoFixtureJucerFile = Join-Path $fixtureProjectDir 'APEXTestVST3Mono.jucer'
$monoFixtureSolution = Join-Path $fixtureProjectDir 'Builds\VisualStudio2026Mono\APEXTestVST3Mono.sln'
if (-not (Test-Path -LiteralPath $monoFixtureJucerFile -PathType Leaf)) {
    Write-Error "Mono VST3 fixture project is missing: $monoFixtureJucerFile"
    exit 2
}
if (-not (Test-Path -LiteralPath $monoFixtureSolution -PathType Leaf)) {
    Write-Error "Generated mono VST3 fixture solution is missing: $monoFixtureSolution. Run Projucer --resave first."
    exit 2
}
$monoFixtureBuildArguments = @(
    $monoFixtureSolution,
    "/t:$target",
    "/p:Configuration=$Configuration",
    '/p:Platform=x64',
    '/m',
    '/nr:false',
    '/nologo',
    '/v:minimal'
)
Write-Host "[test] Building deterministic mono VST3 fixture $Configuration|x64"
& $msbuild @monoFixtureBuildArguments
if ($LASTEXITCODE -ne 0) {
    Write-Error "Mono VST3 fixture MSBuild failed with exit code $LASTEXITCODE."
    exit 1
}
$monoFixtureBundle = Join-Path $fixtureProjectDir "Builds\VisualStudio2026Mono\x64\$Configuration\VST3\APEXTestVST3Mono.vst3"
if (-not (Test-Path -LiteralPath $monoFixtureBundle -PathType Container)) {
    Write-Error "Mono VST3 fixture bundle is missing: $monoFixtureBundle"
    exit 1
}

# Phase D fault fixture (crash/hang sentinels; fully isolated build tree).
$faultFixtureJucerFile = Join-Path $fixtureProjectDir 'APEXTestVST3Fault.jucer'
$faultFixtureSolution = Join-Path $fixtureProjectDir 'Builds\VisualStudio2026Fault\APEXTestVST3Fault.sln'
if (-not (Test-Path -LiteralPath $faultFixtureJucerFile -PathType Leaf)) {
    Write-Error "Fault VST3 fixture project is missing: $faultFixtureJucerFile"
    exit 2
}
if (-not (Test-Path -LiteralPath $faultFixtureSolution -PathType Leaf)) {
    Write-Error "Generated fault VST3 fixture solution is missing: $faultFixtureSolution. Run Projucer --resave first."
    exit 2
}
$faultFixtureBuildArguments = @(
    $faultFixtureSolution,
    "/t:$target",
    "/p:Configuration=$Configuration",
    '/p:Platform=x64',
    '/m',
    '/nr:false',
    '/nologo',
    '/v:minimal'
)
Write-Host "[test] Building deterministic fault VST3 fixture $Configuration|x64"
& $msbuild @faultFixtureBuildArguments
if ($LASTEXITCODE -ne 0) {
    Write-Error "Fault VST3 fixture MSBuild failed with exit code $LASTEXITCODE."
    exit 1
}
$faultFixtureBundle = Join-Path $fixtureProjectDir "Builds\VisualStudio2026Fault\x64\$Configuration\VST3\APEXTestVST3Fault.vst3"
if (-not (Test-Path -LiteralPath $faultFixtureBundle -PathType Container)) {
    Write-Error "Fault VST3 fixture bundle is missing: $faultFixtureBundle"
    exit 1
}

# Phase E1 state fixture (parameter/state control; fully isolated build tree).
$stateFixtureJucerFile = Join-Path $fixtureProjectDir 'APEXTestVST3State.jucer'
$stateFixtureSolution = Join-Path $fixtureProjectDir 'Builds\VisualStudio2026State\APEXTestVST3State.sln'
if (-not (Test-Path -LiteralPath $stateFixtureJucerFile -PathType Leaf)) {
    Write-Error "State VST3 fixture project is missing: $stateFixtureJucerFile"
    exit 2
}
if (-not (Test-Path -LiteralPath $stateFixtureSolution -PathType Leaf)) {
    Write-Error "Generated state VST3 fixture solution is missing: $stateFixtureSolution. Run Projucer --resave first."
    exit 2
}
$stateFixtureBuildArguments = @(
    $stateFixtureSolution,
    "/t:$target",
    "/p:Configuration=$Configuration",
    '/p:Platform=x64',
    '/m',
    '/nr:false',
    '/nologo',
    '/v:minimal'
)
Write-Host "[test] Building deterministic state VST3 fixture $Configuration|x64"
& $msbuild @stateFixtureBuildArguments
if ($LASTEXITCODE -ne 0) {
    Write-Error "State VST3 fixture MSBuild failed with exit code $LASTEXITCODE."
    exit 1
}
$stateFixtureBundle = Join-Path $fixtureProjectDir "Builds\VisualStudio2026State\x64\$Configuration\VST3\APEXTestVST3State.vst3"
if (-not (Test-Path -LiteralPath $stateFixtureBundle -PathType Container)) {
    Write-Error "State VST3 fixture bundle is missing: $stateFixtureBundle"
    exit 1
}

# Phase E2A automation fixture (zero-latency sample-accuracy; isolated tree).
$automationFixtureJucerFile = Join-Path $fixtureProjectDir 'APEXTestVST3Automation.jucer'
$automationFixtureSolution = Join-Path $fixtureProjectDir 'Builds\VisualStudio2026Automation\APEXTestVST3Automation.sln'
if (-not (Test-Path -LiteralPath $automationFixtureJucerFile -PathType Leaf)) {
    Write-Error "Automation VST3 fixture project is missing: $automationFixtureJucerFile"
    exit 2
}
if (-not (Test-Path -LiteralPath $automationFixtureSolution -PathType Leaf)) {
    Write-Error "Generated automation VST3 fixture solution is missing: $automationFixtureSolution. Run Projucer --resave first."
    exit 2
}
$automationFixtureBuildArguments = @(
    $automationFixtureSolution,
    "/t:$target",
    "/p:Configuration=$Configuration",
    '/p:Platform=x64',
    '/m',
    '/nr:false',
    '/nologo',
    '/v:minimal'
)
Write-Host "[test] Building deterministic automation VST3 fixture $Configuration|x64"
& $msbuild @automationFixtureBuildArguments
if ($LASTEXITCODE -ne 0) {
    Write-Error "Automation VST3 fixture MSBuild failed with exit code $LASTEXITCODE."
    exit 1
}
$automationFixtureBundle = Join-Path $fixtureProjectDir "Builds\VisualStudio2026Automation\x64\$Configuration\VST3\APEXTestVST3Automation.vst3"
if (-not (Test-Path -LiteralPath $automationFixtureBundle -PathType Container)) {
    Write-Error "Automation VST3 fixture bundle is missing: $automationFixtureBundle"
    exit 1
}

# Final E2B matrix dense-parameter fixture. It is isolated from the frozen
# E1/E2A fixtures and is built only for E2B category/focused runs.
if ($needsDenseE2BFixture) {
    $denseE2BJucerFile = Join-Path $fixtureProjectDir 'DenseE2B\APEXTestVST3DenseE2B.jucer'
    $denseE2BSolution = Join-Path $fixtureProjectDir 'DenseE2B\Builds\VisualStudio2026DenseE2B\APEXTestVST3DenseE2B.sln'
    if (-not (Test-Path -LiteralPath $denseE2BJucerFile -PathType Leaf)) {
        Write-Error "Dense E2B VST3 fixture project is missing: $denseE2BJucerFile"
        exit 2
    }
    if (-not (Test-Path -LiteralPath $denseE2BSolution -PathType Leaf)) {
        Write-Error "Generated dense E2B VST3 fixture solution is missing: $denseE2BSolution. Run Projucer --resave first."
        exit 2
    }
    $denseE2BBuildArguments = @(
        $denseE2BSolution,
        "/t:$target",
        "/p:Configuration=$Configuration",
        '/p:Platform=x64',
        '/m',
        '/nr:false',
        '/nologo',
        '/v:minimal'
    )
    Write-Host "[test] Building dense E2B VST3 fixture $Configuration|x64"
    & $msbuild @denseE2BBuildArguments
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Dense E2B VST3 fixture MSBuild failed with exit code $LASTEXITCODE."
        exit 1
    }
    $denseE2JucerOutput = Join-Path $fixtureProjectDir "DenseE2B\Builds\VisualStudio2026DenseE2B\x64\$Configuration\VST3\APEXTestVST3DenseE2B.vst3"
    if (-not (Test-Path -LiteralPath $denseE2JucerOutput -PathType Container)) {
        Write-Error "Dense E2B VST3 fixture bundle is missing: $denseE2JucerOutput"
        exit 1
    }
    $denseE2BModule = Join-Path $denseE2JucerOutput 'Contents\x86_64-win\APEXTestVST3DenseE2B.vst3'
    if (-not (Test-Path -LiteralPath $denseE2BModule -PathType Leaf)) {
        Write-Error "Dense E2B VST3 fixture module is missing: $denseE2BModule"
        exit 1
    }
    $denseE2BFixtureBundle = $denseE2JucerOutput
}

$msbuildArguments = @(
    $slnFile,
    "/t:$target",
    "/p:Configuration=$Configuration",
    '/p:Platform=x64',
    '/m',
    '/nr:false',
    '/nologo',
    '/v:minimal'
)

Write-Host "[test] Building APEXTests $Configuration|x64"
& $msbuild @msbuildArguments
if ($LASTEXITCODE -ne 0) {
    Write-Error "MSBuild failed with exit code $LASTEXITCODE."
    exit 1
}

$exeDir = Join-Path $solutionDir "x64\$Configuration\ConsoleApp"
$exePath = Join-Path $exeDir 'APEXTests.exe'
if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
    Write-Error "Test executable is missing: $exePath"
    exit 1
}

$testArguments = @()
if ($Category) { $testArguments += "--category=$Category" }
if ($Name) { $testArguments += "--name=$Name" }
if ($Seed) { $testArguments += "--seed=$Seed" }
if ($ResultsJson) { $testArguments += "--results-json=$ResultsJson" }

# --- Evidence collection ---
$suiteId = if ($Category) { $Category } else { 'runner-smoke' }
$runId = [guid]::NewGuid().ToString('N').Substring(0, 12)
$startUtc = [System.DateTimeOffset]::UtcNow
$startTicks = [System.Diagnostics.Stopwatch]::StartNew()

# Source information
$outerDirty = $false
$applicationDirty = $false
$outerCommit = ''
$applicationCommit = ''

try {
    $outerCommit = (& git -C $outerRoot rev-parse HEAD 2>$null).Trim()
    $outerStatus = (& git -C $outerRoot status --porcelain 2>$null).Trim()
    $outerDirty = (-not [string]::IsNullOrEmpty($outerStatus))
}
catch {
    $outerCommit = '0000000000000000000000000000000000000000'
    $outerDirty = $true
}

try {
    $applicationCommit = (& git -C $repositoryRoot rev-parse HEAD 2>$null).Trim()
    $applicationStatus = (& git -C $repositoryRoot status --porcelain 2>$null).Trim()
    $applicationDirty = (-not [string]::IsNullOrEmpty($applicationStatus))
}
catch {
    $applicationCommit = '0000000000000000000000000000000000000000'
    $applicationDirty = $true
}

# Binary hash
$binarySha256 = ''
if (Test-Path -LiteralPath $exePath -PathType Leaf) {
    $binarySha256 = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
}

# System information
$cpuInfo = (Get-CimInstance -ClassName Win32_Processor -ErrorAction SilentlyContinue | Select-Object -First 1).Name
if ([string]::IsNullOrEmpty($cpuInfo)) { $cpuInfo = 'unknown' }
$osInfo = [string]::Format('{0} {1}', (Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction SilentlyContinue).Caption, (Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction SilentlyContinue).Version)
if ([string]::IsNullOrEmpty($osInfo.Trim())) { $osInfo = 'unknown' }
$powerScheme = (powercfg /getactivescheme 2>$null)
if ([string]::IsNullOrEmpty($powerScheme)) { $powerScheme = 'unknown' }
$locale = [System.Globalization.CultureInfo]::CurrentCulture.Name

# Run tests
Write-Host "[test] Running APEXTests $Configuration"
$previousFixturePath = [Environment]::GetEnvironmentVariable('APEX_TEST_VST3_PATH', 'Process')
$previousMonoFixturePath = [Environment]::GetEnvironmentVariable('APEX_TEST_VST3_MONO_PATH', 'Process')
$previousFaultFixturePath = [Environment]::GetEnvironmentVariable('APEX_TEST_VST3_FAULT_PATH', 'Process')
$previousStateFixturePath = [Environment]::GetEnvironmentVariable('APEX_TEST_VST3_STATE_PATH', 'Process')
$previousAutomationFixturePath = [Environment]::GetEnvironmentVariable('APEX_TEST_VST3_AUTOMATION_PATH', 'Process')
$previousDenseE2BFixturePath = [Environment]::GetEnvironmentVariable('APEX_TEST_VST3_DENSE_E2B_PATH', 'Process')
$previousPluginWorkerPath = [Environment]::GetEnvironmentVariable('APEX_TEST_PLUGIN_WORKER_PATH', 'Process')
try {
    [Environment]::SetEnvironmentVariable('APEX_TEST_VST3_PATH', $fixtureBundle, 'Process')
    [Environment]::SetEnvironmentVariable('APEX_TEST_VST3_MONO_PATH', $monoFixtureBundle, 'Process')
    [Environment]::SetEnvironmentVariable('APEX_TEST_VST3_FAULT_PATH', $faultFixtureBundle, 'Process')
    [Environment]::SetEnvironmentVariable('APEX_TEST_VST3_STATE_PATH', $stateFixtureBundle, 'Process')
    [Environment]::SetEnvironmentVariable('APEX_TEST_VST3_AUTOMATION_PATH', $automationFixtureBundle, 'Process')
    # Test-only worker injection: the runner executable dispatches to the
    # exact production PluginWorkerMainCore path with APEX test hooks enabled.
    [Environment]::SetEnvironmentVariable('APEX_TEST_PLUGIN_WORKER_PATH', $exePath, 'Process')
    if ($denseE2BFixtureBundle) {
        [Environment]::SetEnvironmentVariable('APEX_TEST_VST3_DENSE_E2B_PATH', $denseE2BFixtureBundle, 'Process')
    }
    & $exePath @testArguments
    $testExitCode = $LASTEXITCODE
}
finally {
    [Environment]::SetEnvironmentVariable('APEX_TEST_VST3_PATH', $previousFixturePath, 'Process')
    [Environment]::SetEnvironmentVariable('APEX_TEST_VST3_MONO_PATH', $previousMonoFixturePath, 'Process')
    [Environment]::SetEnvironmentVariable('APEX_TEST_VST3_FAULT_PATH', $previousFaultFixturePath, 'Process')
    [Environment]::SetEnvironmentVariable('APEX_TEST_VST3_STATE_PATH', $previousStateFixturePath, 'Process')
    [Environment]::SetEnvironmentVariable('APEX_TEST_VST3_AUTOMATION_PATH', $previousAutomationFixturePath, 'Process')
    [Environment]::SetEnvironmentVariable('APEX_TEST_VST3_DENSE_E2B_PATH', $previousDenseE2BFixturePath, 'Process')
    [Environment]::SetEnvironmentVariable('APEX_TEST_PLUGIN_WORKER_PATH', $previousPluginWorkerPath, 'Process')
}

$durationMs = [int] $startTicks.ElapsedMilliseconds

# Parse seed as integer
$seedInt = [long] 0
if ($Seed) {
    try { $seedInt = [long] $Seed }
    catch { $seedInt = [long] 0 }
}

# --- Build evidence manifest ---
$evidenceGrade = 'diagnostic'
if ((-not $outerDirty) -and (-not $applicationDirty)) {
    $evidenceGrade = 'release'
}

$evidenceManifest = [ordered]@{
    schemaVersion = 1
    evidenceGrade = $evidenceGrade
    runId = $runId
    suiteId = $suiteId
    source = [ordered]@{
        outerCommit = $outerCommit
        outerDirty = $outerDirty
        applicationCommit = $applicationCommit
        applicationDirty = $applicationDirty
    }
    build = [ordered]@{
        configuration = $Configuration
        platform = 'x64'
        toolset = 'v145'
        binarySha256 = $binarySha256
    }
    dependencies = [ordered]@{
        juceVersion = [string] $contractData.juce.version
    }
    environment = [ordered]@{
        os = $osInfo
        cpu = $cpuInfo
        powerMode = $powerScheme
        locale = $locale
    }
    execution = [ordered]@{
        command = @('APEXTests.exe') + $testArguments
        startedUtc = $startUtc.ToString('o')
        seed = $seedInt
        exitCode = $testExitCode
        durationMs = $durationMs
    }
    results = [ordered]@{
        resultGroups = 0
        assertionsPassed = 0
        assertionsFailed = 0
    }
    artifacts = @()
}

# --- Atomic publish ---
$evidenceRoot = Join-Path $repositoryRoot 'evidence\runs'
$suiteDir = Join-Path $evidenceRoot $suiteId
$tempRunDir = Join-Path $suiteDir $runId

try {
    [System.IO.Directory]::CreateDirectory($tempRunDir) | Out-Null

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    $manifestJson = $evidenceManifest | ConvertTo-Json -Depth 10
    [System.IO.File]::WriteAllText(
        (Join-Path $tempRunDir 'manifest.json'),
        $manifestJson,
        $utf8NoBom
    )

    # Validate the manifest
    $validatorScript = Join-Path $PSScriptRoot 'validate_test_evidence.ps1'
    if (Test-Path -LiteralPath $validatorScript -PathType Leaf) {
        $validateResult = & $validatorScript -Manifest (Join-Path $tempRunDir 'manifest.json')
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Evidence manifest validation failed."
            exit 1
        }
    }

    # Capture stdout/stderr from the test run to a file
    $stdoutPath = Join-Path $tempRunDir 'stdout.txt'
    [System.IO.File]::WriteAllText($stdoutPath, "[test] APEXTests $Configuration`n", $utf8NoBom)

    # Rename directory to final name
    $shortCommit = $applicationCommit.Substring(0, [Math]::Min(7, $applicationCommit.Length))
    $finalDirName = "$($startUtc.ToString('yyyyMMddTHHmmssZ'))-$Configuration-$shortCommit"
    $finalRunDir = Join-Path $suiteDir $finalDirName

    if (Test-Path -LiteralPath $finalRunDir) {
        Remove-Item -LiteralPath $finalRunDir -Recurse -Force
    }
    Rename-Item -LiteralPath $tempRunDir -NewName $finalDirName -Force

    Write-Host "[evidence] Published to $finalRunDir"
}
catch {
    if (Test-Path -LiteralPath $tempRunDir) {
        Remove-Item -LiteralPath $tempRunDir -Recurse -Force
    }
    Write-Error "Failed to publish evidence: $_"
    exit 1
}

if ($testExitCode -ne 0) {
    Write-Error "APEXTests exited with code $testExitCode."
    exit $testExitCode
}

Write-Host "[PASS] APEXTests $Configuration completed."
exit 0
