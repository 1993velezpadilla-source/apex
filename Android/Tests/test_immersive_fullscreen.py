#!/usr/bin/env python3
"""Portable regression for JUCE 8 Android manifests without package attributes."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

REPO = Path(__file__).resolve().parents[2]
PATCHER = REPO / "Android/scripts/enable_immersive_fullscreen.py"
ANDROID_NS = "http://schemas.android.com/apk/res/android"

MANIFEST = '''<?xml version="1.0" encoding="UTF-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
  <application>
    <activity android:name="com.rmsl.juce.JuceActivity">
      <intent-filter>
        <action android:name="android.intent.action.MAIN"/>
        <category android:name="android.intent.category.LAUNCHER"/>
      </intent-filter>
    </activity>
  </application>
</manifest>
'''


def verify_variant(preview: bool) -> None:
    package = ("com.xziel.apex.mobileportrait" if preview
               else "com.xziel.apex.fulltest")
    with tempfile.TemporaryDirectory(prefix="apex-immersive-") as temp:
        project = Path(temp)
        manifest = project / "app/src/main/AndroidManifest.xml"
        manifest.parent.mkdir(parents=True)
        manifest.write_text(MANIFEST, encoding="utf-8")
        gradle = project / "app/build.gradle"
        gradle.write_text(f'''android {{
  namespace "{package}"
  defaultConfig {{ applicationId "{package}" }}
}}
''', encoding="utf-8")
        env = dict(os.environ, APEX_ANDROID_PORTRAIT_PREVIEW="1" if preview else "0")
        cmd = [sys.executable, str(PATCHER), "--project", str(project)]
        subprocess.run(cmd, env=env, check=True, capture_output=True, text=True)
        activity = ET.parse(manifest).getroot().find("./application/activity")
        assert activity is not None
        assert activity.get(f"{{{ANDROID_NS}}}name") == package + ".ApexImmersiveActivity"
        assert activity.get(f"{{{ANDROID_NS}}}screenOrientation") != "portrait"
        java = (project / "app/src/main/java"
                / package.replace(".", "/") / "ApexImmersiveActivity.java")
        text = java.read_text(encoding="utf-8")
        assert f"package {package};" in text
        assert "WindowInsetsController" in text
        assert "SCREEN_ORIENTATION_PORTRAIT" not in text
        # No accidental mutation of original Windows app or source.
        assert not (project / "DAW_Core.jucer").exists()
        # Fail hard on a Gradle app ID mismatch, never rewrite a wrong app.
        gradle.write_text('applicationId "com.example.wrong"', encoding="utf-8")
        rejected = subprocess.run(cmd, env=env, capture_output=True, text=True)
        assert rejected.returncode != 0
        assert "mismatch" in (rejected.stderr + rejected.stdout)


if __name__ == "__main__":
    verify_variant(False)
    verify_variant(True)
    print("APEX Android immersive manifest regression: fulltest + mobileportrait GREEN")
