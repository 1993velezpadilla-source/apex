# APEX Opto 2A — Golden Reference Capture Protocol (2026-10-13)

**Product rule:** Exactly **one** APEX Opto 2A, tuned first against the artist's licensed UAD LA-2A **Gray** for sustained and dynamic lead vocals. Silver and LA-2 are separate *research comparators*, never separate APEX modes.

**Current status:** The physical-T4 research engine and sidechain/attenuator audio path are **experimental and are not connected to the APEX native plugin in PR #103**. The functional tests prove safety and routing, **not** any 1:1 match. The APEX plugin still uses a simplified envelope DSP. Tubes, transformers and real R37 passive components remain to be implemented/calibrated.

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
2. 12AX7, 12BH7, 6AQ5 nonlinear triode/pentode plate characteristics, bias, impedance, tube and supply transformer load lines and the amplifier's feedback network.
3. T1/T2 equivalent magnetic/RC model with sample-rate-safe oversampling/anti-alias controls for nonlinear saturation, **not** a single output tanh.
4. Reference calibration of photoresistance (separately dark, illuminated, history), panel electrical transfer and independent meter CdS response.
5. Exact host calibration, oscilloscope/spectrum waveforms, gain-reduction output meter mapping and exposure tests; validated on real user vocals with headphone monitoring and Apollo latency.

**Hard stop:** A PR108 green result means only the research code compiles and passes functional invariants. It must never be represented as a released LA-2A clone until the measured reference and native host integration gates pass.


## 5. Time-resolved recovery and gain-transfer comparison

The separate original diagnostic \`Scripts/opto_reference_dynamics.py\` analyzes **dry**, licensed UAD, and APEX experimental renders in fixed-size, nonoverlapping windows, using each output's RMS relative to the SAME dry input. It computes dynamic transfer curves rather than inferring T4 behavior from a single loudness number. The golden recording fixture already provides the relevant segments (including a 12-second exposure and 16-second low-level recovery probe).

> Do not confuse output gain with GR. The tool records the **effective total gain through each signal chain**, not the real hardware's internal photoresistance or true VU GR. The recovery shape report subtracts each model's late-period output reference independently so that differences in constant output gain do not masquerade as differences in release shape.

Run once the three captured exports exist (UAD proprietary DSP is NOT executed by the script):

\`\`\`powershell
python Scripts/opto_reference_dynamics.py analyse \
    --manifest "$env:TEMP\apex-opto-golden\apex_opto2a_reference_manifest.json" \
    --dry "$env:TEMP\apex-opto-golden\apex_opto2a_reference_input_48k_pcm24.wav" \
    --uad "uad_gray_reference_48k_pcm24.wav" \
    --apex "apex_opto_response_48k_pcm24.wav" \
    --output "opto_measured_reports"
\`\`\`

Outputs: \`opto_effective_gain_windows.csv\` (input/output levels and UAD vs APEX gain errors for each window) and \`opto_recovery_comparison.json\` (0.05/0.1/0.25/0.5/1/2/5/10/15-second recovery-shape differences). All PCM must be aligned to the same sample timeline BEFORE invoking the comparison; the script rejects mismatched frame counts. Audio waveforms are NOT uploaded to GitHub automatically. Test the diagnostic without any UAD installation via \`python Scripts/opto_reference_dynamics.py self-test\` (synthetic identity, constant-gain-offset and corrupt-length checks).

**Gate:** do not fit tube/T4/transformer constants or use "1:1" terminology without measured UAD/real-unit reference render and source provenance. The research circuit remains separate from the production APEX insert while validation is incomplete.
