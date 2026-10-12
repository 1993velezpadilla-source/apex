# APEX Tune Live — intrinsic vocal pitch correction (prototype V1)

## Scope and provenance

APEX already includes **ApexTune** for offline, clip-based editing/rendering.
This work adds a **separate real-time insert effect** named `APEX Tune Live`
to the existing single `APEX Native` plug-in family. It does not replace the
clip editor or alter automation/phasing branches.

This is a **clean-room implementation** based on published DSP principles:
YIN pitch detection (de Cheveigné & Kawahara, JASA 2002), nearest legal note
selection, and the MIT-licensed Signalsmith Stretch source already pinned by
APEX. No proprietary Antares/Auto-Tune code, binaries, or presets are copied.

## Implementation

- Monophonic tracker: 512-sample analysis window at ~12 kHz (roughly 43 ms),
  64-sample analysis hop, bandlimited downsample, YIN cumulative-mean
  normalized difference function, silence and confidence gates. Runs in
  preallocated fixed-size arrays with no `new`, `vector`, `mutex`, or
  file I/O in the pitch detector callback.
- Scale quantization: Chromatic, Major, Natural Minor, root C to B.
- Amount, retune speed (1-150 ms), key, scale, wet mix, and bypass expose
  stable JUCE parameter IDs for host automation.
- Phase-vocoder shifting uses the project's pinned MIT Signalsmith Stretch;
  configured for a 40 ms frame and 10 ms interval. Actual latency is queried
  at prepare time and reported to the host via `setLatencySamples`.
  Dry is delay-aligned so mixed/bypassed audio does not accidentally pre-echo.
- Host integration: one extra intrinsic plug-in in the unified
  `ApexNativePluginFormat`, cached by `PluginScannerCore` at startup and
  rescans, described in Projucer. Save/restore uses a versioned `ValueTree`.
  JUCE's generic parameter editor is intentional for this first DSP gate.
- Bounded processing: oversized host blocks are processed in prepared-sized
  chunks. The generic editor is accessible, but a dedicated final UI has
  **not** been implemented.

## Evidence / current limits

`Tests/TuneLive/test_yin.cpp` is a **portable unit** checking 12 sinusoidal
F0 cases at 44.1/48/96 kHz, silence rejection, and C major/minor mapping.
It has been compiled and run locally using GCC C++17 and
AddressSanitizer/UndefinedBehaviorSanitizer. A separate PR workflow runs the
same regression on Linux/GCC and Windows/MSVC.

**Important: this is NOT yet a finished or real-hardware-certified pitch
correction product.** These are still open gates:

1. Full Windows Debug+Release JUCE builds of the host with the new intrinsic
   plug-in, in-process insertion, save/close/reopen, rescan, and automation
   read/write must be green on the PR head.
2. End-to-end wet-output measurements with real singing samples: cents error,
   pitch jumps/octave errors, consonants, sibilants, breath, vibrato,
   transients, and formant preservation (not currently implemented).
3. Measured end-to-end latency and callback CPU/xrun allocation probes on
   Apollo Solo USB at 48 kHz/128, plus 64/256 and 44.1/96 kHz configurations.
   The 43 ms detection window is **not** the total plugin latency.
4. Confirm shifter's `process()` makes no audio-thread heap allocations on
   the exact pinned revision; profile worst-case callback time.
5. Stereo/mono transitions, bypass clicks and PDC, automation step changes,
   DAW freeze/bounce/offline export, destructive host reopen, undo, multi
   instance, long running sessions, and plugin state recovery.
6. Production UI: labeled musical key/scale names, humanize and note
   stability, formant-safe correction, input/output F0 feedback, and presets.
7. Additional separate export as VST3 (for use outside APEX) is **not** part
   of this intrinsic-plugin PR.

## Developer checks

From the repository root, compile the portable core without JUCE:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -fsanitize=address,undefined \
  "My DAW/DAW_Core/Tests/TuneLive/test_yin.cpp" -o tune-live-test
./tune-live-test
```

The intrinsic plugin itself must be built through APEX's normal JUCE/
Projucer Windows pipeline. Do not merge until Windows Debug and Release
builds, host smoke, actual audio validation, and the user's beta review.

## Algorithm/license references

- de Cheveigné & Kawahara (2002): DOI 10.1121/1.1458024
- Signalsmith Stretch: https://github.com/Signalsmith-Audio/signalsmith-stretch
  (MIT; use pinned APEX submodule, not an unverified newer release)
- JUCE latency contract: `juce::AudioProcessor::setLatencySamples`
