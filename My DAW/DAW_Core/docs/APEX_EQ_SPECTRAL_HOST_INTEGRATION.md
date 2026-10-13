# APEX Parametric EQ — Spectral Dynamics host integration v1 (DRAFT)

Branch: `feature/apex-eq-spectral-host-integration-gated-v1`
Stack: flagship EQ PR #1 → Spectrum Grab / EQ Match PR #88 →
spectral DSP kernel PR #89 → **this host-integration branch**.

**Not released.** Integration has been committed to a dedicated draft
branch but Windows Debug/Release and real hardware testing are NOT yet
certified. The isolated DSP core is also independently gated in
`APEX_EQ_SPECTRAL_DYNAMICS_GATES.md`.

## Append-only hosted ABI

Prior parameters 0..347 remain byte-for-byte in their original order.
The spectral extension adds:

| ID/index | Purpose | Default |
| --- | --- | --- |
| 348 `peq.spectral.arm` | Host spectral mode (requires audio reprepare) | Off |
| 349..372 `peq.band01..24.spectral.enable` | Individual spectral processing selection | Off |
| 373 `peq.spectral.density` | Global spectral selectivity [0..1] | 0.5 |
| 374 `peq.spectral.tilt` | Optional +3dB/oct detector weighting | On |

**State version 7**; old session/old state loads always default missing
spectral parameters to their own defaults. In particular, a session saved
before this feature cannot silently arm a 1024-sample lookahead.

## Processing order (armed and unarmed)

1. Existing canonical host parameter publication and 24-band static EQ.
2. Existing per-band ordinary dynamic detector, **except** spectral-marked
   bands; they do NOT also run a broadband dynamic gain.
3. Existing linear-phase transition, character, bypass and output stage.
4. **Only if spectral mode is armed at prepare:** apply the STFT per-bin
   reduction to the post-EQ signal through the fixed-latency
   `SpectralAlignedBlend`. Both the bypassed/neutral dry path and processed
   spectral wet path are 1024 samples delayed; a 10ms equal-latency
   wet/dry crossfade is performed with no allocations.
5. Post-analyzer sees the actual delayed output.

Spectral modes are restricted to compatible bell, low/high shelf shapes.
All others keep existing dynamic processing. The per-band spectral setting
does not disable the static EQ frequency response.

## PDC and live tracking

- Default: spectral ARM **Off** = no additional latency. At 48kHz/128
  samples, standard APEX EQ tracking behavior is preserved.
- During `prepareToPlay()`, arm state is latched and DSP/delay storage
  reset. If armed, **1024 samples** are added to the host reported latency
  on top of any existing linear-phase latency.
- Spectral ARM is hosted/automatable but is **intentionally not adopted
  inside processBlock**. Switching its hosted value while audio runs
  raises a visible "RESTART AUDIO" pending state, without changing PDC
  or discontinuously shifting the recording. DAW audio must stop and
  reprepare to commit a new arm state.
- While armed, per-band spectral enable, density, tilt, and existing
  threshold/range/attack/release may change per block without PDC jumps.
- Plugin-global bypass retains the armed delay instead of outputting
  temporally misaligned dry audio.
- Turning spectral ARM Off while playing also needs reprepare; this is
  deliberate, not an instant zero-latency switch.

This is **not yet polished host transport integration**: automatic
stop/start reprepare, real offline bounce behavior, host PDC changes
without transport stops, and plugin graph crossfade need integration
tests in actual APEX sessions.

## GUI

The APEX-branded contextual band inspector has touch-size controls for
Spectral Engine ARM, per-band enable, global Density, and Detector Tilt.
The ARM control explicitly displays pending restart when its saved
parameter differs from the prepared state.

## Native band creation and clipboard safety

The GUI's shared band clipboard now copies the new per-band spectral toggle
as its fifteenth hosted control. Sketch, Spectrum Grab and EQ Match are
different workflows: when creating a new ordinary band in an available
(disabled) slot they reset latent bypass, placement, ordinary Dynamic EQ
enable, detector filter and spectral enable BEFORE activating the band.
This avoids accidental reactivation of an old hidden dynamic or spectral
setting left in the reused slot. Cross-instance Paste intentionally keeps
the saved spectral selection.

Tests cover the cross-instance copied spectral flag, existing-disabled-slot
recycling, and legacy state v6 defaults for re-used processors.

## Required verification gates

1. Compile with pinned JUCE on Windows Debug/Release.
2. Full 375-ID literal state/automation pin suite, project migration and
   save/reopen, including missing-index defaults.
3. Spectral OFF has bit-identical old audio path and no new latency.
4. Spectral ON reports exactly +1024 samples of latency, sums correctly
   with linear phase, and its neutral path produces delayed unity.
5. Dynamic EQ must never double-apply active spectral bands.
6. Spectral bypass/inactive audio remains latency-aligned; transitions are
   free from pops/clicks across different host block sizes.
7. Mono/stereo and M/S FFT purity; no forbidden allocations/locks in audio.
8. 24 spectral bands must meet 48kHz/128 CPU budget without x-runs.
9. Transport reprepare occurs only while audio thread is quiescent and
   does not race GUI instance shutdown.
10. Apollo/Volt listening gates on vocals, low frequencies, high frequencies,
    anti-phase and realistic material at 44.1/48/96 kHz.
11. EQ instance-list, visual per-bin activity overlay, global A/B and robust
    Undo/Redo are separate parity tasks and are NOT yet delivered.

**Do not merge this draft into release builds before verification.**
