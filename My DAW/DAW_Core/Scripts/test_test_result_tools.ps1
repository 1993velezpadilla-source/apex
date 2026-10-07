$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_result_tools.ps1')
$temporary=Join-Path ([IO.Path]::GetTempPath()) ('apex-result-contract-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
$file=Join-Path $temporary 'results.json'
$checked=0
function Write-Result($value) { [IO.File]::WriteAllText($file,($value|ConvertTo-Json -Depth 5),(New-Object Text.UTF8Encoding($false))) }
function Assert-Rejected { try { Read-ApexTestResults -Path $file -Configuration Debug | Out-Null } catch { $script:checked++; return }; throw 'Invalid/missing results were accepted.' }
try {
    Assert-Rejected
    $valid=@{schemaVersion=1;configuration='Debug';resultGroups=1;assertionsPassed=2;assertionsFailed=0;tests=@(@{passes=2;failures=0})}
    Write-Result $valid
    $r=Read-ApexTestResults -Path $file -Configuration Debug
    if($r.assertionsPassed -ne 2 -or $r.assertionsFailed -ne 0){throw 'Valid assertions were lost.'}; $checked++
    $valid.assertionsPassed=1; $valid.assertionsFailed=1; $valid.tests=@(@{passes=1;failures=1})
    Write-Result $valid
    if((Read-ApexTestResults -Path $file -Configuration Debug).assertionsFailed -ne 1){throw 'Real failures were hidden.'}; $checked++
    $valid.assertionsFailed=0; Write-Result $valid; Assert-Rejected
    $valid.configuration='Release'; Write-Result $valid; Assert-Rejected
    $valid.configuration='Debug'; $valid.resultGroups=0; Write-Result $valid; Assert-Rejected
    [IO.File]::WriteAllText($file,'{invalid'); Assert-Rejected
    Write-Host "[PASS] result publication contract: $checked checks."
}
finally { Remove-Item -LiteralPath $file -ErrorAction SilentlyContinue; Remove-Item -LiteralPath $temporary }
