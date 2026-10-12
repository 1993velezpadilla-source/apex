# APEX Mixer Ø — implementation and acceptance

## Status — merged into `main` on 2026-10-12

**The mixer polarity-inversion implementation is complete and merged.**
Source branches were merged in dependency order: #73 (pre-fader send
correctness) → #72 (de-click ramp) → #64 (original Ø and complete integration)
→ `main`, final merge commit `18379b8b5ecc2fe1cff8ebb6044ef3cb1d71b952`.

The final merged **source tree** was verified byte-for-byte identical by its
Git tree hash (`824a5508014cee215c4705f66fe2bc32b276b60a`) to the
head of #73, which passed the full Windows **Debug and Release** gauntlet
in https://github.com/1993velezpadilla-source/apex/actions/runs/38293491934.

The `main` post-merge build/tests are triggered at
https://github.com/1993velezpadilla-source/apex/actions/runs/38298522439
and must be reviewed before publishing a final combined product installer.
This document records the distinction between **merged/CI verified** and
**manually audited on ASIO hardware**.

## Implemented production behavior

- Dedicated **Ø** mixer button on track, bus and folder-bus strips;
  Master may display the cell but cannot change polarity.
- Track-local atomic `polarityInverted` state, serialized into project
  state; older sessions default to **OFF**.
- Per-track, audio-thread-owned transition state retargets on user toggle,
  smoothly ramping +1 → −1 or −1 → +1 across **64 samples**, including
  across multiple callbacks and mid-ramp reversals.
- No extra latency, heap allocation or blocking on the ramp path. Stereo
  channels follow the **same gain curve**; after the ramp the gain is exactly
  ±1 so deterministic null cancellation is preserved.
- Polarity executes after inserts, before pre/post sends and route fanout.
  Bus and flat-fallback processing use the same semantics.
- Routing fix: a **PreSend is mixed only once**, by the dedicated pre-fader
  tap, and not duplicated by the post-fader fanout.
- Changing polarity never alters clip/sample positions or source media.

## Passed automated acceptance

- Isolated C++17 polarity DSP tests on **Windows MSVC and Linux GCC**,
  including stereo and mono inversion/reversion, null cancellation, buffer
  boundaries, bounded transition, transition reversals and exact settled
  gain.
- Real Windows JUCE Debug and Release builds of `DAW_Core.exe` and test
  runner; focused persistence, legacy-OFF, Master guard and null tests.
- Real production `AudioEngine::process` tests of source on/off
  inversion, de-click first-sample transition, and routed sends/bus Ø.
- Pre-fader routing verifies the pre-send output level remains unchanged
  when the source fader changes; verifies two inversions through source
  and bus restore the signal.
- Both Windows configurations published unsigned binaries and test
  evidence (no hardware audio output was tested by GitHub CI).

Full test runs:
- #64: https://github.com/1993velezpadilla-source/apex/actions/runs/38289550442
- #72: https://github.com/1993velezpadilla-source/apex/actions/runs/38292583943
- #73: https://github.com/1993velezpadilla-source/apex/actions/runs/38293491934

## Manual release qualification — explicitly **not yet verified**

The feature may be tested as a **Windows beta**. A production-ready,
signed APEX release still requires operator signoff for:

1. Physical ASIO/Apollo Solo playback and live input at 48 kHz / 128
   samples: listen for clicks at repeated mid-signal Ø toggles.
2. Mixer touch/high-DPI layout, hover/pressed state and narrow-strip
   hit targets.
3. Multichannel sidechains, aux/folder hierarchy, PDC with plugins,
   post-fader recording taps and offline stems/export parity.
4. Close/reopen a representative session with multiple sends, mixed
   mono/stereo tracks, plugins, and older projects.
5. Merge/reconcile the **separate automation/VST3/Bubblegum** branch
   stack so a later product build contains all features together.

## Terminology / out of scope

Ø **polarity inversion** negates waveform samples. The optional short
click-reducing transition does not perform time alignment or spectral phase
rotation. Frequency-dependent all-pass rotation, sample offset alignment,
automatic mic alignment and correlation visualization are **separate,
unimplemented** features.

## Further reading

- Sound On Sound, Mike Senior, *Phase Demystified* (2008):
  https://www.soundonsound.com/techniques/phase-demystified
- Sound On Sound, Sam Inglis, *Making Multi-mic Recordings Work* (2020):
  https://www.soundonsound.com/techniques/making-multi-mic-recordings-work
