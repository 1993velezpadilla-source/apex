#!/usr/bin/env python3
"""Generate a *compile experiment* for the REAL Windows APEX desktop sources.

This is an isolated JUCE Android Studio exporter targeting existing APEX
Main.cpp/MainComponent.cpp and every source listed in the canonical .jucer.
Do not confuse a successful XML export with an installable or usable DAW.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    repo = args.repo_root.resolve()
    original = repo / "My DAW" / "DAW_Core" / "DAW_Core.jucer"
    output = original.with_name("DAW_Core_AndroidFull.jucer")
    if not original.is_file():
        print(f"ERROR: missing canonical desktop .jucer: {original}", file=sys.stderr)
        return 1

    tree = ET.parse(original)
    root = tree.getroot()
    if root.tag != "JUCERPROJECT":
        raise ValueError(f"Expected JUCERPROJECT, got {root.tag}")
    root.set("name", "APEXFullAndroid")
    root.set("bundleIdentifier", "com.xziel.apex.fulltest")
    root.set("version", "0.1.0")
    # Do not let this experimental build generate VST3/ASIO host code on Android.
    defines = [
        flag for flag in root.get("defines", "").split()
        if not flag.startswith(("JUCE_ASIO=", "JUCE_PLUGINHOST_VST3="))
    ]
    defines.extend(["JUCE_ASIO=0", "JUCE_PLUGINHOST_VST3=0"])
    root.set("defines", " ".join(defines))

    modules = root.find("MODULES")
    exports = root.find("EXPORTFORMATS")
    if modules is None or exports is None:
        raise ValueError("Canonical .jucer missing MODULES/EXPORTFORMATS")

    for exporter in list(exports):
        exports.remove(exporter)
    android = ET.SubElement(
        exports, "ANDROIDSTUDIO",
        {
            "targetFolder": "Builds/AndroidFull",
            "androidMinimumSDK": "26",
            "microphonePermissionNeeded": "1",
            "androidEnableContentSharing": "1",
        },
    )
    configs = ET.SubElement(android, "CONFIGURATIONS")
    ET.SubElement(configs, "CONFIGURATION", {
        "name": "Debug", "isDebug": "1", "androidArchitectures": "arm64-v8a",
        "optimisation": "1", "linkTimeOptimisation": "0",
    })
    ET.SubElement(configs, "CONFIGURATION", {
        "name": "Release", "isDebug": "0", "androidArchitectures": "arm64-v8a", "optimisation": "3",
    })
    paths = ET.SubElement(android, "MODULEPATHS")
    for module in modules.findall("MODULE"):
        ident = module.get("id")
        if not ident:
            raise ValueError("Module without ID")
        ET.SubElement(paths, "MODULEPATH", {
            "id": ident, "path": "../../third_party/JUCE/modules",
        })

    # Report missing APEX source files, without silently removing any modules.
    listed = root.findall(".//FILE")
    missing = []
    compiled = 0
    for entry in listed:
        relative = entry.get("file")
        if relative is None:
            continue
        if entry.get("compile") == "1":
            compiled += 1
        if not (original.parent / relative).exists():
            missing.append(relative)

    if hasattr(ET, "indent"):
        ET.indent(tree, space="  ")
    tree.write(output, encoding="utf-8", xml_declaration=True)
    print(f"Android Full exporter: {output}")
    print(f"Desktop files retained: {len(listed)}; translation units: {compiled}")
    print(f"Missing original source files: {len(missing)}")
    for path in missing[:40]:
        print(f"  MISSING {path}")
    if len(missing) > 40:
        print(f"  ... {len(missing) - 40} additional missing files")
    print("WARNING: Projucer export is not a build; full APEX remains UNVERIFIED.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
