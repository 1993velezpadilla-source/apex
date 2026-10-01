# APEX Build Registry

**Last verified:** 2026-07-24

## Build System

- **Framework:** JUCE Projucer → Visual Studio 2026
- **Solution:** `My DAW/DAW_Core/Builds/VisualStudio2026/` (main app)
- **Test Solution:** `My DAW/DAW_Core/Tests/Builds/VisualStudio2026/`
- **Build Script:** `Scripts/build_apex.ps1`
- **Dependency Check:** `Scripts/verify_dependencies.ps1`
- **Policy Check:** `Scripts/test_repository_policy.ps1`

## Build Configurations

| Config | Platform | Command |
|---|---|---|
| Debug | x64 | `.\Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned` |
| Release | x64 | `.\Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned` |

## Build Dependencies

- MSBuild (Visual Studio 2026)
- JUCE Projucer (pinned via `Dependencies/apex-windows-dependencies.json`)
- Windows SDK

## Build Evidence

| Date | Config | Result | Output | Notes |
|---|---|---|---|---|
| 2026-07-24 | Debug x64 | PASS | `DAW_Core.exe` | Signed successfully |
| 2026-07-24 | Release x64 | PASS | `DAW_Core.exe` | Signed successfully |
| 2026-07-24 | Test Debug x64 | PASS | `APEXTests.exe` | 120+ tests, 0 failures |

## Build Notes

- Projucer must regenerate solution before first build
- Dependencies must be materialized via `materialize_dependencies.ps1`
- Signing can be skipped with `-Unsigned` flag
