# APEX Opto 2A — Golden Reference Capture Protocol (2026-10-13)

**Product rule:** Exactly **one** APEX Opto 2A, tuned first against the artist's licensed UAD LA-2A **Gray** for sustained and dynamic lead vocals. Silver and LA-2 are separate *research comparators*, never separate APEX modes.

**Current status:** The physical-T4 research engine, stand-alone triode/transformer component equivalents and a connected experimental processing chain are **NOT connected to the APEX native plugin in PR #103**. These pass functional tests only, **not** 1:1 measurements. APEX Native Opto 2A still uses the earlier simplified envelope DSP. The 12AX7/12BH7 triodes have provisional circuit-law models and the two transformers have linear R/L equivalents, not verified component values. The **6AQ5-like stage is now a separate uncalibrated experiment**, not a verified hardware driver. The complete R37/C12 passive network, actual 6AQ5 AC plate/EL waveform and transformer, true nonlinear magnetization, coupling capacitance, reference calibration and anti-aliasing remain missing.

## 1. Offline reference stimuli (fully original)

Use Python 3 standard library, no extra installations:

```powershell
cd "My DAW/DAW_Core"
python Scripts/opto_reference_capture.py generate --output "$env:TEMP\apex-opto-golden"
```

Generated source file: `apex_opto2a_reference_input_48k_pcm24.wav` (mono 48,000 Hz, signed PCM24) and a JSON manifest with sample-exact boundaries.

The source contains stepped 1 kHz sine levels (-36 to -6 dBFS), 50/80/250/3,000/8,000/14,000 Hz frequency probes, a 12-second T4 exposure tone followed by a **16-second low-level probe signal** to measure recovery, an intermodulation signal and guarded silence sections. The long recovery probe is necessary: pure silence cannot reveal recovery in the output audio!

The PCM24 input does **not** contain a human voice, UAD preset, protected proprietary material or any measured impulse response. It is safe to commit fixture source code, but do **not** commit licensed comparison outputs if there are privacy/license concerns.

## 2. What the artist/engineer must capture later

- Open source file in APEX at 48 kHz, **no resampling or normalization**; use identical mono routing through either processing chain.
- On licensed UAD LA-2A Gray: set Compress, specific documented Gain, Peak Reduction, R37/HF orientation, plugin revision, host type (UAD DSP/UADx) and gain calibration. No channel strips, limiters or converters in the path. UAD DSP may require *real-time* bounce: do not infer offline capability from the software.
- On the future electrically modeled APEX Opto 2A: use mapped controls after calibration and document expected threshold mapping. Do not assume knob 30 == identical electronics until measured.
- Render **mono 24-bit PCM** without dither/normalization, at the same sample rate and exactly the same timeline length. Keep a separate dry render to certify neither path altered file timing. Observe UAD signal calibration and any implicit latency/PDC.
- Capture several control states and exposures, then a *held-out* unseen set to avoid fitting solely the chosen training signal. For stereo APEX, test linked channels separately; the physical LA-2A unit is mono.

## 3. Compare outputs with independent diagnostic code

```powershell
python Scripts/opto_reference_capture.py compare `
    --manifest "$env:TEMP\apex-opto-golden\apex_opto2a_reference_manifest.json" `
    --uad "uad_gray_reference_48k_pcm24.wav" `
    --apex "apex_opto_response_48k_pcm24.wav" `
    --csv "apex_opto_diff_by_segment.csv"
```

The tool currently requires mono **PCM16 or PCM24** and matching timeline lengths. If APEX/UAD exports float WAV, render an additional copy to 24-bit PCM using transparent/no-normalize conversion; do not rename the header or assume float WAV is PCM. The tool intentionally refuses sample-rate/length mismatches; compensate all PDC and roundtrip offsets **before** comparing. Correlation depends on time alignment.

CSV records per segment: UAD RMS level, APEX RMS level, delta RMS, waveform residual RMS and normalized correlation. **No green 1:1 assertion** is emitted solely from these values: comparing dynamic compressors requires matching delay/phase, static GR curve, program-history trajectory, harmonics, spectra and blind vocal listening. A near-zero RMS delta alone is insufficient.

## 4. Structural ground truth

The 1966 Teletronix schematic shows that T4 CdS is in an analogue **shunt attenuator** in the audio path, upstream of the gain-amplifier section. A signal is tapped for the sidechain near the input, rather than simply taking a generic post-compressor feedback envelope. The T4 assembly has two resistive light-sensitive elements (audio level and meter), each with nonlinear exposure/recovery; sidechain drive includes 12AX7/6AQ5 driver and an electroluminescent panel. The amplifier includes feedback but this is **not equivalent** to a generic feedback-compressor topology.

