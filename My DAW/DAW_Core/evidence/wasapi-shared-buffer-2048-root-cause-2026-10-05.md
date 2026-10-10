# WASAPI shared-mode 2048: reproduced output starvation and correction

**Status:** reproduced on Speakers (Realtek(R) Audio) and corrected in controlled output tests. This does not certify the user's song, all plugins, or microphone recording. No user project was opened or modified by these diagnostics.

## Cause and ownership

Windows Audio uses JUCE standard shared WASAPI. Previously, `WASAPIDeviceBase::initialiseStandardClient()` used shared event buffering independently of the requested application block. At 48 kHz the tested Realtek endpoint had a 480-frame engine period and a **1056-frame (22 ms) output queue**, even for a **2048-frame (42.67 ms) application block**.

`WASAPIOutputDevice::copyBuffers()` streamed that block in fragments. The next application callback could consume more time than the entire output queue covered. A 29.867 ms processing workload meets the 2048-frame deadline but exceeds the old queue. This reproduced repeated starvation at 2048 while smaller tested blocks worked.

The previous report's claim that PnP/CIM restrictions prevented all physical endpoint testing was too broad. Native WASAPI COM successfully opened and exercised Realtek output. The evidence below supersedes that limitation; microphone opening still failed in the current test environment.

## Correction

This pass changes the pinned JUCE WASAPI backend and its reproducible dependency patch. It does not change APEX DSP, plugins, routing, recording, monitoring ownership or project persistence.

