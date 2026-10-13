# APEX Native Compressors — evidence-based audit and release gates
Date: 2026-10-13. Owner: APEX. Implementation branch: `feature/apex-native-compressors-audit-opto-core-20261013`.

> **Honest status:** This branch begins a clean-room DSP prototype. The **Opto 2A** native JUCE insert and original themed experimental UI now exist on the branch and have been registered with the unified native format, but require full Windows compile/host tests before calling them usable. The **Prism** product is not yet implemented. No calibrated 1:1 result, listening comparison, public VST3 or production release is claimed.

## Decision: ONE APEX Opto 2A and ONE separate modern compressor; share infrastructure, validate sequentially

**Product A — APEX Opto 2A** first, **ONE product, ONE musical identity, ONE DSP algorithm**. Product decision confirmed by the artist 2026-10-13: do NOT ship separate Gray, Silver and original LA-2 models or disguise them as presets. The reference for fitting is initially **UAD LA-2A Gray** because the priority is natural lead-vocal leveling; use the user's licensed UAD Silver and original LA-2 as **independent comparative controls**, not as extra APEX products and not as targets to average together. An APEX design must earn its own stable sound from measured circuit behavior. No claim that any single unit is the canonical LA-2A: real T4 age, alignment, and tube/transformer tolerances vary. The user possesses UAD LA-2A plugins, but APEX does NOT have access to those binaries or reference audio yet.

**Product B — APEX Prism Compressor** second. Target is **FabFilter Pro-C 3** (current as of this audit, released Jan 2026), especially Clean + flexible sidechain workflow and the dynamic visualization desired for reverb/delay ducking. Full parity eventually entails ~14 styles and modern dynamic functions, not just generic attack/ratio/release. Do not copy proprietary code, visual assets, branding, presets, text or binary; implement independently and use APEX visual identity. No claim that exact proprietary implementation can be known.

**Shared host work:** native plugin registration, parameter ABI, preset + project roundtrip, undo, automation Write/Read/Touch/Latch, UI scales/touch, stereo/mono, sends/aux keys and sidechain source identity, snapshot isolation, sample-accurate smoothing, bypass, channel latency/PDC, 64/128 samples @48kHz performance, offline bounce determinism, no audio-thread locks or allocations. Sidechain must work across an arbitrary plugin/aux bus and saved project reload, not be hard-coded to a reverb/delay.

## Primary source audit (external references)

1. UA hardware history — program- and frequency-dependent optical T4, ~10 ms attack, first 50% of release in ~60 ms, remaining 50% in 1–15 sec dependent on program exposure:
   https://help.uaudio.com/hc/en-us/articles/215779663-A-History-of-the-Teletronix-LA-2A-Leveling-Amplifier
2. UA LA-2A collection manual — Peak Reduction controls level-trigger threshold and spans approx 0 to -40 dB, Gain is post-comp and up to +40 dB; marked 0–100 positions are arbitrary, not dB; rear Limit/Compress; Emphasis R37 affects the **detector**, reference alignment -12 dBFS for that particular UA implementation:
   https://help.uaudio.com/hc/en-us/articles/4419496124180-Teletronix-LA-2A-Leveler-Collection-Manual
3. Original LA-2A hardware manual — R37 FM side-chain pre-emphasis changes relative sensitivity to HF, not audible tone EQ. Real stereo hardware uses paired units and gain-reduction matching:
   https://media.uaudio.com/assetlibrary/l/a/la-2a_manual.pdf
4. FabFilter Pro-C 3 manual + current product spec — full style matrix, sidechain EQ six bands, control visualization, tempo/key/MIDI triggering, variable link, Dry/Mix 0–200%, max lookahead 0–20 ms, optional 32x oversampling:
   https://www.fabfilter.com/help/pro-c/intro/fabfilterproc3
   https://www.fabfilter.com/products/pro-c-3-compressor-plug-in
5. Feb-era hardware/plugin LA-2A comparisons and user experience:
   https://www.reddit.com/r/audioengineering/comments/18wqf2g/the_best_la2a/
   https://www.reddit.com/r/audioengineering/comments/1g9v284
   https://www.reddit.com/r/audioengineering/comments/1l53zd5
   https://www.sweetwater.com/store/detail/LA2A--universal-audio-la-2a/reviews
6. Pro-C 3 experience / workflow caveats: feature richness can increase user effort; sidechain visibility and workflow are praised, presets / price criticized:
   https://www.reddit.com/r/audioengineering/comments/1qk1mlu/do_you_think_the_new_pro_c3_is_a_good_replacement/
   https://musictech.com/reviews/plug-ins/fabfilter-pro-c-3-review/

### Feedback synthesis (anecdotes, NOT frequency-weighted scientific polling)

* Users value LA-2A for musical level riding on vocals/bass; the attack is **not** an 1176-style instantaneous transient catcher.
* Hardware owners report differences in release *feel*, tone, high-frequency smoothness and dynamic depth; some report minimal hardware/plugin difference. Claims are conflicting, so **blind level-matched tests** decide progress, not reputation or a vintage skin.
* Silver is frequently described as faster/forward; Gray as rounder/mid-speed; early LA-2 as slow. User comparisons are not measurement truth.
* Pro-C 3 earns value from clear gain-reduction + input/output visualization, strong internal/external sidechain filtering and accessible digital Clean behavior; some users dislike option overload and cost.
* Prioritize controllability on reverb/delay ducking: aux sends → dedicated external key → sidechain-only filtering → envelope/delayed-release visualization. The audible reverb/delay must not be accidentally high-passed because the **key** was filtered.

## Existing APEX reuse

