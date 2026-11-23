# APEX Tune Live: third singer / octave cross-check (2026-10-13)

## Do not equate detector output with audible errors

In the third real vocal take, **Vocadito #2** (annotated median F0 ~348.565 Hz), the earlier human F0 report counted **20 high-register octave mismatches** for shipping HQ40 versus the unaligned original. That count is **an independent YIN algorithm's classifications of two different waveform outputs**, not a trusted annotation showing that 20 semitones or note jumps occurred in the audio. The original methodology also evaluated raw and wet at the same output sample index, ignoring the pitch processor's **1764-sample PDC at 44.1 kHz**. F0 measurement can be confused by strong upper partials, window/transient behavior, and phase relationships.

The apparent anomaly is concentrated near **5.1–5.3 s** of Vocadito #2. A separate independent high-resolution spectrum of the *same real recordings*, comparing source at time **t** against rendered wet at **t + PDC**, showed:

- Acoustic fundamental around **410–417 Hz**, with strong natural overtones near **830 Hz** and **1245 Hz**.
- Spectral amplitude close to **207 Hz** (one octave below) stayed tiny: in test frames near t=5.10, 5.17, 5.22 and 5.25, the ratio of half-frequency magnitude to the 415 Hz fundamental was around **0.3–1.3%** for dry, HQ40 and 18/12ms research profiles, depending on exact slice/window.
- Independent YIN on processed audio can nonetheless return a spurious half-frequency ~208 Hz near some frames. It is not acoustically evidence of a 208 Hz sung/corrected fundamental.
- Spectral magnitude is *not* a complete subjective/perceptual test, so this confirms only the **checked passage**, not universal absence of octave defects.

To turn this into a reproducible guard, the repo now contains
`Tests/TuneLive/test_human_octave_spectral_audit.cpp`. This evaluates the actual PCM24 files of dry / HQ40 / 18ms / 12ms, compares output against the original after exact profile PDC in sample units (1764 / 794 / 529 at 44.1kHz), and checks energy in the **199–216 Hz** half-frequency band versus **398–432 Hz** fundamental band for four known examples. The time-window basis is 4096 samples, Hann-windowed, with a frequency sweep so normal vibrato need not look like a missing fundamental. It is deliberately a **local fixture guard**: strong 207 Hz components should alert us, but no conclusion about all other registers is asserted.

## Test tone, latency, and musical decisions

| Processor | Windows APEX default? | 44.1kHz fixed shifter PDC for this excerpt |
| --- | --- | ---: |
| HQ40 | **Yes: unchanged, singer-approved** | 1764 samples, 40ms |
| Experimental 18ms | **No, research WAV only** | 794 samples, ~18.0ms |
| Experimental 12ms | **No, research WAV only** | 529 samples, ~12.0ms |
| Period-guided causal research | **No, independent code path** | Not fixed / unresolved |

PDC excludes audio interface, ASIO buffers, host routing and monitoring roundtrip. Lower window lengths do not guarantee identical naturalness: the 12ms low-register output reached **0.984 linear peak** (less than 0.14 dB headroom to full scale) in the unnormalized offline test. Preserve the existing HQ40 engine and GUI until explicitly approved to change them.

## Longer A/B clips to judge by ear

From the downloaded GitHub Actions 3-voice artifact `11825855547`, two 46.55-second listening clips were assembled **locally** (not committed to public GitHub) for independent high/low-register singers. Each plays **four 11-second versions**, in the same order:

1. **Original dry**
2. **Shipping HQ40**
3. **Research 18ms**
4. **Research 12ms**

The segments use identical source musical times **2–13 s**, output shifted by the exact known profile PDC, RMS matched to the dry track with <=0.98 linear peak, separated by 0.85 s silence, and faded 8ms at each edge. MP3 encoding is for ease of listening; official regression comparisons must be made with original 24-bit PCM audio. Neither 18ms nor 12ms is a production feature; no user recordings are uploaded to GitHub.

## Practical promotion rule

Do not modify the production HQ40 algorithm based on *pitch-cent statistics alone* or *unaligned YIN octave flags*. Changes require a spectral/original-singer check, time/level-aligned long vocal listening, monitored CPU/memory tests, and hands-on ASIO singing evaluation before release. The original Vox #24 HQ40 bit-exact WAV golden remains mandatory and protects against accidental changes while the artist approves the current audio.

[GitHub real-singer artifact with raw separate WAV outputs and dataset license](https://github.com/1993velezpadilla-source/apex/actions/runs/38417767125/artifacts/11825855547)
