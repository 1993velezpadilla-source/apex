# C4 Phase 8 — Native Host Integration (PASS)

Status: **C4 PHASE 8 — PASS** — declared 2026-08-13. Phases 5-7 remain frozen.

## The integration defect found and fixed

MEASURED FACT (repository + JUCE 8.0.12 source authority): the APEX host
registers G10 and C4 as two format objects that share the family name
"APEX Native". `juce::AudioPluginFormatManager::addFormat` keys formats by
NAME and silently REJECTS a same-named format (`jassertfalse` + drop in
Debug). Consequence: the C4 format object was dead code — C4 descriptions
could never resolve through the manager ("No compatible plug-in format
exists"), the Debug app asserted on every launch, and only the cache-seeded
description existed without a creation path.

EXACT FIX (host-side; no DSP touched): one registered format object per
unique name — new `Source/PluginHostCore/ApexNativePluginFormat.h`, a
unified "APEX Native" family format that:
- matches any intrinsic identifier (`APEX::G10`, `APEX::C4`, empty);
- `findAllTypesForFile` exposes both natives deterministically;
- `createPluginInstance` routes to `APEX::G10::G10Processor` or
  `APEX::C4::C4Processor` by description (synchronous, message-thread safe);
- `doesPluginStillExist` accepts either identity.
`PluginScanFormatsCore` now registers exactly ONE `ApexNativePluginFormat`.
The per-plugin format classes remain the canonical description builders and
are still covered by their own direct-format tests.

## Registration / seeding (verified in source)

- `Source/PluginScanCore/PluginScanFormatsCore.h` — the unified native
  format registered after the external formats.
- `Source/PluginHostCore/PluginScannerCore.h` — `seedNativeG10Entry()` +
  `seedNativeC4Entry()` called at construction (lines 34-35) and on every
  `beginScan` (lines 256-257); `cacheFromDescription` dedupes on
  path+uniqueId, so the seeds are deterministic and never duplicate.

## .jucer registration (verified complete)

`DAW_Core.jucer` C4Core group: C4Types.h, C4TuningProfile.h, C4FilterCore.h,
C4BandCore.h, C4EngineCore.h, C4Processor.h, C4NativePluginFormat.h,
C4HaloShelfMapper.h, C4ResidualBankCore.h, C4SpectrumCore.h,
C4ResponseCurveCore.h, C4Editor.h, C4Processor.cpp — plus the new
NativePlugins group with ApexNativePluginFormat.h. Resaved with the pinned
JUCE 8.0.12 Projucer before every app build.

## Host tests (green)

- `C4.FormatHost / Phase 8 host path: the unified APEX Native format serves
  G10 AND C4` (new): exactly one registered "APEX Native" format; C4
  instantiates through the REAL manager with editor creation; G10 still
  instantiates through the same manager; `findAllTypesForFile` exposes both
  natives; foreign identifiers fail with a clean error.
- Existing G10 format-manager/registration tests unchanged and green
  (APEX.G10 manifest below) — no scanner-count test was weakened.
- Existing C4 native format, scanner cache, and browser seeding tests green.

## App builds (the real DAW)

| Build | Artifact | SHA-256 |
|---|---|---|
| APEX app Debug (Rebuild, unsigned) | `Builds\VisualStudio2026\x64\Debug\App\DAW_Core.exe` | `44BAACF9...250E92` |
| APEX app Release (Rebuild, unsigned) | `Builds\VisualStudio2026\x64\Release\App\DAW_Core.exe` | `630D2B26...B19DB4` |

Both PASS with zero new warnings/errors; Authenticode NotSigned as requested.

## Real host validation (APEX running)

The Release DAW was launched with C4 integrated; the process window
("DAW_Core") is live and a desktop capture was saved
(`apex_inhost_launch.png`). The in-APEX visual checkpoint is interactive:
insert C4 from the native plugin list, open the editor (660x640 compact /
660x880 expanded), verify the console, and close/reopen/remove freely.
Manual checklist (host-chain/lifecycle): insert C4 on a track; open/close/
reopen editor; remove + reinsert; multiple instances; G10 -> C4 on one
track; save/reopen project (parameters, Spectrum Flag state, BLOOM default
4.0); verify audio passes.

## Regression evidence

| Gate | Result |
|---|---|
| APEX.C4 Debug (post-fix, incl. manager tests) | GREEN — `d991f9b9fc86` |
| APEX.G10 Debug (G10 intact) | GREEN — `d5d52a05ed5a` |
| Global Debug | GREEN — `95b309bb0fe4` |
| Global Release | GREEN — `3c6104054716` |
| APEX app Debug build | PASS (hash above) |
| APEX app Release build | PASS (hash above) |

## Frozen contracts preserved

No C4 production tuning, BLOOM constant, HALO, coupling, residual,
oversampling, Phase 5 sound, or Phase 6 analyzer DSP contract changed.
The only Phase 8 code change is host registration (the unified family
format) plus its tests.
