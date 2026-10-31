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
    def run_patch(self, bundle, manifest_package=None):
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
            activity = ET.SubElement(app, "activity", {
                f"{{{ANDROID_NS}}}name": "com.rmsl.juce.JuceActivity"})
            filt = ET.SubElement(activity, "intent-filter")
            ET.SubElement(filt, "category", {
                f"{{{ANDROID_NS}}}name": "android.intent.category.LAUNCHER"})
            ET.ElementTree(root).write(manifest, encoding="utf-8")

            run = subprocess.run(
                [sys.executable, str(SCRIPT), "--project", str(generated)],
                capture_output=True, text=True, check=False)
            java = (generated / "app/src/main/java" /
                    bundle.replace(".", "/") / "ApexImmersiveActivity.java")
            if run.returncode == 0:
                patched = ET.parse(manifest).getroot().find("./application/activity")
                self.assertEqual(patched.get(f"{{{ANDROID_NS}}}name"),
                                 bundle + ".ApexImmersiveActivity")
                self.assertEqual(patched.get(f"{{{ANDROID_NS}}}screenOrientation"),
                                 "portrait")
                self.assertTrue(java.is_file())
                self.assertIn("WindowInsetsController", java.read_text(encoding="utf-8"))
            return run

    def test_juce8_manifest_without_package_uses_generated_project(self):
        for bundle in ("com.xziel.apex.fulltest", "com.xziel.apex.mobileportrait"):
            with self.subTest(bundle=bundle):
                self.assertEqual(self.run_patch(bundle).returncode, 0)

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
