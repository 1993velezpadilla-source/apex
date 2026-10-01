# APEX Plugin Engineering Playbook

| Field | Value |
|-------|-------|
| Document | Permanent engineering process (active playbook) |
| Status | Active — owned, versioned, reviewed with each plugin |
| Version | 1.7 (derived from G10, C4, Parametric EQ Phases 2-6, Dynamics Core Phase 4; updated 2026-08-15) |
| Applies to | Every future APEX audio plugin (DSP + GUI + format export) |
| Companion doc | `docs/APEX_G10_PLUGIN_POSTMORTEM.md`, `docs/C4_PHASE9_COMPLETION_POSTMORTEM.md`, and `docs/PARAMETRIC_EQ_PHASE2_EVIDENCE.md` |

### Provenance (2026-08-13 recovery)

- The G10 documentation originated in the dedicated historical worktree
  `Apex-g10-vst3` (branch `feature/g10-vst3`, tags
  `g10-windows-v1.0-final` / `g10-canonical-freeze`); commit `c1aaa8d`
  ("docs: add G10 postmortem and plugin engineering playbook") is the
  historical documentation commit.
- The three originals (`APEX_G10_COMPLETE_GUIDE.md`,
  `APEX_G10_PLUGIN_POSTMORTEM.md`, this playbook) were recovered into this
  canonical DAW documentation tree **byte-identically** (SHA-256 verified);
  the current DAW checkouts had failed to inherit them.
- C4 lessons were **appended additively** (§15 and the §4 rows); no G10
  section was rewritten. This file is THE canonical plugin engineering
  book — future plugins EXTEND it; no parallel book may be created.

## 1. Purpose, audience, and authority

A future coding agent must be able to answer quickly:

- What failed before, and what looked like a bug but was not?
- Which tool found the real cause?
- What should I test first?
- Which architecture must I preserve?
- When is an APEX plugin actually ready to freeze?

Authority order (never reversed):

1. Official external specification / SDK contract.
2. DAW Brain v7.0.0-alpha.1 interpretation (§4, §21, §27, §28, §34, §37, §41, §42).
3. Repository implementation.
4. Build artifact, runtime trace, test, and measurement evidence.

This playbook defines required gates. It references postmortems instead of
repeating their narratives.

## 2. Evidence labels and layers

### 2.1 Evidence classes (use inline on every claim)

| Class | Meaning |
|-------|---------|
| [GIT] | Immutable commit/tag/blob |
| [CODE] | Current tracked source contract |
| [TS] | Committed test source — coverage, not a passed run |
| [ARTIFACT] | Retained binary/PDB/module/metadata |
| [RUNTIME] | Observed run (retain raw output in an evidence package) |
| [TAG] | Annotated tag annotation — a claim, not a rerun |
| [USER] | Human-supplied (e.g., manual DAW confirmation) |
| [UNAVAILABLE] | Expected evidence that does not exist yet |

Brain semantic labels (Fact / Synthesis / Inference / Unknown /
Recommendation / Time-sensitive) apply to statements that could be mistaken
for normative facts. Do not conflate these classes with the repository's
`diagnostic`/`release` evidence grade.

### 2.2 Test layers are NOT interchangeable

| Layer | Purpose | Never claim |
|-------|---------|-------------|
| Direct processor/unit tests | Processor-owned contracts, deterministic DSP | Wrapper export, discovery, host behavior |
| Native integration tests | APEX format manager, chain, state, automation | External binary behavior |
| HostSmoke (packaged hosted binary) | Real `.vst3` discovery/instantiation/params/state/editor/lifecycle | Universal DAW compatibility |
| Format validators | Protocol conformance (pinned tools) | Musical or host compatibility |
| Representative real hosts | Actual DAW/version matrix | Can be inferred from HostSmoke |
| Exact published artifact | Reopen the final destination artifact by recorded hash | Tag text is a complete evidence package |

Each layer needs an explicit oracle: exact tool/version/command, OS,
architecture, sample rate, block size, mode, plugin hash, build identity.
Manual/perceptual results need explicit steps and expected results. A failed
or missing layer stays `Unknown`/`Blocked`; it is never silently omitted.

## 3. Roles

| Role | Responsibility |
|------|----------------|
| Patcher | Proposes and implements the smallest architecture-correct change |
| Validator | Runs the evidence; never certifies from compilation alone |
| Publisher | Freezes, hashes, tags, publishes transactionally |

Roles may be logical rather than separate people, but a change must not
promote itself from compilation to VERIFIED_FIXED in one step.

## 4. SYMPTOM → CHECK THIS FIRST

| Symptom | Check this first |
|---------|------------------|
| Crash during shutdown/destruction | ASan/CDB: find the FIRST invalid memory write; destruction is where corruption is detected, not where it started |
| Native works but VST3 fails | Wrapper/host contract first: declared max block, bypass parameter mapping, parameter IDs, state wrapper format |
| Many-instance crash | Isolate audio / editor / params / destruction dimensions separately before blaming instance count |
| Test gives impossible frequency response | Verify signal, layer, operating level, retained state, and FFT method (impulse vs steady-sine, settle time, bin resolution) |
| Bit-exact test fails | Use fresh identically-settled instances; check long-tailed state (e.g., DC blocker tau ≈ 79.6 ms) |
| Analyzer lags or shows stale data | Compute producer vs consumer throughput (frames/s); inspect FIFO freshness policy (drop-oldest vs drop-newest) |
| Duplicate host parameter | Inspect JUCE special parameter mapping (getBypassParameter, hidden-parameter surface) |
| GUI looks cheap | Open the real editor at production size in a real host; run a visual-critic review; keep the screenshots |
| Allocation checker fires in the audio path | Scope the checker strictly around processBlock; move host text/control-plane work out of the measured scope |
| Stale-looking test results | Rebuild first; verify binary hash + source commit + clean/dirty flags in the evidence manifest |
| Oversized-block crash | Chunk at the processor boundary; verify bit-equivalence to legal split blocks; never exceed the host's negotiated maximum in harnesses |
| Legacy parameter appears or disappears | Check the exposure policy: Native 15 vs external 13; state IDs must survive round-trip and stay DSP-inert |
| Harmonic bin looks like alias | Harmonic bins are not aliases; exclude real harmonics AND their mirror bins before measuring alias floor |
| Direct-engine IR test grows block after block while the processor path is stable | JUCE AudioBuffer clear-state trap: a `clear()` can early-return after raw-pointer DSP writes; explicit zero-fill (C4-L1) |
| Smoother stalls just below target (e.g., 5.9998 instead of 6.0) | Float stagnation: per-sample step below half an ULP; snap when `next == current` (C4-L2) |
| OPTIONAL sentinel collides with a legal value | Reserve a unique sentinel range (e.g., norm 0 = OFF; real values in (floor, 1]) (C4-L4) |
| Small parallel-EQ boosts appear to have "no bandwidth" | Parallel boost never returns to 0 dB; measure half-excess width, not −3 dB from peak (C4-L8) |
| BOTH-mode analyzer reads a collapsed transfer (e.g., +2 dB instead of +6 dB) | Shared per-stream smoothing/filter state couples histories; give every logical stream its own state (C4-L20) |
| Native built-in plugin never instantiates through the format manager | JUCE format manager keys formats by NAME and drops same-named formats; use one unified family format (C4-L25) |
| Optional panel hides primary controls when opened | Extensions must add height/space; never compress or remove the primary interface (C4-L26) |
| Fresh captures/renders look stale or wrong | Cloud-synced placeholders can serve old bytes; write review material to a non-synced directory and verify hashes/dimensions (C4-L30) |

## 5. Standard APEX plugin pipeline

Each step has an exit condition. Do not skip steps; skipping requires a
recorded exception (§9).

