# APEX Android / iPhone Portrait-first UX – QA build acceptance

This Android build branch snapshots the current APEX phone GUI work from PR #165.
Keep the full original JUCE APEX shell. Do not substitute a probe or Godot app.

## Expected UX for device validation

- Android phone portrait opens Arrangement first; transport is at the bottom with
  original APEX buttons, project key, tempo, time readout, FX and browser shortcuts.
- Dedicated MIXER opens the original genuine floating MixerWindow (not docked).
- Dedicated SENDS opens the original pink Bubblegum V2 Sends panel; there is no
  full Send Matrix eye on Android/iPhone. iPad/Windows/Mac still retain Matrix.
- Landscape preserves transport operations and shows responsive panels.
- Phone should not paint Bubblegum cables on top of the UI.
- Native Android immersive status/navigation bars stay hidden until swipe.
- On rotation the sends panel remains within the visible activity.
- Touch controls require actual device QA; CLI builds alone cannot prove success.
- Record via a USB class-compliant input such as Volt 1, save/reopen and verify
  FX, sends, clip normalization, and automation persistence.

**The APK is for testing, NOT a completed/verified mobile GUI release.**
