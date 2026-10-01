# APEX Recording Identity and Live Waveform Regression Design

**Date:** 2026-07-23  
**Status:** Approved for implementation  
**Scope:** Clip identity after project reload, live recording waveform visibility, and zero/invalid-input presentation

## 1. Problem statement

APEX has two recording regressions:

1. After saving, closing, and reopening a project, finalizing a recording on another track can make an imported beat disappear from its original track.
2. Live recording waveforms can disappear with a low-buffer-size interface such as an Apollo, while a misleading flat waveform can appear when no hardware input is active.

The first defect is caused by process-local numeric clip IDs. Persisted clips are restored without advancing the process-local counter, so a new recording can reuse an existing ID. Arrangement and audio caches keyed by that ID then update the existing beat instead of creating an independent take.

The second defect is caused by two related contracts. The live renderer returns without drawing whenever one callback peak occupies less than 0.25 pixels, which suppresses common low-latency Apollo configurations. Separately, preallocated input-buffer capacity is interpreted as fresh hardware input even when the callback reports zero valid input channels.

## 2. Goals

- Preserve imported clips and their audio-cache entries when recording after project reload.
- Preserve existing persisted clip IDs without project migration.
- Give every newly created clip a collision-resistant identity.
- Keep live waveforms visible for valid input at all supported device block sizes and zoom levels.
- Continue writing sample-aligned silence when input is unavailable.
- Show a recording outline and `No input — recording silence` status without drawing a fake waveform.
- Preserve realtime safety and existing recording writer behavior.
- Add permanent Debug and Release regressions for both defects.

## 3. Non-goals

- Replacing counter-based IDs for tracks, plugins, routes, or sidechains.
- Repairing arbitrary projects that already contain duplicate persisted clip IDs.
- Redesigning the recording writer, monitoring paths, or routing graph.
- Changing recording file ownership, undo's nondestructive source-file policy, or final waveform-cache architecture.
- Proving Apollo hardware behavior solely through simulated tests.

## 4. Governing contracts

- Existing project clips and imported source media are nondestructive assets.
- Stable IDs cross persistence and module boundaries; raw pointers and process-local counters do not.
- Recording finalization adds only the new source and clip.
- Recording audio never waits for waveform rendering.
- The opened device configuration, not requested UI state or allocated capacity, determines valid inputs.
- The realtime callback performs no allocation, deallocation, blocking, file I/O, GUI access, or formatted logging.

## 5. Clip identity design

### 5.1 New identities

New clips use an opaque identity with the existing prefix and a UUID payload:

```text
CLIP_<UUID>
```

Existing saved IDs such as `CLIP_1` remain valid and are restored unchanged. No migration rewrites legacy project data.

### 5.2 Ownership

`ClipManager` owns fresh clip identity creation because it owns the live clip collection and can verify that a proposed identity is not already present. The following operations use the same fresh-ID path:

- Audio import.
- New MIDI and pattern clips.
- Clip duplication.
- Recording finalization.
- Recording redo.

Project restoration remains a separate persisted-ID path. It must not generate a replacement ID for valid unique persisted clips.

### 5.3 Collision handling

A generated ID is checked against the loaded clip collection before insertion. A UUID collision is retried without notifying listeners or publishing partial state.

If project restoration encounters duplicate persisted IDs, loading must expose a diagnostic conflict rather than silently using one clip to replace another. Automated repair of already-corrupted relationships is outside this patch because automation and other references make the intended owner ambiguous.

### 5.4 Undo and persistence

`RecordAudioTakeCommand` continues storing the live ID returned by finalization. Undo removes only that new clip and never deletes either the recorded WAV or an imported source file. Redo creates another fresh ID and updates the command's live-ID mapping.

Saving and reopening after recording must preserve both distinct IDs and source paths.

## 6. Input-validity design

### 6.1 Separate capacity from validity

The callback input buffer remains preallocated to preserve realtime safety. Its allocated channel count is capacity only.

Each callback also supplies an explicit valid-input count derived from the actual callback arguments. Downstream capture logic uses this count to decide:

- Which channels contain current hardware samples.
- Whether a track's selected channel is available.
- Whether a live peak represents actual input.
- Whether the UI should report no input.

### 6.2 Valid input

For a valid selected channel:

```text
device callback
→ copy current valid channels into prepared storage
→ disk-writer queue
→ one min/max preview record per callback block
→ UI-thread pixel aggregation
```

The writer and preview consume the same selected source samples but remain independent consumers.

