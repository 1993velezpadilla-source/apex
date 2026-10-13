# Pichy Recording Guardian — Pre-Trim V1 (DRAFT, not a certified recording assistant)

## User story
"Do not touch my Apollo/Focusrite hardware gain. Tell me whether the input is
getting dangerous, and if the incoming samples are clean, offer to reduce
APEX digital Input Trim. Never pretend that digital trim repairs a clipped ADC."

APEX remains a DAW. Pichy is not enabled by this PR, and no cloud/API key is used.

## Actual live wiring in this change

1. Each Track owns `PichyInputGuardianCore`. It has a lock-free,
   allocation-free single-writer audio callback API and atomic UI snapshots.
2. `LiveInputMonitorEngine::processBlock()` samples the *routed raw hardware*
   input on armed/monitored tracks BEFORE software Input Trim. It works
   regardless of whether that track is set to meter-only or monitored.
   Mono samples are counted once.
3. `PichyInputGainAdvisorCore` is pure, conservative decision logic.
   A raw peak at/above 0.999, any > 1.0 full-scale float, or invalid sample
   blocks automatic digital gain advice and requests hardware attention.
   Peaks lower than 0 dBFS do **not** prove the preamp/ADC was clean.
4. Existing Bubblegum **Trim / VU** panel exposes raw input peak/risk
   separately from its post-trim VU and offers **Pichy: apply safer digital
   trim** only when the signal is clean enough for that suggestion.
   The button is a user-initiated action, not autonomous Pichy.
5. `InputTrimPanelManager` converts approval to a normal
   `CommandManager` undoable `PichyInputTrimCommand`, resolving TrackID at
   execution/undo time to avoid stale track pointers.
   `Track::setInputTrimDb()` remains the single parameter authority;
   the existing 5 ms smoothing and session serialization remain in charge.
6. Independent raw peak/reset state never changes capture samples;
   no writes to the hardware or recording buffers are performed by Pichy.

## Critical audio/UX invariants

- Software trim CANNOT repair preamp/ADC saturation. The words "RAW MIC near
  full scale" mean *warning*, not a proven source diagnosis.
- The detector counts *sample* peaks, not inter-sample true peaks and not
  pre-ADC analogue distortion. It is a pre-trim digital input observer only.
- `-12 dBFS` is a conservative default illustrative target, not a universal
  studio rule. It may differ across microphone/interface/genre and recording
  style; it must become a user-configurable target later.
- Digital trim suggestions only reduce gain. They do not turn up a quiet
  input, do not adjust hardware, and do not reprocess previously recorded
  clips. The producer can always use the existing trim knob directly.
- Both checking and proposing are based on per-track peak holds since the
  last **Reset meters** in the Trim/VU panel. These are **NOT timestamped take
  markers**. No claims of finding the lyric where a clip occurred yet.
- User voice commands and autonomous adjustments are NOT connected here.
  The button serves as the permission/Undo reference for the later Pichy
  command bridge.
- Legacy Trim/VU panels created without the central manager do not expose
  an enabled Pichy action, avoiding unowned command lifecycle problems.

## Validation gates

Portable numerical probe (JUCE-independent, C++17):

```sh
cd "My DAW/DAW_Core/Tests/Portable"
c++ -std=c++17 -Wall -Wextra -pedantic PichyInputGuardianStandalone.cpp -o pichy_input_guardian
./pichy_input_guardian
```

Still REQUIRED before merge:

1. Windows Debug AND Release clean rebuild of DAW and APEXTests.
2. Existing input-routing, record-mode, monitor-on/off, recording and
   Input Trim panel tests GREEN with Apollo and a generic ASIO interface.
3. Verify 48 kHz/128 live mic with On/Auto/Off modes; no stutters or
   duplicate per-block sample observations.
4. Read out pre-trim 0.5 amplitude -> -6.02 dBFS regardless of changing
   software trim, while post-trim VU changes.
5. Near-full-scale sample -> disabled Pichy button, clear hardware warning.
6. 0.5-peak signal -> suggestion lowers only Input Trim; Ctrl+Z restores
   the exact prior value; redo repeats; save/load persists.
7. Delete/reorder track while panel/Undo is open -> no dangling references.
8. Measure total CPU cost with multiple armed stereo tracks.
9. Verify accessibility/touch sizing and panel visibility at supported UI
   breakpoints; UX review required for compact screens.

## Next independent phases — not implemented in V1

- Exact transport/sample/take-aligned raw-clip event markers, post-take
  summary, review and waveform region linking.
- Input hardware gain capability adapters, per-device opt-in if publicly
  supported by a device driver (Apollo/Focusrite support must be audited).
- Conversation/voice command bridge, explicit permissions, confidence,
  no-interruption policy while recording.
- Dry take/wet monitoring distinction, latency-calibrated punch-in on
  transcript alignment with multi-take non-destructive history.
- Tuning peak/RMS/LUFS criteria in rehearsal by vocal section (verse/chorus)
  without trying to "correct tone" with a gain knob.
