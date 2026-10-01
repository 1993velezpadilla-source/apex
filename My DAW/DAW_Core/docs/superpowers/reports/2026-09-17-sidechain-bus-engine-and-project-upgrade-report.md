# Sidechain Bus Engine + Legacy Project Repair + Upgrade Project Tool — Final Report

Date: 2026-09-17

Workspace: `C:\Users\1993v\OneDrive\Desktop\Apex backup`

Application repository: `My DAW/DAW_Core`

Branch: `feature/apex-windows-baseline-evidence`

## Verdict

**Fixed and user-verified.**

The user-reported defect — a sidechain in an old project ("EXT does not
recognise the sidechain") that never ducked, while newly created sidechains in
new projects worked — was traced to a missing half of the sidechain contract in
the audio engine: `processBusNode` handled neither sidechain sends nor
sidechain receives. A sidechain sourced from a BUS node (the user's "Vocal
Bus") never filled the destination's sidechain buffer, so the destination
plugin's detector received silence. After the fix the user confirmed the
sidechain works in the real project.

The session also delivered the legacy-repair chain (v8 -> v9 migrations,
unconditional and idempotent) and the **Upgrade Project** tool with the
`ProjectUpgradeReport` contract that lets every future format change reach old
projects.

## User-visible defect

- Old project (`Secreto V2.apex.dawproj`): a sidechain edge from `RN_21`
  ("Vocal Bus") into the compressor (`Pro-C 2`) on the reverb track
  (`TRK_25`). The cable was visible, EXT was enabled in the plugin, the
  project played — no ducking ever occurred.
- New projects: the same workflow with a TRACK as the source worked.
- The discriminating variable between working and broken was the SOURCE NODE
  TYPE (track vs bus) — this became the decisive clue.

## Root cause

`Source/AudioEngineCore/AudioEngine.h`:

- `processTrackNodeExt` contained the full sidechain contract:
  - a receive branch (checks published snapshot edges for an active
    `ConnectionType::Sidechain` into the node, then calls
    `chain.processBlockWithSidechain(trackBuf, *sc, numSamples)`), and
  - a send block ("── Sidechain sends ──") that fills
    `findSidechainBuffer(edge.destNodeId)` from the source node's signal.
- `processBusNode` had NEITHER:
  - its routing loop skipped sidechain edges (`if (edge.type ==
    ConnectionType::Sidechain) continue;`) but never filled the destination
    sidechain buffer, and
  - it called plain `processBlock(nodeBuf, numSamples)` on its plugin chain,
    so a bus-hosted compressor never received its sidechain either.

Consequence: for a bus-sourced sidechain the destination received an active
route and an enabled auxiliary bus, yet its detector saw only silence — the
"EXT does not recognise the sidechain" symptom.

## Evidence chain (what proved the host was correct all along)

### 1. Diagnostic dump (`%APPDATA%\DAW_Core\sidechain_diagnostic.log`)

The host state after load was fully correct:

```
conn RN_21 -> RN_27 active=1 bypassed=0 busIndex=1
   destTrack=TRK_25
   chain slots=2
     slot 0 'UADx Pure Plate Reverb' auxActive=0 inBuses: [0 main ch=2 en=1]
     slot 1 'Pro-C 2' auxActive=1 inBuses: [0 main ch=2 en=1] [1 aux ch=2 en=1]
```

Connection active, bus index resolved, auxiliary bus negotiated and enabled
with 2 channels, chain-side commit (`auxActive=1`). The host had done
everything correctly.

### 2. Project file analysis

The v9-stamped project file contained two connections between the same pair:

```xml
<Connection id="RC_50" source="RN_21" dest="RN_27" type="1" gain="1.0" active="1" bypassed="0"/>
<Connection id="RC_53" source="RN_21" dest="RN_27" type="3" gain="1.0" active="1"
            bypassed="0" scPluginId="" scBusIndex="1" scTapPoint="0"/>
```

- `type="1"` = `ConnectionType::Send` — a legitimate reverb send (vocal sent
  to the reverb track). Not a defect; an earlier revision of the migration
  muted it and was reverted (see Corrected mistakes).
- `type="3"` = `ConnectionType::Sidechain` — the sidechain edge, saved by the
  legacy build with an EMPTY plugin target (`scPluginId=""`).

### 3. Engine architecture

`AudioEngine.h` checks sidechain input against the published routing snapshot
("snapshot edges (no RoutingGraph access)") and only adopts a new generation
on a graph change notification. Reactivating a connection by writing the flag
directly bypassed the graph API and never notified — the engine kept seeing
the route as inactive.

## Fix chain (commits, in order)

1. `3e72951` — v9 migration persists resolved sidechain bus indices.
2. `fb41ef1` — re-sync the sidechain bus config once after a migrated project
   finishes loading (plugins may still be instantiating at load time).
3. `a7ac9df` — reactivate sidechain routes that old builds silently
   deactivated (`rollbackUncommittedSidechainRoutes` saved them inactive).
4. `76382b6` — run the legacy repair unconditionally: a v9 stamp can still
   carry broken data (a project re-saved by an intermediate build), so the
   idempotent repair runs on every load.
5. `b79930a` — diagnostic: dump the full sidechain state (connections, flags,
   bus index, chains, real input buses) to a readable log file.
6. `0149205` — retry a rejected bus layout once: the VST3 contract allows a
   plugin to adapt its buses and still return `kResultFalse`; JUCE's host-side
   wrapper treats any non-`kResultTrue` as a hard failure, so an identical
   re-request (now matching the plugin's adapted arrangement) is accepted.
7. `7516a7e` — notify the graph after reactivating migrated routes (the
   engine consumes a published snapshot).
8. `1e8cc78` — repair the two legacy sidechain defects found in the real
   project file: fill an empty `scPluginId` from the first slot exposing a
   non-main input bus (and, at the time, mute duplicate routes — reverted).
9. `ccc1f26` — REVERT the duplicate-send muting: a Send sharing a sidechain
   node pair is the standard reverb-send + duck setup and is legitimate user
   data.
10. `a6c6f94` — **THE ROOT-CAUSE FIX**: bus nodes now send and receive
    sidechain. Both halves mirror the track-node path, including the
    sidechain PDC line. This is why new projects worked (track source) while
    the old project did not (bus source).

## Upgrade Project tool

Commit `e0a6e6e`:

- **Entry points**: an "Upgrade Project" button on the startup panel and an
  "Upgrade Project..." item in the Settings menu.
- **Flow**: system file picker (`*.dawproj`) -> timestamped backup of the
  original next to the file -> open the project (the full repair chain runs
  against the live plugins) -> crash-safe transactional save -> report dialog.
- **Report**: `Source/ProjectCore/ProjectUpgradeReport.h` collects the source
  and target format versions, per-step repair counts and warnings. The
  `ProjectManager` resets it on every load; every migration step records into
  it.

### Future-proof contract

When any future update changes anything about the project format (a new
field, a reworked model, a new button's persisted state), two steps make old
projects heal both on load and through the Upgrade tool:

1. Add an idempotent repair step to the load-time chain (safe to run on every
   load — it heals projects saved by any build, including intermediate broken
   ones).
2. One line: `report->add("What was repaired", count, details)`.

The contract is documented in `ProjectUpgradeReport.h` itself.

## Corrected mistakes (kept for the record)

- The first revision of the duplicate-route repair MUTED an active Send
  sharing a sidechain node pair. The user's project proved it was the
  legitimate reverb send: muting it cut the reverb track's audio. Reverted in
  `ccc1f26`; the migration no longer touches any Send/Direct route.
- Several earlier host-side fixes (`0149205`, `7516a7e`, `1e8cc78`) were
  correct but not sufficient on their own: the audio never left the bus node.
  They remain in place because each closes a real defect found by the
  investigation.

## Validation

- Debug rebuild: PASS (`Scripts/build_apex.ps1 -Configuration Debug -Rebuild -Unsigned`).
- Release rebuild: PASS.
- Focused suites (Debug, seed 0xA9E12026), all PASS across every commit:
  `automation.polish.v1`, `automation.rt-access.v1`,
  `automation.smoother.block-invariance.v1`, `split.transaction.automation.v1`.
- User-verified in the real project: sidechain ducks correctly after
  `a6c6f94`.

## Lessons learned

- The host-side state can be fully correct while the audio path is broken:
  always instrument BOTH ends (the diagnostic log proved the host; the code
  reading found the engine gap).
- The discriminating variable between a working and a broken case is the
  fastest route to the root cause: here, track source vs bus source.
- Read the real project file and the real logs before proposing fixes. The
  project XML (`type` values, `scPluginId=""`) and
  `sidechain_diagnostic.log` were the evidence that ended a long guessing
  cycle.
- A repair that touches user data (muting routes) must be reversible and
  verified against real projects before shipping.

## Files touched (this session)

- `Source/AudioEngineCore/AudioEngine.h` — bus sidechain send + receive (the
  root-cause fix).
- `Source/PluginHostCore/PluginInstanceCore.h` — spec-correct bus layout
  retry.
- `Source/AppCore/ApplicationCore.cpp/.h` — legacy sidechain migration
  (reactivation, bus resolution, plugin target fill), graph notification,
  diagnostic dump, report recording.
- `Source/ProjectCore/ProjectManager.cpp/.h` — unconditional idempotent
  repair, report reset + recording.
- `Source/ProjectCore/ProjectUpgradeReport.h` — NEW: the report + future
  contract.
- `Source/UICore/StartupPanel.h` — Upgrade Project button.
- `Source/MainComponent.cpp/.h` — upgrade flow, file pickers, Settings menu
  entry, post-load completion hook.
