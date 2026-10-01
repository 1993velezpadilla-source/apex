# APEX — Post-Install DAW Brain Repository Verification

You are operating inside the real APEX repository after `DAW_BRAIN.md` has been explicitly installed.

## Authority boundary

The installed Brain is audited knowledge authority.

It is **not** evidence that the current repository implements every capability described in it.

Permanent rules:

`BRAIN_PROSE != CURRENT_APEX_REPOSITORY_STATE`

`IMPLEMENTED != VERIFIED`

`BUILD_PASS != FEATURE_VERIFIED`

`TEST_PASS != UNIVERSAL_CORRECTNESS`

`AGENT_SELF_REPORT != VERIFICATION`

`SYMPTOM_DISAPPEARED != ROOT_CAUSE_PROVEN`

`TOOL_CAPABILITY != TOOL_PERMISSION`

## Mission

Build a fresh, evidence-backed repository state map without rewriting `DAW_BRAIN.md`.

For every consequential capability you inspect, classify it as one of:

- `NOT_IMPLEMENTED`
- `SCAFFOLDED`
- `EXPERIMENTAL`
- `PARTIAL`
- `PRODUCTION_CANDIDATE`
- `VERIFIED_PRODUCTION`
- `DEPRECATED`
- `UNKNOWN`

Never infer implementation from Brain prose.

## Phase 1 — Canonical identity

1. Hash `DAW_BRAIN.md`.
2. Record repository root and current commit/worktree identity if Git is available.
3. Confirm the installed Brain contains:
   - §§1–38 core;
   - promoted AI/Reasoning §43;
   - Volumes 13–46;
   - authority boundaries.
4. Do not modify the Brain.

## Phase 2 — Repository discovery

Inspect the actual repository and map:

- build targets;
- audio callback entry path;
- transport;
- routing;
- recording;
- monitoring;
- plugin hosting;
- export/offline rendering;
- latency/PDC;
- MIDI/timeline;
- step sequencer;
- piano roll;
- project persistence;
- crash/recovery;
- tests;
- release/build tooling;
- security/tool permissions;
- repository intelligence artifacts.

For each map entry record exact file/symbol evidence.

## Phase 3 — Claim-to-repository verification

For each APEX-specific statement used operationally, separate:

1. Brain architectural requirement.
2. Current repository observation.
3. Interpretation.
4. Test evidence.
5. Verification status.

Do not convert an architectural target into current implementation truth.

## Phase 4 — Build and tests

Run the repository's normal supported build/test workflow.

A successful build proves only the build claim.

For important audio paths, run or identify appropriate targeted tests and record:

- command;
- build/configuration;
- result;
- exact test;
- relevant artifact/log;
- limitations.

## Phase 5 — Live evidence registries

Create/update, without fabricating status:

- `APEX_STATE.md`
- `APEX_ARCHITECTURE_MAP.md`
- `APEX_CAPABILITY_REGISTRY.md`
- `APEX_FIX_REGISTRY.md`
- `APEX_TEST_REGISTRY.md`
- `APEX_BUILD_REGISTRY.md`
- `APEX_PERFORMANCE_BASELINES.md`

Use stable IDs where the project already defines them.

## Phase 6 — Contradictions

If Brain architecture and repository behavior differ:

- do not silently edit either side;
- record the contradiction;
- determine whether it is implementation debt, stale Brain knowledge, intentional deviation, or unknown;
- collect evidence before proposing a change.

## Phase 7 — Final report

Return:

- installed Brain SHA-256;
- repository identity;
- build status;
- test status;
- verified capabilities;
- partial/scaffolded capabilities;
- contradictions;
- unknowns;
- next highest-risk evidence gaps.

Do not say “APEX is verified” globally.

Only individual claims/capabilities with adequate evidence may receive `VERIFIED_PRODUCTION`.