- Primary schematic: https://tile.loc.gov/storage-services/master/mbrs/recording_preservation/manuals/Teletronix%20Model%20LA-2A%20Leveling%20Amplifier.pdf
- UA hardware LA-2A: https://media.uaudio.com/assetlibrary/l/a/la-2a_manual.pdf
- UA Collection: https://help.uaudio.com/hc/en-us/articles/4419496124180-Teletronix-LA-2A-Leveler-Collection-Manual

`ApexT4OpticalCellCore.h` models EL excitation, two light-dependent resistances, exposure memory, time constants and voltage division. `ApexT4LevelerResearchCore.h` connects an **uncalibrated** 10 ms digital detector/key emphasis to the T4 cell and stereo dry/wet signal.

### Known approximation and gates still missing

1. R37/C12 measured passive network and frequency-dependent control voltage (existing key EQ is a digital approximation).
2. **Uncalibrated 12AX7 + 12BH7 triode equations now exist** in `ApexAnalogCircuitResearchCore.h`: static 3/2 power-law current vs cathode/grid/plate, B+ and resistive loads, 45-step bracketed solve precomputed into fixed tables at prepare time. Actual 12AX7/12BH7 datasheet fitting, tube capacitive dynamic response, coupled 6AQ5 control-driver transfer calibration, feedback and power-supply effects are still MISSING.
3. **Linear T1/T2 research equivalents now exist** with primary magnetizing and leakage inductance plus source/load/copper resistances. Current models estimate LF/HF rolloff and phase and are NOT measured transformer models. Nonlinear core loss, hysteresis, saturation, winding resonance, oversampling and antialiasing remain MISSING. A single output tanh is explicitly forbidden as a replacement for real core physics.
4. Reference calibration of photoresistance (separately dark, illuminated, history), panel electrical transfer and independent meter CdS response.
5. Exact host calibration, oscilloscope/spectrum waveforms, gain-reduction output meter mapping and exposure tests; validated on real user vocals with headphone monitoring and Apollo latency.

**Hard stop:** A PR108 green result means only the research code compiles and passes functional invariants. It must never be represented as a released LA-2A clone until the measured reference and native host integration gates pass.


## 5. Time-resolved recovery and gain-transfer comparison

The separate original diagnostic `Scripts/opto_reference_dynamics.py` analyzes **dry**, licensed UAD, and APEX experimental renders in fixed-size, nonoverlapping windows, using each output's RMS relative to the SAME dry input. It computes dynamic transfer curves rather than inferring T4 behavior from a single loudness number. The golden recording fixture already provides the relevant segments (including a 12-second exposure and 16-second low-level recovery probe).

> Do not confuse output gain with GR. The tool records the **effective total gain through each signal chain**, not the real hardware's internal photoresistance or true VU GR. The recovery shape report subtracts each model's late-period output reference independently so that differences in constant output gain do not masquerade as differences in release shape.

Run once the three captured exports exist (UAD proprietary DSP is NOT executed by the script):

```powershell
python Scripts/opto_reference_dynamics.py analyse `
    --manifest "$env:TEMP\apex-opto-golden\apex_opto2a_reference_manifest.json" `
    --dry "$env:TEMP\apex-opto-golden\apex_opto2a_reference_input_48k_pcm24.wav" `
    --uad "uad_gray_reference_48k_pcm24.wav" `
    --apex "apex_opto_response_48k_pcm24.wav" `
    --output "opto_measured_reports"
