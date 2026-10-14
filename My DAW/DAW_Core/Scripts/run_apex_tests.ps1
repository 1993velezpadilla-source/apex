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
$needsDenseE2BFixture = ((-not $Category -and -not $Name) -or $Category -eq 'PluginSandboxPhaseE2B')
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

& (Join-Path $PSScriptRoot 'publish_test_run.ps1')
exit $LASTEXITCODE
