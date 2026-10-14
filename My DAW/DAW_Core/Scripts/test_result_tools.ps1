function Read-ApexTestResults {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Path, [Parameter(Mandatory=$true)][string]$Configuration)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw 'The runner did not publish results JSON; the run is incomplete.' }
    $result = [IO.File]::ReadAllText([IO.Path]::GetFullPath($Path)) | ConvertFrom-Json
    if ($result.schemaVersion -ne 1 -or [string]$result.configuration -cne $Configuration) { throw 'Results schema/configuration does not match this invocation.' }
    foreach ($field in @('resultGroups','assertionsPassed','assertionsFailed')) {
        $value = $result.$field
        if ($null -eq $value -or $value -is [string] -or $value -is [bool] -or $value -lt 0 -or [math]::Floor([double]$value) -ne [double]$value) {
            throw "Invalid runner counter: $field"
        }
    }
    $groups = @($result.tests)
    if ($result.resultGroups -le 0 -or $groups.Count -ne $result.resultGroups) { throw 'Missing or inconsistent runner result groups.' }
    [long]$passed = 0
    [long]$failed = 0
    foreach ($group in $groups) {
        foreach ($field in @('passes','failures')) {
            $value = $group.$field
            if ($null -eq $value -or $value -is [string] -or $value -is [bool] -or $value -lt 0 -or [math]::Floor([double]$value) -ne [double]$value) { throw "Invalid test-group counter: $field" }
        }
        $passed += [long]$group.passes
        $failed += [long]$group.failures
    }
    if ($passed -ne $result.assertionsPassed -or $failed -ne $result.assertionsFailed -or ($passed+$failed) -le 0) { throw 'Runner totals disagree with its executed assertions.' }
    [pscustomobject]@{resultGroups=[long]$result.resultGroups;assertionsPassed=$passed;assertionsFailed=$failed}
}
