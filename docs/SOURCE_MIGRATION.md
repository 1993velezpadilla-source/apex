# Desktop source migration to GitHub

This candidate was exported from the current `Apex backup` Desktop folder on
2026-10-07. The original folder remains unchanged. The root `DAW_BRAIN.md` is the
current canonical Brain; historical documents remain context, not proof that a
feature works or that a test passed.

## What a checkout contains

Application sources, the active ArrangementEditor under
`My DAW/DAW_Core/Builds/VisualStudio2026/ArrangementEditor`, embedded resources,
JuceLibraryCode, generated Visual Studio projects, test sources and VST3 fixture
projects, dependency contracts and compatibility patches, scripts and project
documentation are retained. Similar names are not treated as duplicate content.
SignalSmith libraries are pinned Git submodules.

The unreferenced Desktop tree `My DAW/DAW_Core/Source/ThirdParty/signalsmith-stretch2`
is preserved byte for byte at `reference-source/signalsmith-stretch2`. The existing
dependency contract explicitly forbids that tree in the active source index; the
application and test projects use the declared pinned submodules instead. No
reference-library files are discarded and the original Desktop tree is unchanged.

Compiled output, IDE caches, crash dumps, installed SDKs, installers, local
recordings, old executable deliveries and historical generated build trees are
not build inputs. They remain in the original backup. A source ZIP is a source
backup, not a backup of that entire Desktop folder.

## Clean Windows preparation

Clone with `git clone --recurse-submodules`, then from `My DAW/DAW_Core` run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\prepare_ci.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Seed 0xA9E12026
```

Visual Studio 2026 (major version 18), C++ v145 and Windows SDK are required.
`prepare_ci.ps1` downloads the official JUCE 8.0.12 Windows release, checks its
locked SHA-256, and invokes the existing dependency materializer and verifier.
The four locked JUCE patches, including the current WASAPI buffer patch, are
preserved. A dependency verification failure cancels the application build.

## Acceptance gate

The Windows workflow builds Debug and Release and runs the complete suite with
all required fixture plugins, including Dense E2B. Test stdout, stderr and actual
JSON counters are retained. Missing or corrupt results mean INCOMPLETE, never
PASS. Only a successful job publishes its application EXE/PDB artifact.

The migration remains a candidate until both clean GitHub jobs pass. Existing
suite failures must be investigated rather than skipped or renamed as passes.
The local attempt to execute the existing SDK materializer was blocked by the
computer's antivirus; no local clean build is claimed. The original Desktop
environment remains the reference until the clean build acceptance gate passes.

CI passing proves the checkout can build and pass its automated tests. Recording,
monitoring, third-party plugins, Realtek/WASAPI and ASIO buffer behavior still need
physical audio-device checks. This migration does not change the audio engine or
certify that every previously reported audio defect is fixed.
