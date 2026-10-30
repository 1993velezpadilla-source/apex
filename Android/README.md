# APEX Android — Port / Phase 1 (2026-10-30)

## Scope and source of truth

The production Windows DAW remains in `My DAW/DAW_Core/` and **is not replaced**.
The Android port begins with the independent JUCE 8.0.12 Android Studio exporter in
`Android/APEXAndroid.jucer` and a lightweight C++ audio-device probe in
`Android/Source/Main.cpp`. The Android work is contained in its own Git branch.

**Status (2026-10-30):** the Phase 1 JUCE Android ARM64 USB-audio
probe **compiled GREEN** and produced a GitHub Actions APK artifact; it has **not
been installed or tested on a Pixel Fold / Volt 1**. Separately, the
`apex-android-full-shell-audit.yml` workflow experimentally compiles the
**actual desktop APEX sources** for Android. A full DAW APK is **not yet GREEN**,
and no full DAW startup or recording has been verified on Android.

## Android policy: no plugin sandbox

**Decision:** Android does **not** support or require the Windows plugin
sandbox/worker-process subsystem. The native host runs compatible built-in APEX
effects in-process. A Windows sandbox slot is not silently converted to a
different execution mode on Android: insertion is rejected with an explicit
message, preserving the desktop project state instead of risking DSP changes.

The full-shell Android exporter sets `APEX_ENABLE_PLUGIN_SANDBOX=0` and
`JUCE_PLUGINHOST_VST3=0`. The Android app starts in normal DAW mode and will
not launch Windows worker or VST3 sandbox subprocesses. Some cross-platform
plugin-host declarations still carry lightweight compatibility types so that
the shared source compiles; these **do not implement** an Android sandbox.

Windows remains unchanged and retains its existing VST3 and optional
sandbox functionality. The original desktop `.jucer` is never rewritten.

## Phase 1 hardware smoke test

1. Install the GitHub Actions debug APK on Android (once the build is GREEN).
2. Plug a USB audio class-compliant device such as the UA Volt 1 into USB-C.
   Use powered USB hub if the interface needs more power than the phone provides.
3. Permit microphone access; check Android's USB audio routing/connection.
4. In APEX Android use **Audio devices** and **Refresh audio connection**.
5. Observe the **actual** device name, negotiated sample rate, buffer size,
   active input/output channels and real input meter.
6. Check monitoring **only with wired headphones connected to the interface**.
   Monitoring is disabled by default to avoid feedback.
7. Disconnect/reconnect USB and check for clean recovery (not yet guaranteed).

Important: Android decides USB device ownership and routing. The JUCE
`AudioDeviceManager` backend can only use devices the OS exposes. Showing a
JUCE device selector does not bypass Android limitations or guarantee access
to every USB interface. The Volt 1 is the first physical test, **not** a
vendor-specific allowlist. Compatibility must be qualified per phone, interface,
Android version, USB-C power and sample-rate/latency behavior.

## Android port plan, in tested gates

| Gate | Completion evidence |
| --- | --- |
| A0 — Bootstrap | Build a real ARM64 installable APK with JUCE 8.0.12 exporter. |
| A1 — USB class-compliant audio | Real Volt 1 capture, meter, routing, hotplug, 48 kHz where supported; no crashes. |
| A2 — Take recording | Record PCM into WAV on a disk thread; safe stop/finalization; reopen and play. |
| A3 — Share APEX core | Introduce platform-independent adapters, connect `AudioEngine`, `RecordingEngine`, `ProjectManager` incrementally; preserve Windows implementation. |
| A4 — Phone/touch UI | Track arrangement, transport, mixer, safe clip controls, mobile-first layout. |
| A5 — Native FX and saves | Port native EQ, compressor, limiter and other supported FX; validate persistence, latency, Undo, round-trip projects. |
| A6 — Compatibility release | Device matrix, stress/latency measurements, interruption/route/permission tests, Android release packaging. |

### Strict boundaries

- Audio callback: no locks, no allocations, no disk I/O, no GUI, no plugin lifecycle calls.
- Android built-in USB audio route is tried first. A direct USB driver (with
  USB host permission, ownership, feedback scheduling, and UAC protocol
  implementation) is **a separate future design decision**, not silently
  represented as part of A1.
- Do not copy all Windows `MainComponent` / `Main.cpp` into Android and
  turn off guarded desktop features in bulk.
- Do not attempt to load Windows VST3 DLLs on Android.
- No promise of a single low-latency number: measure physical loopback,
  hardware model, Android routing, buffer and sample rate.
- Preserve existing APEX project files on failed writes; never falsely mark
  recording finalization or project persistence as complete.

## CI build

The workflow `.github/workflows/apex-android-usb-audio.yml` builds JUCE's
host Projucer, exports the Android Studio project, runs Gradle, and publishes
any actual APK files as GitHub Actions artifacts. It **does not** synthesize
an APK or substitute another engine. No runtime tests are run in CI yet.

JUCE Android uses the Projucer Android Studio exporter. JUCE's public CMake
API does not currently support building Android targets directly.

## Existing authoritative APEX components to reuse later

- `My DAW/DAW_Core/Source/AudioEngineCore/AudioEngine.h`
- `My DAW/DAW_Core/Source/RecordingCore/RecordingEngine.h`
- `My DAW/DAW_Core/Source/ProjectCore/ProjectManager.h`
- `My DAW/DAW_Core/Source/DeviceCore/AudioDeviceBlockAdapterCore.h`

These are *identified integration candidates*, not currently linked into
the Android binary.

## Licensing note

JUCE 8 is subject to the JUCE licence (commercial or AGPLv3). Review this
before distributing proprietary APEX Android builds.
