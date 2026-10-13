# APEX / Pichy Foundation — Live Capability Discovery V1 (read-only)

**Scope:** APEX native intelligence foundation, **not** a local LLM, not voice chat, and not a write-capable studio robot. Related overall feature tracker: #98.

## Why this architecture

APEX is a DAW first. Pichy should discover native and third-party plugins from **the same live host chain**, never from manually maintained plugin-specific knowledge. C4, G10, Parametric EQ, VST3, and future intrinsic plugins become discoverable **once they exist in the host chain**. This does not claim that APEX Tune Live is already finalized/integrated.

The authoritative track/instance/parameter hierarchy comes from:

- `ApplicationCore::capturePichyCapabilitySnapshot(out)`, the canonical control-plane entry point.
- `TrackManager` and existing `pluginChains_` lookups, **without** calling the mutating `getPluginChain()`.
- `PluginChainCore::getNumSlots() / getSlot()` and `PluginInstanceCore::getPluginInstanceId()`.
- `juce::AudioProcessor::getParameters()`, including `AudioProcessorParameterWithID::paramID` when present.
- A JUCE-free, validated `apex::pichy::PichyCapabilitySnapshot`, owned by the caller.

Plugin parameter names, normalized current values, units and metadata come from the actual processor. The instance UUID is durable across slot reorders; index is treated as location only. No open plugin GUI, no UI pixel clicking, no invented parameter mapping. A third-party VST3 with index-only parameter access appears as **display-only** (`stableId=false`), not silently treated as a stable write target. Sandboxed plugins without a parent-visible processor show the plugin slot, but no fabricated parameter controls.

## Explicit boundaries

- **OFF by default:** no background Pichy inference/threads, model files, external services, recurring polling or network API calls.
- **Read-only:** no setParameter, no bypass changes, no track/routing mutations. `wouldPermitFutureWrite` is only schema-level eligibility for a later separately authorized bridge.
- **Message thread only:** snapshot collection allocates and may call plugin-host getters. Call on request; never from `processBlock()`, recording callback, or realtime DSP.
- **Fail closed:** request refuses during project-state restore, on wrong thread, unknown/duplicate instance or parameter IDs, out-of-range samples and unbounded plugin/track counts. On failure output is empty, not stale.
- **No hardcoding:** Native and third-party plugin types use the same discovery path. No assumption that the currently installed native plugins are the final suite.
- **No overclaim:** a successful snapshot is not evidence that a plugin's parameter is safe to set automatically or that the third-party plugin always supports remote editing. Writing demands parameter lifetimes, stable IDs, parameter-domain fidelity, host gestures, Undo/Redo, automation read/write arbitration, explicit permissions, and no audio-thread locks.

## V1 capabilities

| Data | Present |
|---|---|
| Track ID, name, armed/monitoring state, master flag, input trim | Yes (snapshot) |
| Plugin UUID, format, identifier, position, bypass, sandbox status | Yes |
| Per-plugin host-exposed ID, name, unit, normalized value, steps | Yes if processor exposed |
| Stable vs index-only parameter distinction | Yes |
| Duplicate/malformed identity rejection | Yes |
| Capped memory/schema + validated ranges | Yes |
| Live assistant, mic transcription, self-running automation, destructive edits | **No** |

## Reproducible tests

`My DAW/DAW_Core/Tests/Portable/PichyCapabilityRegistryStandalone.cpp`

Runs g++ C++17 on Linux and MSVC on Windows in `.github/workflows/pichy-capability-registry.yml`. Cases: APEX-native metadata, synthetic VST3 with unstable index-only parameter, slot reordering, cross-track identity collision, duplicated parameter keys, malformed IDs, NaN/out-of-range values, absent sandbox processor, memory caps, query correctness.

**Required before merge:** Windows Debug AND Release full APEX app build and JUCE integration smoke (open session with actual APEX native plugin and external VST3, capture snapshot while stopped and while playing, verify no new chains/GUI; reorder plugin and restore project; sandbox fallback; thread guard and race check). Existing full Windows build workflow is triggered via `TrackCore/Track.h` changes and is not the same as the portable schema test.

## Next, separate gated PRs

1. **Host transaction bridge (message thread)**: typed parameter writes with explicit user permission, stable parameter IDs and plugin/track generation validation; `beginChangeGesture`, `setValueNotifyingHost`, `endChangeGesture`, integration with APEX automation and Undo; third-party unsupported parameters must be rejected.
2. **PichySessionWorldModelCore**: immutable observations of selected track/clip, transport, routing, sends, automation, input monitoring, markers and take lifecycle.
3. **Local text assistant** through shipped, supervised, resource-bounded runtime; voice/chat share later exactly this command/permission engine.
4. **Pichy Live** local push-button/full duplex/multilingual/proactive-but-silent-when-recording speech.
5. **Curated Audio Engineering Brain** from legally acquired books, AES/academic literature, manuals, producer interviews, with evidence-aware retrieval and explainability.

No user must supply an API key. Audio never waits for AI.