```

Outputs: `opto_effective_gain_windows.csv` (input/output levels and UAD vs APEX gain errors for each window) and `opto_recovery_comparison.json` (0.05/0.1/0.25/0.5/1/2/5/10/15-second recovery-shape differences). All PCM must be aligned to the same sample timeline BEFORE invoking the comparison; the script rejects mismatched frame counts. Audio waveforms are NOT uploaded to GitHub automatically. Test the diagnostic without any UAD installation via `python Scripts/opto_reference_dynamics.py self-test` (synthetic identity, constant-gain-offset and corrupt-length checks).

**Gate:** do not fit tube/T4/transformer constants or use "1:1" terminology without measured UAD/real-unit reference render and source provenance. The research circuit remains separate from the production APEX insert while validation is incomplete.


## 6. Electrical-stage experiment (requires golden data before adoption)

* `ApexAnalogCircuitResearchCore.h`: a **12AX7-like plate voltage** stage, **12BH7-like cathode follower**, with independent static triode current-law fits. Plate/cathode voltages are solved by bounded bisection at initialisation and precomputed into fixed-size lookup tables. The gain law is normalized near silence so user-level matching does not conceal differences in nonlinear waveshape. All B+, load, grid bias, mu and current constants are provisional hypotheses, **not official LA-2A BOM measurements**.
* `ApexTransformerResearchCore` (same header): causal frequency-limited input/output RL approximations, NOT saturation, winding capacitance, magnetic memory, transformer stage calibration or UA DSP recreation.
* `ApexOptoCircuitPathResearchCore.h`: connects two linear transformer models with a pre-cell 6AQ5-like driver controlling the T4 optical shunt, then experimental 12AX7/12BH7 stages. Left/right electrical models are state-independent, but both channels share the stronger-side T4 sidechain. This is a **research fixture**, not the DAW's shipping audio path.
* `Tests/Standalone/ApexAnalogCircuitResearchTests.cpp` and `ApexOptoCircuitPathResearchTests.cpp` assert mathematical monotonicity, bounded/fixed-rate behavior, mono/stereo, dry/bypass, continuous sample clock and no audio-thread allocation. GREEN means only **structural** operation.

**Engineering gate before merging native audio:** fit T4, both transformers and tubes separately with licensed UAD Gray outputs + sourced component measurements; publish residual/THD/IMD/frequency-response/attack-release error plots; establish realistic 48k/64-sample CPU and anti-aliasing/oversampling budgets; then compare blind, level-matched singing performances from Apollo. Never silently swap the native processor DSP for uncalibrated research code.


### 6AQ5 beam-power optical-drive stage — research only (2026-10-13)

`Apex6AQ5DriverResearchCore.h` now implements a normalized, **uncalibrated** 6AQ5-inspired beam-power transfer from sidechain drive to EL excitation. It derives DC current from plate/screen supplies, plate resistance, cathode feedback and grid bias using a bounded 46-step solver in `prepare()`. The resulting stage is precomputed into a fixed 2049-point lookup table and processed through a causal rectifier envelope with separate attack/release rates. The amplifier's real 6AQ5 AC waveform, EL matching transformer, plate/screen load network, 6AQ5 datasheet curves and spectral nonlinear distortion are **not reproduced** yet.

`ApexT4LevelerResearchCore.h` opts into this stage with `use6AQ5Driver=true`; the default remains `false` to preserve baseline experimental measurements. `ApexOptoCircuitPathResearchCore.h` explicitly enables it in the full standalone chain. The working APEX native insert (PR #103) remains entirely unchanged.

The driver has Windows MSVC + Linux sanitizer gates for dark/light transfer, monotonic DC current, rate-consistent LED/EL envelope, recovery, buffer invariance, invalid-calibration sanitization and long-silence denormal suppression. These are **functional math tests, not sonics/1:1 proof**. No assumption should be made that a 6AQ5 is an ordinary 12AX7 preamp triode.


## 7. Optional x4 oversampled magnetic-color experiment (2026-10-13)

`ApexMagneticTransformerResearchCore.h` adds a **phenomenological** magnetic-color transfer to the *independent research pipeline*. It combines the existing provisional source/copper/load R/L transfer with bounded nonlinear transfer, a slowly varying flux-history term and optional asymmetry. It **does not** solve a physical B-H curve, model core saturation current, magnetic hysteresis, eddy current losses, real transformer laminations or measured UA/Teletronix parameters.

The nonlinear transfer runs inside a 4x FIR oversampling island, with **65-tap Blackman-windowed interpolation and decimation**. Coefficients and phase tables are built once in `prepare()`; there are no heap allocations/locks or FIR design work during `process()`. x1 is also available **for scientific alias comparison only**.

### Verified and unverified

- `ApexMagneticTransformerResearchTests.cpp` drives a 10 kHz tone at 48 kHz native rate. A nonlinear third harmonic above Nyquist folds back to 18 kHz at x1. The x4 path must measurably reduce that folded component relative to x1. A reduction is **not** synonymous with alias-free operation, and no "1:1" spectral profile is asserted. Local GCC test (development machine, not target Windows DAW) reported folded-component amplitude around 0.076 (x1) versus 0.000033 (x4); GitHub MSVC/Linux numbers and real vocal material may differ.
- The 4x interpolation/decimation FIR **adds 16 samples of explicit algorithmic delay per transformer** (not zero-latency). `ApexOptoCircuitPathResearchCore::prepare(sr, true)` opts into separate x4 input/output models for each audio channel, **adds 32 samples total**, and inserts a 32-sample delay into the dry/bypass branch to preserve mix alignment. The `latencySamples()` instance method reports 32 in this mode and 0 in the baseline research mode. Fixed delay does **not** include frequency-dependent R/L phase or host/plugin roundtrip latency.
- The older working **native APEX Opto 2A PR #103 remains unchanged**. No host latency/PDC registration, production x4 toggle, or automatic preset upgrade has been attempted. The *research* chain's triode waveshapers and 6AQ5 optical sidechain have their own spectra; isolated magnetic filtering does not establish that the entire compressor is alias-free.
- A CI smoke benchmark processes 2 seconds of x4 mono audio and prints wall-clock time, but is **not** an acceptable real-time CPU percentage, worst-case ISR deadline test or audio drop-out assurance. Test actual DAW 48k/64 and 48k/128 at multiple channel counts and with competing VST3s before release.

### Acceptance gates for any native integration

1. Fit input and output transformer frequency/phase and saturation independently to legally captured UA Gray renders and/or measured physical reference components. Treat the model as *original* electrical-inspired work.
2. Repeat spectral checks for 44.1/48/96/192 kHz, 1k–20k sine sweeps, third/higher harmonics and intermodulation, with varying asymmetry and flux memory.
3. Benchmark dual-channel 4x + T4 + tube stages as an entire host instance at 48k/64 and 48k/128; verify no allocations/locks and keep control automation stable.
4. If adopted: explicitly register correct plugin latency/PDC, align dry/parallel/host bypass, compensate screenshots and offline bounces, and ensure parameter state migration. 32 samples in this *particular* research configuration are not an unconditional shipped latency promise.
5. Compare level-matched real vocals through the licensed UAD Gray and APEX, including slow recovery and harmonics, before characterizing it as an authentic LA-2A sound.


## 8. Host latency boundary and x4 integration hold (2026-10-13)

The current working APEX native Opto (PR #103) calls `setLatencySamples(0)`
in `ApexOptoLevelerProcessor::prepareToPlay()`; that is correct for its
current standalone zero-FIR-delay algorithm, **not** for this separate x4
research path. Do not silently transplant x4 DSP while keeping that report.

Code-level host boundary audit (main as seen 2026-10-13):
- `PluginInstanceCore::getLatencySamples()` obtains the JUCE processor's
  `getLatencySamples()` or sandbox effective latency.
- `PluginChainCore::totalLatencySamples()` sums slot reports; APEX
  `AudioEngine` publishes chain-latency snapshots and owns PDC lines.
- Crucially, `PluginChainCore` has a **separate per-slot wet/dry mix** and a
  **separate chain-output wet/dry mix**, implemented with
  `PluginSlotMixCore::apply(dryView,wetView,...)` and
  `PluginSlotMixCore::apply(chainInputBuffer_,...)`. These snippets do not
  independently demonstrate that their dry buffers are latency-aligned.
  The x4 research DSP's own dry/bypass delay does **not** automatically fix
  the outer host slot or chain parallel dry path. External mixing must be
  validated or corrected BEFORE x4 can become a native insert.
- The low-latency live-monitor PDC bypass mode removes some *graph*
  compensation, but it cannot remove the intrinsic 32-sample transformer FIR
  delay. At 48 kHz, 32 samples are approximately 0.667 ms of additional
  algorithmic delay, on top of interface and other DSP latencies.

**Release-blocking integration matrix** (all currently unverified inside a
real APEX session):

1. Prepare-time JUCE latency equals the selected FIR mode. Assert 32 in x4;
   0 only in the baseline, after preparation and session restoration. Latency
   changes must be published on the non-audio control thread and cause PDC
   recomputation, never graph allocation in the callback.
2. Render a transient through both a wet insert and an unaffected parallel
   path, first with no slot/chain mix, then slot 50% mix, chain 50% mix, 0%
   mix and bypass. The dry reference must be delayed *once* and match the wet
   path latency; no unaligned dry impulse at sample 0 when wet appears near
   sample 32. Verify 64/128 sample block boundary crossings and stereo.
3. Check external sends and routing buses independently of local compressor
   mix, including latency propagation, export, freeze/bounce, save/reload and
   undo/redo after changing plugin order.
4. On armed/live-monitored tracks, check Full/Reduced/Bypass PDC modes and
   verify user-visible plugin algorithmic delay remains documented. Validate
   Apollo monitoring with all recording plugins active.
5. Real 48k/64- and 48k/128-sample Windows x64 host benchmarks (per-instance
   callback P99/max, dropout counter, competing Auto-Tune/EQ/VST3, CPU
   contention, dynamic automation, release configuration) are mandatory.
6. Only AFTER all host gates pass, consider an explicit prepare-time
   experimental x4 quality option and stable session migration. Do not
   activate or auto-upgrade the native Opto silently.

**Separate optimization note:** A bounded 4097-entry tanh curve is built
on the non-audio thread and shared across instances; a fixed interpolation
replaces two `std::tanh` calls per oversampled magnetic tick. Curve error
is checked against exact math, and whole-path quality remains research only.
CI standalone CPU percentages are advisory, not Apollo/DAW safety approval.
