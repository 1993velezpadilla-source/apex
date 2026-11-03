#!/usr/bin/env python3
"""Record real-device scroll/zoom stalls with Perfetto and existing APEX markers.

Open APEX on the Fold's front display before running this from an ADB-connected
computer. Capture empty and clip sessions separately. No APK install or reset.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess

PACKAGE = "com.xziel.apex.mobileportrait"


def trace_config(seconds: int) -> str:
    return f'''buffers {{ size_kb: 32768 fill_policy: RING_BUFFER }}
duration_ms: {seconds * 1000}
data_sources {{ config {{
  name: "linux.ftrace"
  ftrace_config {{
    ftrace_events: "sched/sched_switch"
    ftrace_events: "sched/sched_waking"
    ftrace_events: "power/cpu_frequency"
    ftrace_events: "ftrace/print"
    atrace_categories: "gfx"
    atrace_categories: "view"
    atrace_categories: "input"
    atrace_categories: "wm"
    atrace_apps: "{PACKAGE}"
  }}
}} }}
data_sources {{ config {{
  name: "linux.process_stats"
  process_stats_config {{ scan_all_processes_on_start: true }}
}} }}
data_sources {{ config {{ name: "android.surfaceflinger.frametimeline" }} }}
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", required=True, choices=("empty", "clip"))
    parser.add_argument("--seconds", type=int, default=20)
    parser.add_argument("--serial", help="ADB serial, when more than one device is connected")
    parser.add_argument("--output", type=Path, default=Path("output/apex-device-traces"))
    parser.add_argument("--dry-run", action="store_true", help="Print config without contacting a device or writing files")
    args = parser.parse_args()
    if not 10 <= args.seconds <= 60:
        parser.error("--seconds must be between 10 and 60")
    config = trace_config(args.seconds)
    if args.dry_run:
        print(config)
        return 0
    prefix = ["adb"] + (["-s", args.serial] if args.serial else [])

    def adb(*command: str, data: str | None = None, timeout: int = 30) -> str:
        result = subprocess.run(prefix + list(command), input=data, text=True,
                                capture_output=True, timeout=timeout)
        if result.returncode:
            raise RuntimeError("ADB failed: " + result.stderr.strip() + result.stdout.strip())
        return result.stdout

    if adb("get-state").strip() != "device":
        raise RuntimeError("An authorized ADB device connection is required")
    focus = adb("shell", "dumpsys", "window")
    focus_lines = [line.strip() for line in focus.splitlines() if "mCurrentFocus=" in line]
    if not any(PACKAGE + "/" in line for line in focus_lines):
        raise RuntimeError("Open the APEX Mobile timeline on the front display first")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    name = f"apex-{args.scene}-{stamp}"
    args.output.mkdir(parents=True, exist_ok=True)
    metadata = {
        "scene_requested": args.scene, "duration_seconds": args.seconds,
        "model": adb("shell", "getprop", "ro.product.model").strip(),
        "android": adb("shell", "getprop", "ro.build.version.release").strip(),
        "sdk": adb("shell", "getprop", "ro.build.version.sdk").strip(),
        "emulator": adb("shell", "getprop", "ro.kernel.qemu").strip() == "1",
        "display_size": adb("shell", "wm", "size").strip(),
        "display_density": adb("shell", "wm", "density").strip(),
        "focused_window": focus_lines,
        "package_version": [line.strip() for line in adb("shell", "dumpsys", "package", PACKAGE).splitlines()
                            if "versionCode=" in line or "versionName=" in line],
    }
    (args.output / (name + ".json")).write_text(json.dumps(metadata, indent=2))
    (args.output / (name + ".pbtxt")).write_text(config)
    remote = "/data/misc/perfetto-traces/" + name + ".perfetto-trace"
    print(f"Recording {args.seconds} seconds: scroll and pinch in APEX now.", flush=True)
    log = adb("shell", "perfetto", "--txt", "-c", "-", "-o", remote,
              data=config, timeout=args.seconds + 30)
    (args.output / (name + "-capture.txt")).write_text(log)
    trace = args.output / (name + ".perfetto-trace")
    partial = trace.with_suffix(trace.suffix + ".partial")
    adb("pull", remote, str(partial))
    if partial.stat().st_size < 1024:
        raise RuntimeError("Trace is empty/trivial; keep diagnostics and repeat capture")
    partial.replace(trace)
    print("Saved real-device trace: " + str(trace.resolve()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
