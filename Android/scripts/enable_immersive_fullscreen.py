#!/usr/bin/env python3
"""Enable native Android immersive fullscreen for the REAL JUCE APEX Android export.

Only patches GENERATED Android Studio files after Projucer --resave. Never
changes the original Windows .jucer, JUCE Android Java source, or APEX DSP.
The old JUCE Component::setFullScreen(true) does not reliably hide native
status/navigation system bars on Android 15/16.
"""
from __future__ import annotations

from pathlib import Path
import argparse
import xml.etree.ElementTree as ET

ANDROID_NS = "http://schemas.android.com/apk/res/android"
ANDROID_ACTIVITY = "com.xziel.apex.mobileportraitqa.ApexImmersiveActivity"
JAVA_SOURCE = r"""package com.xziel.apex.mobileportraitqa;

import android.os.Build;
import android.os.Bundle;
import android.content.pm.ActivityInfo;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

/**
 * Real JUCE activity with Android 11+ WindowInsetsController immersive mode.
 *
 * No global immersive polling timer (keeps the DAW UI thread free).
 * Reapply after focus/resume: opening plugin editors, Android permissions,
 * file picker dialogs, and returning from the notification shade can reset
 * system-bar visibility. Android's edge gestures still work normally.
 */
public final class ApexImmersiveActivity extends com.rmsl.juce.JuceActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        ensureSensorRotation();
        applyApexImmersiveMode();
    }

    @Override
    protected void onResume() {
        super.onResume();
        ensureSensorRotation();
        applyApexImmersiveMode();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            ensureSensorRotation();
            applyApexImmersiveMode();
        }
    }

    private void ensureSensorRotation() {
        // Pixel Fold: allow portrait/landscape regardless of Android quick-setting
        // rotation lock. This is QA policy for the mobile DAW, not an OS change.
        if (getRequestedOrientation() != ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR)
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR);
    }

    private void applyApexImmersiveMode() {
        final View decor = getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            final WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.statusBars()
                                | WindowInsets.Type.navigationBars());
            }
        } else {
            // Project minSdk=26. Legacy flags are supported up to API 29.
            decor.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }
}
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path,
        default=Path("My DAW/DAW_Core/Builds/AndroidFull"))
    args = parser.parse_args()
    generated = args.project.resolve()
    manifest = generated / "app/src/main/AndroidManifest.xml"
    if not manifest.is_file():
        raise RuntimeError("Projucer must export the original Android app first: " + str(manifest))

    ET.register_namespace("android", ANDROID_NS)
    tree = ET.parse(manifest)
    root = tree.getroot()
    candidates = root.findall("./application/activity")
    launcher = [
        a for a in candidates
        if any(
            any(category.get(f"{{{ANDROID_NS}}}name")
                    == "android.intent.category.LAUNCHER"
                    for category in filt.findall("category"))
            for filt in a.findall("intent-filter"))
    ]
    if len(launcher) != 1:
        raise RuntimeError(f"Expected exactly one launcher Activity, found {len(launcher)}")
    activity = launcher[0]
    old = activity.get(f"{{{ANDROID_NS}}}name", "")
    if old not in ("com.rmsl.juce.JuceActivity", ANDROID_ACTIVITY):
        raise RuntimeError("Unexpected launcher activity; refuse unsafe patch: " + old)
    activity.set(f"{{{ANDROID_NS}}}name", ANDROID_ACTIVITY)
    # JUCE must consume rotation changes instead of destroying/recreating the
    # DAW session. Both configuration flags must survive the Projucer export.
    config_changes = set(activity.get(f"{{{ANDROID_NS}}}configChanges", "").split("|"))
    if not {"orientation", "screenSize"}.issubset(config_changes):
        raise RuntimeError("Android Activity would restart the DAW on rotation")

    # Projucer's Android exporter (or a stale generated manifest) can keep an
    # orientation lock even after the C++ Desktop mask is corrected.
    # FULL_SENSOR permits portrait and landscape when Android auto-rotate is off.
    activity.set(f"{{{ANDROID_NS}}}screenOrientation", "fullSensor")
    tree.write(manifest, encoding="utf-8", xml_declaration=True)

    java = (generated / "app/src/main/java/com/xziel/apex/mobileportraitqa"
            / "ApexImmersiveActivity.java")
    java.parent.mkdir(parents=True, exist_ok=True)
    java.write_text(JAVA_SOURCE, encoding="utf-8")

    # Audit checks: both manifest and full generated class MUST agree.
    again = ET.parse(manifest)
    app_activity = again.getroot().find("./application/activity")
    assert app_activity is not None
    assert app_activity.get(f"{{{ANDROID_NS}}}name") == ANDROID_ACTIVITY
    assert app_activity.get(f"{{{ANDROID_NS}}}screenOrientation") == "fullSensor"
    assert "WindowInsetsController" in java.read_text(encoding="utf-8")
    assert "SCREEN_ORIENTATION_FULL_SENSOR" in java.read_text(encoding="utf-8")
    print(f"[APEX ANDROID IMMERSIVE] activity {old} -> {ANDROID_ACTIVITY}")
    print(f"[APEX ANDROID IMMERSIVE] manifest={manifest}")
    print(f"[APEX ANDROID IMMERSIVE] source={java}")
    print("[APEX ANDROID IMMERSIVE] orientation: fullSensor (portrait + landscape even with system rotation lock)")
    print("[APEX ANDROID IMMERSIVE] lifecycle: onCreate/onResume/onWindowFocusChanged")
    print("[APEX ANDROID IMMERSIVE] Android >=30 native system bars, 26-29 fallback")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