### 6.3 Zero or invalid input

When the callback reports zero valid inputs, or a track selects a channel outside the opened active layout:

- The recorder receives prepared zero samples to keep the take aligned.
- No live peak is published for that track.
- A bounded atomic status marks that track's current recording input unavailable.
- The arrangement continues drawing the recording clip outline.
- The header displays `No input — recording silence`.
- No synthetic centerline waveform is drawn.

The status returns to valid when a later callback supplies the selected channel again. This transition does not allocate or log formatted text from the callback.

## 7. Live waveform rendering design

`LiveRecordWaveformCore` continues publishing one fixed-capacity min/max record per callback block. Overflow continues dropping the oldest visualization records without affecting disk recording.

The UI renderer removes the `pxPerPeak < 0.25` all-or-nothing rejection. It selects between two bounded drawing modes:

- **One or more pixels per peak:** draw each peak using the existing path.
- **Multiple peaks per pixel:** combine all peaks intersecting a pixel column and draw one bar using the minimum and maximum across that group.

Aggregation occurs only on the UI thread. The number of bars is bounded by the visible pixel width and available peak count. The audio callback does no additional work beyond publishing the existing fixed-size peak record and input-validity status.

## 8. UI behavior

During recording, each armed track displays one of two states:

| Input state | Clip outline | Header | Waveform |
|---|---:|---|---:|
| Valid selected input | Yes | `Recording...` | Actual bounded peaks |
| Zero/invalid selected input | Yes | `No input — recording silence` | None |

The warning is an engine-owned capture fact displayed by the UI. The UI does not infer input availability from waveform shape.

## 9. Failure behavior

- Failure to create a recording file retains existing behavior: no take starts for that track and the error is reported off the callback.
- A missing input does not discard the take; aligned silence remains recoverable.
- A visualization overflow or skipped UI frame never changes written audio.
- A generated-ID collision is resolved before insertion and before listener notification.
- A duplicate persisted ID is diagnosed; it must never silently overwrite an arrangement or audio-cache entry.

## 10. Test design

### 10.1 Reload and record identity regression

1. Restore a project with an imported Track 1 beat using legacy ID `CLIP_1`.
2. Finalize a non-empty recording on Track 3.
3. Assert the new ID is nonempty, uses the clip prefix, and differs from `CLIP_1`.
4. Assert all live clip IDs are unique.
5. Assert Track 1 still references the beat source and Track 3 references the take source.
6. Assert both audio-cache entries remain distinct.
7. Undo and assert only the Track 3 take disappears.
8. Save and restore and assert both IDs and source paths remain stable.

### 10.2 Low-buffer waveform regression

Publish deterministic nonzero peaks and render to an image for:

- 48 kHz / 64 frames.
- 48 kHz / 128 frames.
- 48 kHz / 512 frames.
- Minimum, default, and representative zoomed-in pixels-per-second values.

Every valid-input case must produce visible non-background waveform pixels. Debug and Release must produce equivalent semantic results.

### 10.3 No-input regression

- A zero-input callback reports zero valid channels despite nonzero prepared capacity.
- Recording writes the expected aligned silence duration.
- Live peak count remains zero.
- Input status is unavailable.
- The UI chooses the no-input label and draws no waveform.

### 10.4 Invalid-channel regression

- Open two valid channels and select an out-of-range track channel.
- Confirm aligned silence is written.
- Confirm no preview peaks are published.
- Confirm no-input status is exposed.

## 11. Verification

Implementation follows test-driven development:

1. Add focused tests and observe the expected failures before production changes.
2. Apply the clip-identity correction and rerun its focused regression.
3. Apply input-validity and waveform aggregation corrections and rerun their focused regressions.
4. Run focused Debug and Release tests.
5. Run repository policy, dependency, and evidence validation scripts.
6. Rebuild unsigned Debug and Release applications.
7. Run complete Debug and Release test suites using the repository seed.
8. Confirm both application binaries and test binaries exist and evidence manifests report zero failed assertions.
9. Perform Apollo hardware validation at the actual sample rate, buffer size, active channel mask, and track input selection.

Compilation and simulated tests do not alone prove Apollo behavior. Hardware validation remains a release requirement.

## 12. Change-control boundaries

- Do not overwrite or revert unrelated working-tree modifications.
- Do not include broad identifier migration or unrelated recording refactors.
- Do not modify monitoring ownership, offline rendering, or routing authority.
- Do not commit unless the user explicitly requests a commit.
