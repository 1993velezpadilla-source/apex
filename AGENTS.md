# APEX Agent Rules

## Official workspace

The official APEX workspace is:

`C:\Users\1993v\OneDrive\Desktop\Apex backup`

Do not inspect, edit, build, or reference another copy of APEX unless the user explicitly authorizes it.

## Canonical knowledge

- The canonical DAW architecture reference is `../../DAW_BRAIN.md` in the official workspace.
- `.opencode/agents/daw-brain.md` defines the conversational DAW Brain agent.
- The actual repository and measured runtime determine what APEX currently implements.

## Required workflow

For repository questions:

1. Read the relevant DAW Brain section.
2. Inspect the actual implementation.
3. Identify the authority owner, thread roles, data flow, and lifecycle.
4. Report exact file and symbol evidence.
5. Separate verified facts from architectural recommendations.
6. Do not edit until the user authorizes implementation.
7. After edits, build and run focused regression tests.
8. Never equate compilation with behavioral correctness.

## Project invariants

- No blocking, allocation, file I/O, GUI calls, plugin lifecycle work, or graph rebuilding in the realtime callback.
- Preserve one-owner monitoring paths and prevent duplicate live-input contribution.
- Preserve offline-render isolation from live monitoring and transport-only fades.
- Preserve previous valid project/export artifacts on publication failure.
- Use stable IDs across persistence boundaries.
- Do not invent repository details.

## Rules

- Do not invent paths.
- Do not invent classes.
- Do not invent symbols.
- Do not invent results of compilations.
- Do not invent results of runtime.
- Do not modify files outside the workspace.
- Do not use another copy of APEX.
- Do not make large refactors without authorization.
- Do not delete functional systems to replace them with an ideal architecture.
- Preserve functional parts of APEX during any future migration.

## Proven build and test commands

All commands run from `My DAW/DAW_Core`:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\test_repository_policy.ps1
powershell -ExecutionPolicy Bypass -File Scripts\verify_dependencies.ps1
powershell -ExecutionPolicy Bypass -File Scripts\test_validate_test_evidence.ps1
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Seed 0xA9E12026
```

Every command must exit 0. Both application binaries and both test binaries must
exist. Evidence manifests must contain zero failed assertions. See
`My DAW/DAW_Core/docs/BUILDING_WINDOWS.md` and
`My DAW/DAW_Core/docs/TESTING_WINDOWS.md` for full details.

## Debug command

When the user says **"debug APEX"**, run:

```
powershell -ExecutionPolicy Bypass -File tools\debug-apex.ps1
```

This script:
1. Builds DAW_Core as x64 Debug via MSBuild.
2. Verifies the EXE and PDB exist.
3. Locates cdb.exe (Debugging Tools for Windows).
4. Launches APEX under CDB with symbol paths.
5. Catches unhandled native C++ exceptions and access violations.

**When CDB exits after a crash:**
1. Find the newest file in `crash-reports\`.
2. Read `tools\analyze-latest-crash.md` and follow its analysis workflow.
3. Report the exception code, faulting address, and first APEX frame.
4. Classify the crash (null deref, use-after-free, etc.).
5. Identify the root cause vs. symptom.
6. Report file-and-line evidence before editing source code.
7. Propose the smallest safe correction.
8. Ask for approval before modifying source code.
9. Build x64 Debug after approved correction.
10. Never claim the crash is fixed from compilation alone.

## Current project audit and session memory

Read `APEX_PROJECT_AUDIT.md` and `APEX_STATE.md` for the 2026-10-03 snapshot before planning development. Detailed evidence, persistence paths, architecture and backlog AUD-01 through AUD-12 are in `docs/apex-audit/`. Recheck Git and affected code when the state changes; this snapshot does not certify a global release.

The canonical Brain is the file at this workspace root. The `My DAW/DAW_Core/DAW_BRAIN.md` file is historical. Preserve active source in `My DAW/DAW_Core/Builds/VisualStudio2026/ArrangementEditor`; that folder is not disposable build output.
