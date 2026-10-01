# APEX Parametric EQ Phase 6 — Genuine Linear Phase

**PHASE 6 CLOSED — PASS — diagnostic working-tree milestone**

Declared 2026-08-15. The shared workspace remains dirty on
`feature/apex-windows-baseline-evidence` at
`e4176c2569a5cd1f3cb5cf43e6367f5e0312a1a1`; no clean release commit/tag
exists, so this is a diagnostic working-tree milestone, not release-grade
evidence.

## Phase 6 scope

Add a genuine linear-phase operating mode to the APEX Parametric EQ:
4095-tap symmetric FIR kernels (2047-sample latency), worker-built kernel
publication, click-bounded Minimum/Linear mode switching, a minimal
functional TopBar phase selector, and the accepted Phase 6 limitations
(dynamics and audition suppressed in settled linear phase). No dynamic
linear phase, no linear-phase audition, no quality modes.

## Parameter ABI extension (state v4)

- `peq.phase` at index **317**; total parameters **318**; state version **4**.
- Compile-time pins: `kDynamicDetectorParameter == 314`,
  `kPhaseModeParameter == 317`, `kNumParameters == 318`. The Phase-5
  `kNumParameters == 317` static_assert was corrected.
- The State suite pins every ID literally, including `peq.phase` at 317,
  and round-trips all 318 parameters; v1/v2/v3 states load with the
  deterministic default (minimum phase).

## Linear phase architecture

- `LinearPhaseFft`: fixed 4096-point radix-2 forward/inverse FFT with
  cached twiddles (constructed once, worker side only).
- `LinearPhaseKernels`: four 4095-tap FIR kernels (LL/LR/RL/RR);
  `kLinearPhaseLatencySamples = 2047`; `isNeutral()` recognises the exact
  centred delta.
- `LinearPhaseKernelBuilder`: samples the 2x2 response at 2049 bins, builds
  conjugate-symmetric spectra, inverse-transforms, applies a Hann window,
  and emits odd-length centred kernels.
- `LinearPhaseConvolverChannel`: fixed `std::array<double, 4095>` ring,
  direct O(M) convolution, no allocation, no locks.
- Processor integration: low-priority `juce::Thread` `KernelWorker` builds
  kernels outside the callback; race-safe double-buffered publication via
  lock-free atomics (request / generation / active index); the audio thread
  adopts published generations with a bounded 10 ms old/new kernel
  crossfade. Mode switching uses a 10 ms per-sample crossfade with
  `setLatencySamples(2047 <-> 0)` applied before the audible ramp.

## Kernel construction defects (root-caused and fixed)

1. **Centering defect.** The original extraction used a modulo shift that
   paired taps across the 4096 wrap (h[1] with h[2] after truncation),
   producing an asymmetric kernel. Fixed by a frequency-domain phase
   rotation `e^{-j2pi k 2047/4096}` that lands the impulse exactly at the
   odd-kernel centre with exact symmetry.
2. **Design-spectrum defect.** The builder originally sampled the
   minimum-phase complex response; its phase broke the conjugate symmetry
   the FFT requires, so the impulse response was one-sided. Fixed by
   sampling the **zero-phase magnitude** of each 2x2 entry: linear phase is
   supplied by the symmetric tap arrangement, never by embedding the
   minimum-phase phase into the sampled spectrum.

## Proofs (all green in focused Debug and Release)

| Property | Oracle | Result |
|----------|--------|--------|
| Kernel symmetry | LL/RR taps centre+d vs centre-d, 4095 offsets | exact within 1e-9 |
| Crossfeed mirror | LR[t] == RL[M-1-t] | exact within 1e-9 |
| Neutral pure delay | flat settings -> delta at tap 2047 | exact within 1e-9 |
| Magnitude | 937.5 Hz bin-exact sine vs canonical `bandResponse`, 44.1/48/96/192 kHz | within 0.1 dB at all four rates |
| Group delay | 1.0-1.4 kHz, unwrapped against expected slope | 2047 +/- 0.5 samples |
| Phase slope | response aligns with -2 pi f tau at 200..20000 Hz step 200 Hz | within 0.01 rad |
| Impulse latency | measured impulse peak | exactly 2047 == reported latency |

## Deterministic asynchronous-worker test engagement

- Read-only test observables on `Processor`:
  `linearKernelGenerationForTesting()` (published generation) and
  `isLinearPhaseModeSettledForTesting()`. Zero production call sites.
