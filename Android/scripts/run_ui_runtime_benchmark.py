#!/usr/bin/env python3
"""Exercise the real APEX APK on an emulator; never clear a physical device.

Record input, native arranger and Android Window timings separately.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time

PACKAGE = "com.xziel.apex.mobileportrait"
PHASES = ("pan", "pinch", "unpinch", "idle", "reverse",
          "vertical-pan", "vertical-pinch", "vertical-unpinch")


def adb(*args: str, binary: bool = False):
    result = subprocess.run(["adb", *args], capture_output=True, timeout=30, check=True)
    return result.stdout if binary else result.stdout.decode("utf-8", errors="replace")


def validate_result(result: dict, scene: str, renderer: str,
                    suppress_master_preview: bool = False,
                    reuse_horizontal_bitmaps: bool = False) -> None:
    if result.get("error"):
        raise ValueError("Native benchmark error: " + str(result["error"]))
    phases = result.get("phases", [])
    if tuple(p.get("phase") for p in phases) != PHASES:
        raise ValueError("Missing real gesture phases")
    if result.get("events", 0) < 20:
        raise ValueError("Insufficient delivered touch events")
    if not result.get("hardware_canvas"):
        raise ValueError("Activity did not use the production accelerated Android Canvas")
    gpu = renderer == "gpu"
    if bool(result.get("master_preview_suppressed")) != suppress_master_preview:
        raise ValueError("Java master preview diagnostic switch mismatch")
    if bool(result.get("horizontal_bitmap_reuse")) != reuse_horizontal_bitmaps:
        raise ValueError("Java horizontal bitmap reuse switch mismatch")
    for phase in phases:
        if phase.get("ui_callback_samples", 0) < 2:
            raise ValueError("Insufficient UI callback timing samples")
        if phase["phase"] != "idle" and phase["inputWallMs"]["count"] < 2:
            raise ValueError("No measured gesture dispatches")
        peer_count = phase["peerWallMs"]["count"]
        if (phase["peerRasterCpuMs"]["count"] != peer_count or phase["peerRasterWaitMs"]["count"] != peer_count):
            raise ValueError("Missing raster CPU versus wall-time profiling samples")
        if not gpu and phase["phase"] != "idle" and peer_count < 2:
            raise ValueError("No measured Android native window paints")
        if phase["peerTiledFrames"] != (0 if renderer == "int-array" else peer_count):
            raise ValueError("The requested native presentation path was not used")
        if phase["peerReducedTimelineFrames"] != 0:
            raise ValueError("The comparison requires native physical density")
        before, after = phase["before"], phase["after"]
        if bool(after.get("masterPreviewSuppressedForBenchmark")) != suppress_master_preview:
            raise ValueError("Native master preview diagnostic switch mismatch")
        if gpu and phase["phase"] != "idle" and after.get("gpuFrames", 0) <= before.get("gpuFrames", 0):
            raise ValueError(f"OpenGL renderer did not advance during {phase['phase']}")
        if scene in ("playback", "recording"):
            if not after.get("playing") or after["playheadFrames"] <= before["playheadFrames"]:
                raise ValueError(f"Playback did not advance during {phase['phase']}")
            if after["audioCallbacks"] <= before["audioCallbacks"]:
                raise ValueError(f"Audio device callbacks did not advance during {phase['phase']}")
        for state in (before, after):
            if state.get("framePacing") != (renderer == "frame-paced"):
                raise ValueError("The requested frame pacing policy did not become active")
            if state.get("tracksWindowBuffered") or state.get("timelineWindowBuffered"):
                raise ValueError("The Android navigation comparison requires direct window painting")
            if renderer == "reference" and state.get("directRowPaint"):
                raise ValueError("Reference case unexpectedly disabled row caching")
            if state.get("trackHeaderHeight") != state["laneHeight"]:
                raise ValueError("Track headers and arranger lanes are different heights")
            # Verify the REAL JNI-backed MainComponent reserved enough
            # horizontal runway to scroll past the historic 30-second edge.
            # Old 9.x APKs passed short pan tests while REC became trapped
            # near bar 15 at 120 BPM because the content stopped at 30s.
            if state.get("timelineContentSeconds", 0) < 600.0:
                raise ValueError("Android arranger still uses a short/chunked 30-second timeline")
            if state.get("timelineContentWidth", 0) < state["pps"] * 600.0 - 1.0:
                raise ValueError("Android arranged content width does not cover the reserved runway")
            if (state.get("clips", 0) > 0) != (scene != "empty"):
                raise ValueError("Requested clip/empty scene was not actually loaded")
            if state.get("backend") != ("OpenGL" if gpu else "software"):
                raise ValueError("The requested arranger backend did not become active")
    if phases[0]["before"]["scrollX"] == phases[0]["after"]["scrollX"]:
        raise ValueError("Pan did not move the production Viewport")
    if not reuse_horizontal_bitmaps and any(p.get("peerMeanReusedTilePixels", 0) for p in phases):
        raise ValueError("Baseline unexpectedly reused compositor bitmaps")
    if reuse_horizontal_bitmaps and any(not phases[i].get("peerScrollTileFrames") for i in (0, 4)):
        raise ValueError("Horizontal gestures did not exercise the compositor bitmap reuse path")
    if scene == "recording":
        # The old 30-second extent trapped REC pans near bar 15 even while
        # the audio callback continued to write new samples. The benchmark
        # now starts an active take at 29s and drags right past 30s.
        if phases[0]["after"]["scrollX"] / phases[0]["after"]["pps"] <= 30.0:
            raise ValueError("Live REC scroll still clamps at the historic 30-second edge")
    for phase, direction in zip(phases[1:3], (1, -1)):
        if (phase["after"]["pps"] - phase["before"]["pps"]) * direction <= 0:
            raise ValueError(f"{phase['phase']} did not change production zoom in the expected direction")
        if phase["after"]["zoomMs"]["count"] == 0:
            raise ValueError(f"{phase['phase']} did not time the native zoom transaction")
    if phases[4]["before"]["scrollX"] == phases[4]["after"]["scrollX"]:
        raise ValueError("Reverse pan did not move the production Viewport")
    if phases[5]["after"]["scrollY"] <= phases[5]["before"]["scrollY"]:
        raise ValueError("Vertical pan did not scroll tracks")
    if renderer in ("retained", "frame-paced"):
        for phase in (phases[0], phases[4], phases[5]):
            before, after = phase["before"], phase["after"]
            if after["arrangerReusePixels"] <= before["arrangerReusePixels"]:
                raise ValueError(f"{phase['phase']} reused no rasterized arranger pixels")
            if after["arrangerRasterPixels"] <= before["arrangerRasterPixels"]:
                raise ValueError(f"{phase['phase']} drew no newly exposed arranger pixels")
            # The Android compositor previously moved a full native-density
            # cache on every horizontal scroll callback. The retained halo
            # must avoid that. Even if the viewport crosses a storage edge,
            # one rebase must serve multiple scroll events.
            if phase["phase"] in ("pan", "reverse"):
                moves = after.get("scrollEvents", 0) - before.get("scrollEvents", 0)
                rebases = after.get("arrangerCacheRebases", 0) - before.get("arrangerCacheRebases", 0)
                if moves >= 6 and rebases > max(2, moves // 3):
                    raise ValueError(
                        f"Excessive Android arranger bitmap rebases during {phase['phase']}: "
                        f"{rebases} for {moves} viewport changes")
    for phase, direction in zip(phases[6:], (1, -1)):
        if (phase["after"]["laneHeight"] - phase["before"]["laneHeight"]) * direction <= 0:
            raise ValueError(f"{phase['phase']} did not change lane height")
    for phase in phases[5:]:
        if phase["before"]["pps"] != phase["after"]["pps"]:
            raise ValueError(f"{phase['phase']} unexpectedly changed horizontal zoom")
    for phase in phases:
        for key in ("rebuilds", "waveformRefreshes"):
            if phase["before"][key] != phase["after"][key]:
                raise ValueError(f"{phase['phase']} unexpectedly changed {key}")
    before, after = phases[0]["before"], phases[0]["after"]
    if suppress_master_preview:
        if after["masterPreviewDirtyPixels"] != before["masterPreviewDirtyPixels"]:
            raise ValueError("Master preview still invalidated during disabled A/B run")
        if after["masterPreviewUpdateCalls"] != before["masterPreviewUpdateCalls"]:
            raise ValueError("Master preview still updated during disabled A/B run")
    elif scene in ("playback", "recording") and after["masterPreviewDirtyPixels"] <= before["masterPreviewDirtyPixels"]:
        raise ValueError("No visible master preview changes during active horizontal scroll")
    if scene == "recording":
        if (not before.get("recording") or not after.get("recording")
                or not after.get("recordingInputAvailable")
                or after["recordedFrames"] <= before["recordedFrames"]
                or after["liveWaveformPaints"] <= before["liveWaveformPaints"]):
            raise ValueError("The live take did not draw new waveform columns before stopping")
    if scene == "deep":
        if phases[1]["after"]["pps"] != 16_000 or phases[6]["after"]["laneHeight"] != 360:
            raise ValueError("Pinches did not reach both new zoom limits")
        if phases[2]["after"]["pps"] <= 4_000:
            raise ValueError("Pinch out did not remain in the expanded horizontal range")
    if scene != "empty":
        for phase in phases[:3]:
            if phase["after"].get("waveformPaints", 0) <= phase["before"].get("waveformPaints", 0):
                raise ValueError(f"No production waveform draw observed during {phase['phase']}")
            if phase["after"].get("waveformPeaksWalked", 0) <= phase["before"].get("waveformPeaksWalked", 0):
                raise ValueError(f"Waveform paint returned without usable cached audio peaks during {phase['phase']}")
        if phases[0]["before"].get("visibleAudioWaveforms", 0) < 1:
            raise ValueError("Benchmark began with no viewport-visible source audio clip")
        if phases[0]["before"].get("readyAudioWaveforms", 0) < 1:
            raise ValueError("Benchmark began before source waveform peaks were ready")
        # The batching optimization must be exercised by at least one broad
        # repaint (usually the horizontal pinch), not merely compiled in.
        if phases[2]["after"].get("waveformBatchedPaints", 0) <= phases[0]["before"].get("waveformBatchedPaints", 0):
            raise ValueError("Grouped waveform drawing was never exercised during horizontal navigation")


def validate_focus(window_dump: str) -> None:
    focus = re.search(r"mCurrentFocus=([^\n]+)", window_dump)
    if focus is None or PACKAGE + "/" not in focus.group(1):
        raise ValueError("APEX does not own the focused window; runtime evidence is obstructed")


def record_focus(path: Path) -> None:
    # The `windows` subcommand omits DisplayContent's mCurrentFocus on API 35.
    # Android's activity-focus handoff is asynchronous even after am start -W.
    # Preserve a hard failure if another app really owns the window, but
    # tolerate the short transition immediately after the native READY marker.
    # Never synthesize focus or silently accept an obstructed screenshot.
    for attempt in range(4):
        dump = adb("shell", "dumpsys", "window")
        path.write_text(dump)
        try:
            validate_focus(dump)
            return
        except ValueError:
            if attempt == 3:
                raise
            time.sleep(0.5)


def screenshot(path: Path) -> None:
    data = adb("exec-out", "screencap", "-p", binary=True)
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("No real Android screenshot")
    width, height = struct.unpack(">II", data[16:24])
    if width < 300 or height < 500 or len(data) < 12000:
        raise ValueError("Blank/trivial or incorrectly sized Android surface")
    path.write_bytes(data)


def run_case(output: Path, renderer: str, scene: str, repeat: int,
             suppress_master_preview: bool = False,
             reuse_horizontal_bitmaps: bool = False) -> dict:
    # Only a disposable CI emulator may disable the master waveform.
    variant = "horizontal-reuse" if reuse_horizontal_bitmaps else renderer
    name = f"{scene}-{variant}{'-master-off' if suppress_master_preview else ''}-{repeat}"
    adb("shell", "am", "force-stop", PACKAGE)
    adb("shell", "pm", "clear", PACKAGE)  # emulator-only guard is mandatory in main()
    adb("shell", "pm", "grant", PACKAGE, "android.permission.RECORD_AUDIO")
    adb("logcat", "-c")
    adb("shell", "am", "start", "-W", "-n", PACKAGE + "/.ApexImmersiveActivity",
        "--es", "apex_renderer", renderer, "--es", "apex_scene", scene,
        "--ez", "apex_master_preview_suppressed",
        "true" if suppress_master_preview else "false",
        "--ez", "apex_horizontal_bitmap_reuse",
        "true" if reuse_horizontal_bitmaps else "false")
    deadline = time.monotonic() + 100
    ready = None
    before = False
    peak = False
    result = None
    phases = {}
    while time.monotonic() < deadline:
        log = adb("logcat", "-d", "-v", "raw", "APEX_UI_BENCH:I", "*:S")
        if "READY " in log and ready is None:
            ready = time.monotonic()
        if ready is not None and not before:
            screenshot(output / f"{name}-before.png")
            record_focus(output / f"{name}-before-window.txt")
            before = True
        if not peak and "PEAK horizontal" in log:
            screenshot(output / f"{name}-peak.png")
            peak = True
        for line in log.splitlines():
            if line.startswith("PHASE "):
                phase = json.loads(line[len("PHASE "):])
                phases[phase["phase"]] = phase
            elif line.startswith("RESULT "):
                result = json.loads(line[len("RESULT "):])
        if result is not None:
            break
        time.sleep(0.5)
    full_log = adb("logcat", "-d", "-v", "threadtime")
    (output / f"{name}-logcat.txt").write_text(full_log)
    # 9.5 proved frame-time wait dominates CPU raster (~38ms off-CPU
    # versus ~9ms CPU). Make the native per-component profiler VISIBLE in
    # the Actions log; retaining it only in a zipped logcat left the
    # adversarial loop without actionable component attribution.
    # Keep the complete trace in the artifact, summarize one independent
    # playback and recording run per renderer to avoid log flooding.
    if scene in ("playback", "recording") and repeat == 1:
        worst_wait = {}
        for line in full_log.splitlines():
            matched = re.search(
                r"PEER_PAINT_WAIT (\S+) calls=(\d+) cpu_ms=([\d.]+) "
                r"wall_ms=([\d.]+) wait_ms=([\d.]+)", line)
            if matched:
                part = matched.group(1)
                cpu, wall, wait = map(float, matched.groups()[2:])
                if part not in worst_wait or wait > worst_wait[part][0]:
                    worst_wait[part] = (wait, cpu, wall)
        for part, (wait, cpu, wall) in sorted(
                worst_wait.items(), key=lambda item: item[1][0], reverse=True)[:10]:
            print(f"[PEER-WAIT] {name} component={part} total_wait_ms={wait:.2f} "
                  f"total_cpu_ms={cpu:.2f} total_wall_ms={wall:.2f}", flush=True)
        timeline_clips = [line for line in full_log.splitlines()
                          if "TIMELINE_CLIP " in line]
        for line in timeline_clips[:6]:
            print(f"[TIMELINE-CLIP] {name} {line.split('TIMELINE_CLIP ', 1)[-1]}", flush=True)
        damage_origins = [line.split("DAMAGE_ORIGIN ", 1)[-1]
                          for line in full_log.splitlines() if "DAMAGE_ORIGIN " in line]
        for line in damage_origins[:24]:
            print(f"[DAMAGE-ORIGIN] {name} {line}", flush=True)
        bitmap_submits = [line.split("BITMAP_SUBMIT ", 1)[-1]
                          for line in full_log.splitlines() if "BITMAP_SUBMIT " in line]
        for line in bitmap_submits[:12]:
            print(f"[BITMAP-SUBMIT] {name} {line}", flush=True)
    if result is None:
        raise RuntimeError(f"{name}: no native runtime result within 100 seconds")
    result["phases"] = [phases[key] for key in PHASES if key in phases]
    (output / f"{name}.json").write_text(json.dumps(result, indent=2))
    screenshot(output / f"{name}-after.png")
    record_focus(output / f"{name}-after-window.txt")
    validate_result(result, scene, renderer, suppress_master_preview, reuse_horizontal_bitmaps)
    first = (output / f"{name}-before.png").read_bytes()
    last = (output / f"{name}-after.png").read_bytes()
    if hashlib.sha256(first).digest() == hashlib.sha256(last).digest():
        raise RuntimeError(f"{name}: production screen did not change")
    result["repeat"] = repeat
    result["master_preview_suppressed"] = suppress_master_preview
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("android-ui-runtime-evidence"))
    args = parser.parse_args()
    if adb("shell", "getprop", "ro.kernel.qemu").strip() != "1":
        raise RuntimeError("Refusing benchmark data reset: target is not an emulator")
    args.output.mkdir(parents=True, exist_ok=True)
    # AOSP's first immersive-mode tutorial and Pixel Launcher's cold-boot ANR
    # obscured the first run. This disposable emulator has no home-screen task
    # under test. Do not suppress APEX ANRs or dismiss errors from the app.
    adb("shell", "settings", "put", "secure", "immersive_mode_confirmations", "confirmed")
    packages = set(adb("shell", "pm", "list", "packages").splitlines())
    for launcher in ("com.google.android.apps.nexuslauncher", "com.android.launcher3"):
        if "package:" + launcher in packages:
            adb("shell", "am", "force-stop", launcher)
            adb("shell", "pm", "disable-user", "--user", "0", launcher)
    adb("install", "-r", "-g", str(args.apk.resolve()))
    time.sleep(15)  # cold-boot system setup is outside the measurement
    results = []
    failures = []
    try:
        for scene in ("playback", "recording", "empty", "clip", "deep"):
            # Both paths retain the same JUCE cache and frame pacing. Only
            # compositor bitmap reuse changes, with both orders tested.
            orders = (("frame-paced", "horizontal-reuse"), ("horizontal-reuse", "frame-paced"))
            if scene == "deep":
                orders += (("int-array",),)
            for repeat, order in enumerate(orders, 1):
                for variant in order:
                    reuse_horizontal = variant == "horizontal-reuse"
                    renderer = "frame-paced" if reuse_horizontal else variant
                    name = f"{scene}-{variant}-{repeat}"
                    try:
                        result = run_case(args.output, renderer, scene, repeat,
                                          reuse_horizontal_bitmaps=reuse_horizontal)
                        results.append(result)
                        print(f"Runtime gesture/surface checks passed: {name}", flush=True)
                        if (scene in ("playback", "recording") and variant == "frame-paced"
                                and repeat == 2):
                            # Paired A/B playback/REC, preserving the imported
                            # clip and live-take waveforms. Only master is
                            # suppressed in this benchmark-only control.
                            control_name = f"{scene}-{renderer}-master-off-{repeat}"
                            try:
                                control = run_case(args.output, renderer, scene, repeat,
                                                   suppress_master_preview=True)
                                results.append(control)
                                print(f"[MASTER-A/B] diagnostic control passed: {control_name}",
                                      flush=True)
                            except Exception as error:
                                failures.append({"case": control_name, "error": str(error)})
                                print(f"Runtime check FAILED: {control_name}: {error}",
                                      flush=True)
                    except Exception as error:
                        failures.append({"case": name, "error": str(error)})
                        (args.output / f"{name}-logcat.txt").write_text(adb("logcat", "-d", "-v", "threadtime"))
                        screenshot(args.output / f"{name}-failure.png")
                        print(f"Runtime check FAILED: {name}: {error}", flush=True)
    finally:
        (args.output / "runtime-results.json").write_text(json.dumps({
            "hardware": "CI x86_64 Android emulator; not Pixel Fold",
            "metric": "UI Choreographer callback intervals; not presented FPS",
            "results": results,
            "failures": failures,
        }, indent=2))
    for result in results:
        for phase in result["phases"]:
            before, after = phase["before"], phase["after"]
            reused = after["arrangerReusePixels"] - before["arrangerReusePixels"]
            redrawn = after["arrangerRasterPixels"] - before["arrangerRasterPixels"]
            copied = after.get("arrangerCacheCopyPixels", 0) - before.get("arrangerCacheCopyPixels", 0)
            rebased = after.get("arrangerCacheRebases", 0) - before.get("arrangerCacheRebases", 0)
            # Attribution of the reported "Minecraft chunk-load" horizontal
            # stalls: the native arranger counts actual full-clip-loop
            # iterations, not just renderer rebuilds or painted waveforms.
            # Log counts relative to real viewport movement so a retained
            # bitmap cannot mask expensive visibility work on the UI thread.
            scroll_events = after.get("scrollEvents", 0) - before.get("scrollEvents", 0)
            clip_checks = after.get("scrollClipChecks", 0) - before.get("scrollClipChecks", 0)
            if scroll_events < 0 or clip_checks < 0:
                raise RuntimeError("Non-monotonic native viewport/clip-scan counters")
            scans_per_scroll = clip_checks / scroll_events if scroll_events else 0.0
            variant = "horizontal-reuse" if result.get("horizontal_bitmap_reuse") else result["renderer_requested"]
            print(f"{result['scene']} {variant} "
                  f"pass {result['repeat']} "
                  f"master={'off' if result.get('master_preview_suppressed') else 'on'} "
                  f"{phase['phase']} "
                  f"p95={phase['ui_interval_p95_ms']:.1f} ms, "
                  f"worst={phase['ui_interval_worst_ms']:.1f} ms, "
                  f"peer draw p95={phase['peerWallMs']['p95']:.2f} ms, "
                  f"peer transfer p95={phase['peerTransferMs']['p95']:.2f} ms, "
                  f"window draw p95={phase['windowDrawMs']['p95']:.2f} ms, "
                  f"GPU p95={phase['windowGpuMs']['p95']:.2f} ms, "
                  f"bitmap pixels updated/reused={phase.get('peerMeanUpdatedTilePixels', 0):,}/"
                  f"{phase.get('peerMeanReusedTilePixels', 0):,}, "
                  f"scroll events={scroll_events} clip iterations={clip_checks} "
                  f"checks/event={scans_per_scroll:.2f}, "
                  f"arranger pixels reused={reused:,}/redrawn={redrawn:,} "
                  f"cache pixels shifted={copied:,} rebases={rebased}")
    for scene in ("empty", "clip", "playback", "recording", "deep"):
        for repeat in (1, 2):
            paired = {r["horizontal_bitmap_reuse"]: r for r in results
                      if r["scene"] == scene and r["repeat"] == repeat
                      and r["renderer_requested"] == "frame-paced"
                      and not r["master_preview_suppressed"]}
            if len(paired) < 2:
                continue
            for phase_index in (0, 4, 5):
                baseline, reuse = (paired[enabled]["phases"][phase_index] for enabled in (False, True))
                print(f"[BITMAP-A/B] {scene} pass={repeat} {reuse['phase']} "
                      f"callback p95={baseline['ui_interval_p95_ms']:.1f}/{reuse['ui_interval_p95_ms']:.1f}ms "
                      f"worst={baseline['ui_interval_worst_ms']:.1f}/{reuse['ui_interval_worst_ms']:.1f}ms "
                      f"window p95={baseline['windowTotalMs']['p95']:.1f}/{reuse['windowTotalMs']['p95']:.1f}ms "
                      f"bitmap pixels updated={baseline.get('peerMeanUpdatedTilePixels', 0):,}/"
                      f"{reuse.get('peerMeanUpdatedTilePixels', 0):,}")
    # A/B is diagnostic, not a pass/fail 60fps certification. Use the same
    # renderer/transport and report the master work separately from rendering.
    for scene in ("playback", "recording"):
        paired = [x for x in results if x["scene"] == scene
                  and x["renderer_requested"] == "frame-paced"
                  and x["repeat"] == 2 and not x.get("horizontal_bitmap_reuse")]
        enabled = next((x for x in paired if not x.get("master_preview_suppressed")), None)
        disabled = next((x for x in paired if x.get("master_preview_suppressed")), None)
        if enabled is not None and disabled is not None:
            for axis in ("pan", "reverse", "vertical-pan", "idle"):
                on = next(p for p in enabled["phases"] if p["phase"] == axis)
                off = next(p for p in disabled["phases"] if p["phase"] == axis)
                on_ms = on["ui_interval_p95_ms"]
                off_ms = off["ui_interval_p95_ms"]
                on_repaints = (on["after"]["masterPreviewRepaintCalls"]
                               - on["before"]["masterPreviewRepaintCalls"])
                off_repaints = (off["after"]["masterPreviewRepaintCalls"]
                                - off["before"]["masterPreviewRepaintCalls"])
                print(f"[MASTER-A/B] {scene} {axis}: on p95={on_ms:.1f}ms "
                      f"off p95={off_ms:.1f}ms delta={on_ms-off_ms:+.1f}ms "
                      f"master repaint requests on={on_repaints} off={off_repaints} "
                      f"master update p95={on['after']['masterPreviewUpdateMs']['p95']:.3f}ms "
                      f"master draw p95={on['after']['masterPreviewPaintMs']['p95']:.3f}ms",
                      flush=True)
    # This job validates gesture correctness, NOT 60fps presentation.
    # The x86_64 SwiftShader emulator reports Choreographer callback spacing,
    # not SurfaceFlinger-presented frame intervals. Report misses explicitly
    # so GREEN functional checks cannot be mistaken for a physical-phone fix.
    target_60hz_ms = 1000.0 / 60.0
    slow = [(result["scene"], "horizontal-reuse" if result["horizontal_bitmap_reuse"]
             else result["renderer_requested"], result["repeat"],
             phase["phase"], phase["ui_interval_p95_ms"])
            for result in results for phase in result["phases"]
            if phase["ui_interval_p95_ms"] > target_60hz_ms]
    if slow:
        print(f"[PERFORMANCE-NOT-PROVEN] {len(slow)} of "
              f"{sum(len(x['phases']) for x in results)} emulator phases "
              f"exceeded a 60Hz callback interval at p95; "
              "functional checks alone do not certify smooth scrolling",
              flush=True)
        for scene, renderer, repeat, phase, value in sorted(
                slow, key=lambda entry: entry[4], reverse=True)[:12]:
            print(f"[SLOW-UI-CALLBACK] {scene} {renderer} pass={repeat} "
                  f"phase={phase} p95={value:.1f}ms", flush=True)
    else:
        print("[PERFORMANCE-NOT-PROVEN] 60Hz callback intervals met in the "
              "emulator; actual Pixel Fold frame presentation still untested",
              flush=True)
    if failures:
        raise RuntimeError(f"{len(failures)} runtime cases failed; review retained evidence")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
