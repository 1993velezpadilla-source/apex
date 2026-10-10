# Clip automation positions after project reopen

- Date: 2026-10-04
- Change: rescale sample-domain automation lane points, clip-region starts/lengths, and clip-local points whenever project load proves that the audio device sample rate differs from the saved project rate.
- Regression case: save/restore a clip with Tape Stop points at 0.25 s and 0.75 s, then reconcile 44.1 kHz to 48 kHz. The clip and points retain those offsets; clip-region local points are also rescaled.
- Focused test: project.sample-rate-reconcile.v1 — 12 groups, 47 assertions passed, 0 failed.
- App builds: Debug and Release completed successfully, unsigned (APEX_SKIP_SIGNING=1).
- Debug app SHA-256: 77D20F3EFF57C3552263D02C1EDDA707C10FF7061248E6FF0C02111BCD128D27
- Release app SHA-256: CE91BBB2AF79BD77B50C293A5EB138F2A19B35CA8F826DC6D1191AA26FEFF518
- Runtime audio validation on the user's device/project remains to be done; the regression test verifies persisted sample positions and the project restore code path.
- Test results: utomation-sample-rate-reconcile-Debug.json.
