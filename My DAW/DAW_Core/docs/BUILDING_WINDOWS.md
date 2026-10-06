# Building APEX on Windows

Run all commands below from the `DAW_Core` application repository root. The
dependency contract in `Dependencies/apex-windows-dependencies.json` is the
authority for the supported Windows toolchain and third-party revisions.

## Prerequisites

- Windows 10 or 11 x64.
- Visual Studio 18 with the MSBuild component and the v145 C++ toolset.
- An installed Windows SDK selected by MSBuild through
  `WindowsTargetPlatformVersion=10.0`.
- Windows PowerShell 5.1.
- Git with submodule support.
- The JUCE 8.0.12 Windows archive named `juce-8.0.12-windows.zip` in
  `../Sdk setups/`.

Initialize the declared SignalSmith dependencies after cloning:

```powershell
git submodule update --init --recursive
```

The repository pins SignalSmith Stretch at
`57b93f4e9206a089a45387eaa39bdc9f310d3308` and SignalSmith Linear at
`88c701ce8d581946de5ee587848cde4a572ed6b5`.

## Materialize JUCE

JUCE remains outside Git. The immutable upstream input is JUCE 8.0.12 commit
`29396c22c93392d6738e021b83196283d6e4d850`. The materializer verifies the
pristine archive and tracked APEX patch, extracts into a temporary sibling
directory, verifies the pristine patch target, applies the patch there, and
verifies the patched target before atomic publication. It compares every
relative path and file SHA-256 before accepting an existing SDK and refuses to
replace or modify a mismatched installation:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\materialize_dependencies.ps1 -JuceArchive "..\Sdk setups\juce-8.0.12-windows.zip"
```

The expected archive SHA-256 is
`15B4ED8302127138DFEE98E61A2B5AE1481B39649B139E02E77BFC29462B7EFB`.
The tracked patch is
`Dependencies/patches/juce-8.0.12/0001-apex-asio4all-compatibility.patch`
with SHA-256
`88C03F5BE28A026FC074B51A3E0E9E57840C57435ECEEE700235B0BEF77E257E`.
Its pristine and patched `juce_ASIO_windows.cpp` hashes are respectively
`A681161DEEDCBC341C4649C5A35EDE4847FFDA8B49AB9A0ED43878DB22A51E14`
and
`93355E3422CB4C329823FD980D01CCB4F6315809D118C6463204CD893BAADD10`.
The verified SDK is installed at
`../Sdk setups/juce-8.0.12-windows/JUCE` and is never staged in this
repository.

### Provisional ASIO4ALL Patch

The patch preserves APEX's investigated ASIO4ALL zero-buffer and delayed-
callback compatibility behavior as exactly five hunks and 24 net added lines.
It is an APEX-local patch, not an upstream JUCE change. Its contract status is
`provisional-hardware-validation-required`; it is neither hardware-proven nor
release-approved. It does not change APEX callback code, and build or
dependency-gate success is not evidence that its device behavior is safe.

Release acceptance requires all of the following evidence:

- Capture JUCE ASIO logs and CDB evidence for ASIO4ALL cold open, ordinary and
  rapid close/reopen, control-panel Apply/close, and a WDM endpoint held and
  then released by another process.
- Record every reported buffer minimum, maximum, preferred value, and
  granularity; requested and actual sizes; both `createBuffers` results; the
  `start` result; first-callback latency; and the final opened configuration.
- Reproduce the zero-preferred path and prove that 512 is legal under the
  driver's reported constraints. If it cannot be reproduced, retain the crash
  claim as unverified.
- Exercise callbacks arriving before 3 seconds, between 3 and 10 seconds,
  after 10 seconds, and never. Verify eventual audio flow, bounded recovery,
  driver disposal, and UI responsiveness rather than only `open()` success.
- Repeat startup and reopen cycles enough to expose timing races, with
  playback, recording, and one-owner live monitoring verified after every
  successful open.
- Run vendor-ASIO gates on Apollo Solo USB at 48 kHz/64 and Volt 1 at 48
  kHz/64 and 128, plus Realtek/WASAPI fallback. Record actual sample rate,
  block size, channels, xruns, callback stalls, and crash status.
- Re-run dependency verification, focused device-lifecycle tests, Debug and
  Release x64 rebuilds, and hardware smoke tests after selecting the final
  dependency contract. Compilation alone is not release acceptance.

### WASAPI shared buffer patch

`0004-apex-wasapi-shared-buffer.patch` sizes the standard shared render queue
for large application callbacks and restores small client blocks and shared
sample-rate enumeration. Its identities are pinned by
`juce.compatibilityPatch4` and `Get-ApexWasapiPatch`; both dependency scripts
apply it after patches 0001-0003 before comparing the SDK payload.

Realtek output reproduction, the 32-2048 / 44.1-48 kHz matrix, diagnostic
commands, artifact hashes and remaining hardware limits are recorded in
[`evidence/wasapi-shared-buffer-2048-root-cause-2026-10-05.md`](../evidence/wasapi-shared-buffer-2048-root-cause-2026-10-05.md).
The output regression is verified; microphone/recording and vendor-ASIO
hardware acceptance are separate, outstanding gates.

## Verify Dependencies

Run the build-foundation and dependency gates before building:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\test_build_foundation.ps1
powershell -ExecutionPolicy Bypass -File Scripts\verify_dependencies.ps1
```

The dependency gate requires and verifies the pinned JUCE archive and patch,
reconstructs the transformed SDK in a temporary tree, checks the complete
installed payload against it, and verifies Projucer, both SignalSmith commits
and origins, parent gitlinks, duplicate include topology, and the Projucer
source list.

## Unsigned Builds

Use the single build interface for ordinary verification. `-Unsigned` sets
`APEX_SKIP_SIGNING=1` only around MSBuild and restores the caller's previous
environment value afterward. This path performs no certificate creation,
trust-store modification, timestamping, or signing.

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned
```

The script discovers Visual Studio 18 with `vswhere.exe`, selects amd64
MSBuild, verifies dependencies, builds `DAW_Core.sln` for x64, requires the EXE
and PDB, prints both SHA-256 hashes, and requires `NotSigned` Authenticode
status for unsigned output.

Expected outputs:

```text
Builds/VisualStudio2026/x64/Debug/App/DAW_Core.exe
Builds/VisualStudio2026/x64/Debug/App/DAW_Core.pdb
Builds/VisualStudio2026/x64/Release/App/DAW_Core.exe
Builds/VisualStudio2026/x64/Release/App/DAW_Core.pdb
```

Omitting `-Unsigned` preserves the existing development signing behavior.
Signing is a separate publication operation and must not be enabled for
ordinary verification builds.

A successful compilation verifies the build interface and compiler inputs;
it is not behavioral proof of transport, audio, recording, save, export, or
plugin-host correctness.
