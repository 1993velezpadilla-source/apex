# APEX — Programme routing PDC audit (research; Oct 14, 2026)

> **Release gate: BLOCKED.** This research plan is NOT activated in the
> shipping APEX engine. The existing audio thread, session, and plugin
> lifecycle must remain unchanged until a reviewed implementation is ready.

## Observed from actual APEX source

1. `Source/MasterCore/MasterPdcCore.h::sync` enumerates **only**
   `ConnectionType::Direct` whose destination is the literal `"master"`.
   It computes `max(direct source chain latency)-source chain latency`.
   This is insufficient for delayed signals that travel through aux buses
   or sends and reconverge with a direct branch.
2. `Source/AudioEngineCore/AudioEngine.h::processTrackNodeExt` sums
   `PreSend` prior to fader and `Direct/Send/FolderSum` after fader.
   Only edges targeting `"master"` enter `masterPdc_.processEdge`.
   No programme-route PDC is applied before an **aux bus** sums different
   incoming latencies. This can affect parallel compression and wet/dry nulls.
3. `AudioEngine::rebuildPdcLines` currently keys sidechain delay lines by
   `edge.destNodeId`, not per-edge ID. Multiple independent source sends
   to the same sidechain destination therefore share one delay line; their
   tap-specific histories need independent verification. Sidechain key
   compensation must be separate from audible programme alignment.
4. On routing-version changes, `AudioEngine::processWithSnapshot` calls
   `syncNodeBuffers`, which may `reserve`, `emplace`, or `prepare` delay
   buffers. Likewise `MasterPdcCore::sync` can grow its vector or lines.
   These functions should NOT become the integration point for new PDC
   allocations in the realtime thread. A future implementation needs
   complete control-thread preparation and snapshot-safe retirement.
5. `PluginChainCore` research under test-only
   `APEX_ENABLE_EXPERIMENTAL_HOST_DRY_PDC` already aligns per-slot dry
   mixing and a chain's internal dry branch, including 44.1/48 kHz.
   Graph-level PDC must operate **outside** the plugin-chain path; they
   solve distinct alignment problems.

## Proposed calculation (control-thread, immutable snapshot)

Inputs:
- Adopted `RoutingSnapshot`: node IDs, node track IDs, topo order,
  active programme edges and their type (`Direct`, `Send`, `PreSend`,
  `FolderSum`; `Sidechain` explicitly excluded).
- Published `ChainLatencyMap`: each node's local plugin latency in samples.

For each node in topological order:

```text
arrival[node] = max(output[src] for enabled programme edges src -> node; 0)
localInputDelay[node] = arrival[node] if it has own clips/live input else 0
output[node] = arrival[node] + ownPluginLatency[node]
```

Then for each enabled programme edge:

```text
edgeDelay[src -> dst] = arrival[dst] - output[src]
```

**Critical:** delaying the local clips/live-input branch at a destination
track is required when it also accepts a delayed upstream route. Merely
delaying its incoming edges does not align the two signals before inserts.
All arithmetic uses validated nonnegative integers and rejects overflow or
invalid DAG order. Sidechains have distinct tap-point and detector-timing
semantics and do not influence programme maxima.

Example: vocal plugin latency 32, send to aux compressor latency 16,
and vocal dry direct-to-master. The aux arrival is 32, aux output 48,
dry master edge requires **16** additional samples; wet edge requires 0.
If bass with latency 0 sends to this aux, bass-to-aux requires **32**
samples BEFORE the aux plugin and bass direct-to-master requires 48.
This cannot be solved by direct-to-master-only PDC.

## Implemented research artifacts

- `Source/PDCCore/ProgrammeRoutePdcPlannerResearchCore.h`
  — no JUCE dependency; builds a fail-closed DAG PDC plan on control thread.
- `Source/PDCCore/ProgrammeRoutePdcJuceResearchBridge.h`
  — converts the **real immutable** `RoutingSnapshot` and per-track
  latency map into that plan. It is NOT called from `AudioEngine`.
- `Tests/Standalone/ProgrammeRoutePdcPlannerResearchTests.cpp`
  — merging sends, direct branches, pre-fader routes, folders, local
  injected audio, sidechain exclusion, bypass, inactive edges, cycles,
  invalid IDs and latency overflow. Windows MSVC + Linux sanitizers.
- `Tests/Source/Compressor/OptoCircuitJuceResearchHostTests.cpp`
  — independent real-JUCE snapshot/latency-map adaptation assertion.

## Still missing for shipping

1. Versioned and **immutable** prepared PDC plan owned by the control plane.
   No audio-thread `std::vector`/`unordered_map` growth, mutexes or DSP
   allocations. Edge-owned preallocated RT delay lines with stable identities.
2. Correct programme delay location for **track**, **aux**, **folder**,
   **master**, and pre-fader routes, with dynamically toggled edges and
   recycled/removed nodes; avoid one-block stale published plans.
3. Smooth crossfades when delays change after plugin insert/delete,
   sample-rate reprepare, bypass, or automatic latency changes. Reset
   histories on seek/transport boundaries where the host contract requires.
4. Differentiate record-armed **live monitoring** path from latency-balanced
   offline mix. Keep C6 monitoring Bypass/Reduced/Full semantics.
5. Sidechain per-source histories, PreFX/PostFX/PostMixer taps,
   multiple simultaneous key sources, sandboxed workers and worker bypass.
6. End-to-end APEX audio-engine graph tests: two tracks + aux + send + master,
   31/64/128/256/1024 buffers, 44.1/48/96k, mono/stereo, sample-by-sample
   impulse alignment, null tests, ASAN/UBSAN, change-under-playback, CPU.
7. UAD LA-2A/UA Apollo reference captures and human listening ABX remain
   **independent acoustic-fidelity gates**. These tests prove integer latency
   arithmetic, not physical analogue accuracy.

**No 1:1 sonic claim. Do not merge research host/PDC code into Release.**
