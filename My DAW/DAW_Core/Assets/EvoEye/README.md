# APEX — EVO EYE, self-contained Windows executable

The boot artwork is the owner's ORIGINAL supplied photograph (do not regenerate).
Its exact official caption is **AUDIO PRODUCTION ENGINEERING**.

## Packaging / deployment

- The photo at `My DAW/DAW_Core/Assets/EvoEye/evo_eye.jpg` is **source-build input only**.
- MSVC's resource compiler embeds JPEG bytes as Win32 RCDATA numeric resource **27182** into `DAW_Core.exe`.
- `Source/SplashScreen.h` retrieves resource 27182 using `FindResourceW/LoadResource/LockResource` and opens it as a GDI+ in-memory bitmap using `SHCreateMemStream`.
- The renderer blinks/pulses on its own Win32 thread, independent of the JUCE message loop, until the actual StartupPanel is ready.
- The installer explicitly **EXCLUDES** `EvoEye/` from files copied alongside the EXE. An installed APEX does not read an external boot JPEG/PNG or require any image path.
- Missing resource fails open: normal APEX and its StartupPanel still start. The installer intentionally blocks packaging until source artwork is present so an official release cannot silently omit its logo.

## Source-image acceptance

Use the exact provided artwork (1536 x 864, JPEG bytes, originally attached as `1000244033.png`, which has a JPEG signature). Rename its source copy `evo_eye.jpg`. The C++ renderer obscures the baked-in old caption and draws the **AUDIO PRODUCTION ENGINEERING** caption dynamically. Do NOT regenerate the artwork.

The GitHub connector cannot copy the photo's binary from the chat into a repository. The JPEG source asset is still **pending commit** in this PR, even though the resource compilation and runtime loader are implemented.

## Acceptance gates

1. Commit the user's original JPEG as `My DAW/DAW_Core/Assets/EvoEye/evo_eye.jpg` before release.
2. Windows Debug + Release compile and verify resource ID 27182 is actually present in both application EXEs (not merely source compilation).
3. Launch the real EXE with the source-build Assets directory hidden/removed: the animated eye still appears. Verify blink, exact slogan, and fade-to-StartupPanel.
4. Verify New Project / Open Project / Recent Projects / Rescan Plugins from PR #135.
5. Install EXE alone and verify no EvoEye image is required in `Program Files`.
6. Worker/subprocess modes must never display the splash.
