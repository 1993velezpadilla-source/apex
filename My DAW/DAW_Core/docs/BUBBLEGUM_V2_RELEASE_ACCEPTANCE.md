# APEX Bubblegum V2 — Release Acceptance (Windows)

**Scope:** Bubblegum V2 routing, QuickSend, AutoBus/AutoSend, Send Matrix, PRE/POST
fader switching, cable presentation, sidechain and automation persistence.
**Source of truth:** RoutingGraph audio routing plus Bubblegum V2 visual state.

This is a **release gate**, not a statement that physical hardware QA has passed.
Passing GitHub Actions cannot confirm visible frames, actual ASIO device latency,
the monitoring path or real mouse/touch experience. Do not label V2 "100% verified"
without both the CI and manual evidence below.

## Automated gate — exact commit required

- [ ] Windows Debug build and full suite GREEN on the exact candidate SHA.
- [ ] Windows Release build, full suite, GUI smoke and real project-roundtrip GREEN.
- [ ] Repository checks GREEN on the exact candidate SHA.
- [ ] External MIT VST3 smoke Debug and Release GREEN on the exact candidate SHA.
- [ ] Regression: a PRE cable is looked up **by its destination**, not by the first
      send record (which may be Direct→Master); route identity is stable.
- [ ] Regression: mixed PRE/POST sibling sends preserve individual tap types,
      cable IDs and published audio edges across save/restore.
- [ ] No failing or unreviewed Bubblegum-specific PR is required for the release.

## Manual playback / visual / project gate

Test on **actual APEX Windows Release**, with a disposable project and captured
logs/screen recording. Use a real ASIO interface; record its driver, device,
sample rate, buffer size, APEX EXE hash, and commit SHA. Tests involving feedback
must not be performed with a live microphone routed to speakers.

| ID | Operation | Required observable result |
|---|---|---|
| BG-H01 | Select vocal A as source; open Bubblegum and send to reverb | One visible cable and **one** audible send; no extra route to Master |
| BG-H02 | Change source to vocal B and back; close/reopen Bubblegum | Cables follow the selected source without frozen or wrong-source frames |
| BG-H03 | QuickSend A→Reverb; tap again; tap to re-enable | Route exists through bypass and restores with **same** cable/RouteID |
| BG-H04 | Toggle A→Reverb POST→PRE→POST | Cable never disappears; PRE remains audible when source fader drops; POST follows fader |
| BG-H05 | Create A→Reverb PRE and A→Delay POST simultaneously | MIX is reported; changing one tap doesn't change the other; both audible edges are distinct |
| BG-H06 | Delete the PRE send using QuickSend delete / offscreen popup | Both visual and actual DSP connection disappear, even after play/stop |
| BG-H07 | Turn cable visibility off/on; add/remove sends from offscreen popup | Correct cables appear without stale cached frame or extra user action |
| BG-H08 | Undo/Redo send creation, deletion and PRE/POST mutations | Audio path, presentation and stable state all track history without orphan routes |
| BG-H09 | Save, close, reopen project with PRE+POST sends and distinct levels | Both paths, levels, route identities and active/bypass states survive roundtrip |
| BG-H10 | Automate Send gain and bypass; stop/replay; save/reload | Read/Write playback matches the live send and restored automation binds correct RouteID |
| BG-H11 | AutoBus/AutoSend + Send Matrix routing with Bus/FolderBus and Master | No duplicate Master paths or feedback; folder collapsed/offscreen badges stay correct |
| BG-H12 | Create a plugin sidechain route, toggle/bypass and reopen session | Sidechain signal follows graph state; its visual cable isn't confused with an ordinary send |
| BG-H13 | Repeat with 44,100 and 48,000 Hz at 128-sample ASIO buffer | Stable playback/record for >=20 min each; record glitches, dropouts and actual measured CPU |
| BG-H14 | Resize mixer; HiDPI, touch-sized window and track scroll | Cable anchors, popups, hidden destinations and hit targets remain usable and correctly positioned |

## Release decision

- **GREEN**: all exact-SHA CI gates passed **and** BG-H01–BG-H14 observed
  on actual Windows hardware. Attach evidence (screenshots/video, driver/device,
  recording chain, APEX version, SHA, settings and bug list).
- **YELLOW**: code/CI is GREEN but physical visual or ASIO tests are missing.
  Say **integration candidate**, never "100% verified".
- **RED**: an actual routing fault, crash, automation desync, stale cable frame,
  unresolved CI failure, or duplicate audio path is reproduced. Fix and re-run.

**Recording safety:** monitor through only one intended path. Use low speaker/headphone
levels for the first ASIO test and avoid routing any live mic into feedback loops.