| # | Step | Exit condition |
|---|------|----------------|
| 1 | Musical/product purpose | Written one-paragraph identity: what the plugin is, who it serves, its signature behavior |
| 2 | Architecture research | Brain-consistent design with chosen filter/topology families and licensing notes |
| 3 | DSP prototype | Scalar reference that is bit-deterministic per input |
| 4 | Parameter/state contract | Stable IDs, ranges, defaults, state schema version, exposure policy (Native vs external) |
| 5 | Realtime safety | No allocation/locks/I-O in the callback; preallocated scratch; denormal flush; bounded SPSC taps |
| 6 | Characterization tests | Per-band/per-parameter contracts (center gain, shape, cross-rate stability) |
| 7 | Numerical safety | Finite outputs, silence handling, no feedback loops in tests, stateful bounds |
| 8 | Sample-rate tests | Contract holds at 44.1/48/96/192 kHz; near-Nyquist safety |
| 9 | Block-size tests | Bit-equivalence across block sizes; oversized-block chunking proof |
| 10 | CPU/multi-instance | Allocation audit scoped to the callback; 1/4/8/16 instances finite |
| 11 | Native integration | Processor loads, state round-trips, automation works in the APEX host |
| 12 | GUI implementation | Editor at production size; local-coordinate painting; gesture/wheel/double-click handling |
| 13 | Real-editor review | Actual-size screenshots in a real host; visual-critic pass; evidence retained |
| 14 | Analyzer/meter truth | Post-processing tap, power-sum not sample-sum, freshness throughput math, stage-correlated timing |
| 15 | State/automation | Save/restore tolerance; legacy state preserved and inert; automation→GUI index mapping |
| 16 | Debug/Release gates | Both configurations build; both test binaries pass with zero failed assertions |
| 17 | Canonical freeze | Tag the integrated Native baseline; record hash and contracts |
| 18 | Thin VST3 target | Entry point returns the canonical processor; no delegating processor; exposure policy applied |
| 19 | HostSmoke | Real discovery/instantiation/identity/params/state/editor/blocks/mono-stereo/instances/lifecycle; declared max respected |
| 20 | ASan/CDB when needed | First-cause diagnostics; raw reports archived |
| 21 | Real external-DAW validation | User opens the exact published bundle in a real DAW; name/version recorded |
| 22 | Final Release SHA | Module hash recorded and verified against the exact built bundle |
| 23 | Publication | Transactional stage/backup/swap; published hash re-verified; rollback retained until smoke passes |
| 24 | Git commit/tag | Clean milestone commit + annotated tag recording contracts and hashes |
| 25 | Postmortem/playbook update | New bugs appended; new reusable steps promoted; this playbook version bumped |

## 6. HostSmoke — the reusable strategy

HostSmoke is a JUCE console host harness that drives the **actual published
bundle** through `AudioPluginFormatManager::createPluginInstance`, never the
processor directly. It must cover:

- real VST3 discovery (exactly one description; name/manufacturer/effect)
- instantiation and identity (format, category, uid)
- parameter enumeration: exact public count, names, stable IDs (verify
  converted VST3 IDs), absence of legacy controls
- mono and stereo layouts with finite audio
- deterministic audio: neutral output finite; each band changes response;
  Input/Output/Bypass change gain
- state save/restore into a fresh instance
- legacy-state round-trip: modify only the raw processor `ValueTree` inside
  the JUCE component-state wrapper, preserve the opaque suffix, resave,
  verify DSP bit-inertness
- editor open/close/reopen
- block sizes ≤ declared maximum (and a separate direct-processor test for
  beyond-maximum chunking)
- multiple instances (1/4/8/16)
- repeated lifecycle (e.g., 8 instances × 32 cycles)
- clean destruction (editors before instances; `releaseResources` before
  reset) and clean process exit

**Critical rule: direct processor robustness tests and VST3 host contract
tests are NOT the same layer.** Never intentionally violate a host API
contract (e.g., submitting a block larger than the declared maximum) to test
processor robustness — do that in the direct layer instead.

## 7. Diagnostics workflow

- ASan and CDB are part of the APEX plugin workflow, not emergency tools.
- On any crash: reproduce, identify the first invalid access (not the later
  allocator check), record exception code/address/first APEX frame, classify,
  find root cause vs symptom, then propose the smallest correction.
- Realtime paths emit only fixed-size, bounded, nonblocking facts; formatting
  and persistence belong to a non-realtime drain.
- Archive raw evidence: ASan output, CDB transcripts, HostSmoke stdout,
  screenshots, manifests, hashes. A tag annotation is not an archive.
- Record measurement contracts: tap point, units, range, clock, alignment,
  environment, settle/discard windows.

## 8. Plugin-specific validation extensions

Future processors inherit the COMMON infrastructure but require their own DSP
tests.

**Compressor** — detector, attack/release, knee, ratio, gain-reduction
accuracy, level dependence, overshoot, stereo-linking, state retention.

**Delay** — circular buffer wrap, interpolation accuracy, tempo sync,
feedback stability, time-change transitions (no clicks), tail behavior.

**Reverb** — RT60, decay stability, denormal handling, tail CPU cost,
sample-rate scaling, diffusion/feedback matrix stability.

**Limiter** — true-peak (oversampled) metering, lookahead latency, ceiling
accuracy, release recovery, overshoot bounds, brickwall verification.

**Gate / De-esser / Saturation / Multiband** — gate: threshold/attack/
release/hysteresis/hold; de-esser: sidechain detection, range, only-sibilant
operation; saturation: level-matched THD, monotonicity, aliasing floor;
multiband: crossover phase alignment, band isolation, split/merge
bit-exactness.

## 9. Freeze, publication, and completion states

### 9.1 Completion states

| State | Meaning |
|-------|---------|
| PATCHED | Change applied, no evidence yet |
| VALIDATION_PENDING | Evidence collection in progress |
| VERIFIED_FIXED | Required gates green with archived evidence |
| BLOCKED | A gate cannot pass; record why and what is needed |

### 9.2 Freeze and tag policy

- Source freeze, build identity, artifact identity, and publication are
  separate events. A later source change is not still the earlier freeze.
- Use annotated tags pointing at immutable commits; record contracts and
  hashes in the annotation. Preferred: signed or attested tags.
- Rebuild after approval creates a new build identity if output changes.

### 9.3 Transactional publication

```text
complete bundle → staging sibling → verify staged files/hashes
→ move existing destination to backup → move staging into destination
→ verify published hash and bundle completeness → smoke the PUBLISHED path
→ only then remove the backup
```

- A failed copy/publish leaves the previous valid artifact in place.
- Metadata must never reference an artifact that has not completed upload and
  verification.
- Record: source commit, tag, published path, published SHA-256, parameter
  contracts, test results, and the manual DAW validation.

### 9.4 Exceptions

Any skipped gate requires a written exception with owner, reason, risk, and
remediation. Silent omission is prohibited.

## 10. Lessons that must survive G10

1. **Never retune DSP because one suspicious test failed.** Validate the
   test before modifying proven sound (G10-16, G10-17).
2. **Ghost crashes require first-cause diagnostics.** ASan/CDB; detection
   site ≠ origin (G10-30).
3. **ASan/CDB are part of the APEX plugin workflow.**
4. **Preserve stable state IDs** across persistence and exposure boundaries;
   hiding legacy state is not deleting it (G10-32).
5. **Real host validation matters** — manual third-party DAW confirmation is
   distinct from and complementary to HostSmoke.
6. **Real editor visual review matters** — production size, real host, keep
   screenshots (G10-25, G10-26).
7. **Analyzer/meter freshness matters** — throughput math before painting
   (G10-27).
8. **Performance and multi-instance behavior are product features** (G10-31).
9. **Freeze/hash/tag known-good milestones**; archive raw evidence.
10. **Do not let agents erase historical evidence after a fix** — commit
    absurd readings as warnings; append corrections, never rewrite.

## 11. Engineering memory update rules

- Every fixed bug appends to the owning postmortem: SYMPTOM / ROOT CAUSE /
  FIX / PROOF / LESSON, with evidence classes and exact file/line references.
- The playbook absorbs only reusable process lessons; plugin-specific DSP
  details stay in the postmortem.
- Corrections supersede or amend earlier conclusions; they never silently
  rewrite history.
- A human-readable view (this playbook, postmortems) is never the underlying
  evidence authority; the repository, artifacts, and run records are.
