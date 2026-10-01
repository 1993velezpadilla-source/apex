# C4 Phase 1 — Evidence Matrix (PASS)

Status: **PHASE 1 = PASS** — declared 2026-08-12 after every gate below is green.

## Evidence manifests and artifacts

| Gate | Result | Evidence |
|------|--------|----------|
| APEX.C4 Debug suite | PASS (exit 0) | manifest `d2f1b0e78181` — `evidence/runs/APEX.C4/20260812T234652Z-Debug-e4176c2` |
| Global Debug regression (all categories) | PASS (exit 0) | manifest `330b4f23afb3` |
| APEX.C4 Release suite | PASS (exit 0) | manifest `e14717d25887` |
| Global Release regression (all categories) | PASS (exit 0) | manifest `4f1be3848a55` |
| Debug APEX application | PASS | `Builds/VisualStudio2026/x64/Debug/App/DAW_Core.exe` — 39,407,616 bytes — 2026-08-12 22:25:06 — SHA-256 `53997A6163A0FE536A9553085EF225C4AADADB5D9E0E219DC7CDAC44E6B884F7` — unsigned (requested) — PDB SHA-256 `6BCDE93853D5E70D463B80595D91DD27B06CFD120D4958C409593C2D3496DC34` |
| Release APEX application | PASS | `Builds/VisualStudio2026/x64/Release/App/DAW_Core.exe` — SHA-256 `4B04EC4AB95D9201A644AF9B0BFBC4CEBAE03679AA5DA44FA23B58B473E74D24` — unsigned (requested) — PDB SHA-256 `D1C05E5BE526A377641455DB5D1A7D1B9616F37096253817B1F920E6B853EA78` |
| Repository policy | PASS (exit 0) | `test_repository_policy.ps1` |
| Dependency verification | PASS (exit 0) | `verify_dependencies.ps1` |

## Gate matrix

Legend: R = regression test class (all under Tests/Source/C4, category APEX.C4).

