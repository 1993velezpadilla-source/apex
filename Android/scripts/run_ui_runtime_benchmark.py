#!/usr/bin/env python3
"""Exercise the real APEX APK on an emulator; never clear a physical device.

UI Choreographer intervals are input/UI-thread cadence, NOT presented FPS.
The same APK supports both backends through explicit launch extras.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time

PACKAGE = "com.xziel.apex.mobileportrait"


def adb(*args: str, binary: bool = False):
    result = subprocess.run(["adb", *args], capture_output=True, timeout=30, check=True)
    return result.stdout if binary else result.stdout.decode("utf-8", errors="replace")


def validate_result(result: dict, renderer: str, scene: str) -> None:
    if result.get("error"):
        raise ValueError("Native benchmark error: " + str(result["error"]))
    phases = result.get("phases", [])
    if [p.get("phase") for p in phases] != ["pan", "pinch", "idle", "reverse"]:
        raise ValueError("Missing real gesture phases")
    if result.get("events", 0) < 20:
        raise ValueError("Insufficient delivered touch events")
    for phase in phases:
        if phase.get("ui_callback_samples", 0) < 2:
            raise ValueError("Insufficient UI callback timing samples")
        for state in (phase["before"], phase["after"]):
            if (state.get("clips", 0) > 0) != (scene == "clip"):
                raise ValueError("Requested clip/empty scene was not actually loaded")
            expected = "opengl" if renderer == "gpu" else "software"
            if state.get("backend") != expected:
                raise ValueError("Requested backend not active (including silent fallback)")
            if renderer == "gpu" and not state.get("gpuReady"):
                raise ValueError("GPU context not active")
    if phases[0]["before"]["scrollX"] == phases[0]["after"]["scrollX"]:
        raise ValueError("Pan did not move the production Viewport")
    if phases[1]["before"]["pps"] == phases[1]["after"]["pps"]:
        raise ValueError("Pinch did not change production zoom")
    if phases[3]["before"]["scrollX"] == phases[3]["after"]["scrollX"]:
        raise ValueError("Reverse pan did not move the production Viewport")
    if renderer == "gpu" and phases[-1]["after"]["renderCallbacks"] <= phases[0]["before"]["renderCallbacks"]:
        raise ValueError("GPU callback counter did not advance")


def screenshot(path: Path) -> None:
    data = adb("exec-out", "screencap", "-p", binary=True)
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("No real Android screenshot")
    width, height = struct.unpack(">II", data[16:24])
    if width < 300 or height < 500 or len(data) < 12000:
        raise ValueError("Blank/trivial or incorrectly sized Android surface")
    path.write_bytes(data)


def run_case(output: Path, renderer: str, scene: str, repeat: int) -> dict:
    name = f"{scene}-{renderer}-{repeat}"
    adb("shell", "am", "force-stop", PACKAGE)
    adb("shell", "pm", "clear", PACKAGE)  # emulator-only guard is mandatory in main()
    adb("shell", "pm", "grant", PACKAGE, "android.permission.RECORD_AUDIO")
    adb("logcat", "-c")
    adb("shell", "am", "start", "-W", "-n", PACKAGE + "/.ApexImmersiveActivity",
        "--es", "apex_renderer", renderer, "--es", "apex_scene", scene)
    deadline = time.monotonic() + 100
    ready = None
    before = middle = False
    result = None
    while time.monotonic() < deadline:
        log = adb("logcat", "-d", "-v", "raw", "APEX_UI_BENCH:I", "*:S")
        if "READY " in log and ready is None:
            ready = time.monotonic()
        if ready is not None and not before:
            screenshot(output / f"{name}-before.png")
            before = True
        if ready is not None and time.monotonic() - ready > 5.5 and not middle:
            screenshot(output / f"{name}-pinch.png")
            middle = True
        for line in log.splitlines():
            if line.startswith("RESULT "):
                result = json.loads(line[len("RESULT "):])
        if result is not None:
            break
        time.sleep(0.5)
    (output / f"{name}-logcat.txt").write_text(adb("logcat", "-d", "-v", "threadtime"))
    if result is None:
        raise RuntimeError(f"{name}: no native runtime result within 100 seconds")
    (output / f"{name}.json").write_text(json.dumps(result, indent=2))
    validate_result(result, renderer, scene)
    screenshot(output / f"{name}-after.png")
    first = (output / f"{name}-before.png").read_bytes()
    last = (output / f"{name}-after.png").read_bytes()
    if hashlib.sha256(first).digest() == hashlib.sha256(last).digest():
        raise RuntimeError(f"{name}: production screen did not change")
    result["repeat"] = repeat
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("android-ui-runtime-evidence"))
    args = parser.parse_args()
    if adb("shell", "getprop", "ro.kernel.qemu").strip() != "1":
        raise RuntimeError("Refusing benchmark data reset: target is not an emulator")
    args.output.mkdir(parents=True, exist_ok=True)
    adb("install", "-r", "-g", str(args.apk.resolve()))
    results = []
    try:
        for scene in ("empty", "clip"):
            # Reverse the order in the second pass to expose warm-up/order bias.
            for repeat, renderers in enumerate((("software", "gpu"), ("gpu", "software")), 1):
                for renderer in renderers:
                    result = run_case(args.output, renderer, scene, repeat)
                    results.append(result)
                    print(f"Runtime gesture/surface checks passed: {scene}, {renderer}, pass {repeat}", flush=True)
    finally:
        (args.output / "runtime-results.json").write_text(json.dumps({
            "hardware": "CI x86_64 Android emulator; not Pixel Fold",
            "metric": "UI Choreographer callback intervals; not presented FPS",
            "results": results,
        }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
