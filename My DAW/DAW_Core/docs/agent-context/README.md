# APEX — Agent Context

Persistent knowledge for AI agents working on APEX. If you are an agent
starting a new session on this repository, read this folder BEFORE touching
code.

## Read order

1. `AGENTS.md` (repository root) — workspace rules, build/test commands,
   invariants.
2. This README — where things live and the rules that were learned the hard
   way.
3. The newest file in this folder — the latest session handoff.

## Session handoffs

| Date | File | Covers |
|---|---|---|
| 2026-09-17 | `2026-09-17-session-handoff.md` | Sidechain bus engine root cause, legacy project repair chain, Upgrade Project tool, Quick Tracks vocal FX families |

Detailed engineering reports live in `docs/superpowers/reports/`.

## Essential commands (from `My DAW/DAW_Core`)

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026 -Name <test>
```

- **APEX must be CLOSED to link** — otherwise `LNK1104` on `DAW_Core.exe`.
- **Run tests SEQUENTIALLY** — parallel runs collide on MSBuild `CL.read.1.tlog`.
- Focused regression set: `automation.polish.v1`, `automation.rt-access.v1`,
  `automation.smoother.block-invariance.v1`, `split.transaction.automation.v1`.
  Feature suites: `APEX.QuickWorkflow`, `APEX.QuickTrackCore`.

## Runtime diagnostics (read these FIRST — do not guess)

| What | Where |
|---|---|
| Sidechain state dump (connections, buses, plugin targets, duplicates) | `%APPDATA%\DAW_Core\sidechain_diagnostic.log` |
| Plugin/UI audit flow | `%APPDATA%\DAW_Core\plugin_ui_flow.log` |
| Startup trace | `%APPDATA%\DAW_Core\startup_trace.log` |
| Recent projects | `%APPDATA%\DAW_Core\recent_projects.txt` |

The user's projects are plain XML (`.dawproj`). Read them directly when a
project-specific defect is reported — the XML shows connection types
(`0=Direct, 1=Send, 2=PreSend, 3=Sidechain, 4=FolderSum`), gains, active
flags and sidechain metadata (`scPluginId`, `scBusIndex`, `scTapPoint`).

## Hard-won rules

1. **Host state can be fully correct while audio is broken.** Instrument both
   ends before concluding anything.
2. **The discriminating variable is the fastest route to a root cause.** For
   the sidechain defect it was track-source (worked) vs bus-source (broken).
3. **Never trust a return value alone for bus negotiation** — the VST3 spec
   allows a plugin to adapt its buses and still return `kResultFalse`.
4. **The engine consumes a PUBLISHED SNAPSHOT** of the routing graph. Any
   direct flag write must be followed by `notifyGraphChanged()` or the audio
   thread never sees it.
5. **Repairs that touch user data must be reversible** and verified against
   real projects before shipping. (A migration once muted a legitimate Send;
   see the 2026-09-17 report.)
6. **Format changes ship with two steps**: an idempotent repair in the
   load-time chain + one `report->add(...)` call (see
   `Source/ProjectCore/ProjectUpgradeReport.h`).