| # | Gate | EXPECTED | ACTUAL | MEASURED VALUE | PASS | REGRESSION TEST | NOTES |
|---|------|----------|--------|----------------|------|-----------------|-------|
| 1 | Neutral fast path | Bit-identical wire when neutral | Bit-identical across full matrix | 0 diff (max |Δ| = 0) | PASS | C4.NeutralPath | Silence, impulse, deterministic random, sine, mono, stereo, blocks 32/128/512/1024, all 6 rates, save/reload, after boosts/cuts/Q/HPF-LPF/morph |
| 2 | WEIGHT bell | Center gain = requested | Exact | ±0.05 dB class | PASS | C4.Engine | 100 Hz ±6, 300 Hz −6 |
| 3 | SCULPT bell | Center gain = requested | Exact | ±0.05 dB class | PASS | C4.Engine | 1 kHz ±6, 800 Hz −6 |
| 4 | BITE bell | Center gain = requested | Exact | ±0.05 dB class | PASS | C4.Engine | 4 kHz ±6, 2 kHz −6 |
| 5 | OPEN bell | Center gain = requested | Exact | ±0.05 dB class | PASS | C4.Engine | 10 kHz ±6, 5 kHz −6 |
| 6 | Gain extremes | ±15/±12 dB center | Within 0.35 dB | 0.35 dB bound | PASS | C4.Engine | TPT peak offset documented |
| 7 | HPF | −3.01 dB at fc, 18 dB/oct | Match | fc: −3.01; fc/2: −18.13 | PASS | C4.Filters | 100 Hz @ 48k; extremes 20 Hz and 1.5 kHz @ 44.1k; monotonic; all 6 rates |
| 8 | LPF | −3.01 dB at fc, 12 dB/oct (digital bilinear) | Match | fc: −3.01; 2fc: −13.53 (bilinear −13.53); 4fc: −32.42 (bilinear −32.42) | PASS | C4.Filters | The analog values (12.30/24.10) do NOT hold digitally; expectations computed from tan-warp formula |
| 9 | LPF 24 kHz @ 44.1 kHz | Finite, near-flat (TEMPORARY clamp) | Near-flat | 0.00 dB @ 15 kHz, −0.01 dB @ 20 kHz | PASS | C4.Filters | `0.49*fs` clamp → 21.6 kHz; tan(π·21609/44100)≈221 compresses the response in-band. TEMPORARY — replaced by C4HaloShelfMapper in Phase 2 |
| 10 | Gain/Q law | Proportional-Q: qEff = qBase·(1+drive·(|g|/15)^exp) | Verified | +15 dB → Q 1.52; cuts more focused | PASS | C4.Engine, C4.Automation | Boost/cut asymmetry per profile geometry |
| 11 | Composition topology | Complex linear superposition | Match | ≤ 0.15 dB across all 9 configurations | PASS | C4.Composition | G(f)=1+Σ(H_b−1); shared centers sum to 2A−1=+9.54 dB (measured 9.51); magnitude-only model rejected (0.34–0.83 dB errors) |
| 12 | Complex IR superposition | Total == Σ solos (complex) | Match | ≤ 0.15 dB | PASS | C4.Composition | The authoritative proof; includes neighbor skirts and phase |
| 13 | Bell/Shelf morph | 0/25/50/75/100% finite, continuous | Monotone 25 Hz: 0→5.87 dB; phase deltas ≤ 0.196 rad | PASS | C4.MorphAudit | Phase-vs-blend smooth but not linear; bound 0.25 rad |
| 14 | Morph automation | No click/spike; finite | max|out| ≤ 2.5×peak | PASS | C4.MorphAudit, C4.Automation | Fast toggling 500 blocks |
| 15 | Morph state continuity | Automated settle == direct request | Exact | 5.874 dB both | PASS | C4.MorphAudit | Hold-shelf now requested explicitly (test bug closed) |
| 16 | Smoother convergence | Exact target arrival, all rates | Exact | 15.000000 dB; every excursion reaches target bit-exactly | PASS | C4.Smoother | No-progress snap + tolerance snap; ±→0, tiny/large deltas, repeated excursions, 6 rates, engine neutral re-entry |
| 17 | Neutral re-entry | Gate re-opens exactly after excursions | Exact | isSettledNeutral true | PASS | C4.Smoother, C4.NeutralPath | After boosts/cuts/Q/HPF-LPF/mode |
| 18 | Lifecycle | Construct→prepare→process, reset cycles, deterministic | Bit-identical IRs | 0 bit diffs across 5 reset cycles | PASS | C4.Lifecycle | Original growing-IR reproducer now permanent: direct-engine impulse DECAYS (tail < 1e-4; historically 132.458) |
| 19 | Direct engine vs processor | Sample-for-sample agreement | Match | 0 float diffs over 124 blocks; 0 signed-zero diffs | PASS | C4.Lifecycle | Same effective parameters; explicit zero-fill harness (JUCE isClear contract documented) |
| 20 | Long silence | Zero output, bounded states | Zero | output == 0.0; all states < 1e-6 after 10 s | PASS | C4.Lifecycle | |
| 21 | Long bounded input | Finite and bounded | Finite | max|out| = 1.78 (envelope 0.25·ΣA·1.1 = 6.19) | PASS | C4.Lifecycle | 15 dB × 4 bands, 5 s LCG noise |
| 22 | Long-run state monitor | No state grows without excitation | Bounded | all SVF/one-pole states < 1e-6 after 30 s silence at max settings | PASS | C4.Lifecycle | 20 Hz, Q 10, +15 dB, HPF/LPF enabled, impulse + 30 s |
| 23 | Numerical safety | Finite at extremes, denormal-safe | Finite | all rates | PASS | C4.NumericalSafety | Extreme settings, silence/tiny signals, automation stress |
| 24 | RT allocations | Zero allocation/lock in processBlock | Zero | allocation-free across 48 subcases (11 automation classes × blocks 1/32/128/333/1024, mono+stereo) | PASS | C4.RtAllocation | Neutral, shaping, extreme, per-block changes, freq/Q/trim sweeps, morph, HPF/LPF, bypass |
| 25 | Active numSamples range | Only [0, numSamples) touched | Verified | sentinels beyond active range intact (bypass blend + engine paths) | PASS | C4.RtAllocation | Fixed to use an AudioBuffer view (test construction bug closed) |
| 26 | Automation | Rapid gain/freq/Q/BLOOM/filters/mode/bypass sweeps finite, bounded | Finite | all classes | PASS | C4.Automation | Gain step ramps (no zipper), zero-crossing click-free, settled convergence vs reference 0.04 dB |
| 27 | Parameter table | 19 params with exact IDs/kinds/ranges/defaults | Match | table verified | PASS | C4.Processor | Frozen contract: docs/C4_PARAMETER_CONTRACT.md |
| 28 | Text conversions | getText ↔ getValueForText round trip, every kind | Exact | all kinds | PASS | C4.Processor | Unit parsing reads the TEXT's unit (1e+06 bug closed) |
| 29 | HPF/LPF OFF | 0.0 = OFF; cutoffs strictly inside (0,1); round trips | Exact | min cutoff norm = 1/256; OFF ↔ 0.0 ↔ "Off" | PASS | C4.Processor, C4.Filters | OFF-collision defect closed (norm floor) |
| 30 | State save/reload | Preserves every parameter; tolerant restore | Verified | missing/garbage/non-finite/clamped handled; neutral stays neutral bit-identically | PASS | C4.Processor, C4.NeutralPath | |
| 31 | Block-size independence | Identical output 32/128/512/1024 | Bit-identical | 0 diffs | PASS | C4.Processor, C4.Lifecycle | |
| 32 | Oversized chunking | Chunked == sequential legal blocks | Match | verified | PASS | C4.Processor | |
| 33 | Mono == stereo-left | Deterministic shared control state | Match | 0 diffs | PASS | C4.Processor, C4.Lifecycle | |
| 34 | Host/native format | Identity, creation, rejection, isolation, automation IDs | Verified | all C4.FormatHost tests | PASS | C4.FormatHost | APEX Native format #2 (uniqueId 0x4334, identifier APEX::C4) |
| 35 | G10 registration coexistence | Scanner seeds G10 AND C4 exactly once each | Verified | 2 APEX Native cache entries | PASS | G10.Format.Host | G10 test expectation updated to the intended Phase 1 state (test bug closed) |
| 36 | 44.1 kHz | All suites green | Green | | PASS | all C4 suites | SampleRates: neutral bit-identical, band response, HPF 100 Hz, LPF 10 kHz, OPEN 20 kHz finite |
| 37 | 48 kHz | All suites green | Green | | PASS | all C4 suites | |
| 38 | 88.2 kHz | All suites green | Green | | PASS | all C4 suites | |
| 39 | 96 kHz | All suites green | Green | | PASS | all C4 suites | |
| 40 | 176.4 kHz | All suites green | Green | | PASS | all C4 suites | |
| 41 | 192 kHz | All suites green | Green | | PASS | all C4 suites | |
| 42 | C4 Debug suite | All 13 categories green, exit 0 | Green | manifest d2f1b0e78181 | PASS | all | C4.Engine, C4.Filters, C4.NeutralPath, C4.NumericalSafety, C4.Automation, C4.Processor, C4.RtAllocation, C4.SampleRates, C4.FormatHost, C4.Composition, C4.MorphAudit, C4.Smoother, C4.Lifecycle |
| 43 | Global Debug suite | All categories green, exit 0 | Green | manifest 330b4f23afb3 | PASS | all | G10 and every existing category unaffected |
| 44 | Debug APEX build | Clean rebuild, unsigned | PASS | SHA-256 53997A61… | PASS | build_apex.ps1 | C4 native registration present, G10 registered |
| 45 | C4 Release suite | All 13 categories green, exit 0 | Green | manifest e14717d25887 | PASS | all | No Debug-vs-Release discrepancy |
| 46 | Global Release suite | All categories green, exit 0 | Green | manifest 4f1be3848a55 | PASS | all | |
| 47 | Release APEX build | Clean rebuild, unsigned | PASS | SHA-256 4B04EC4A… | PASS | build_apex.ps1 | |
| 48 | Repository policy | Exit 0 | PASS | test_repository_policy.ps1 | PASS | | |
| 49 | Dependency verification | Exit 0 | PASS | verify_dependencies.ps1 | PASS | | |
| 50 | Temporary material | Diagnostic/probe/ASan/logging gone | Verified | C4DiagnosticTests.cpp absent; lastReadProbe_ 0 hits; fsanitize 0 hits; 0 logMessage/printf in Source/C4Core; test hooks limited to get*ForTest introspection + morph blend test hook | PASS | | |

## Residual limitations (documented, intentional)

1. **0.49·Nyquist high-frequency clamp (TEMPORARY)**: the LPF 24 kHz control
   at 44.1 kHz behaves as ~21.6 kHz and is near-flat in-band. This is the
   Phase 1 placeholder, NOT the final OPEN/HALO implementation. Phase 2
   (C4HaloShelfMapper) replaces it with the intentional perceptual mapping.
2. **OPEN shelf plateau measurement**: the analog 1st-order shelf converges
   to +6 dB only asymptotically; Phase 1 pins the plateau probe at 4×f0.
3. **JUCE `AudioBuffer::clear()` isClear contract**: direct-engine tests must
   use explicit zero-fill (documented in C4TestUtils); the engine itself is
   raw-pointer only and correct. The APEX host's plugin-buffer lifecycle
   follows the same documented rule.
4. **Parameter IDs frozen** at Phase 1 (see docs/C4_PARAMETER_CONTRACT.md).
   Any future change requires explicit versioning/migration.
