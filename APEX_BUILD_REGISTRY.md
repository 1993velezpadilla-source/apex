# APEX Build Registry

> Registro histórico de julio de 2026. Para builds, pruebas enfocadas, hashes, fallos y límites actuales del 3 de octubre, leer [APEX_PROJECT_AUDIT.md](APEX_PROJECT_AUDIT.md) y [calidad](docs/apex-audit/quality.md). La batería completa actual no está aprobada. Las antiguas etiquetas NO TEST no describen la existencia actual de suites y no deben usarse como inventario vigente.

**Last updated:** 2026-10-03. Historical July rows were not all re-verified; FIX-017 Debug and Release entries below are focused evidence, not full-suite certification.

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
| 2026-10-03 | App Debug x64 | PASS | `DAW_Core.exe` | FIX-017; unsigned; SHA-256 `B98C221BD66954BEB361F5D8EB58D7D0DB6450450C616B01C26F1686877959A9`; dependency preflight warned on Signalsmith Git ownership and continued |
| 2026-10-03 | Test Debug x64 | PASS | `APEXTests.exe` | FIX-017 focused suite; SHA-256 `11A1A516E554FB3C28A4D2A2812AFF6611BC0E34A7D5F07BFA915DED927507C5` |
| 2026-10-03 | App Release x64 | PASS | DAW_Core.exe | FIX-017 current source; 14,053,888 bytes; unsigned; SHA-256 EEE64C936367120B20BAD1E0B29CB654109B34D3CDA937FD0F61DC789C41231F; direct MSBuild after Git ownership blocked the wrapper preflight |
| 2026-10-03 | Test Release x64 | FOCUSED PASS | APEXTests.exe | mixer.clip-input-meter-signal.v1; 8/8 cases, 0 failures; 14,879,232 bytes; SHA-256 E11F977EA2AE8643ECAF9EB98BBB7D23B7BB7744EC1D3C2E9B294D5B576FC9B8; no full suite run |

## Build Notes

- Projucer must regenerate solution before first build
- Dependencies must be materialized via `materialize_dependencies.ps1`
- Signing can be skipped with `-Unsigned` flag

