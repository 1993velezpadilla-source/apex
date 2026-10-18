# APEX Evo Eye — Fully Procedural Boot GUI

The original eye photo supplied by the owner is an **art-direction reference only**. It is not a texture, splash PNG, JPG, sprite or Windows executable resource.

**Official text:** AUDIO PRODUCTION ENGINEERING

## Architecture

- `Source/SplashScreen.h` draws the design frame-by-frame in **native C++ / GDI+**.
- The eye interior is a live `Gdiplus::GraphicsPath` aperture clipped and recomputed every frame, with a luminous vertical pupil, animated electric iris filaments, specular highlights and multi-layer translucent glow.
- Outer armored horns, upper/lower eyelids, blades, central spikes and APEX wordmark are actual vector paths with metallic gradients and edge lighting (a custom DAW/plugin GUI, **not** a stored image).
- The pupils and the eyelid curve move independently; eyelids actually converge to close, rather than showing a still photograph with a rectangle over it.
- A live red beam, particles, progress motif and ambient ring geometry reinforce the cybernetic style.
- The exact subtitle is rendered as C++ GUI text, not pre-baked into bitmap pixels.
- A dedicated non-audio Win32 rendering thread refreshes at ~30 FPS while the JUCE message thread initializes. After the existing StartupPanel becomes visible, the splash fades away.
- Rendering uses design coordinates 1200×750, scaled to the available desktop dimensions, with no runtime network, file loading, JUCE resource manager or embedded photograph.

## Windows production gates

- `Scripts/test_evo_eye_procedural.ps1` asserts procedural geometry and the exact subtitle, rejects raster image load calls, and verifies the compiled Windows EXE has **no old JPEG RCDATA resource ID 27182**.
- `.github/workflows/windows-build.yml` runs the audit on both Windows Debug and Release builds, followed by the existing full-suite and actual startup-panel smoke tests.
- `Scripts/install_daw.ps1` packages the built executable without any Eve Eye image sidecar.
- Visual acceptance still needs an actual frame capture of blinking at runtime, plus inspection of the visual similarity to the owner's reference photograph. Compilation alone does not establish appearance.

## Reference policy

The photo was deliberately removed from this branch's source files. Do not re-add it to the splash or reintroduce old ‘picture as a boot logo’ behavior. Future changes should refine the **C++ vector geometry and procedural shading**, as with APEX's native compressor GUI.