- **MANDATORY continuity rule (proven by the C4 recovery):** before editing
  or rebuilding ANY APEX plugin, verify first — canonical plugin
  workspace/worktree, repo path, branch, commit/tag, git status/diff,
  dedicated test/build project, last known artifact/hash, existing plugin
  documentation, this playbook, and the existing `ApexNativePluginFormat`
  registration. Never assume the current DAW checkout contains the latest
  plugin-specific history; never recreate previously completed plugin work
  from memory before checking isolated historical worktrees.
- **MANDATORY documentation-promotion rule:** at every native plugin's
  completion, all reusable bugs, root causes, test lessons, architecture
  improvements, and debugging rules MUST be promoted into this playbook;
  plugin-specific sonic choices stay in the plugin-specific documentation.
  No reusable lesson may remain trapped only inside a conversation or a
  temporary agent report.

## 12. Definition of Done (per Brain §42)

A plugin is ready to freeze when:

- required gates (steps 1-22) are green with archived evidence;
- dependency/license scan passes;
- artifact inventory is complete; hashes match;
- rollback plan exists; approval is recorded;
- freeze tag exists; publication is transactional; published hash verified;
- postmortem/playbook updated.

Do not declare success from compilation alone. If evidence is insufficient,
say `Unknown` and identify the exact evidence needed.

## 13. Final G10-cycle lessons (2026-08-11)

The complete G10 development cycle produced these reusable lessons. They are
generic and must survive into every future APEX plugin.

1. **Workspace / continuity authority** — Before modifying any plugin:
   identify the canonical worktree, branch, and commit/tag; inspect
   `git status`/`git diff`; identify the dedicated solution and the last
   validated binary and its SHA. Never assume `DAW_Core` contains the newest
   plugin work. Never recreate changes from memory before checking the
   isolated plugin workspace. Synchronize into `DAW_Core` only after the
   isolated plugin validation is green. Record major handoffs with:
   canonical workspace, official sync target, changed files, completed
   tests, pending tests, solution path, artifact hashes, known workarounds,
   and the exact next gate.

2. **DSP truth / visual truth / interaction truth** — A professional audio
   plugin has three separate correctness domains; all three must agree. A
   mathematically correct filter does NOT guarantee a correct rendered UI.
   A visually correct node does NOT guarantee correct hit-testing. A working
   control does NOT guarantee correct lifecycle behavior. Test each domain
   against its own contract and cross-check the pixel geometry they share.

3. **Response curve / node geometry** — Final pixel-space geometry must have
   ONE authority. G10's successful chain:
   `responseDbToPlotY(db) → getNominalCurvePointAtHz(hz) → getBellNodePosition()`.
   Invariant: VISIBLE CURVE PIXEL == NODE CENTER == HIT-TEST CENTER ==
   PANEL ANCHOR. Never allow the curve and controls to use different plot
   coordinate spaces (full-surface X vs plot-relative X is a real bug).

