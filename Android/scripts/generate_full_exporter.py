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
    # Android uses in-process JUCE/native plugins, not the Windows
    # subprocess/VST3 sandbox. The original Windows .jucer is NOT modified.
    defines.extend([
        "JUCE_ASIO=0",
        "JUCE_PLUGINHOST_VST3=0",
        "APEX_ENABLE_PLUGIN_SANDBOX=0",
    ])
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
    # Projucer's Android exporter emits headerPath relative to the generated
    # Builds/AndroidFull directory (and then prefixes ../ from app/).
    # Two parent segments are needed to reach DAW_Core/Source from Builds.
    # Verified against the Android clang++ -I commands in the first ARM64 log.
    shared_headers = ";".join((
        "../../Source/ThirdParty",
        "../../Source/ThirdParty/signalsmith-stretch",
        "../../Source/ThirdParty/signalsmith-stretch/include",
        "../../Source/ThirdParty/signalsmith-linear",
    ))
    configs = ET.SubElement(android, "CONFIGURATIONS")
    ET.SubElement(configs, "CONFIGURATION", {
        "name": "Debug", "isDebug": "1", "androidArchitectures": "arm64-v8a",
        "optimisation": "1", "linkTimeOptimisation": "0",
        "headerPath": shared_headers,
    })
    ET.SubElement(configs, "CONFIGURATION", {
        "name": "Release", "isDebug": "0", "androidArchitectures": "arm64-v8a", "optimisation": "3",
        "headerPath": shared_headers,
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
    android_excluded = []
    android_utf8_remapped = []
    # Some original Windows translation units are UTF-16 LE. Android's Clang
    # rejects their BOM; convert into a generated UTF-8 sibling and point ONLY
    # the generated Android exporter at it (never rewrite canonical sources).
    utf16_windows_units = {
        "Builds/VisualStudio2026/ArrangementEditor/AutomationImplementations.cpp",
    }
    # Windows' SEH (__try/__except) + ASIO wrappers are not Android source:
    # JUCE's Android AudioIODevice backend handles Android/USB audio instead.
    # This changes ONLY the generated exporter. Windows .jucer stays intact.
    windows_only_translation_units = {
        "Source/DeviceCore/SafeAsioDeviceTypeCore.cpp",
    }
    for entry in listed:
        relative = entry.get("file")
        if relative is None:
            continue
        source_path = original.parent / relative
        if not source_path.exists():
            missing.append(relative)
            continue
        if relative in windows_only_translation_units:
            entry.set("compile", "0")
            android_excluded.append(relative)
        if relative in utf16_windows_units and entry.get("compile") == "1":
            data = source_path.read_bytes()
            if data.startswith((b"\xff\xfe", b"\xfe\xff")):
                decoded = data.decode("utf-16")
            else:
                decoded = data.decode("utf-8-sig")
            utf8_copy = source_path.with_name(source_path.stem + "_AndroidUTF8.cpp")
            utf8_copy.write_bytes(decoded.encode("utf-8"))
            entry.set("file", utf8_copy.relative_to(original.parent).as_posix())
            android_utf8_remapped.append(relative)
        if entry.get("compile") == "1":
            compiled += 1

    if hasattr(ET, "indent"):
        ET.indent(tree, space="  ")
    tree.write(output, encoding="utf-8", xml_declaration=True)
    print(f"Android Full exporter: {output}")
    print(f"Desktop files retained: {len(listed)}; translation units: {compiled}")
    print(f"Windows-only translation units excluded from Android: {len(android_excluded)}")
    for path in android_excluded:
        print(f"  ANDROID-EXCLUDED {path}")
    print(f"UTF-16 source units remapped to generated UTF-8 copies: {len(android_utf8_remapped)}")
    for path in android_utf8_remapped:
        print(f"  ANDROID-UTF8 {path}")
    print(f"Missing original source files: {len(missing)}")
    for path in missing[:40]:
        print(f"  MISSING {path}")
    if len(missing) > 40:
        print(f"  ... {len(missing) - 40} additional missing files")
    print("WARNING: Projucer export is not a build; full APEX remains UNVERIFIED.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
