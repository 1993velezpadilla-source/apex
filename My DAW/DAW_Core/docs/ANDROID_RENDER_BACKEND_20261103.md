# Android render backend investigation — 2026-11-03

User outcome: the shared `31575849` APK remains extremely laggy and unusable on
Pixel Fold. The earlier APK/build results do not establish a performance fix.
Keep PR #170 draft. Physical-device lag resolution remains unverified.

## Verified rendering path

The pinned JUCE 8.0.12 Android peer advertises only `Software Renderer`.
`AndroidComponentPeer::handlePaintCallback` obtains a Java integer array,
creates a `LowLevelGraphicsSoftwareRenderer`, paints the Component tree, releases
the array and calls Android Canvas `drawBitmap`. The Activity's hardware
acceleration flag does not turn that C++ rasterization into GPU drawing.

The canonical APEX project does not include `juce_opengl`, and the shared phone
APK did not attach an OpenGL graphics context. Therefore the previous waveform
and repaint changes left the same CPU raster/copy presentation path in place.
This is a verified architectural fact, not a measured attribution of every
device stall to software rendering.

Primary source:
- JUCE 8.0.12 `modules/juce_gui_basics/native/juce_Windowing_android.cpp`,
  `AndroidComponentPeer::handlePaintCallback` and `getAvailableRenderingEngines`.
- JUCE 8.0.12 `modules/juce_opengl/opengl/juce_OpenGLContext.h`,
  `setComponentPaintingEnabled`, `attachTo`, `detach`, and `setContinuousRepainting`.

## Candidate change

The generated portrait-preview exporter adds `juce_opengl` and
`APEX_ANDROID_GPU_UI=1`. The Windows source project and fulltest renderer are
unchanged. The main Android window owns an `AndroidGpuPresenter` that paints
the existing complete Component tree through JUCE's OpenGL graphics context.
It is demand-driven (no continuous render loop), uses VSync and a bounded 32 MiB
image cache, and detaches before its Component tree is destroyed.

Context callbacks only clear the GL target, record lifecycle state and count
render callbacks. They do not edit clips, touch plugins or process audio. JUCE
owns the rendering thread and Component-painting synchronization. Startup
without a usable context/frame falls back to software and logs the actual
backend. A fallback is rejected by the GPU runtime test rather than counted
as GPU success.

## Runtime comparison

`apex-android-ui-runtime.yml` builds the real original APEX app for the x86_64
Android emulator. The same optimized APK can launch with
`--es apex_renderer software` or `--es apex_renderer gpu` for a controlled
comparison. `--es apex_scene empty|clip` enables the benchmark only on explicit
adb launches. Normal launches simulate no input and run no benchmark polling.

The native setup refuses to clear/load a user project. In CI the host script
requires `ro.kernel.qemu=1` before resetting the separate preview application's
data. The clip scene writes a synthetic WAV before timing and imports it through
the existing production command. Both scenes drive real Android MotionEvents
through the Activity/peer, then the existing arranger gesture timer and actual
Viewport/zoom setters. There is no substitute renderer or fake timeline model.

Each scene/backend runs twice in opposite backend order. Evidence includes
Android screen captures, logcat, native scene/backend/viewport/zoom snapshots,
delivered event counts and UI Choreographer interval percentiles. Checks reject
unchanged viewport/zoom, a missing clip, an inactive/fallback GPU and a static
or trivial screen capture. Captures during a pinch are retained for inspection.

**Timing limitation:** Choreographer intervals describe UI-thread callback
cadence. OpenGL render callback counts are not OS-presented FPS. An x86_64 CI
emulator using SwiftShader is not the physical Pixel Fold GPU. These tests
establish production gesture and surface behavior and produce comparative
diagnostics; they do not certify physical-device smoothness or a 60 FPS claim.

## Validation status at implementation

- 20 local Python tests passed: generated exporter/backend/architecture scope,
  canonical project preservation, activity/manifest transforms, and runtime
  evidence rejection conditions.
- Workflow YAML parses and diff whitespace checks pass.
- Native ARM64 debug/release, x86_64 compilation, actual runtime captures and
  Pixel Fold testing are pending. Do not distribute this candidate as a
  verified solution before reviewing the actual runtime results.
