# Called by run_apex_tests.ps1 after the runner and fixtures have been built.
. (Join-Path $PSScriptRoot 'test_result_tools.ps1')
$suiteId = if ($Category) { $Category } elseif ($Name) { $Name } else { 'full-suite' }
$suiteId = $suiteId -replace '[^A-Za-z0-9_.-]', '_'
$runId = [guid]::NewGuid().ToString('N').Substring(0,12)
$startUtc = [DateTimeOffset]::UtcNow
$runDir = Join-Path (Join-Path $repositoryRoot 'evidence/runs') "$suiteId/$($startUtc.ToString('yyyyMMddTHHmmssZ'))-$Configuration-$runId"
[IO.Directory]::CreateDirectory($runDir) | Out-Null
$stdoutPath = Join-Path $runDir 'stdout.txt'
$stderrPath = Join-Path $runDir 'stderr.txt'
$resultsPath = Join-Path $runDir 'results.json'
if ($ResultsJson) { $resultsPath = [IO.Path]::GetFullPath($ResultsJson) }
if (Test-Path -LiteralPath $resultsPath) { throw "Refusing to reuse existing results JSON: $resultsPath" }
[IO.Directory]::CreateDirectory((Split-Path -Parent $resultsPath)) | Out-Null
$testArguments = @()
if ($Category) { $testArguments += "--category=$Category" }
if ($Name) { $testArguments += "--name=$Name" }
if ($Seed) { $testArguments += "--seed=$Seed" }
$testArguments += "--results-json=$resultsPath"
$fixtureEnvironment = @{
    APEX_TEST_VST3_PATH=$fixtureBundle
    APEX_TEST_VST3_MONO_PATH=$monoFixtureBundle
    APEX_TEST_VST3_FAULT_PATH=$faultFixtureBundle
    APEX_TEST_VST3_STATE_PATH=$stateFixtureBundle
    APEX_TEST_VST3_AUTOMATION_PATH=$automationFixtureBundle
    APEX_TEST_VST3_DENSE_E2B_PATH=$denseE2BFixtureBundle
    APEX_TEST_PLUGIN_WORKER_PATH=$exePath
}
$previousEnvironment = @{}
$testExitCode = 2
$resultStatus = 'INCOMPLETE'
$resultsError = $null
$counts = $null
$stopwatch = [Diagnostics.Stopwatch]::StartNew()
try {
    foreach ($key in $fixtureEnvironment.Keys) {
        $previousEnvironment[$key] = [Environment]::GetEnvironmentVariable($key,'Process')
        [Environment]::SetEnvironmentVariable($key,$fixtureEnvironment[$key],'Process')
    }
    $quotedArguments = ($testArguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }) -join ' '
    Write-Host "[test] Running APEXTests $Configuration; logs: $runDir"
    $process = Start-Process -FilePath $exePath -ArgumentList $quotedArguments -WorkingDirectory $exeDir -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    # Keep the native handle open until the exit code has been read, including
    # for a runner that exits before Start-Process returns.
    $null = $process.Handle
    if (-not $process.WaitForExit(90*60*1000)) {
        $process.Kill()
        $process.WaitForExit()
        throw 'Test runner exceeded its 90-minute limit; results remain incomplete.'
    }
    $process.WaitForExit()
    if ($null -eq $process.ExitCode) { throw 'Test process did not expose an exit code.' }
    $testExitCode = [int]$process.ExitCode
    $counts = Read-ApexTestResults -Path $resultsPath -Configuration $Configuration
    $resultStatus = if ($testExitCode -eq 0 -and $counts.assertionsFailed -eq 0) { 'PASS' } else { 'FAILED' }
}
catch { $resultsError = [string]$_; Write-Warning $resultsError }
finally {
    foreach ($key in $previousEnvironment.Keys) { [Environment]::SetEnvironmentVariable($key,$previousEnvironment[$key],'Process') }
    $stopwatch.Stop()
}
# A missing/invalid result file must never produce a successful CI exit.
if ($resultStatus -ne 'PASS' -and $testExitCode -eq 0) { $testExitCode = 2 }
if ($resultsPath -ne (Join-Path $runDir 'results.json') -and (Test-Path -LiteralPath $resultsPath -PathType Leaf)) {
    Copy-Item -LiteralPath $resultsPath -Destination (Join-Path $runDir 'results.json')
}
function Get-ApexSourceIdentity([string]$Directory) {
    $commit = (@(& git -C $Directory rev-parse HEAD 2>$null) -join '').Trim()
    $ok = $LASTEXITCODE -eq 0 -and $commit -match '^[0-9a-f]{40}$'
    $status = @(& git -C $Directory status --porcelain 2>$null)
    [pscustomobject]@{commit=$(if($ok){$commit}else{'0000000000000000000000000000000000000000'});dirty=(!$ok -or $LASTEXITCODE -ne 0 -or $status.Count -gt 0)}
}
$outerIdentity = Get-ApexSourceIdentity $outerRoot
$coreIdentity = Get-ApexSourceIdentity $repositoryRoot
$seedInt = [long]0
if ($Seed) { $seedInt = if ($Seed -match '^0[xX]') { [Convert]::ToInt64($Seed.Substring(2),16) } else { [long]$Seed } }
[IO.File]::WriteAllText((Join-Path $runDir 'run-status.json'),([ordered]@{status=$resultStatus;error=$resultsError} | ConvertTo-Json),(New-Object Text.UTF8Encoding($false)))
$artifacts = @()
foreach ($leaf in @('stdout.txt','stderr.txt','results.json','run-status.json')) {
    $path = Join-Path $runDir $leaf
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $artifacts += [ordered]@{path=$leaf;bytes=[long](Get-Item -LiteralPath $path).Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    }
}
$manifest = [ordered]@{
    schemaVersion=1; evidenceGrade='diagnostic'; runId=$runId; suiteId=$suiteId
    source=[ordered]@{outerCommit=$outerIdentity.commit;outerDirty=$outerIdentity.dirty;applicationCommit=$coreIdentity.commit;applicationDirty=$coreIdentity.dirty}
    build=[ordered]@{configuration=$Configuration;platform='x64';toolset='v145';binarySha256=(Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash}
    dependencies=[ordered]@{juceVersion=[string]$contractData.juce.version}
    environment=[ordered]@{os=[Environment]::OSVersion.VersionString;cpu=$env:PROCESSOR_IDENTIFIER;powerMode='not measured';locale=[Globalization.CultureInfo]::CurrentCulture.Name}
    execution=[ordered]@{command=@('APEXTests.exe')+$testArguments;startedUtc=$startUtc.ToString('o');seed=$seedInt;exitCode=$testExitCode;durationMs=$stopwatch.ElapsedMilliseconds}
    results=$counts
    artifacts=$artifacts
}
$temporaryManifest = Join-Path $runDir 'manifest.json.tmp'
[IO.File]::WriteAllText($temporaryManifest,($manifest | ConvertTo-Json -Depth 10),(New-Object Text.UTF8Encoding($false)))
Move-Item -LiteralPath $temporaryManifest -Destination (Join-Path $runDir 'manifest.json')
foreach ($log in @($stdoutPath,$stderrPath)) {
    if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Tail 25 | Write-Host }
}
Write-Host "[evidence] $resultStatus : $runDir"
if ($resultStatus -eq 'PASS') {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'validate_test_evidence.ps1') -Manifest (Join-Path $runDir 'manifest.json')
    if ($LASTEXITCODE -ne 0) { exit 1 }
    Write-Host "[PASS] APEXTests ${Configuration}: $($counts.assertionsPassed) passed assertions, $($counts.assertionsFailed) failed."
    exit 0
}
exit $(if ($testExitCode -ne 0) { $testExitCode } else { 1 })
