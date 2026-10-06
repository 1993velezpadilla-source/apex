# Copilot Instructions

## Project Guidelines
- For this DAW codebase, preserve the modular nucleus ('nucleos') architecture across the whole codebase, avoid monolithic changes, prioritize workflow speed/performance, and provide a short 3-5 bullet implementation plan before code changes.
- For large feature imports in this DAW codebase, prefer phased delivery with build validation at each phase boundary, including a stub-then-fill pattern for UI-heavy phases before full painting/interaction code.
- For this DAW, Bubblegum must behave as a routed mixer overlay with explicit input zones and preserved normal mixer usability:
  - Header drag always wins
  - Native controls always win
  - Routing only works in a dedicated routing zone
  - Bubblegum needs explicit source assignment
  - Toolbar controls must be clear
  - Spectator/focus behavior should be defined
  - Clear visual state distinctions are necessary
  - Cable visuals and animations must remain coherent during scrolling or moving the mixer window; avoid flipbook-like redraws or time-varying shape changes tied to repaint cadence.
  - Use timer-based polling of viewport and mixer screen coordinates in Bubblegum overlay as the single source of truth for motion; keep paint pure and use motion-quality LOD split in cable rendering.
  - Cables should have anchors that feel like fixed routing points under each track, sag that feels gravity-driven with more natural mid-cable tension, and drips that cluster near the lowest sag points with occasional anchor drips of varied size rather than even patterns. The routing zone should feel integrated into the mixer architecture rather than a separate drawing area. The droplet body should keep a fixed size while send knob changes should only affect drip behavior and liquid motion: slightly shorter tails, more/fewer drips, stronger/weaker animation, and more solid/faint appearance.
  - For Bubblegum sidechain routing visuals, keep the existing Bubblegum routing model and render sidechain cables as chain-like ellipses along the Bezier with active flow plus trigger-driven pulses, reduced-motion fallback, offscreen animation pause, and efficient batching/canvas rendering.
  - Preserve existing cable visibility and offscreen behavior together; do not regress any existing Bubblegum feature while adding new toggles.
  - For Bubblegum selection box visuals, use thinner, elegant, high-class/penthouse-style lines rather than thick ordinary/flashy outlines.
