# Recording mode and Monitor Off regression — 2026-10-04

## Behavior

When a record-armed track uses Wet / Post-Fader recording, Monitoring Off keeps the microphone outside the audible track/plugin path. The recorder now captures the selected hardware input dry for that callback instead of skipping the take and writing silence. This does not enable input monitoring. With Monitoring Auto or On, recording still uses the post-fader track tap and prints its effects and fader. If that tap is missing while monitoring is active, the recorder writes silence for that callback to preserve the take's sample clock.

The Mixer Arm button's right-click menu and the Arranger track-row context menu both offer Dry / Pre-Fader and Wet / Post-Fader. The selected mode remains track state and is already covered by save/restore regression coverage. A Post-Fader take made with Monitoring Off is dry; its effects remain live on playback, so bypass them if they should not be applied a second time.

## Cause and change

`AudioEngine::addLiveInputToTrackBuffer` returns false when Monitoring is Off. The post-fader tap only runs after live input enters that buffer, while `RecordingEngine::processBlock` previously skipped every Post-Fader take. The result was an empty take even though the audio callback still had the hardware samples.

`RecordingEngine::processBlock` now uses the take's pre-resolved Track pointer to detect explicit Monitor Off and write the selected input into the take. It marks that callback as written before enqueueing, preventing the callback-scope clock-preservation fallback from appending a duplicate silent block. The Arranger's right-click track menu uses `Track::setRecordMode`, matching the Mixer menu and the persisted model.

## Verification

- `APEX.Recording`, Debug x64: 45 test groups, 374 assertions passed, 0 failed. Result JSON and console transcript are in `recording-monitor-off-Debug.json` and `recording-monitor-off-Debug.txt`.
- The integration regression checks the processed Auto-mode callback, a dry callback with Monitoring Off, the Off setting remaining unchanged, and silence when the Auto-mode post-fader tap is absent.
- App Debug x64 build passed unsigned; SHA-256 `5F43169B81D72912A2AA1570EDB663D59D6028DFD45E47E2E49B45D14FE88AB7`.
- App Release x64 build passed unsigned; SHA-256 `F6F071DB8E02CD210405A15A944FEFE9BA89548A6FB3265DB3D3DC1A5B47859F`.
- Test Debug x64 executable SHA-256 `39E8034B87018318557DA4713566CAEF4DF5136A47376A71945B6975FDB88489`.

## Scope limit

The regression uses the real AudioEngine and RecordingEngine with test audio buffers; no physical microphone/interface or user song was opened. Monitoring Off intentionally falls back to dry input because that mode excludes the mic from the effect-processing path. Printing effects while keeping the mic inaudible would require an isolated processing path and is outside this fix.

Status: **FOCUSED_VERIFIED / RUNTIME_PENDING**.
