#!/usr/bin/env python3
"""Generated JUCE Android manifest / immersive Activity regressions."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

SCRIPT = Path(__file__).resolve().parents[1] / "enable_immersive_fullscreen.py"
ANDROID_NS = "http://schemas.android.com/apk/res/android"


class ImmersiveManifestTests(unittest.TestCase):
    def run_patch(self, bundle, manifest_package=None, extra_activity_first=False,
                  launcher_exported=None, decoy_launcher_only_first=False, rerun=False, duplicate_launcher=False):
        with tempfile.TemporaryDirectory() as temporary:
            core = Path(temporary) / "DAW_Core"
            generated = core / "Builds" / "AndroidFull"
            manifest = generated / "app/src/main/AndroidManifest.xml"
            manifest.parent.mkdir(parents=True)

            project = ET.Element("JUCERPROJECT", bundleIdentifier=bundle)
            ET.ElementTree(project).write(
                core / "DAW_Core_AndroidFull.jucer", encoding="utf-8")

            attributes = {} if manifest_package is None else {"package": manifest_package}
            root = ET.Element("manifest", attributes)
            app = ET.SubElement(root, "application")
            if extra_activity_first:
                ET.SubElement(app, "activity", {
                    f"{{{ANDROID_NS}}}name": "com.example.UnrelatedActivity"})
            if decoy_launcher_only_first:
                decoy = ET.SubElement(app, "activity", {
                    f"{{{ANDROID_NS}}}name": "com.example.CategoryOnlyDecoy"})
                decoy_filter = ET.SubElement(decoy, "intent-filter")
                ET.SubElement(decoy_filter, "category", {
                    f"{{{ANDROID_NS}}}name": "android.intent.category.LAUNCHER"})
            launcher_attributes = {
                f"{{{ANDROID_NS}}}name": "com.rmsl.juce.JuceActivity"}
            if launcher_exported is not None:
                launcher_attributes[f"{{{ANDROID_NS}}}exported"] = launcher_exported
            activity = ET.SubElement(app, "activity", launcher_attributes)
            filt = ET.SubElement(activity, "intent-filter")
            ET.SubElement(filt, "action", {
                f"{{{ANDROID_NS}}}name": "android.intent.action.MAIN"})
            ET.SubElement(filt, "category", {
                f"{{{ANDROID_NS}}}name": "android.intent.category.LAUNCHER"})
            if duplicate_launcher:
                other = ET.SubElement(app, "activity", {
                    f"{{{ANDROID_NS}}}name": "com.example.OtherLauncher"})
                other_filter = ET.SubElement(other, "intent-filter")
                ET.SubElement(other_filter, "action", {
                    f"{{{ANDROID_NS}}}name": "android.intent.action.MAIN"})
                ET.SubElement(other_filter, "category", {
                    f"{{{ANDROID_NS}}}name": "android.intent.category.LAUNCHER"})
            ET.ElementTree(root).write(manifest, encoding="utf-8")
            original_manifest = manifest.read_bytes()

            run = subprocess.run(
                [sys.executable, str(SCRIPT), "--project", str(generated)],
                capture_output=True, text=True, check=False)
            java = (generated / "app/src/main/java" /
                    bundle.replace(".", "/") / "ApexImmersiveActivity.java")
            if run.returncode == 0:
                activities = ET.parse(manifest).getroot().findall("./application/activity")
                patched = next(a for a in activities
                               if a.get(f"{{{ANDROID_NS}}}name")
                               == bundle + ".ApexImmersiveActivity")
                if extra_activity_first:
                    self.assertEqual(activities[0].get(f"{{{ANDROID_NS}}}name"),
                                     "com.example.UnrelatedActivity")
                self.assertEqual(patched.get(f"{{{ANDROID_NS}}}name"),
                                 bundle + ".ApexImmersiveActivity")
                self.assertEqual(patched.get(f"{{{ANDROID_NS}}}screenOrientation"),
                                 "user")
                self.assertEqual(patched.get(f"{{{ANDROID_NS}}}exported"),
                                 "true")
                self.assertTrue(java.is_file())
                source = java.read_text(encoding="utf-8")
                self.assertIn("WindowInsetsController", source)
                self.assertIn("SCREEN_ORIENTATION_USER", source)
                self.assertNotIn("SCREEN_ORIENTATION_PORTRAIT", source)
                if rerun:
                    first_manifest = manifest.read_bytes()
                    first_java = java.read_bytes()
                    second = subprocess.run(
                        [sys.executable, str(SCRIPT), "--project", str(generated)],
                        capture_output=True, text=True, check=False)
                    self.assertEqual(second.returncode, 0, second.stderr)
                    self.assertEqual(manifest.read_bytes(), first_manifest)
                    self.assertEqual(java.read_bytes(), first_java)
            if run.returncode != 0:
                self.assertEqual(manifest.read_bytes(), original_manifest,
                                 "Rejected manifests must not be rewritten")
                self.assertFalse(java.exists(),
                                 "Rejected manifests must not create an Activity")
            return run

    def test_juce8_manifest_without_package_uses_generated_project(self):
        for bundle in ("com.xziel.apex.fulltest", "com.xziel.apex.mobileportrait"):
            with self.subTest(bundle=bundle):
                self.assertEqual(self.run_patch(bundle).returncode, 0)

    def test_unrelated_activity_before_launcher_is_allowed(self):
        # The generated manifest is not guaranteed to put JUCE's launcher first.
        for bundle in ("com.xziel.apex.fulltest", "com.xziel.apex.mobileportrait"):
            with self.subTest(bundle=bundle):
                self.assertEqual(
                    self.run_patch(bundle, extra_activity_first=True).returncode, 0)

    def test_launcher_category_without_main_action_is_ignored(self):
        # A decoy category must not trigger a false multiple-launcher error.
        for bundle in ("com.xziel.apex.fulltest", "com.xziel.apex.mobileportrait"):
            with self.subTest(bundle=bundle):
                self.assertEqual(
                    self.run_patch(bundle, decoy_launcher_only_first=True).returncode, 0)

    def test_reapplying_patch_preserves_generated_files(self):
        # Android exporter scripts may be rerun; patching must be idempotent.
        for bundle in ("com.xziel.apex.fulltest", "com.xziel.apex.mobileportrait"):
            with self.subTest(bundle=bundle):
                self.assertEqual(self.run_patch(bundle, rerun=True).returncode, 0)

    def test_duplicate_launcher_is_rejected_without_mutation(self):
        for bundle in ("com.xziel.apex.fulltest", "com.xziel.apex.mobileportrait"):
            with self.subTest(bundle=bundle):
                run = self.run_patch(bundle, duplicate_launcher=True)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn("Expected exactly one launcher Activity, found 2", run.stderr)

    def test_launcher_exported_false_is_repaired(self):
        # Android 12+ requires exported=true for the launcher to start.
        self.assertEqual(self.run_patch("com.xziel.apex.mobileportrait",
                                        launcher_exported="false").returncode, 0)

    def test_manifest_package_matching_project_is_allowed(self):
        bundle = "com.xziel.apex.mobileportrait"
        self.assertEqual(self.run_patch(bundle, bundle).returncode, 0)

    def test_manifest_project_package_mismatch_is_rejected(self):
        run = self.run_patch("com.xziel.apex.mobileportrait", "com.xziel.apex.fulltest")
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("package mismatch", run.stderr)

    def test_unknown_project_package_is_rejected(self):
        run = self.run_patch("com.example.untrusted")
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("Unexpected APEX Android package", run.stderr)


if __name__ == "__main__":
    unittest.main()
