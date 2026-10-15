# APEX Automation V2 — Windows hardware acceptance gate

**Status: NOT HARDWARE-CERTIFIED.** This is a reproducible sign-off protocol, not a claim that these checks have passed. Production automation is integrated into `main` through PR #124 (commit `f47d2592ccd812423f82a56c10d494361a05c073`). The PR-specific Windows Debug/Release runner passed 810,798 assertions per configuration; an automated runner is not a physical ASIO interface, a human-controlled VST3 GUI, or a long vocal recording.

## Release-candidate identity and safety

- Use a **successful** `Windows build and tests` run on the **exact tested commit**. Prefer the `Apex-Windows-Release-<sha>` artifact, and archive the associated `apex-test-evidence-Release-<sha>` manifest. Do not mix EXEs from different commits, and do not label a running/pending workflow GREEN.
- These CI artifacts are **unsigned EXE/PDB builds**, not a signed installer. Keep the original ZIP and its commit SHA.
- Back up real projects. Perform this protocol in a disposable test project, not on irreplaceable recordings. Use test audio with safe monitoring levels.
- Record APEX build/commit, Windows version, ASIO driver and firmware, interface, VST3 names/versions, sample rate and block size for each run.
- With an Apollo Solo USB3 or other interface, use **one monitoring path** (DAW or hardware Console) when checking live latency; never mix two monitored copies and misdiagnose their comb filtering as Automation V2 defects.

## Matrix: repeat each functional case at 48,000 Hz / 128 and 44,100 Hz / 128

| ID | Physical/UI action (use an actual APEX window) | Required observation |
| --- | --- | --- |
| AV2-H01 | Record track Volume and Pan movement in **Write** with transport running; stop; play from start. | Stable curves are displayed and follow the audible/visible movement; Stop stops writing. |
| AV2-H02 | Start with an existing curve; select **Read** and play; move the playhead repeatedly. | Curve drives the live controls without drawing duplicate points or continuing to write. |
| AV2-H03 | Select **Touch** on an armed parameter; press/drag/release while playing. | Existing automation is overridden only during touch, then returns to the underlying lane; no jumps caused by a stuck touch flag. |
| AV2-H04 | Select **Latch**; touch/release during play; Stop; play again. | Last touched value stays latched until Stop, then latch state resets; no hidden rearm at the next Play. |
| AV2-H05 | Run **Trim** on an existing automation lane, then Undo and Redo. | Audible/visible offset is correct; baseline curve and undo history remain recoverable. |
| AV2-H06 | Select **Off**, then **Read**, for track and plugin automation. | Off does not unexpectedly apply playback; returning to Read uses the prior curve. |
| AV2-H07 | In a real third-party **VST3 editor**, automate an arbitrary continuous parameter in Write and Touch; use two instances, if possible. | Each instance records to the correct parameter/slot; movement plays back without duplicate edits or cross-instance bleed. |
| AV2-H08 | Automate a discrete VST3 toggle; change mode rapidly (Latch→Read, Touch→Read, Stop→Play). | Toggle is discrete; modes respect the last user command and no stale gesture remains held. |
| AV2-H09 | Add/move/delete automation points and entire lane; Undo/Redo; resize/reopen the arrangement. | Canvas repaints each change immediately: no phantom deleted curve and no stale COW snapshot. |
| AV2-H10 | Create native, send, pan/volume and VST3 lanes; save; close **APEX**; reopen the same project. | Every lane and plugin instance ID is restored; same curves and parameter targets play audibly. |
| AV2-H11 | Move/copy plugin instances across slots and tracks after saving a backup; save/reopen. | Existing automation follows stable instance identity; does not bind to a different plugin. |
| AV2-H12 | With normal vocal monitoring active, run a representative session for ≥20 minutes while moving faders and plugin controls. | No crashes, stuck Read/Touch/Latch state, audible pops attributable to automation, missing playback, or progressive UI lag. |

For **each** sample-rate block, also exercise Pause/Seek/Loop and transport restart, and listen for dropouts with actual ASIO hardware. If 128 samples cannot operate stably, document that as a **failure for this gate**, then optionally diagnose at 256; a successful 256 test does not silently replace the 128-sample requirement.

## Separate numerical and compatibility boundaries

The `External open-source VST3 smoke` workflow exercises five SHA256-pinned real plugin binaries (reverb, delay, EQ, compressor, chorus). It verifies actual loading, processing, opaque state round-trip, and simulated VST3 gestures/PPQ control. **It does not click vendor GUIs, measure Apollo round-trip latency, prove sample-accurate VST3 IParameterChanges, or certify hardware stability.** Windows build/tests also exercises a real DAW GUI launch and project save/open smoke, but this is not hands-on physical acceptance.

If a particular VST3 (for example, a commercial Auto-Tune) is unavailable or licensed only on the tester's computer, record the exact version and manual result; do not substitute a mock or claim that CI validated that proprietary plugin. No third-party binaries should be checked into this repository.

## Evidence and go/no-go

Record one result per **AV2-Hxx × sample rate**: PASS / FAIL / NOT RUN, screenshot or short screen recording, project filename, exact reproduction steps and observed vs expected behavior. Attach crash logs where applicable, omitting personal paths and licenses.

**GREEN release gate** = the exact-commit Windows Debug and Release suites PASS, the full pinned real-VST3 CI smoke PASS, every physical test above PASS at both 44.1/48 kHz and 128 samples, and a human has reviewed the evidence. If any item is missing, keep the build labeled **beta / pending physical acceptance**. Do not rewrite the DSP architecture merely to satisfy a documentation gate.

Suggested tracker fields:

```text
Commit SHA:
APEX artifact / SHA256:
Windows / driver / interface:
VST3 names / versions:
Sample rate / ASIO frames:
Case ID:
Result (PASS / FAIL / NOT RUN):
Steps / expected / actual:
Evidence location:
Tester / date:
```