- Shared test helpers (`ParametricEQTestUtils.h`):
  `waitForLinearKernelGeneration` (bounded 10 s timeout, ~1 ms sleeps,
  non-busy-spin) and `requestLinearPhaseAndSettle` (request + wait + 50 ms
  settling silence covering both 10 ms crossfades).
- Every measurement interval that assumes a settled kernel (magnitude,
  impulse/latency, block-size invariance, dynamics/audition limitations,
  lifecycle, RT allocation) synchronizes through the helper first.
- Playbook lesson promoted: **PEQ-L9 — ASYNC-DSP test readiness rule** (see
  playbook v1.7).

## Mode switching

- Block-size invariance: two 16384-sample renders (block sizes 1 and 512),
  both primed to settled linear phase, with a deliberate Minimum toggle at
  exactly sample 8192 (block-aligned for both sizes): **bit-exact**
  (`expectEquals` per sample).
- Click bound: maximum per-sample jump across the switch < 0.3.
- Entering LP: mode crossfade + kernel crossfade each bounded at 10 ms;
  leaving LP: minimum-phase engine snap + 10 ms crossfade, zero latency
  restored.

## Phase 6 product limitations (accepted, frozen)

- Minimum phase: static EQ, dynamic EQ, and audition all supported as in
  Phase 5.
- Linear phase: static EQ supported; dynamic EQ intentionally suppressed
  (published dynamic gain state deterministically zero);
  `beginAudition` rejected while settled in linear phase. Both restore on
  return to minimum phase. Not solved in Phase 6 by design.

## Functional Phase selector (minimal, temporary engineering UI)

- TopBar seventh button bound to canonical `peq.phase`:
  `setClickingTogglesState(false)`; state display owned by `refresh()`;
  clicks use the standard
  `beginChangeGesture`/`setValueNotifyingHost`/`endChangeGesture` path.
  Text reflects "Minimum"/"Linear" from the canonical parameter (no
  GUI-only authoritative state).
- External/host changes reach the top bar through a 12 Hz synchronization
  timer (same pattern as the existing inspector/graph timers).
- Seven-button responsive layout; 44 px minimum touch targets at every
  breakpoint; editor reopen and state restore synchronize automatically.
- `ParametricEQ.Editor` extension verifies: Minimum->Linear and
  Linear->Minimum through the real click-command path, canonical parameter
  changes, external parameter change reflected by the GUI, editor reopen,
  state restore into a fresh processor, and geometry at all five
  breakpoints. The current editor remains TEMPORARY ENGINEERING UI; G10 is
  the canonical future visual-family authority.

## CPU characterization (evidence-only; no pass/fail gates)

Environment: Windows 11, AMD Ryzen Z1 Extreme, High Performance power mode.
512-sample stereo blocks; `logMessage` evidence inside
`ParametricEQ.LinearPhase`.

| Case | Debug | Release |
|------|-------|---------|
| MP 6-band @ 44.1 kHz | 268.5 us/block | 36.9 us/block |
| MP 6-band @ 48 kHz | 246.6 us/block | 36.8 us/block |
| MP 6-band @ 96 kHz | 264.4 us/block | 44.3 us/block |
| MP 6-band @ 192 kHz | 359.4 us/block | 39.3 us/block |
| LP 6-band @ 44.1 kHz | 13729.7 us/block | 3144.6 us/block |
| LP 6-band @ 48 kHz | 15442.2 us/block | 3151.1 us/block |
| LP 6-band @ 96 kHz | 15482.5 us/block | 3192.7 us/block |
| LP 6-band @ 192 kHz | 14981.4 us/block | 3235.5 us/block |
| LP 24-band @ 48 kHz | 16543.9 us/block | 3275.1 us/block |
| LP 24-band @ 96 kHz | 15471.7 us/block | 3290.3 us/block |
| Kernel generation 24-band @ 48 kHz | 63500.1 us/build | 8854.5 us/build |
| Kernel generation 24-band @ 96 kHz | 67490.8 us/build | 8348.5 us/build |

Usability judgement (Release): linear phase costs ~3.2 ms of the 10.67 ms
48 kHz block budget (about 30% of one core, stereo, band-count
independent), and kernel generation costs ~9 ms per 24-band rebuild on the
worker thread. The current direct 4095-tap convolution is usable on the
current Windows target; kept unchanged. No partitioned-convolution
framework was introduced.

