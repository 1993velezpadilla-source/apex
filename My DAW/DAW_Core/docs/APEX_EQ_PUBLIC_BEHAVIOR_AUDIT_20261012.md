# APEX native flagship EQ — public behavior parity audit (2026-10-12)

Target: deliver comparable professional functionality **inside APEX** using a
clean-room DSP/UI implementation. Public reference: FabFilter Pro-Q 4 and the
Pro-Q 4.10 multi-plug-in instance-list extension. This is a **feature inventory,
not proof of sonic equivalence**, and is not authorization to copy proprietary
source, preset libraries, branding, screenshots or visual trade dress.

Primary public documentation:
- https://www.fabfilter.com/help/pro-q/intro/fabfilterproq
- https://www.fabfilter.com/help/pro-q/using/overview
- https://www.fabfilter.com/help/pro-q/using/dynamic-eq
- https://www.fabfilter.com/help/pro-q/using/spectral-dynamics
- https://www.fabfilter.com/help/pro-q/using/instance-list
- https://www.fabfilter.com/help/pro-q/using/eqmatch
- https://www.fabfilter.com/help/pro-q/using/processingmode
- https://www.fabfilter.com/help/pro-q/using/surround
- https://www.fabfilter.com/help/pro-q/using/analyzer
- https://www.fabfilter.com/help/pro-q/using/output
- https://www.fabfilter.com/news (Jan. 15, 2026 multi-plugin extension)

## Feature gate matrix

| Professional behavior | APEX code observed in EQ branch | Missing gate |
| --- | --- | --- |
| 24 editable, automatable bands | PRESENT | Host reload/editor acceptance |
| Bell / shelf / cut / notch / bandpass / tilt / all-pass | PRESENT | Extreme Q/shape numerical sweep |
| Continuous/fractional slope selection | PRESENT | Brickwall/96 dB/oct behavior matrix |
| Responsive graph, node gestures and touch | PRESENT | Multi-select/transforms and unscripted usability |
| Stereo + Left/Right + Mid/Side placement | PRESENT | Full stereo crosstalk and coefficient measurements |
| Dynamic EQ threshold/range/attack/release | PRESENT | Program-dependent behavior, release transients, high-Q |
| External-key and per-band dynamic detector filtering | PRESENT | Sidechain listen / integrated real route |
| Spectral dynamics within selected band | MISSING | Independent FFT/windowed subband DSP, latency/PDC, tilt/density |
| True Natural Phase mode | MISSING | Independent analog-phase design and measured response |
| Minimum/zero-latency + Linear Phase mode | PRESENT | Group delay/kernel transition/pre-ring validation |
| EQ Sketch freehand fitting | PRESENT ON BRANCH | Complex gesture fidelity and band-fitting validation |
| Character nonlinear coloration | PRESENT ON BRANCH (Pure/Velvet/Heat) | Aliasing/level-matching/thd sweeps at 44.1-192k |
| PRE/POST analyzer + transient band audition | PRESENT | External analyzer, pitch identification, configurable resolution |
| APEX Spectrum Grab peak preview | PRESENT ON THIS CHILD BRANCH | Windows CI + music test, not FabFilter's exact detector |
| Copy/paste all per-band hosted settings | PRESENT ON THIS CHILD BRANCH | Windows CI + cross-session verification |
| EQ Match with reference audio | MISSING | Capture temporal averaging, optional external/file ref, bounded fitting |
| Cross-instance panel and collision analysis | MISSING | DAW track registry and immutable snapshots of instance graphs |
| Cross-plugin instance control (Pro-C 3/Pro-DS/Pro-G) | MISSING | Native APEX multi-plugin host metadata and design contract |
| Output gain / pan / polarity / auto gain / scale | PARAMETERS/DSP IN EQ BRANCH | Editor, true loudness compensation and meters |
| A/B comparison; plugin-local undo/redo | MISSING | Transactional history tied to host gesture semantics |
| Piano overlay, scale labels and MIDI learn | MISSING | Exact snap behavior and input mapping |
| Surround/immersive speaker targeting up to 9.1.6 | MISSING | Host routing and per-layout channel contract first |
| Fullscreen / resize / resolution UX | PARTIAL | UI pass at DPI, portrait and tablet sizes |
| External VST3 / CLAP / AU / AAX distribution | OUT OF SCOPE FOR INTRINSIC APEX | Separate packaging, notarization and format validation |

## Required architectural ordering

1. Keep the native processor ID `APEX::ParametricEQ`, existing parameter IDs
   and state migration stable. New controls are append-only if they must be
   serialized or automated. GUI-only temporary previews are not parameters.
2. Finish branch merges and baseline Windows Debug+Release, RT/no-allocation,
   state, automation, 44.1/48/96/192kHz, 64/128/512/2048 block tests.
3. EQ Match: maintain explicit capture/reference state, detect empty frames,
   gather a meaningful multi-second average, normalize differing program levels,
   fit a bounded set of standard APEX bands; never alter the source audio.
4. Spectral dynamics: create a **separate** true STFT/spectral DSP path with
   fixed/preallocated scratch and explicit reported latency. Do not approximate
   frequency-selective triggering by changing one whole biquad band gain.
   Enforce phase-coherence, PDC, hard bypass, automation and DSP bounds.
5. Cross-instance host registry: ownership belongs to APEX RoutingGraph/host;
   UI reads immutable or lifetime-pinned snapshots. No GUI-to-audio locks,
   raw dangling plugin pointers, or audio-thread graph traversal.
6. Analog/Natural Phase: measure real independent magnitude + phase curves
   against the mathematical target at high-Q and Nyquist-edge extremes. Do not
   claim bit-level Pro-Q equivalence to an opaque proprietary algorithm.
7. Surround only after true APEX multichannel bus layout and per-speaker
   placement are implemented; never fake it with a stereo downmix.
8. Final professional acceptance: comparative null/magnitude/phase tests,
   oversampling/alias measurements for coloration, CPU budget, zero-click
   automation, sample-accurate latency reporting, multitrack session restore,
   Apple Silicon/macOS portability, accessibility and real listening reviews.

## Evidence status

The child PR implements Spectrum Grab and native clipboard code/tests, but
**no Windows CI or audio hardware GREEN evidence was available when this audit
was written**. The owner must accept and merge the parent flagship branch
before these changes can ship in an APEX installer. A 1:1 result, including all
undocumented/proprietary DSP choices, is not established by public manuals.