4. **High-Q curve sampling** — Uniform curve sampling can miss narrow peaks
   (G10's Q40 Bell exposed this). Do NOT solve this by blindly increasing
   the sample count. Insert critical parameter frequencies into the response
   curve sample set, or use another rigorous adaptive/interpolated method.
   Every objective visual bug reported by a real user becomes a regression
   test.

5. **Sample-rate lifecycle** — A UI frequency-response implementation must
   use the processor's REAL prepared sample rate. G10 exposed a bug where
   `getSampleRate()` remained zero because `prepareToPlay()` did not call
   `setRateAndBufferSizeDetails(sampleRate, samplesPerBlock)`. Never
   silently fall back to nonsense sample-rate values for visual DSP math.

6. **ASan / memory corruption** — The crash location may NOT be the
   corruption origin. G10 example: the `LookAndFeel` destructor appeared to
   crash; ASan proved the root cause was
   `BellControlPanel::~BellControlPanel → commitEditor → model callback →
   rebuildCurve → write into curvePath_ after curvePath_ destruction`.
   Destructor rule: UI teardown must not commit edits or notify owners that
   may already be partially destroyed; use quiet teardown paths. When
   corruption appears after a visual/allocation change, find the FIRST
   invalid memory access with ASan before random bisection.

7. **TextEditor lifetime** — Never destroy a TextEditor directly from its
   own callback. Avoid self-destruction / callback lifetime UB. Use safe
   deferred/quiet teardown patterns. Text editing must remain stable through
   Enter, Escape, Tab, focus-loss, and component teardown.

8. **Floating panel interaction** — A floating inspector must never
   reposition while the user is actively using one of its own controls.
   Panel-origin gesture: PANEL STAYS FIXED, VALUE CHANGES, NODE MOVES,
   CURVE MOVES. After the gesture finishes, smart placement may run once.
   Graph-origin gestures can have separate behavior (e.g., a micro HUD).
   Explicitly distinguish interaction origin.

9. **Non-obstructive panel placement** — Smart placement must remain
   usable. Do not let cursor avoidance make a panel run away from the user.
   G10 full-inspector final allowed placement families: ABOVE, TOP-RIGHT,
   TOP-LEFT, RIGHT, LEFT — never below the selected Bell. Cursor movement
   alone must not continuously reflow the panel.

10. **Hover / selection contract** — Hover is not selection. Hover-only:
    visual highlight. Selected idle: full inspector. Graph drag: micro HUD.
    Deselected: no inspector.

11. **Bypass is not delete** — Persistent per-band bypass must retain
    frequency, gain, and Q. Bypass skips/fades the DSP contribution while
    preserving the band; deleting removes the band and releases the slot.

12. **Silent-state parameter transitions** — When a processor stage is
    fully bypassed/disabled, expensive tuning smoothers can remain frozen.
    Before fade-in, snap hidden internal targets while the mix is zero, then
    fade only the audible contribution. This avoids wasted DSP and avoids
    audible tuning ramps.

13. **State migration** — State migration must be version-specific and
    semantic. Never assume a normalized value has the same meaning after
    parameter-law changes. G10: v2 Mini EQ values had specific frequency/Q
    laws; migration was old normalized → exact old physical value → new
    normalized, with `hasProperty` checks. Do not migrate schemas in which
    the feature did not exist.

14. **Host resize / scale** — Host window size != plugin editor logical
    size != host scale factor. Keep these concepts separate; avoid double
    scaling where logical bounds are already scaled and a transform is
    applied again (remember the historical c² scaling failure). Use logical
    content bounds + a single transform authority.

15. **Host bus configuration** — Preferred channel-configuration ordering
    can affect host metadata (G10's REAPER "(mono)" issue came from the
    preferred-configs ordering). Validate actual host presentation, not just
    processor acceptance.

16. **Test harness validity** — A failing host test can itself violate
    plugin contracts. G10 example: HostSmoke prepared for 512 samples but
    submitted 4096; ASan exposed the harness overflow. Test harnesses must
    obey the same contracts expected of a real host.

17. **Build pipeline** — Do NOT pipe live MSBuild output through
    `Select-Object -First …`: it can stop consuming native stdout and leave
    MSBuild/cl.exe processes alive, causing misleading hangs and file locks.
    Capture complete output first, filter logs afterward. Check for stale
    compiler/build processes when files unexpectedly remain locked. Use the
    dedicated plugin solution, not an unrelated root DAW solution.

18. **Header / ODR rules** — Header-defined out-of-class methods must obey
    ODR (G10 produced LNK2005 until `BellControlPanel` header method
    definitions were made `inline`). Respect declaration ordering for nested
    types.

19. **Test source hygiene** — AI-generated reasoning/prose must never leak
    into source files. Inspect diffs for backticks, explanatory prose,
    stray braces, duplicate test files, and malformed namespaces. One shared
    test-header syntax error can create huge fake compiler cascades.

20. **Real component hierarchy testing** — Paint success does not prove
    interaction success. Tests should exercise the real component hierarchy
    and the real mouse path when validating double-click, drag, mouseUp,
    wheel, context menu, selection, and panel interaction. Do not prove an
    interaction only by directly calling the helper function the
    interaction itself is supposed to invoke.

21. **Visual approval is a separate gate** — Automated tests verify
    objective contracts; human eyes approve visual balance, obstruction,
    perceived alignment, animation feel, readability, and aesthetics. Never
    call a UI finished solely because tests are green.

22. **Metering architecture** — Lightweight plugin meters: audio thread
    measures, atomically publishes, no locks, no allocation, no UI calls;
    message/UI thread reads atomics and performs display ballistics, peak
    hold, and quantized repaint decisions. G10 final contract: INPUT meter =
    real level entering internal processing after Input Trim; OUTPUT meter =
    exact final level delivered to the host; compact channel authority =
    louder peak of L/R; ballistics = fast attack, ~280 ms smooth release,
    ~600 ms peak hold.

23. **Filter tail testing** — "Silence input" does not necessarily mean
    immediate zero output; IIR/filter state can retain a legitimate tail.
    When a test intends to feed repeated silence blocks, clear/fill the
    input buffer before EACH `processBlock` call. Do not accidentally feed a
    processed output block back into the processor as the next input and
    then call the resulting tail a meter bug.

24. **Oversized blocks** — Never assume runtime block size equals the
    `prepareToPlay` maximum. G10 previously exposed scratch overflow with
    oversized blocks. Chunk safely within prepared internal capacity and
    avoid runtime allocation.

25. **Public parameter contract** — Internal compatibility parameters and
    public host parameters can differ. G10 example: legacy Analog/Quality
    parameters remain serialized internally but are hidden externally and
    inert in the audible path. Avoid duplicate host bypass exposure; use the
    processor bypass authority correctly.

26. **Design philosophy from G10** — Do not confuse feature count with
    product completeness. G10 succeeded by keeping two responsibilities
    distinct: MUSICAL TONE SHAPING plus LIMITED SURGICAL CORRECTION. The
    10-band musical core remains the identity; the Mini EQ remains
    intentionally bounded (HPF, 3 Bells maximum, LPF). Do not turn every
    future plugin into an everything-plugin.

## 14. References

- `docs/APEX_G10_PLUGIN_POSTMORTEM.md` — the G10 case, bug database, and
  evidence classes.
- `Plugins/G10VST3/HostSmoke/Source/Main.cpp` — reference HostSmoke.
- `Plugins/G10VST3/README.md`, `Scripts/build_g10_vst3.ps1` — product build
  and transactional publication reference.
- DAW Brain v7.0.0-alpha.1: §4 (3992-5575), §21 (28008-29206), §27
  (35773-37355), §28 (37356-38964), §34 (45912-47416), §37 (50249-51805),
  §41 (56094-57370), §42 (57371-59408).
- C4 case study: `docs/C4_PHASE9_COMPLETION_POSTMORTEM.md`, the C4 phase
  chain (`docs/C4_PHASE1_EVIDENCE.md` .. `docs/C4_PHASE8_HOST_INTEGRATION.md`),
  and the C4 suites (`Tests/Source/C4/*`).

## 15. C4 Development Postmortem — reusable lessons for every future APEX plugin

Status: **C4 was a successful R&D/engineering milestone.** Its final-suite
product decision is separate (see lesson C4-L29). These lessons generalize
the C4 case; C4-specific tuning constants are deliberately NOT global rules.
Format per lesson: SYMPTOM / FALSE LEADS / ROOT CAUSE / FIX / WHY IT WORKS /
REGRESSION TEST / RULE / APPLIES-TO.

### Test-harness traps

**C4-L1 — JUCE AudioBuffer clear-state trap (mandatory).**
- SYMPTOM: direct-engine impulse-response tests grew exponentially block
  after block ("growing IR") while the processor path stayed stable; a +6 dB
  bell looked like feedback.
- FALSE LEADS: unstable filter, hidden feedback loop, DSP corruption,
  memory issue. Seemingly harmless instrumentation changed the manifestation
  (Heisenbug behavior) because `getWritePointer()` invalidated JUCE's
  internal `isClear` bookkeeping.
- ROOT CAUSE: `juce::AudioBuffer::clear()` early-returns when the buffer is
  flagged cleared; DSP writes through previously obtained raw pointers do
  not reset the flag, so a later flagged `clear()` silently skipped and the
  previous output fed back as the next input.
- FIX: explicit active-range zero-fill in harnesses (`clearBufferExplicit`
  in `Tests/Source/C4/C4TestUtils.h`).
- WHY: unconditional writes cannot be skipped by metadata.
- TEST: `C4.Lifecycle / Impulse decays through the lifecycle at every rate`.
- RULE: **Never trust JUCE `clear()` after raw-pointer DSP writes unless the
  clear-state bookkeeping is known valid; diagnostic accesses are NOT
  observationally neutral.**
- APPLIES-TO: every direct-engine test harness, every future plugin.

**C4-L2 — Smoother float stagnation.**
- SYMPTOM: a one-pole targeting 6.0 stalled at 5.999828 forever; the exact
  neutral gate stayed closed though the value was perceptually settled.
- ROOT CAUSE: the per-sample step fell below representable precision;
  `next == current` while `current != target`.
- FIX: exact snap-to-target when no representable progress remains
  (`C4Smoother` in `Source/C4Core/C4FilterCore.h`).
- WHY: explicit termination converts asymptotic approach into a guarantee.
- TEST: `C4.Smoother / Tiny delta (historical stagnation case): 0 -> 6 exactly`
  at every supported rate.
- RULE: **Every smoother that must eventually satisfy an exact-state gate
  needs a stagnation/snap-to-target guarantee.**
- APPLIES-TO: neutral paths, bypass crossfades, any exact-convergence gate.

**C4-L3 — Shared mutable test-buffer channel lifecycle.**
- SYMPTOM: a later harness stage accessed channel 1 and crashed.
- ROOT CAUSE: a shared helper had resized a persistent test buffer from
  stereo to mono; later code assumed two channels.
- FIX: helpers own local buffers or restore the documented geometry.
- RULE: **Shared mutable audio test buffers must never silently change
  channel count/block geometry across helper calls.**
- APPLIES-TO: all shared test utilities.

**C4-L4 — OFF normalization collision (parameter contract).**
- SYMPTOM: OFF and the minimum active cutoff were indistinguishable.
- ROOT CAUSE: both mapped to normalized 0.0.
- FIX: reserve norm 0.0 exclusively for OFF; real values map into
  `(kHpfLpfNormFloor=1/256, 1]` (`C4Processor.h`).
- TEST: `C4.Processor / HPF/LPF OFF: unambiguous, round-trips`.
- RULE: **Optional continuous parameters must reserve a unique sentinel
  value/range for OFF that cannot collide with a legal active value.**
- APPLIES-TO: any future optional filter/range parameter.

**C4-L5 — Frequency text parser boundary.**
- SYMPTOM: values around 999.99994/1000 Hz mis-parsed across the Hz/kHz
  unit boundary (double-scaling).
- FIX: parse explicit units, no heuristic double scaling; round-trip
  text → value → text in tests at boundaries just below/exactly at/above
  the unit transition.
- TEST: `C4.Processor / Text round trips (getText <-> getValueForText)`.
- RULE: **Round-trip parameter text at every display-unit boundary.**
- APPLIES-TO: every plugin exposing frequency controls.

**C4-L6 — Active-range harness mistake.**
- SYMPTOM: a test expected untouched samples beyond the processing length
  while passing the full buffer as active.
- ROOT CAUSE: wrong harness contract; production was correct.
- FIX: pass the same explicit active range the assertion assumes.
- TEST: `C4.RtAllocation / Active-range proof: samples beyond numSamples are
  never touched`.
- RULE: **Never expect a "phantom untouched range" when numSamples says the
  region is active.**
- APPLIES-TO: every RT-allocation/active-range audit.

**C4-L7 — Continuous-phase test signals.**
- SYMPTOM: gain-smoothing measurements read a false peak/gain.
- ROOT CAUSE: sine phase reset every block injected a discontinuity the
  measurement then attributed to the DSP.
- FIX: one continuous phase counter across blocks.
- RULE: **Periodic measurement stimuli must preserve phase continuity across
  blocks unless discontinuities are the explicit intent.**
- APPLIES-TO: automation/smoothing measurement suites.

**C4-L8 — Physically justified numerical bounds.**
- SYMPTOM: "looks safe" output limits broke under coherent stacking.
- FIX: derive bounds from topology, max gain, worst-case coherent stacking,
  channel count, and the actual transfer.
- TEST: `C4.NumericalSafety / Extreme settings: finite and bounded` +
  automation stress.
- RULE: **Numerical-safety bounds must be physically justified, not
  aesthetically chosen.**
- APPLIES-TO: every DSP safety suite.

**C4-L9 — First shared error compiler rule.**
- SYMPTOM: walls of compiler errors across many TUs.
- ROOT CAUSE: one shared-header declaration/include failure.
- RULE: **Debug the earliest shared error first; repeated downstream
  messages are one bug, not many.**
- APPLIES-TO: every shared test header (namespace/using/include hygiene).

### Filter / topology measurement

**C4-L10 — Parallel-topology width metric.**
- SYMPTOM: small boosts read "no bandwidth" with a −3 dB-from-peak metric.
- ROOT CAUSE: a parallel console sum (`y = x + (A−1)·B`) never returns to
  0 dB for boosts; the classic width is undefined.
- FIX: half-excess width (where the boost excess falls to half).
- TEST: `C4.Phase5Tuning / Geometry candidates: bandwidth table`.
- RULE: **Derive width metrics from the actual topology; use half-excess for
  parallel boosts.**
- APPLIES-TO: any parallel-sum EQ.

**C4-L11 — Digital near-Nyquist expectations.**
- SYMPTOM: a 24 kHz LPF at 44.1 kHz looked "broken" (near-flat in-band).
- ROOT CAUSE: digital realizability; the engine clamps to 0.49·fs
  (~21.6 kHz) — physically correct, not a DSP bug.
- RULE: **Judge near-Nyquist behavior against the actual digital transfer
  and sample-rate constraints, not analog intuition.**
- APPLIES-TO: every LPF/HF shelf test.

**C4-L12 — Complex filter composition.**
- SYMPTOM: magnitude-only superposition predicted wrong combined responses.
- ROOT CAUSE: filter responses are complex (phase matters); at shared
  centers the combination can be `2A − 1`, not naive dB addition.
- FIX: compose complex transfer responses (real + imaginary) or measured
  impulse responses.
- TEST: `C4.Composition` (complex response audit, `complexResponseAtFrequency`
  in C4TestUtils).
- RULE: **Multi-filter composition tests must operate on complex/IR truth,
  not magnitude-only intuition.**
- APPLIES-TO: all future multi-band EQs.

**C4-L13 — Morph / interpolation expectations.**
- SYMPTOM: a test imposed linear magnitude/phase trajectories across a
  morph; smooth interpolation does not guarantee linearity.
- FIX: validate continuity, bounded step changes, finite states, and
  click-freedom — not unjustified linear interpolation.
- TEST: `C4.MorphAudit` (blend points, continuity, fast automation).
- RULE: **Morph tests pin continuity and click-safety, not linearity.**
- APPLIES-TO: any bell/shelf or topology morph.

**C4-L14 — HALO / intermediate frequency legality.**
- SYMPTOM: an intermediate morph at 44.1/48 kHz fed an illegal internal
  frequency state into the SVF path.
- ROOT CAUSE: legal endpoints do not guarantee legal interpolated states.
- FIX: clamp realizable frequency per sample immediately before coefficient
  generation (`C4BandCore::advance`, 0.49·fs) + mapper boundedness.
- TEST: `C4.HaloMapper / Automation torture ... all six rates`.
- RULE: **Validate any modulated/morphed frequency at the final point before
  state generation; torture-test at every rate.**
- APPLIES-TO: any mapped/morphable frequency control.

### Tuning / evidence discipline

**C4-L15 — Coupling perceptual floor.**
- SYMPTOM: coupling measured 0.01–0.06 dB valley fill even at +15/+15 — a
  functioning mechanism below the perceptual floor (parallel skirts
  compress the contour ~2.5×).
- FIX: evidence-driven adjustment of the coupling variables ONLY (strength
  0.50, maxContourDb 1.6) with all inflation caps preserved.
- RULE: **Mathematically nonzero is not musically meaningful; measure the
  final topology; change only the responsible variable when a localized
  design target fails.**
- APPLIES-TO: any interaction/coupling design.

**C4-L16 — Oversampling decision methodology.**
- EVIDENCE LOGIC: 1× missed the alias bound in one measured cell; 2× crossed
  it; 4× improved further but was NOT selected automatically.
- DECISION: lowest factor satisfying the target with margin, balanced
  against CPU, zero-latency behavior, and many-instance scalability (2×).
- RULE: **Oversampling is an engineering tradeoff, not a quality-number
  contest.**
- APPLIES-TO: every nonlinear processor.

**C4-L17 — Real-audio vs synthetic evidence.**
- The repository held ONE real vocal source; no drum/bass/mix-bus claims
  were fabricated. Synthetic signals drove engineering measurement; real
  audio drove reproducible render evidence.
- RULE: **Never convert technical measurements into fake subjective
  listening claims; label MEASURED FACT vs DESIGN DECISION vs HUMAN
  LISTENING JUDGMENT.**
- APPLIES-TO: every tuning phase.

**C4-L18 — Source-specific loudness matching.**
- Synthetic BOOM and the real vocal produced different RMS/peak deltas
  (+1.86/+2.17 vs +1.56/+1.65 dB) — expected, because EQ gain depends on
  source spectrum.
- RULE: **Never reuse a loudness compensation from a different source;
  measure the actual comparison material.**
- APPLIES-TO: every level-matched comparison or render pack.

### Analyzer architecture

**C4-L19 — Shared per-stream smoothing state (mandatory).**
- SYMPTOM: PRE and POST worked individually; BOTH read ~+2 dB instead of
  +6 dB — uniform across every FFT bin.
- FALSE LEADS: FFT scalloping, window misalignment, FIFO drops, smoothing
  cold-start (all instrumented and eliminated before the true cause).
- ROOT CAUSE: one shared `smoothed_` array alternated between the PRE and
  POST one-pole histories; solved steady state predicts
  `0.2904 × (postDb − preDb)` — matching the measurement exactly.
- FIX: independent per-stream state (`preSmoothed_`/`postSmoothed_` +
  per-stream init flags).
- TEST: `C4.Spectrum / BOTH: the derived response curve` (+5.98 dB measured
  after fix) and the cut counterpart (−6.00 dB).
- RULE: **Never share temporal smoothing/filter state between logically
  independent measurement streams (PRE/POST, L/R, M/S, sidechain/main)
  unless sharing is mathematically intentional and proven.**
- APPLIES-TO: every future analyzer/meter with multiple taps.

**C4-L20 — FFT transfer estimation.**
- Single-bin post−pre subtraction is fragile: Hann scalloping, off-center
  bins, independent frame alignment, smoothing differences.
- FIX: tightly bounded local-region estimator (`maxDbNear(post) −
  maxDbNear(pre)` over f/1.4..f×1.4) — never a wide search that can grab
  harmonics.
- RULE: **Do not estimate a transfer response from one FFT bin when a local
  region or an analytic model is available.**
- APPLIES-TO: all spectral-transfer measurements.

**C4-L21 — Signal spectrum vs DSP response.**
- PRE/POST FFTs answer "what energy exists"; the total C4 response curve is
  derived from the ENGINE's analytic linear transfer state
  (`C4ResponseCurveCore.h`) — BLOOM harmonics are not faked as a linear
  curve.
- RULE: **Distinguish signal spectra from the processor's transfer function;
  use the analytic model when the DSP provides one.**
- APPLIES-TO: every EQ/analyzer pairing.

**C4-L22 — Analyzer realtime architecture (canonical pattern).**
- audio thread → preallocated SPSC tap/ring → low-priority worker thread →
  windowed FFT + smoothing → double-buffered atomic snapshot → GUI polling
  at 60 FPS while the FFT runs at its own ~11 Hz rate. No FFT in
  `processBlock`, none on the GUI thread; Closed = dormant (no worker, no
  taps, no FFT).
- RULE: **The realtime thread must never be responsible for spectral
  analysis or GUI cadence; this is the default pattern for future APEX
  analyzers.**
- APPLIES-TO: every future spectrum/meter surface.

**C4-L23 — Snapshot lifecycle invalidation.**
- Analyzer snapshot state/index is invalidated on close, prepare/reprepare,
  and sample-rate change; reopening starts clean; multiple instances are
  independent.
- RULE: **Async analyzer/display state must never expose stale snapshots
  from a previous lifecycle, rate, or instance; test open/close/reopen and
  multi-instance explicitly.**
- APPLIES-TO: all published async state.

### GUI workflow

**C4-L24 — Compiled-editor visual review is a gate.**
- The first C4 GUI compiled and passed hook tests but read as four
  horizontal dashboard cards — the user rejected it BEFORE visual freeze.
  Redesign: four tall vertical console strips (bronze/amber/red-orange/
  ice-blue, gold BLOOM), top-to-bottom reading.
- RULE: **Green GUI tests ≠ visual approval. Every plugin gets a
  compiled-editor visual-review checkpoint: native preview → fresh
  screenshots/review board → user review → batched corrections → final GUI
  gates.**
- APPLIES-TO: every future editor.

**C4-L25 — Extensions must not replace primary controls.**
- Round 2: the expanded analyzer compressed the strips until gain faders
  vanished. Fix: the console keeps its fixed full height and the window
  expands DOWNWARD.
- RULE: **Optional visual extensions add space; they never remove or
  cripple the plugin's primary controls.**
- APPLIES-TO: analyzers, meters, advanced panels, browsers.

**C4-L26 — Live exact readouts from the canonical parameter path.**
- Fader position alone was insufficient; each gain fader now shows an exact
  live dB readout driven by the SAME parameter-text formatter used by drag,
  reset, automation, and state restore.
- RULE: **Precision controls expose exact values through the canonical
  parameter-formatting path — never a separate GUI-only formatter that can
  drift.**
- APPLIES-TO: every precision audio control.

### Host integration

**C4-L27 — JUCE format-manager name collision (mandatory).**
- SYMPTOM: C4 never instantiated through the real manager; Debug asserted
  on every launch.
- ROOT CAUSE: `AudioPluginFormatManager::addFormat` keys formats by NAME
  and silently drops a second format with the same name; two "APEX Native"
  format objects (G10 + C4) could not coexist.
- FIX: ONE unified `ApexNativePluginFormat` (`Source/PluginHostCore/
  ApexNativePluginFormat.h`) serving the whole family: matches any intrinsic
  identifier, enumerates both natives, routes creation by description.
- TEST: `C4.FormatHost / Phase 8 host path: the unified APEX Native format
  serves G10 AND C4` + intact G10 registration tests.
- RULE: **Do not create one AudioPluginFormat subclass instance per built-in
  APEX plugin when they share a family name; add future natives to the
  existing ApexNativePluginFormat.**
- APPLIES-TO: every future native APEX plugin — THE critical rule.

**C4-L28 — Scanner regression contract.**
- Adding a native plugin must update (not weaken) the contract: known
  plugins present, the new plugin exists EXACTLY once, deterministic
  ordering/seeding, no duplicates, processor creation succeeds, and the
  prior plugin's regression stays green (G10 remained green throughout).
- APPLIES-TO: every future scanner change.

### Product

**C4-L29 — Technical completion ≠ final-suite inclusion.**
- C4 reached full technical validation, frozen production tuning, user GUI
  approval, analyzer validation, host integration, and permanent
  documentation — and may still not join the final suite if the subjective
  product outcome is not personally compelling.
- RULE: **Engineering approval and product/musical-value approval are
  separate gates. Do not force a plugin into the suite merely because
  significant engineering was invested; do not discard the engineering —
  capture it here.**

**C4-L30 — Cloud-synced artifact hazards.**
- OneDrive cloud placeholders served stale PNG bytes for fresh writes and
  unreliable timestamps masked stale builds.
- FIX: user-facing previews/render packs go to a non-synced directory;
  verify dimensions/hashes after writes; force recompilation when
  timestamps lie.
- RULE: **Never trust synced placeholders for evidence material; verify
  artifact identity by content, not metadata.**
- APPLIES-TO: all evidence capture and publication.

## 16. Parametric EQ Phase 2 — reusable integration lessons

**PEQ-L1 — Large processor fixtures belong on the test heap.**
- SYMPTOM: processor tests overflowed the test thread's stack before reaching
  their assertions.
- ROOT CAUSE: a production processor can legitimately embed multiple fixed DSP
  engines, analyzer FIFOs, FFT workspaces, and immutable snapshot slots. A
  local automatic fixture duplicated that complete bounded storage on a stack
  sized for ordinary unit tests.
- FIX: keep production realtime storage fixed and allocation-free, but create
  the processor fixture with `std::make_unique` outside the measured callback
  scope.
- RULE: **Do not redesign bounded production storage merely to fit a unit-test
  stack. Heap-allocate unusually large fixtures, then scope allocation audits
  strictly around the realtime call being proved.**
- APPLIES-TO: analyzers, convolution processors, oversampling engines, and any
  processor with large fixed scratch/snapshot storage.

**PEQ-L2 — Exact crossfade endpoints require exact routing.**
- SYMPTOM: otherwise block-independent transitions differed by one ULP at the
  final sample.
- ROOT CAUSE: evaluating `a + 1 * (b - a)` is mathematically `b` but need not
  produce the identical floating-point bit pattern because of intermediate
  rounding.
- FIX: while the mix is strictly between endpoints, interpolate; at a settled
  endpoint, assign/copy the endpoint buffer directly. Parametric EQ applies
  this to current→target and wet→dry transitions.
- RULE: **When the contract says bit-exact endpoint, route the endpoint exactly;
  do not expect an algebraically equivalent interpolation expression to be
  bit-identical.**
- APPLIES-TO: bypass, topology transitions, quality changes, parallel engine
  swaps, and every sample-order/block-size exactness gate.

## 17. Parametric EQ Phase 3 — placement, audition, and measurement lessons

**PEQ-L3 — Configuration vs history reset (mandatory).**
- SYMPTOM: a freshly configured audition band behaved as if unconfigured; the
  next block saw mismatched settings and cancelled the hold.
- ROOT CAUSE: `reset()` cleared settings, shape, placement, morph AND
  coefficients; `configure(resetState=true)` assigned the fresh design and
  then called `reset()`, erasing it.
- FIX: separate `clearHistory()` (filter state only) from a full `reset()`;
  `configure()` assigns configuration first and clears history last.
- RULE: **Runtime state/history clearing must never erase freshly configured
  filter coefficients or control configuration.**
- APPLIES-TO: every processor with configure-then-reset lifecycle semantics.

**PEQ-L4 — Fast-path feature bypass.**
- SYMPTOM: audition control state was correct (hold=true) yet the mix never
  ramped — the feature was silently non-functional.
- ROOT CAUSE: the no-transition/no-bypass fast path called the engine
  directly, bypassing the chunk path where the newly added audition stage
  lived.
- FIX: the fast-path predicate now also requires the new subsystem to be
  inactive.
- RULE: **When a new processing stage is added, every optimization fast path
  must be revisited; a fast path that bypasses a subsystem makes its control
  state meaningless without any error signal.**
- APPLIES-TO: every future processor stage (dynamics, spectral, lookahead).

**PEQ-L5 — Test oracle matrix rule.**
- SYMPTOM: correct placement DSP was indicted by tests assuming single-channel
  impulse injection represented "dual mono" and expecting crossfeed where the
  matrix says silence.
- FIX: derive the expected 2×2 transfer matrix mathematically first
  (Left = [[H,0],[0,1]], Right = [[1,0],[0,H]], Mid/Side projections), then
  write oracles; use genuine dual-mono impulses for M/S cases.
- RULE: **Stereo/MidSide transfer tests must establish the mathematical
  matrix contract before judging DSP correctness.**
- APPLIES-TO: every multichannel processor test.

**PEQ-L6 — Transient audition/solo state.**
- Momentary audition must be transient and lifecycle-safe: token-owned
  lock-free commands, audio-side adoption, cancellation on delete/bypass/
  placement/shape changes, reset/reprepare, state restore, and editor
  destruction; it must never be a hosted parameter or serialized property.
- RULE: **Momentary audition/solo state remains transient, lifecycle-safe,
  and never persistent; stale tokens must not cancel newer gestures.**

**PEQ-L7 — Finite-window RMS oracle rule.**
- SYMPTOM: a moving, non-reproducing Release-only failure (three different
  measured values across runs) with instrumentation proving the mix and hold
  were exactly settled.
- ROOT CAUSE: uninitialized `juce::AudioBuffer` memory fed into the processor
  during settling transiently excited linear filter states with run-dependent
  stack garbage; the same mechanism can be amplified by non-bin-exact RMS
  windows whose finite-sum correction is start-phase dependent.
- FIX: explicitly clear every test buffer; use bin-exact excitation
  frequencies for strict RMS oracles; add a sample-exact cross-check that
  proves bitwise-identical outputs between equivalent configurations.
- RULE: **Never feed uninitialized buffers into processors under test, and do
  not use arbitrary non-bin-exact sines for strict finite-window RMS
  comparisons when window start phase can change the measured value between
  runs.**
- APPLIES-TO: every measurement oracle that asserts tight numeric equality.

**PEQ-L8 — Discrete control click delivery.**
- JUCE `triggerClick()` posts an asynchronous command message that is dropped
  without a dispatch loop, and radio-grouped toggle buttons swallow `onClick`
  in favour of toggle-state notifications.
- RULE: **For buttons whose state is owned by a refresh/controller (not the
  button), use `setClickingTogglesState(false)`; tests must drive the real
  click command path (or a dispatch pump), never a silent async post.**
- APPLIES-TO: all segmented/placement controls and top-bar toggles.

**PEQ-L9 — ASYNC-DSP test readiness rule.**
- SYMPTOM: linear-phase tests failed intermittently and inconsistently — an
  impulse peak measured at sample 0, block-size-invariance renders diverging
  at ~1560 samples, and an audition-rejection check passing falsely — all
  because the low-priority kernel worker had not published its kernel before
  the measurement interval began.
- ROOT CAUSE: unit-test `processBlock` loops execute far faster than real
  time, so wall-clock worker scheduling was serving as an implicit oracle;
  the same code engaged the linear path at a different sample offset in each
  render.
- FIX: a read-only published-generation observable plus a test-only helper
  that requests the mode, polls the generation with short sleeps (letting the
  low-priority worker run), and starts the measurement interval only after
  readiness and full settling. No production control flow uses the accessor.
- RULE: **DSP tests whose expected state is produced by an asynchronous
  worker must synchronize against an explicit observable generation or
  readiness condition before starting the measurement interval. Never use
  wall-clock worker scheduling as an implicit test oracle. The wait must be
  bounded, non-busy-spinning, test-only, and independent of production
  control flow.**
- APPLIES-TO: every test of worker-published DSP state (kernels, caches,
  analysis buffers), and every multi-render invariance comparison.

## 18. Dynamics Core Phase 4 — reusable dynamics lessons

**DYN-L1 — Time-constant oracles must use the mathematically derived bound.**
- Asymptotic exponential decay never "reaches zero exactly" in a finite
  window; asserting exact zero after a fixed sample count indicts correct
  DSP. Use the `initial * e^(-n/tau)` bound (e.g., 20τ for < 1e-8 relative
  residual) and derive the tolerance from the law, not from observed values.
- RULE: **Envelope/decay oracles derive tolerances from the time-constant
  law; exact convergence is only asserted where the snap-to-target guard
  guarantees it.**
- APPLIES-TO: attack/release ballistics, filter settling, RMS windows.

**DYN-L2 — Timing comparisons require pre-charged state.**
- An attack-vs-release comparison failed because the release envelope was
  never settled to its target before measuring its decay.
- RULE: **Before comparing transition rates, drive the system to the exact
  settled state first; otherwise the measurement compares transients, not
  time constants.**

**DYN-L3 — Sidechain-ready cores consume key signals, never own routing.**
- The dynamics core exposes detector-key overloads; host sidechain routing,
  latency alignment, and PDC remain consumer responsibilities.
- RULE: **A reusable processor core defines the mathematical inputs it
  consumes; it never reaches into host routing.**
- APPLIES-TO: dynamics, de-essing, ducking, keyed gates.

## 19. Dynamic EQ Phase 5 — signed/magnitude integration lessons

**DYN-L4 — Signed product law vs magnitude envelope (mandatory).**
- SYMPTOM: a freshly engaged dynamic EQ cut evolved at release speed
  (-1.71/-3.11/-4.24/-5.16 dB per 512-sample block) instead of the 1 ms
  attack; a later naive magnitude decomposition jumped 20 dB at range sign
  flips.
- ROOT CAUSE: the reusable envelope selects attack/release by comparing
  target against state in its NON-NEGATIVE reduction-magnitude domain.
  Feeding signed product gains (-9 dB target from state 0) selected the
  release branch. Reconstructing the sign AFTER magnitude smoothing then
  multiplied the stale magnitude by the new sign at range flips, jumping.
- FIX: keep a product-level SIGNED state advanced with the reusable
  envelope's exact attack/release coefficients, choosing direction by
  comparing |target| against |state|. An opposite-sign target moves the
  state THROUGH zero continuously; sign flips cannot jump.
- RULE: **Reusable dynamics envelopes operate in their canonical domain.
  When a product uses signed gain semantics, adapt at the product boundary
  (sign-aware direction rule over the reusable timing law); never change the
  shared envelope's attack/release semantics to fit one product.**
- APPLIES-TO: dynamic EQ, de-essers, duckers, every signed-gain dynamics
  consumer.

**DYN-L5 — Detector key-channel routing.**
- SYMPTOM: Right/Side/Mid dynamic detectors responded to left-channel-only
  program material.
- ROOT CAUSE: the internal-source stereo key right pointer fell back to the
  left pointer (the fallback was only valid for mono external keys).
- FIX: internal keys are the exact program channels; the mono fallback
  applies to the external-key path only.
- RULE: **Detector-domain key routing is part of the DSP contract; test
  absent-component cases (left-only must not drive a Right-domain detector,
  anti-correlated must not drive Mid) explicitly.**

**DYN-L6 — Per-sample continuity vs per-block deltas.**
- A 1 ms attack legitimately moves ~9 dB across a 64-sample block; click
  safety must be proven on PER-SAMPLE state deltas of a 1-sample-block
  rendering, not on per-block deltas.
- RULE: **Continuity oracles measure the smallest time step the
  architecture defines; block-size deltas are bounded by the attack law,
  not by an arbitrary small constant.**
- APPLIES-TO: every automation click-safety test.

**DYN-L7 — Evidence-only measurements must not assert.**
- A CPU diagnostic gate failed under full-suite Debug load despite being
  labelled "evidence only".
- RULE: **Wall-clock performance gates are environment-dependent; log the
  measurement, never assert it. Correctness gates must be deterministic.**
- APPLIES-TO: all performance characterization.

## 20. APEX #2 Buffer/Reprepare closure (2026-08-22)

**STATUS: #2A = VALIDATED COMPLETE. #2B = VALIDATED COMPLETE.
#2 BUFFER / REPREPARE = CLOSED (frozen).**

The user manually validated the final Release end-to-end with a real project,
real third-party plugins, and the production audio interface/driver. This
section is the canonical engineering record. Do NOT reopen #2 or modify the
fixed audio path without a directly caused new regression.

### 20.1 Symptom

Severe zipper/buffer corruption and audio discontinuity when changing the
device buffer size (~480 -> 2048) after a project was replaced at runtime
(New Project -> File/Open), either before first Play or after Play/Stop.

### 20.2 Exact reproductions (user workflows)

- GOOD: Fresh APEX -> open real project through Recent Projects -> change
  ~480 -> 2048 -> Play. Always CLEAN.
- BAD: Fresh APEX -> New Project -> File/Open same real project -> DO NOT
  Play -> change ~480 -> 2048 -> Play. Previously severe corruption.
- BAD: Fresh APEX -> New Project -> File/Open -> Play at ~480 -> Pause/Stop
  -> change to 2048 -> Play. Previously severe corruption.

### 20.3 GOOD vs BAD project load path

Recent and File/Open converge through
`MainComponent::openProjectFileWithOverlay()` ->
`ProjectManager::loadFromFile()` -> `restoreFromState()`. BAD uniquely adds
`ProjectManager::newProject()` before the File/Open. `newProject()` clears
plugin chains, but the callback-held snapshot retained ownership of the
previous project's `PluginChainCore` objects.

### 20.4 Root causes and fixes (all real defects found during #2)

**BUF-L1 — Routing buffer preparation / RT allocation.**
- SYMPTOM: routing node buffers could require allocation after
  prepare/project restoration.
- ROOT CAUSE: node buffers were sized/cleared on the first audio callback
  after reconfiguration instead of off the realtime thread.
- FIX: pre-size routing buffers off the realtime thread (worst-case block)
  in `AudioEngine::prepare()`.
- REGRESSION: `routing-buffer.prepare.v1`
  (`Tests/Source/Diagnostics/RoutingBufferPrepareTests.cpp`).

**BUF-L2 — Automation smoother 1024 assumption.**
- SYMPTOM: automation progression logic was effectively limited around 1024
  samples; larger blocks mis-applied automation.
- FIX: closed-form one-pole advancement supporting arbitrary valid block
  sizes. Regression coverage proved equivalence across 4x512 / 2x1024 /
  1x2048.
- REGRESSION: `automation.smoother.block-invariance.v1`
  (`Tests/Source/Diagnostics/AutomationSmootherCoreTests.cpp`).

**BUF-L3 — Clip DSP lazy preparation/allocation.**
- SYMPTOM: clip DSP objects could require first-use work (pitch/FIR cores)
  after release/reprepare on the audio thread.
- FIX: message/control-thread prewarm during `AudioEngine::prepare()` plus
  generation-validated clip-audio resolution.
- REGRESSION: `dsp.block-invariance.v1`
  (`Tests/Source/Diagnostics/DspBlockInvarianceTests.cpp`).

**BUF-L4 — VolumeRampCore / MuteFadeCore capacity defect.**
- SYMPTOM: ramp generation could exceed prepared capacity at larger blocks.
- FIX: capacity-safe generation and worst-case (>= 8192) preparation.

**BUF-L5 — ClipRegionPluginCore active-frame processing.**
- SYMPTOM: a preallocated larger buffer could be passed to hosted plugin
  processing instead of the active callback frame count.
- FIX: active AudioBuffer view limited to current numSamples.

**BUF-L6 — Plugin lifecycle reprepare defect (#2A).**
- SYMPTOM: Play -> Stop -> 480 -> 1024 crash; cold project -> 480 -> 1024 ->
  Track 1 silent.
- FIX: `ApplicationCore::releaseResources()` releases hosted plugin chains;
  `PluginInstanceCore` release-before-reprepare protection;
  `AudioEngine` playback/fade lifecycle reset.
- STATUS: #2A manually validated complete; permanently closed unless a
  future regression explicitly reproduces it.

**BUF-L7 — AudioDeviceBlockAdapterCore.**
- SYMPTOM: a backend callback could exceed the graph's currently prepared
  maximum.
- FIX: production adapter splits oversized backend callbacks into
  prepared-safe chunks (e.g. backend 2048 on prepared 1024 -> 1024 + 1024).
- PRODUCTION FILE: `Source/DeviceCore/AudioDeviceBlockAdapterCore.h`.

**BUF-L8 — Final project-replacement snapshot retirement defect
(final audible root cause of #2B).**
- SYMPTOM: New Project -> File/Open -> 2048 corrupted; Recent -> 2048 clean.
- ROOT CAUSE: `AudioEngine::pluginChainsBlockSnap_` retained the previous
  project's plugin-chain snapshot after runtime project replacement. The
  stale snapshot's final `shared_ptr` release could later occur inside
  `processWithSnapshot()` on an audio callback, destroying
  previous-project/plugin-chain state on the realtime thread. This violated
  the immutable snapshot ownership contract.
- FIX: `AudioEngine::prepare()` (`Source/AudioEngineCore/AudioEngine.h`)
  now retires `pluginChainsBlockSnap_` on the drained control-plane boundary
  while callbacks are gated/drained. The next callback adopts the currently
  published project snapshot normally. Retirement moved out of the realtime
  callback path.
- REGRESSION: `track.project-replacement.reprepare.v1` (in
  `Tests/Source/Diagnostics/TrackReprepareHarnessTests.cpp`).

### 20.5 Signalsmith false lead (preserved, closed)

- Earlier Stage 6 evidence was invalid: output sample counts differed and
  separate Signalsmith instances could have different random seeds.
- Corrected deterministic testing used equal output sample counts and fixed
  seed 12345.
- Results: `ClipIndependentPitchCore` = PASS; direct Signalsmith fixed-seed
  test = PASS. Signalsmith in the tested APEX configuration was block-size
  invariant.
- Do NOT reopen the Signalsmith hypothesis without new evidence.

### 20.6 Regressions preserved (evidence manifests)

| Test / suite | Pre-fix RED | Debug | Release |
|---|---|---|---|
| track.project-replacement.reprepare.v1 | 5562b571956d | c2ed5e64ac11 | ce29ce97c70d |
| track.reprepare.survival.v1 | — | d565f6137d6f | 2e1c90df7aeb |
| APEX.Diagnostics | — | 06b05add1b5d | cfa9fe3bc484 |
| APEX.Smoke | — | 7694d2334d09 | 200dee53a04a |
| PluginHost | — | 2d6185209995 | e12457321e95 |
| Sidechain | — | 46b8a81ccf3d | fc34c4fd9d52 |
| ProjectReload | — | 0ccdbdbc689f | 3c962874e64f |
| Full suite | — | eff386332e95 | 8675a363dd09 |

Pre-fix RED failure (manifest 5562b571956d, exit 1):
"previous-project chain was not reclaimed at the drained control-plane
boundary". Full Debug and Release suites exit 0 with zero failed assertions;
runner reported "All tests completed successfully."

### 20.7 Manual validation (user, final Release)

- TEST 1 — Recent -> 2048 -> Play: CLEAN (no zipper, no buffering noise, no
  corruption).
- TEST 2 — New -> File/Open -> 2048 before Play -> Play: CLEAN (previously
  broken).
- TEST 3 — New -> File/Open -> Play 480 -> Stop/Pause -> 2048 -> Play:
  CLEAN (previously broken).

### 20.8 Final validated Release

- Path: `Builds/VisualStudio2026/x64/Release/App/DAW_Core.exe`
- Size: 13,533,184 bytes
- SHA-256: `52B71DC158C07F8025C530DA08DA54732E2644CAC9C42D27FF613FA0C4A4E6CB`
- PDB SHA-256: `397FABB67BA13308E3A4BB429A8263FDC6885D4B0771E7D393ED019860950C21`

### 20.9 Freeze instructions

- Do NOT investigate or modify the audio engine further within #2 scope.
- Do NOT search for another bug, modify DSP behavior, refactor architecture,
  change callback behavior, or optimize unrelated code.
- Preserve regressions, documentation, and evidence; leave the repository
  ready for the next numbered task.

## 21. Permanent APEX engineering invariants (frozen 2026-08-22, #2)

**A. REALTIME SNAPSHOT OWNERSHIP** — Immutable snapshots may be adopted by
the audio callback, but retirement/final destruction of project/plugin-chain
ownership must occur off the realtime thread.

**B. PROJECT REPLACEMENT** — Runtime project replacement must: gate callback
admission; drain admitted callbacks; retire previous callback-held project
snapshots; release old project-dependent DSP resources; install/rebuild new
project state; prepare against current device configuration; prewarm
required buffers/DSP; publish current snapshots; reopen callbacks.

**C. BLOCK SIZE CONTRACT** — No DSP/module may assume 1024. All prepared
processing must support the valid configured maximum and/or use a bounded
adapter when backend callback size exceeds the prepared graph maximum.

**D. ACTIVE FRAME COUNT** — Preallocated storage capacity is NOT the active
processing frame count. Hosted processors receive only the active callback
sample count.

**E. PREPARE/RELEASE PAIRING** — Hosted plugin chains and DSP objects must
have coherent release/prepare lifecycle across: device reconfiguration,
project replacement, project reload, and runtime buffer change.

**F. NO FIRST-CALL RT PREPARATION** — No lazy buffer allocation / DSP
construction / topology preparation may occur on the first callback after
project load or reprepare.
