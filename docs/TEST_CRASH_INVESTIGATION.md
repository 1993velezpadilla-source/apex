# Full-suite crash investigation — 2026-10-08

## Verified starting point

The latest migration candidate is `25e929edf494a9eb93cfd10061243d84af7daaa5`.
Windows run `37645465866` built the application and test executable in Debug and
Release. Neither full-suite execution published final results. The Release
evidence artifact `11496403606` records native exit code `-1073741819`
(`0xC0000005`, access violation), not a missing-source compile failure.

The last started subtest was
`QuickWorkflow.CoreAndPopup / QuickTrackBusCardsKeepPendingAndCreateOnlySemantics`.
Its `pressButton` helper dispatches the shared JUCE message queue. That makes an
earlier suite's surviving callback a possible cause; the last subtest alone does
not establish the faulting object's owner.

## Diagnostics

The test runner installs a Windows exception filter after plugin-worker dispatch.
It prints the exception and a symbol/source-line stack, and writes a minidump to
the publisher's unique evidence directory. Crashes retain their nonzero exit and
INCOMPLETE status. No filter is installed in the production app or plugin worker.
The diagnostic workflow runs Quick Workflow in isolation before the full suite.

## Source-path defect

`ProjectPersistenceTransactionTests.cpp` resolves `Source/AppCore/ApplicationCore.cpp`,
`Source/ProjectCore/ProjectManager.h`, and `Source/MainComponent.cpp` relative to
the current working directory. The publisher previously launched from the
executable directory under `Tests/Builds/VisualStudio2026/x64/...`, where those
source files do not exist. Their source contracts therefore failed despite a
complete checkout.

Launch from the application repository root. The executable and plugin fixtures
already use absolute paths. Keep the source-contract assertions intact; focused
Persistence validation is required to verify this correction.

## Acceptance

Run `37798874933` reproduced the native crash at
`QuickTrackColorSystem::isManual`, line 230, after Quick Workflow in isolation
passed 17 result groups / 395 assertions. `QuickTrackRoleColorPicker::paint`
queries that store through a raw builder reference even when its detached
callout survives the builder's destruction. Hiding/exiting a JUCE modal queues
its deletion; it does not establish synchronous destruction.

The picker now observes the control-plane builder through a JUCE weak reference.
Painting and both AUTO/swatch callbacks check the owner before accessing it.
The lifetime regression paints and delivers actual queued button clicks after
destroying the builder, and verifies that the former owner's colour store does
not change. Normal live swatch behaviour is also exercised.

Windows validation of the corrections is pending. Other assertions in the
original full run also failed; resolving the native crash does not certify those
assertions or a global release. Both clean Windows configurations and full-suite
result JSON remain required before migration acceptance.
