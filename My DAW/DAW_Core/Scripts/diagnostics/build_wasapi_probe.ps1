param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231'
$sdk='C:\Program Files (x86)\Windows Kits\10'
$modules=Join-Path $root '..\Sdk setups\juce-8.0.12-windows\JUCE\modules'
$appObjects=Join-Path $root "Builds\VisualStudio2026\x64\$Configuration\App"
[xml]$project=Get-Content (Join-Path $root 'Builds\VisualStudio2026\DAW_Core_App.vcxproj')
$group=$project.Project.ItemDefinitionGroup | Where-Object { $_.Condition -like "*${Configuration}|x64*" } | Select-Object -First 1
$defines=@($group.ClCompile.PreprocessorDefinitions.Split(';') | Where-Object { $_ -and $_ -notlike '%*' } | ForEach-Object { '/D'+$_ })
$includes=@(@("$vc\include", "$sdk\Include\10.0.26100.0\ucrt", "$sdk\Include\10.0.26100.0\um", "$sdk\Include\10.0.26100.0\shared", "$sdk\Include\10.0.26100.0\winrt", $modules) | ForEach-Object { '/I'+$_ })
$objects=@(Get-ChildItem -LiteralPath $appObjects -Filter 'include_juce*.obj' | ForEach-Object { $_.FullName })
$runtime=if($Configuration -eq 'Debug'){'/MDd'}else{'/MD'}
$output=Join-Path $root "evidence\WasapiJuceProbe-$Configuration.exe"
& "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /O2 $runtime @defines @includes "$PSScriptRoot\WasapiJuceProbe.cpp" "/Fo$root\evidence\WasapiJuceProbe-$Configuration.obj" "/Fe$output" @objects /link "/LIBPATH:$vc\lib\x64" "/LIBPATH:$sdk\Lib\10.0.26100.0\um\x64" "/LIBPATH:$sdk\Lib\10.0.26100.0\ucrt\x64" ole32.lib oleaut32.lib shell32.lib uuid.lib comdlg32.lib gdi32.lib
exit $LASTEXITCODE
