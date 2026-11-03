#!/usr/bin/env python3
"""Pin the NDK used by the generated JUCE Android Gradle app.

AGP's implicit default currently requests NDK 28.1, even though CI explicitly
installs 26.3. Avoid an unnecessary network download on every APEX build.
Only the generated AndroidFull app/build.gradle is modified, never the .jucer.
"""
from __future__ import annotations

from pathlib import Path
import re

NDK_VERSION = "26.3.11579264"
APP_GRADLE = (Path(__file__).resolve().parents[2] / "My DAW" / "DAW_Core"
              / "Builds" / "AndroidFull" / "app" / "build.gradle")


def pin_ndk_version(content: str, version: str = NDK_VERSION) -> str:
    existing = re.compile(r"(?m)^(?P<indent>[ \t]*)ndkVersion\s*(?:=\s*)?['\"][^'\"\n]+['\"]\s*$")
    match = existing.search(content)
    if match:
        indent = match.group("indent")
        return existing.sub(f'{indent}ndkVersion "{version}"', content, count=1)

    android = re.compile(r"(?m)^(?P<indent>[ \t]*)android\s*\{")
    match = android.search(content)
    if not match:
        raise ValueError("Generated app/build.gradle has no android { block")
    insertion = f'\n{match.group("indent")}    ndkVersion "{version}"'
    return content[:match.end()] + insertion + content[match.end():]


def main() -> int:
    if not APP_GRADLE.is_file():
        raise SystemExit(f"Missing generated app Gradle file: {APP_GRADLE}")
    before = APP_GRADLE.read_text(encoding="utf-8")
    after = pin_ndk_version(before)
    APP_GRADLE.write_text(after, encoding="utf-8")
    assert pin_ndk_version(after) == after, "NDK pin must be idempotent"
    print(f"[APEX-ANDROID] Pinned generated app NDK to {NDK_VERSION}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