## RT safety result

Confirmed by code inspection plus the green RT-allocation test
(UnitTestAllocationChecker over 128 blocks including a mid-loop mode
toggle):

- processBlock performs no heap allocation (all scratch/ring buffers are
  members sized in prepare).
- No locks anywhere in the audio path; cross-thread publication is
  lock-free atomics only.
- FFT/kernel construction runs only on the KernelWorker thread; the audio
  thread stages a POD snapshot request and adopts published generations.
- Publication/adoption is bounded: double-buffered kernels, monotonic
  generation, 10 ms kernel and mode crossfades.
- Test-readiness accessors have zero production call sites; the readiness
  helpers never affect production behavior.
- reset/reprepare are deterministic (lifecycle suite green at
  44.1/48/96/192 kHz).

## Final validation (final source tree)

| Gate | Result |
|------|--------|
| Focused Debug `APEX.ParametricEQ` | PASS — 171 groups, 280,799 assertions, 0 failed |
| Focused Release `APEX.ParametricEQ` | PASS — exit 0 (same test set) |
| Complete repository Debug | PASS — 781 groups, 682,707 assertions, 0 failed |
| Complete repository Release | PASS — 781 groups, 682,707 assertions, 0 failed |
| Debug application build | PASS |
| Release application build | PASS |
| `test_repository_policy.ps1` | PASS |
| `test_validate_test_evidence.ps1` | PASS — 6 passed, 0 failed |
| `verify_dependencies.ps1` | PASS |
| Source hygiene + temporary-diagnostic/probe scan | PASS — zero probe/printf/temporary diagnostics in Phase 6 sources |
| `git diff --check` (Phase 6 file set) | PASS — clean; pre-existing unrelated dirty work untouched |

Note: `-Name` maps to JUCE `runTestsWithName` (test-name match only);
category runs use `-Category "APEX.ParametricEQ"`.

## Final evidence run identifiers

| Scope | Configuration | Run ID | Exit |
|-------|---------------|--------|------|
| Focused `APEX.ParametricEQ` | Debug | `bb5cf7aa587a` | 0 |
| Focused `APEX.ParametricEQ` | Release | `744e9dc9ec76` | 0 |
| Complete repository | Debug | `a615ff60f34d` | 0 |
| Complete repository | Release | `aa35ab7b7461` | 0 |

Assertion counts above were verified by direct invocations with
`--results-json` (focused Debug 171/280,799/0; complete Debug and Release
each 781/682,707/0).

## Final artifact inventory (UTC, SHA-256)

| Artifact | Size | Timestamp (UTC) | SHA-256 |
|----------|------|-----------------|---------|
| `Builds\VisualStudio2026\x64\Debug\App\DAW_Core.exe` | 39,851,008 B | 2026-08-15T20:01:36Z | `D4399F3436E20FFC7AC27D179D4F06FB8E56916B76834736C3044B368885ED43` |
| `Builds\VisualStudio2026\x64\Release\App\DAW_Core.exe` | 13,359,616 B | 2026-08-15T20:15:04Z | `82AE20971A2E6778C1AF7AF6E3249AA3B9E3F2FA6D11A5EC14746EB15952F75F` |
| `Tests\Builds\VisualStudio2026\x64\Debug\ConsoleApp\APEXTests.exe` | 27,876,352 B | 2026-08-15T17:40:16Z | `E19D816D1CE5F8EA2802B1B46B18A182F11B345E034ECF7C8D6B2E51C0E8DA28` |
| `Tests\Builds\VisualStudio2026\x64\Release\ConsoleApp\APEXTests.exe` | 9,776,640 B | 2026-08-15T17:47:23Z | `041163869FAA3ED3A67D00C850112580AE456B5397433C44B6F04F736CD8A7AB` |

All Phase 6 sources predate the test-binary build timestamps; hashes
reflect the final source tree.

## Known limitations (accepted, not Phase 6 blockers)

- Dynamic EQ and audition are intentionally unavailable in settled linear
  phase (published state zero / rejected token), per the frozen Phase 6
  limitation table above.
- Linear phase costs ~30% of one core at 48 kHz stereo in Release with the
  direct convolution; acceptable on the current Windows target, documented
  as evidence (not a gate).
- The editor is temporary engineering UI; the G10 visual family remains
  the canonical future authority and requires separate approval.

## Closure

**PHASE 6 CLOSED — PASS — diagnostic working-tree milestone**