- `Source/DynamicsCore/ApexDynamicsCore.h`: peak/RMS, generalized knee/ratio, 2-path key API. It does **not** model T4 multi-stage recovery or tube/transformer harmonics.
- `Source/DSPCore/DynamicsDetectorCore.h`: another feedforward utility; do not accidentally treat either generic 1-pole detector as LA-2A 1:1.
- `Source/G10Core/` and `Source/G10UI/`: review as UI/parameter/CPU test integration patterns, but do not shoehorn dynamics into equalizer 10-band filters.
- `Tests/Source/PluginHost/SidechainRoutingRegressionTests.cpp`: host-level test starting point. Must prove aux send, reload, latency and arbitrary source plug-ins.

## Implemented here (prototype only)

- `Source/CompressorCore/ApexOptoLevelerCore.h`: fixed-storage native original optical-inspired DSP with shared stereo gain reduction, separate frequency weighting in key detector, multi-stage recovery with exposure memory, simplified soft knee and variable compression/limiting, optional asymmetric color stage, zero-latency dry/wet parallel blend, smooth mix/output gain.
- `Tests/Standalone/ApexOptoLevelerCoreTests.cpp`: six portable suites. Test harness gate compiles and executes on Linux CI. **DO NOT treat green checks as analog 1:1 evidence**.

### Known intentional approximation limits

- LA-2A optocell electro-luminescent physics, photoconductive memory and its non-ideal static characteristic not yet measured or physically fitted.
- Tube transfer / bias, transformer hysteresis, tone frequency response / phase, harmonics + intermodulation, startup/turn-on and VU calibration not modeled accurately. The color function is a deliberately simple placeholder and should not be promoted as authentic circuitry.
- The adjustable Emphasis filter is a one-pole approximation, not a measured R37 circuit.
- Attack/release 10 ms/~60 ms/seconds are nominal anchors only. A Green functional test does not establish sample-accurate matching to UAD Gray or physical hardware.
- No high-order oversampling or anti-aliasing for saturation yet. No external sidechain bus on Opto, parameter-interpolation audit, robust plugin host/session reopen test, VU calibration, reference WAV fixtures, or listening audit. A prototype native wrapper and editor exist, not a shipping-quality UI.

## Reference capture and acceptance gates (MUST complete before 1:1 language)

0. **Golden reference provenance**: initial sonic target UAD LA-2A Gray, with licensed Silver and original LA-2 compared as controls but never exposed as extra APEX modes. Record exact licensed plug-in version/format, settings and internal level alignment; transport, clock/converter loopback and latencies compensated as necessary. The artist has UAD LA-2As; requesting matched offline output renders or providing an approved local test harness is needed for comparison. No claim of remote plugin access. Document subjective vs measured.
1. **Static curve**: broadband and 30/80/100/300/1k/3k/10k/15k sine/RMS tests, Peak Reduction [0,25,50,75,100], Limit/Compress, Gain; fit knee and level-dependent ratio to measured gain reduction within explicit error budgets.
2. **Time behavior**: 1 ms to 20 s bursts including previous state/history, release first half and long tail, multiple program densities and input bandwidth, alternating bursts; measure envelope timing at multiple sample rates. Compare both peak reduction *and* output waveforms.
3. **Nonlinear stage**: THD+N vs level/frequency, harmonic spectra, CCIF/IMD, frequency response, channel imbalance, noise floor, aliasing at 44.1/48/96 kHz; genuine analog unit background hum/noise modeled optionally, default quiet for monitoring.
4. **Level-matched blind A/B** across vocal sustained/rap/plosives/bass/drum buses, avoid placebo and loudness bias; match LUFS ±0.1 dB or better and test multiple playback systems.
5. **Native DAW**: registration, GUI with APEX controls and large readable VU, parameter persistence & automation, mono/stereo, stereo link; parallel wet/dry 0–100%, send-bus parallel and Null/phase/PDC check; CPU budget at 48k/64 and 48k/128; monitor during recording; soft bypass without click.
6. **Windows CI**: Debug and Release, compiling the whole APEX executable and tests on real Windows; JUCE builds, source policy, exception/no allocation gates. Reference measurement evidence attached.
7. **User vocal test** with actual Apollo input and the requested vocal workflow. Only after this mark a release candidate; not after one unit-test green.

## Pro-C 3 comparator parity board (not implemented)

**Phase B1 — core transparence**: Clean / Versatile / Smooth, feedforward peak + RMS, ratio, knee up to 72 dB, variable attack/release, range, lookahead and PDC, auto-release/threshold, adaptive stereo linking, M/S, hold, host automation, wet/dry-to-200% semantic.
**Phase B2 — key/routing**: mono/stereo external key on any track/send/reverb/delay, six-band filter exclusively on detector (including mid/side filters), GUI audition key, key meter, sidechain diagnostic and project reload.
**Phase B3 — visuals**: scrolling trace of raw/detected/output/GR signal, editable transfer-curve display, readable responsive GUI 640px through desktop sizes (touch safe), scopes stable across buffer sizes.
**Phase B4 — advanced styles**: original implementations of 14 algorithm archetypes, own coloration modes, oversampling up to measured CPU-compatible cap, accurate bypass and latency.
**Phase B5 — interoperability**: production format native and optional VST3 wrapper (if desired), APEX preset system, A/B undo/redo, multichannel layouts as supported, Win/macOS parity, official golden listening.

## Initial release verdict

**Opto prototype: DSP+native wrapper+GUI implemented on experimental branch. Portable DSP CI GREEN on initial commit; full native Windows host gauntlet PENDING as of this document edit.**
**1:1 LA-2A: NOT VERIFIED.**
**Transparent Pro-C 3-like compressor: NOT IMPLEMENTED.**
