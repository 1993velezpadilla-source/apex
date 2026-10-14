# APEX Tune Live — verified release size and real-time CPU profiling

## Why a ~5.6 MB download can be the same ~14 MB executable

The file delivered for the first singer test was a **ZIP**, not the uncompressed `DAW_Core.exe`. The ZIP is not a stripped-down DAW.

Verified against the **actual Windows x64 Release binary** uploaded by CI on 2026-10-13 (run 38399310851, artifact 11813984198):

| File | Bytes | Meaning |
| --- | ---: | --- |
| `DAW_Core.exe` | **14,124,032** (13.47 MiB; ~14.1 decimal MB) | Windows APEX Release DAW, native Tune Live installed in the APEX native host |
| `APEX_Tune_Live_Windows_Test_Only.zip` | **5,780,977** (~5.51 MiB) | ZIP/Deflate transfer package containing that exact EXE |
| Full Actions Release artifact | **55,595,843** (~53.02 MiB) | ZIP containing `DAW_Core.exe` plus developer PDB symbols |
| `DAW_Core.pdb` (inside full artifact) | **170,536,960** (~162.64 MiB) | Debug symbols for crash diagnosis; not needed just to run the EXE |

The uncompressed `DAW_Core.exe` SHA-256 on that CI build is:

`263f86c52e6dbcbecaabe3103a5b9065221ca1cc164ddb6868422e2f364a20a8`

We verified the *same* SHA-256 in the compressed test-only distribution and the independently downloaded EXE. Nothing was deleted from the app to reach ~5.6 MB: normal ZIP compression explains that size. A local ~40 GB Visual Studio/JUCE build directory can contain sources, intermediates, caches and prior builds and does **not** imply a ~40 GB release binary. The previous local EXE (13–15 MB, user-reported) is the same order of magnitude as the new 14.1 MB binary; byte-identical comparison requires the actual old EXE.

**Do not infer plugin audio fidelity or feature completeness from the size of an EXE or ZIP.** Different compiler/linker flags, CRT choices, included assets and debug symbols can change the binary size without changing sound. This build is unsigned (`Authenticode NotSigned`), not a retail-signed installer.

## How the CPU bench was fixed

The older `test_eight_insert_telemetry.cpp` loop had `std::sin` and `std::pow` voice synthesis *inside* its timed callback. The result mixed test-signal generation with actual real-time pitch processing, making CPU measurements hard to interpret.

As of commits `1ec3962` and `4b43ae3`:
- Production `DspCore` computes a 10 ms envelope coefficient during `prepare()`, not for every audio sample.
- The dry-PDC delay-line cursor wraps with one branch rather than a division/modulo in the per-sample inner loop. This must preserve exact delay/PDC timing and must pass the existing strict 44.1/48/96kHz mono/stereo, bypass, wet correction and reproducible bounce tests.
- The eight-insert test creates 2 stereo and 6 mono, 3 seconds of harmonically rich, subtly vibrato-modulated source **before** starting its benchmark clock. It primes DSP for one second and times **only calls to the production `process()`** over the remaining two seconds.
- It reports aggregate DSP-only callback median, p95, p99, maximum, and fraction of callbacks longer than the 128-sample/48kHz buffer budget of ~2.67 ms. It also checks finite output and nonzero energy.
- Timings are **diagnostic**, not release pass/fail thresholds on shared, non-realtime CI machines: runner scheduling/CPU contention do not measure Apollo Solo ASIO round-trip or hardware xruns.

Keep the original **40 ms plugin PDC**. The separate ~14.7ms experimental F0 seed is not production audio monitoring latency and does not prove that we can replace the HQ pitch shifter. Promote algorithmic audio changes only after singer test + no-regression QA. Keep editor/UI unchanged until separately requested.
