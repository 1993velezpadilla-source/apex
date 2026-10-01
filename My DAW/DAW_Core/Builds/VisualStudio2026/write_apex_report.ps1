$ErrorActionPreference = 'Stop'
$workspaceRoot = (Resolve-Path ..\..).Path
$sourceRoot = Join-Path $workspaceRoot 'Source'
$desktop = [Environment]::GetFolderPath('Desktop')
$outFile = Join-Path $desktop 'APEX_COMPLETE_STATUS_REPORT.txt'

$allFiles = Get-ChildItem $sourceRoot -Recurse -File | Sort-Object FullName
$coreFiles = $allFiles | Where-Object { $_.Name -like '*Core.h' -or $_.Name -like '*Core.cpp' }
$sourceRel = $allFiles | ForEach-Object { $_.FullName.Substring($sourceRoot.Length + 1).Replace('\','/') }
$coreRel = $coreFiles | ForEach-Object { $_.FullName.Substring($sourceRoot.Length + 1).Replace('\','/') }

$sb = New-Object System.Text.StringBuilder
$append = {
    param([string]$text)
    [void]$sb.AppendLine($text)
}

& $append 'APEX_COMPLETE_STATUS_REPORT'
& $append ('Generated: ' + (Get-Date).ToString('s'))
& $append ('Workspace root: ' + $workspaceRoot)
& $append ('Source root: ' + $sourceRoot)
& $append ''
& $append 'SECTION 1 — PROJECT IDENTITY & ARCHITECTURE OVERVIEW'
& $append '1.1 — What is APEX?'
& $append 'APEX is a Windows desktop DAW implemented in C++ with JUCE and built from a Visual Studio solution/project layout. The codebase targets professional-style audio creation workflows spanning vocal recording, mixing, mastering, and an emerging beat-making/MIDI workflow. It is not organized like a monolithic JUCE template app; it is decomposed into many narrowly scoped subsystem nuclei with explicit runtime wiring through ApplicationCore and related managers.'
& $append ''
& $append '1.2 — Nucleo Architecture'
& $append 'A "nucleo" in this codebase is a focused subsystem nucleus. In practice this usually appears as a class or file cluster ending in *Core. The architecture emphasizes modular feature ownership, explicit orchestration, and UI/logic separation. Repo instruction files explicitly require preserving this modular nucleus architecture and avoiding monolithic changes.'
& $append 'Observed conventions and rules:'
& $append '- Logic/state/engine modules commonly live in feature folders and use the *Core suffix.'
& $append '- UI code is concentrated under Source/UICore and UI-adjacent folders; application logic is kept out of UI where possible.'
& $append '- Runtime discovery/connection is largely explicit in ApplicationCore through setter injection, subsystem initialization order, listeners, callbacks, and snapshot publishers.'
& $append '- Cross-thread audio/message-thread state commonly uses atomics or immutable snapshot publication instead of ad hoc shared mutable state.'
& $append '- Bubblegum and other overlay systems are tied to routing/project state rather than being purely decorative.'
& $append ''
& $append 'Full flat list of *Core files currently under Source:'
foreach ($f in $coreRel) { & $append ('- ' + $f) }
& $append ''
& $append '1.3 — Source Tree Map'
& $append 'Complete Source/ file tree:'
foreach ($f in $sourceRel) { & $append ('- ' + $f) }
& $append ''
& $append '1.4 — ProjectManager (Single Source of Truth)'
& $append 'From inspected ProjectManager.h/.cpp, ProjectManager owns project serialization/orchestration rather than all runtime state. It serializes Tracks, Clips, Markers, RoutingGraph, FaderRange, ApplicationState, FolderBus state, transport state, plugin-chain state via ApplicationCore, click state, and automation state into a DAWProject ValueTree/XML file. It is not the owner of live audio processing, plugin windows, or rendering systems. It is not presented in inspected code as a global thread-safe transactional store; autosave/concurrency concerns are separated into AutosaveManagerCore and background I/O patterns.'
& $append ''
& $append 'SECTION 2 — AUDIO ENGINE & ROUTING'
& $append '2.1 — AudioEngine'
& $append 'Inspected AudioEngine.h shows AudioEngine as the real-time/offline render core. It binds TrackManager, ClipManager, TransportController, RoutingGraph, AudioFileManager, plugin chains, MIDI playback/input, virtual keyboard, automation manager, track peak metering, clip-region plugins, folder-bus snapshot model, and vocal tune integration. It provides prepare(), releaseResources(), process(), resetOfflineRenderBuffers(), resetOfflineDebugLogging(), renderOfflineBlock(), and subsystem setters. Offline rendering uses a routing snapshot and asserts masterWasProcessed_ after processWithSnapshot(); a code comment documents that publishSnapshotOnly() must occur before renderOfflineBlock() and that the previous silent clear behavior was removed to avoid corrupted exports.'
& $append 'AudioEngine also contains explicit bug-fix comments for sample-rate reprepare, stale per-clip DSP-state cleanup on releaseResources(), and automation clock publication before block processing.'
& $append ''
& $append '2.2 — RoutingGraphCore & RoutingGraphProcessorCore'
& $append 'The workspace contains RoutingGraph.h plus snapshot/publisher types rather than a file literally named RoutingGraphCore.h in the inspected set. RoutingGraph manages nodes and directed RoutingConnection edges. It enforces no self-loop, blocks audio-path cycles for non-sidechain edges, maintains a permanent master node, auto-routes new tracks/buses to master, and exposes a producer-first topological processing order for the audio engine. RoutingSnapshot is immutable plain data for audio-thread consumption. RoutingConnection carries id, source, destination, type, gain, active, bypassed, and sidechain metadata (destination plugin id, bus index, tap point).'
& $append ''
& $append '2.3 — Bus Layout & Plugin Hosting'
& $append 'Inspected PluginInstanceCore.h confirms a dedicated plugin-host wrapper with lifecycle handling and a managed plugin editor window. ApplicationCore contains syncPluginChainSidechainBusConfig(), which scans active sidechain routing connections and matches them to destination track chains/slots, confirming that sidechain bus configuration is a live concern in the runtime graph. The precise sidechain bus bug and complete landed fix status would require additional implementation files not line-inspected in this pass.'
& $append ''
& $append '2.4 — VolumeRampCore'
& $append 'Inspected VolumeRampCore.h shows a one-pole IIR smoother with tau = 5 ms, atomic UI-thread targets for volume/pan, and audio-thread generation of per-sample equal-power left/right ramps. It exists specifically to eliminate zipper noise for volume and pan changes and handles live and larger offline blocks.'
& $append ''
& $append '2.5 — SignalSmith Stretch Integration'
& $append 'Repo instructions identify SignalSmith Stretch under Source/ThirdParty/signalsmith-stretch as the approved external DSP library and the replacement for the prior custom pitch engine because of zipper/static issues. AudioEngine includes time/pitch-related cores from the ArrangementEditor area, including PitchSmootherCore and ClipIndependentPitchCore. A full per-call integration trace was not completed here, but the codebase clearly contains the replacement path and associated smoother infrastructure.'
& $append ''
& $append 'SECTION 3 — PLUGIN AUTOMATION SYSTEM'
& $append 'Inspected files: PluginAutomationGestureCore.h, PluginAutomationRecorderCore.h, LastTouchedPluginParameterCore.h, plus ApplicationCore wiring.'
& $append 'PluginAutomationGestureCore tracks explicit and implicit gestures keyed by track/plugin/slot/parameter, with implicitTimeoutMs = 200. updateValue() opens implicit gestures when necessary, beginExplicit()/endExplicit() transition state, and expireImplicitGestures() yields finished gestures for final-point writes.'
& $append 'LastTouchedPluginParameterCore stores the most recently touched plugin parameter on the message thread.'
& $append 'PluginAutomationRecorderCore runs on a 60 Hz timer, gates writes via shouldRecord() against transport-playing state and global automation mode, writes/updates lanes in AutomationManagerCore, mirrors data into apex::automation lane storage, and publishes snapshots periodically. ApplicationCore wires transport position/playing lambdas and reveals automation UI state when lanes are written.'
& $append 'The AUTO-REC bug described by the user prompt remains plausible from inspected code because there is a playing-state gate but no clearly inspected separate arm gate in this subset.'
& $append ''
& $append 'SECTION 4 — AUTOSAVE & CRASH RECOVERY'
& $append 'AutosaveManagerCore owns autosaveDirty, userDirty, recording/export/plugin-scan safety gates, a single-job background I/O pool, last autosave info, and a file lock. RecoverySessionCore is the sole owner of session.lock creation/update/clean-shutdown marking. CrashRecoveryCore is a pure reader that checks session.lock.previous, finds recovery info, and can discard recovery by moving files into a .discarded location. ApplicationCore initializes recovery/autosave first and exposes a deferred recovery prompt path.'
& $append ''
& $append 'SECTION 5 — BUBBLEGUM CABLE SYSTEM (VISUAL ROUTING)'
& $append 'BubblegumV2System aggregates source sync, panel, target list, send state/levels, input, visual, transitions, send feedback, track pulse, visual link, cable system, offscreen detection, sidechain state, sidechain target list, and sidechain cable rendering. It binds directly to RoutingGraph and TrackManager. It also bridges send automation through apex::automation parameters back into live RoutingConnection gain/active state followed by publishSnapshotOnly(), proving Bubblegum is graph-backed mixer routing UI rather than detached visualization.'
& $append ''
& $append 'SECTION 6 — VOCAL RECORDING WORKFLOW'
& $append 'ApplicationCore wires RecordingEngine, LiveInputMonitorEngine, AudioFileManager, transport, track/clip managers, and ApexTuneIntegrationCore. The vocal tune adapter can load mono clip audio from source files, schedule background jobs, post to message thread, spawn an editor window, mark project dirty, and query transport sample position. Full confirmation of punch, comping, device/backend details, and exact disk-write behavior would require additional RecordingCore inspection.'
& $append ''
& $append 'SECTION 7 — MIXING WORKFLOW'
& $append 'The source tree contains MixerCore, ChannelStripCore, MasterStripCore, MeteringCore, SendCore, SoloCore, MuteCore, SidechainCore, and related UI. Inspected TrackSelectionVisualCore is a dedicated selected-strip renderer with cached chrome, multiple gradient/glow layers, and lava-lamp visuals, making it a credible rendering hot spot consistent with the project notes about FPS investigation.'
& $append ''
& $append 'SECTION 8 — MASTERING WORKFLOW'
& $append 'ApplicationCore owns MasterBusEngine and OutputRoutingEngine. ProjectManager persists a MasterPhaseD ValueTree including master gain, ceiling mode, ceiling dB, dither mode/bits, and meter display. AudioEngine offline rendering uses routing snapshots and asserts masterWasProcessed_ instead of silently clearing output, aligning with the documented fix for prior export gaps.'
& $append ''
& $append 'SECTION 9 — BEAT MAKING WORKFLOW'
& $append 'The codebase contains MidiCore, piano-roll playback support, virtual keyboard support, AutomationSequence, and other beat/MIDI-adjacent modules, but this report generation pass did not line-inspect the full beat-making stack. Based on visible module inventory and repo instructions, beat making is present but not yet as complete as the vocal/mix/master path.'
& $append ''
& $append 'SECTION 10 — UI ARCHITECTURE'
& $append 'UI/logic separation is visible in folder layout and instructions. Plugin editor lifecycle code uses juce::Component::SafePointer for async safety. ApplicationCore performs explicit dependency injection/wiring. Repo instructions impose color, overlay, and interaction constraints across the DAW UI, especially Bubblegum and mixer behavior.'
& $append ''
& $append 'SECTION 11 — STABILITY & CRASH HISTORY'
& $append 'Directly inspected stability signals include: SafePointer-guarded plugin window callbacks; shutdown ordering that closes editors before releasing chains; atomic routing edge flags for message/audio-thread interaction; AudioEngine comments and code for sample-rate reprepare and stale-state clearing; and offline export assert behavior replacing silent corruption.'
& $append ''
& $append 'SECTION 12 — REMOVED / DEPRECATED SYSTEMS'
& $append 'Instruction files mention Demon Mode removal, legacy Bubblegum paintEndpointBall presence, and replacement of the prior pitch engine by SignalSmith Stretch. Full residual-code validation was not completed in this pass.'
& $append ''
& $append 'SECTION 13 — CODING STANDARDS & CONSTRAINTS'
& $append 'Observed/instruction-defined standards include *Core naming, atomic cross-thread flags, jassert for export regressions, modular nucleo architecture, and SignalSmith Stretch as the approved external DSP library in this area.'
& $append ''
& $append 'SECTION 14 — UPCOMING WORK & ROADMAP'
& $append 'Likely active/queued work visible from prompt plus repository guidance: automation write-gating cleanup, remaining FPS diagnostics in selection/Bubblegum visuals, sidechain/bus-layout edge-case hardening, scanner work, and longer-term MIDI/beat-making expansion.'
& $append ''
& $append 'SECTION 15 — RAW CODE DUMPS'
& $append 'The following section contains the full raw source for every file under Source/ as present in the workspace at generation time.'
& $append ''

[System.IO.File]::WriteAllText($outFile, $sb.ToString(), [System.Text.UTF8Encoding]::new($false))

foreach ($file in $allFiles) {
    $rel = $file.FullName.Substring($sourceRoot.Length + 1).Replace('\','/')
    Add-Content -LiteralPath $outFile -Value ('=== FILE: Source/' + $rel + ' ===') -Encoding UTF8
    Get-Content -LiteralPath $file.FullName | Add-Content -LiteralPath $outFile -Encoding UTF8
    Add-Content -LiteralPath $outFile -Value '' -Encoding UTF8
}

Add-Content -LiteralPath $outFile -Value 'SECTION 16 — FINAL HONEST ASSESSMENT' -Encoding UTF8
Add-Content -LiteralPath $outFile -Value '16.1 — What works well right now, end-to-end, without obvious architectural contradiction from inspected code?' -Encoding UTF8
Add-Content -LiteralPath $outFile -Value 'Core subsystem wiring, routing snapshot publication, volume/pan smoothing, autosave/recovery scaffolding, Bubblegum-to-routing integration, and export corruption guardrails are all concrete and nontrivial.' -Encoding UTF8
Add-Content -LiteralPath $outFile -Value '16.2 — What is partially working but fragile?' -Encoding UTF8
Add-Content -LiteralPath $outFile -Value 'Automation-write semantics, sidechain layout edge cases, dense visual performance paths, and any uninspected recording/beat-making details should be treated as partially verified rather than fully proven by this pass.' -Encoding UTF8
Add-Content -LiteralPath $outFile -Value '16.3 — What is stubbed or uncertain from this pass?' -Encoding UTF8
Add-Content -LiteralPath $outFile -Value 'Anything not line-inspected in implementation files beyond the dumped raw code should be considered uncertain at narrative level even though the source is present below.' -Encoding UTF8
Add-Content -LiteralPath $outFile -Value '16.4 — Single most dangerous technical debt visible from architecture?' -Encoding UTF8
Add-Content -LiteralPath $outFile -Value 'High subsystem count and feature density with strong runtime coupling through ApplicationCore creates scale risk: each new feature can touch routing, automation, UI, metering, persistence, and export paths unless snapshot/ownership boundaries remain disciplined.' -Encoding UTF8
Add-Content -LiteralPath $outFile -Value '16.5 — New developer first priority?' -Encoding UTF8
Add-Content -LiteralPath $outFile -Value 'Understand ApplicationCore wiring order, RoutingGraph/RoutingSnapshot flow, ProjectManager persistence, plugin chain ownership, and the project instruction files governing Bubblegum, mixer visuals, automation, export safety, and performance constraints.' -Encoding UTF8

Write-Output $outFile
Write-Output ((Get-Item $outFile).Length)
