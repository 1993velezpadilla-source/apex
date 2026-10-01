# Session Handoff — 2026-09-17

Full engineering report: `docs/superpowers/reports/2026-09-17-sidechain-bus-engine-and-project-upgrade-report.md`.
This file is the agent-facing summary: what changed, why, and what is still open.

## 1. Sidechain from/to BUS nodes did not work (root cause found)

**Symptom**: an old project's sidechain (cable visible, EXT enabled in the
plugin) never ducked, while newly created sidechains in new projects worked.

**Root cause**: `Source/AudioEngineCore/AudioEngine.h` — `processBusNode` had
NEITHER half of the sidechain contract:

- no sidechain send block (the "── Sidechain sends ──" loop existed only in
  `processTrackNodeExt`), so a sidechain SOURCED from a bus (the user's
  "Vocal Bus") never filled the destination's sidechain buffer;
- no sidechain receive branch (it called plain `processBlock` on its plugin
  chain), so a bus-hosted plugin never received its sidechain either.

The destination plugin's detector therefore saw silence. The host side was
correct the whole time (proven by `sidechain_diagnostic.log`: `auxActive=1`,
aux bus `en=1` 2ch).

**Fix** (commit `a6c6f94`): both halves added to `processBusNode`, mirroring
the track path, including the sidechain PDC line.

## 2. Fixes that were correct but not sufficient (all retained)

Each closes a real defect found during the investigation; keep them:

| Commit | Fix |
|---|---|
| `0149205` | VST3 spec: retry a rejected bus layout once (a plugin may adapt and still return `kResultFalse`; JUCE treats any non-`kResultTrue` as failure). Bounded, sidechain-only. |
| `7516a7e` | `notifyGraphChanged()` after reactivating migrated routes — the engine consumes a published snapshot. |
| `1e8cc78` | Fill an empty legacy `scPluginId` from the first slot with a non-main input bus. |
| `a7ac9df` | Reactivate sidechain routes that old builds deactivated. |
| `76382b6` | Run the legacy repair UNCONDITIONALLY (a v9 stamp can still carry broken data). |
| `fb41ef1` | One-shot deferred bus-config re-sync after load (plugins may still be instantiating). |
| `3e72951` | v9 migration persists resolved sidechain bus indices. |

**Corrected mistake** (`ccc1f26`): an earlier revision muted Sends sharing a
sidechain node pair. That is the legitimate reverb-send + duck setup — the
migration no longer touches any Send/Direct route.

## 3. Legacy project repair chain (runs on EVERY load, idempotent)

In `Source/ProjectCore/ProjectManager.cpp` + `Source/AppCore/ApplicationCore.cpp`:

1. `track.tape_stop` lanes → clip-local `clip.<id>.tape_stop` lanes.
2. Sidechain routes reactivated; bus index 0 resolved to the first non-main
   input bus; empty plugin targets filled.
3. Graph notified so the engine adopts a snapshot with the repaired routes.

## 4. Upgrade Project tool + the future-proof contract

Commit `e0a6e6e`:

- Startup panel button + Settings menu item → system file picker (`*.dawproj`)
  → timestamped backup → open (repair chain runs against live plugins) →
  crash-safe save → report dialog.
- `Source/ProjectCore/ProjectUpgradeReport.h` — **the contract**: when a
  future update changes the project format, add (1) an idempotent repair step
  to the load-time chain and (2) one `report->add("...", count)` call. Old
  projects then heal on load AND through the tool.

## 5. Quick Tracks / Quick Route — vocal FX families

Commit `59f9fdb`:

- Six family cards added (`vocal_reverb`, `vocal_delay`, `vocal_pitch`,
  `vocal_mod`, `vocal_char`, `vocal_stereo`), all routing to the FX bus like
  `vocal_fx`, each keeping its own plus/minus/create/clear controls.
- Dropdown arrow on family cards + the generic Vocal FX card. Nested menus:
  Reverb = **by Source** (Vocal/Doubles/Adlib-HL/Harmonies) + **Types**
  (Plate/Hall/Shimmer/Room/Chamber/Spring); Delay = **by Source** + **Types**
  (Slap, 1/8, 1/4, Dotted 1/8, Ping-Pong).
- `QuickWorkflowRequest::nameOverride` / `QuickTrackRequest::nameOverride`
  create the same track as the owning role with the chain's name (uniquified
  via `QuickTrackNaming::nextNameFor`). Creation, colour family, routing and
  identity are unchanged.
- `QuickWorkflowTests.cpp` updated to the new canonical catalog (14 track
  roles, 21 cards).

## 6. Method that ended the guessing cycle (reuse it)

1. Read the runtime logs from `%APPDATA%\DAW_Core\` FIRST (they are on this
   machine — no need to ask the user to paste anything).
2. Read the user's actual project XML (`.dawproj`) when the defect is
   project-specific.
3. Find the discriminating variable between the working and broken cases
   (here: track-source vs bus-source) and follow it into the code.
4. Never equate a build passing with a behavior working.

## 7. Repository state at handoff

- Builds: Debug + Release PASS with the current tree.
- Tests: `automation.polish.v1`, `automation.rt-access.v1`,
  `automation.smoother.block-invariance.v1`, `split.transaction.automation.v1`,
  `APEX.QuickWorkflow`, `APEX.QuickTrackCore` — all PASS.
- Git: clean tree; the session's work is in commits `3e72951` … `59f9fdb`
  plus the WIP snapshot `a61303a` (771 files) and the report `0273b91`.
- No git remote is configured (local repository only).

## 8. Open items

- **User visual verification of the Quick Tracks dropdowns** (built and
  test-verified, not yet user-verified).
- **JUCE dependency patches**: `Dependencies/patches/juce-8.0.12/0002` and
  `0003` exist on disk; the dependency manifest previously reported an entry
  mismatch (4965 vs 4964, `juce_VST3EnumerationDiagnostic.h`) — register any
  patch so `verify_dependencies.ps1` passes.
- Optional: additional vocal FX chains = one catalog line each in
  `QuickWorkflowCore.h` (`vocalFxVariantCatalog`).
