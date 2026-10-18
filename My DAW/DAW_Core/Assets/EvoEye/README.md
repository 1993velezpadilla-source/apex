# APEX Evo Eye — boot splash resource

**Source of truth:** the original APEX eye artwork provided by the project owner,
not a new AI rendering. Preserve the full 1536 × 864, 16:9 visual and its
APEX wordmark.

## Asset contract

Commit the supplied image with these exact paths and names:

- Source checkout: \`My DAW/DAW_Core/Assets/EvoEye/evo_eye.jpg\`
- Installed alongside EXE: \`EvoEye/evo_eye.jpg\`

The supplied attachment called \`1000244033.png\` is actually JPEG data.
Rename/copy those **same bytes** to \`evo_eye.jpg\`; do not transcode the original
or regenerate it. The runtime overlays the revised wording
**MUSIC PRODUCTION ENGINEER** over the source's old caption so the entire eye,
spiked metal, smoke, reflections and APEX emblem stay unchanged.

The normal Windows installer already copies \`Assets/*\` alongside the EXE;
the startup code resolves that installed path and a source-checkout path for
development builds. A missing image never stops APEX from showing its existing
startup panel; this is a fail-open UI path, not an audio-engine dependency.

## Lifecycle

1. Resolve process role. Never show splash in plugin scanner/worker mode.
2. Create Evo Eye Win32 layered window on its own render thread.
3. Render the actual supplied image at aspect-correct size with blink, energy
   pulse, and replacement caption. No fake scan percentage.
4. Construct the real DAW window and existing StartupPanel.
5. Show the DAW/StartupPanel; fade Evo Eye (~220 ms); destroy splash HWND.
6. Never display the splash again over an open project. On early shutdown,
   stop and join the splash render thread.

## Acceptance criteria

- **Block release/merge until the actual JPEG has been committed.**
- Run Windows x64 Debug **and** Release builds.
- Launch real EXE: eye visibly blinks while DAW is initializing, no white/gray
  flash, no frozen eye if JUCE constructor blocks.
- Confirm exact text: MUSIC PRODUCTION ENGINEER.
- Confirm New Project / Open Project / Recent Projects / Rescan Plugins are
  visible and actionable after the splash (PR #135 startup-panel fix).
- Repeat with empty plugin cache, warm cache, plugin scan running, and
  --apex-ci-startup-panel. Plugin workers must show no splash.
- Confirm all splash handles and GDI+ objects are released after transition.
- Do not claim 100%/GREEN from source review alone.