- Standard shared callbacks longer than the engine period now request a queue covering one callback plus one engine period. The existing block adapter services it using current padding and an interruptible 1 ms wait. Smaller callbacks retain event buffering. See [Microsoft's Initialize contract](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize) for the shared event/timer buffering distinction.
- Input servicing follows the polling mode of its corresponding stream, retaining the existing bounded reservoir.
- Standard shared client blocks now include **32, 64, 128, 256, 512, 1024, 2048**. These are application block sizes, not claims of physical 32-frame Windows periods. Exclusive and low-latency negotiation keep their existing policies.
- The earlier 44.1 kHz enumeration correction is retained: shared AUTOCONVERTPCM clients preserve the requested rate when Windows reports a closest mix format. Actual 44.1 kHz opens passed; this does not change the physical endpoint's 48 kHz mix clock.

Source: `../Sdk setups/juce-8.0.12-windows/JUCE/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp`.
Changed symbols: `openClient`, `waitForNextBuffer`, `getStreamFlags`, `initialiseStandardClient`, `WASAPIOutputDevice::copyBuffers`, `WASAPIAudioIODevice::initialise` and the input-only branch of `run`. No extra wait or diagnostic work is added inside the APEX DSP callback.

## Native physical output reproduction

`Scripts/diagnostics/WasapiBufferProbe.cpp` writes silence through native WASAPI and simulates 70% processing duty with JUCE's existing 1 ms timer resolution and Pro Audio scheduling. Each case runs approximately 1.5 seconds. `emptyBeforeWrite` counts empty queues after startup, not acoustically recorded clicks.

| 48 kHz, 2048 client block | Old event path | Buffered path |
| --- | ---: | ---: |
| Actual queue capacity | 1056 frames | 2528 frames |
| Callback processing time | 29.867 ms | 29.867 ms |
| Empty queues before writing | 24 | 0 |
| Minimum measured queued frames | 0 | 640 |

64, 128, 256, 512 and 1024 also had zero empty queues in this scheduled run. Authoritative result: [wasapi-buffer-probe-70pct-scheduled-2026-10-05.txt](wasapi-buffer-probe-70pct-scheduled-2026-10-05.txt). Earlier native probes without matching timer resolution are exploratory.

## Actual production JUCE backend

`Scripts/diagnostics/WasapiJuceProbe.cpp` links the application's actual `include_juce*.obj` files. It opens, starts, stops, closes and reopens the real endpoint and runs silent callbacks under controlled load. It checks offered rates/sizes, actual callback size, and generated sample-time versus wall-time.

- **Before:** 32/64/128 were missing and clamped to 144; 2048 produced only **0.71566** seconds of audio per wall-clock second. 256/512/1024 passed. [Baseline](wasapi-juce-before-2026-10-05.txt). This baseline was stopped after the seven 48 kHz cases; no old-backend 44.1 kHz matrix is claimed.
- **After:** **14/14** output cases passed, 32 through 2048 at both 48 and 44.1 kHz, at 70% duty, with the selected and actual callback sizes equal. [Matrix](wasapi-juce-after-output-2026-10-05.txt).
- **Sustained 2048:** 85% duty, 12 seconds per rate: audio progression **1.00061** at 48 kHz and **0.99948** at 44.1 kHz. [Stress](wasapi-juce-2048-stress-2026-10-05.txt).
- Output-only `getXRunCount() == -1` means unavailable. JUCE's counter is capture-side; it cannot establish render underrun absence. The native padding probe provides the starvation measurement.
- All duplex attempts failed to open input. Microphone recording and live monitoring hardware validation remain **unverified**. No input samples were saved. [Duplex](wasapi-juce-after-duplex-2026-10-05.txt).

## Builds, regression and reproducibility

- Full Release and Debug application builds succeeded. Release remains optimized, with link-time optimization disabled to avoid the previously observed linker/compiler memory limit. Builds use the existing unsigned `APEX_SKIP_SIGNING=1` path.
- Rebuilt Debug and Release tests each passed `APEX.Device`: 13 groups, 51 assertions, zero failures; and `dsp.block-invariance.v1`: 14 groups, 25,417 assertions, zero failures. See `wasapi-final-*-Debug-2026-10-05.json` and `wasapi-final-*-Release-2026-10-05.json`.
- No global release/suite approval is claimed. Broader earlier reprepare cursor and aggregate-suite failures remain outside this change.
- Patch 0004 is wired into materialization and verification. A pristine file from the pinned archive was patched in the official repository fixture and matched the installed source byte for byte. All three edited PowerShell scripts parse without errors.
- The full dependency gate was not re-certified: executing the patch helper encountered an antivirus block. Security settings were not changed. The direct patch/hash round trip passed.

| Artifact | SHA-256 |
| --- | --- |
| Pristine JUCE WASAPI source | `C5367D92BA534779CC8ACCEDD20424F7E3B6DAA7CE597D33CA8858451AB36759` |
| Patched installed source | `B0D577BCC9D4F03721A0AF594753EDF739A39048E43E38BF24B1EC4DA7A77696` |
| `Dependencies/patches/juce-8.0.12/0004-apex-wasapi-shared-buffer.patch` | `8565917FC95A7E83B9184FB94685B2950B2458E34E2F7BDE16A3A17FC716BF43` |
| Updated `Builds/VisualStudio2026/x64/Release/App/DAW_Core.exe` | `F1C1B868226C61835962B096102AF9967088616F4E2C80CC77E3799398232916` |

## Reproduce the production probe

After building Release, run from the official DAW_Core repository:

```powershell
& .\Scripts\diagnostics\build_wasapi_probe.ps1 -Configuration Release
& .\evidence\WasapiJuceProbe-Release.exe output 0.70
& .\evidence\WasapiJuceProbe-Release.exe output 0.85 2048 12000
```

Run timing probes sequentially without competing builds or CPU benchmarks. Output is the default. The optional `duplex` mode attempts input but never stores or routes its samples to output. Diagnostics are not part of the application project.

Cross-check: [PortAudio WASAPI source](https://github.com/PortAudio/portaudio/blob/master/src/hostapi/wasapi/pa_win_wasapi.c) distinguishes polling from event servicing. The production code and measured tests above, rather than analogy alone, establish this correction.
