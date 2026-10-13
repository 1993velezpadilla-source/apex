# Pichy Phase 2 — Safe Action Preflight (stacked on PR #100)

**Status:** Development draft. Pichy is still OFF; no LLM, voice, plugin writes or network/API-key dependency is introduced.

## User-intent contract

Pichy must eventually understand producer language such as:
- “Súbele un chin a esa frecuencia.”
- “En el vocal, bájame la ganancia del EQ.”
- “El tercer plugin está muy duro, ¿lo ajustamos?”
- “No cambies nada hasta que yo diga que sí.”

The producer controls approval. Freeform LLM text cannot authorize anything.

## Implemented now

`PichyActionPreflightCore`, a JUCE-free, deterministic C++17 validator operating over the actual plugin/parameter snapshots from the prior PR #100:

1. Verify source snapshot is structurally valid.
2. Verify caller's `observedCaptureId` is from the same reviewed snapshot. A capture ID is NOT a project revision.
3. Validate track UUID, persisted plugin instance UUID, plugin-authored parameter ID (not slot index or visible label).
4. Reject unavailable processors/sandbox proxies without parent-visible parameters.
5. Reject index-only/fallback parameter identity, non-automatable parameters, bad/nonfinite or out-of-range normalized values.
6. Detect if the parameter changed since preview; reject rather than overwriting manual/automation/host edits.
7. Never commit a proposal without **explicit approval from a trusted app controller**. This is only a contract: model text is untrusted and may not set authorization.
8. Return immutable-copy metadata for a future executor; no borrowing of live `Track*`, `AudioProcessor*`, or `AudioProcessorParameter*`.

`ApplicationCore::previewPichyParameterChange()` exposes preflight as a pure preview.
`ApplicationCore::revalidatePichyParameterChange()` acquires a **fresh live** hosted parameter snapshot before possible execution and fails closed on missing plugin, changed parameter, invalid session/scan or project restore.

**This PR does not actually alter a plugin parameter.** Full Undo, host gestures, automation arbitration and user interface approval must be implemented as the next separately gated change.

## Gate: three distinct levels

| Gate | Reason | Executor allowed? |
|---|---|---|
| Intent + snapshot | Valid semantic target, finite normalized value, current facts | Never without approval |
| User authorization + preflight | Confirmed action against exact observed snapshot | Plan eligible, no action yet |
| Fresh live revalidation | The target still exists and value is unchanged | Future executor may proceed only after this and host-specific policy |

A full executor must additionally handle:
- Explicit user interface provenance and short-lived single-use approval tokens.
- Enforced message-thread affinity.
- Native JUCE parameter gesture sequence (`beginChangeGesture` / `setValueNotifyingHost` / `endChangeGesture`) and host notification.
- Correct host automation Write/Read/Touch/Latch arbitration.
- Undo/Redo preserving current session/track/plugin generation and handling deleted/reloaded slots.
- Worker sandbox IPC if supported; otherwise reject.
- Destructive vs non-destructive policy; never change a microphone preamp or erase a take from a plugin parameter action.
- No work at all on realtime audio thread and no exception/partial-apply hazard.

## Evidence

- `Tests/Portable/PichyActionPreflightStandalone.cpp`: consent gating, proactive requests, stale captures, missing/deleted/reordered instances, invalid values and identities, changed parameters, sandbox unavailable, unchanged targets.
- `.github/workflows/pichy-action-preflight.yml`: independent C++17 Linux g++ and Windows MSVC testing.
- `.github/workflows/pichy-apex-windows.yml`: NEW required full APEX Windows Debug/Release clean build + deterministic smoke for future Pichy header/integration changes. This plugs the gap where portable schema tests were GREEN but a JUCE production compile broke in PR #100.
- PR #100 contains the capability registry and actual APEX host observation; PR #96 independently contains audio input pre-trim guardian.
- Master product specification and all-chat requirements: issue #98.

## No exaggerated status

C++17 portable success does not imply the Windows APEX desktop build succeeds. Even a full Windows smoke is not studio QA, nor proof that the installed VST3 actually exposes every parameter. Until the future trusted command executor exists, Pichy can inspect and preview but not write plugin parameters. The actual future offline conversational model/voice/knowledge layers are distinct phases.