- UI colors should come from the Col:: namespace.
- Overlay components should call `setInterceptsMouseClicks(false, false)`.
- Tracks must support an unlimited practical number of plugins with no hard slot caps, no hidden truncation, and no UI-only insertion limits; performance issues should be solved with scrolling, virtualization, lazy UI creation, or rendering improvements instead of fixed plugin-count ceilings.
- The mixer fader scale must be monochrome: white dB tick labels, white adjacent tick lines, and a white fader fill instead of multicolor.
- Selected track highlights must use a luxury black-glass mixer-strip look, featuring reflective floor, depth-sorted polished obsidian spheres, subtle warm/gold lighting, and restrained premium motion. The sphere effect should not have a hard black background, should exhibit stronger 3D depth, and include more spheres when it improves the look, rather than the previous purple sphere effect. Mixer strips should preserve the original elegant premium black-glass look rather than a plain modern panel with only floating bubbles.
- Keep the piano roll note keyboard on the left, and make any separate playable MIDI keyboard feel usable like a tablet DAW/Logic Pro iPad style keyboard rather than replacing the piano roll lane.
- Left-edge clip resizing must behave exactly like right-edge resizing and must not slip or move the audio content inside the clip.
- Clip property changes must reflect in real time in the DAW UI.
- For the current DAW audio issue, only the pitch knob is broken; stretch is not the active problem anymore. Preserve the current DAW pitch build baseline, targeting only the removal of the remaining small static noise without changing the overall pitch/timbre behavior.
- Normalize should behave like FL Studio's precomputed sample process: scan the clip/sample buffer or selected range for peak, compute one fixed gain multiplier to target dBFS, multiply the working sample buffer, rebuild waveform/cache immediately, and avoid implementing Normalize as a realtime insert/effect or simple clip gain change.
- For DAW input trim UI, provide a button on both timeline tracks and mixer strips that opens a floating panel with a VU meter and gain knob, rather than an inline purple circular trim knob.
- For timeline track UI, do not add a second monitor mode text button; keep the existing monitor control between Solo and Record, use a better monitoring icon, and expose input gain staging as a Trim button/panel instead of an inline knob or duplicate monitor button.
- Automation UI additions should target a smooth 60 Hz/FPS interaction/update cadence and avoid paint-driven animation or unstable redraw behavior.
- For APEX plugin automation, implement the professional host-side model: plugin GUI gestures update last-touched parameter, automation lanes are created/drawn by APEX, recording supports Read/Write/Touch/Latch modes, and plugin parameter playback should target actual exposed plugin parameters per plugin instance/slot rather than plugin wet/dry shortcuts.
- When recovering or validating this DAW after cleanup damage, compare against the last Release build baseline rather than assuming Debug is the correct working reference, even if the Release version is older.
- When fixing DAW performance issues, do not delete or remove existing features/files; make additive/minimal changes to restore 60 Hz behavior.
- Preserve the MixerPanel 60 Hz explicit repaint pacing fix and never regress it: the mixer must keep/recover 60 Hz in floating/non-fullscreen mode after moving the mixer window, without deleting Bubblegum or mixer features.
- In the DAW FX Chain UI, each individual plugin should use a single wet/dry knob, and the header wet/dry control should act as an independent master wet/dry output for the whole chain without changing the individual plugin knob values. Additionally, remove left-side wet/dry text and percentages near the row, make the under-knob wet/dry label clearly readable, and ensure the knob pointer line visually matches the filled ring position.
- Timeline interaction fixes must be validated functionally: horizontal edge zoom must support both zoom-in and zoom-out, and vertical zoom controls must be actually usable. Additionally, validate timeline scrollbar behavior to ensure edge dragging resizes/zooms the visible range rather than moving the whole scrollbar.
- Fixes need to be validated against the user's exact active UI/repro path because parallel implementations can make a patch appear to do nothing.
- For this DAW, blade/split must behave like professional DAWs: it should only split the audio clip and keep waveform handling separate; do not use engine-to-arrangement mirroring to mutate or auto-repair waveform/source bounds during split.

## Build Configuration
- After every set of code changes, always build using the project's current configuration — whatever Configuration (Debug/Release) and Platform (x64/x86) is already set in the project. Never hardcode Debug or Release or x64. Detect the active configuration from the .sln or user context and match it exactly.

## Audio Export
- The output.clear() on !masterWasProcessed_ in AudioEngine::renderOfflineBlock() was the root cause of 32-sample / 544-sample silent gaps in exported WAV files. 
- Fix this by:
  - Calling routingGraph_->publishSnapshotOnly() in ApplicationCore::beginOfflineRender() to ensure the snapshot is always fresh before the render loop.
  - Replacing the defensive output.clear() with a jassert(masterWasProcessed_) so future snapshot-ordering regressions trip the debugger instead of silently corrupting exports.
- The pattern "exact zeros at pos % 32 == 31 boundaries, 10% of file" is the fingerprint of a wholesale buffer clear, not a missing-contribution bug.

## Scanner Implementation
- Prioritize finishing the new scanner implementation without stalling on secondary UI polish during scanner rebuilds.
- Focus on fixing compile/runtime blockers, removing old scanner paths, building, running a clean full rescan, and reporting factual results.
- For `PluginScanStartupDialog.h`, only make minimal safe compatibility changes needed for the new scanner.
- For scanner debugging in this DAW, do not narrow analysis to Waves unless proven; prioritize proving the exact reduction point in the full plugin scan pipeline with hard runtime logs and real counts.

## Search Guidelines
- Never run recursive PowerShell search commands (e.g., `Get-ChildItem -Recurse | Select-String`, `Select-String -Path ..\**\*.h`) in this DAW workspace — they hang and force the user to restart. 
- Use workspace tools (`code_search`, `file_search`, `find_symbol`, `get_symbols_by_name`, `get_file`) for searching instead.

## General Preferences
- Always consolidate ALL responses into one single large box/block — never split information into multiple separate sections, blocks, or messages. Everything in one huge box for fast copy-paste. If needed, provide output as a .txt file instead of splitting into sections.
- If fixes fail, provide a clear diagnostic prompt describing the observed regressions, what was changed, and what needs verification before more code edits.