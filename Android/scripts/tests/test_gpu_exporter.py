from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

SCRIPT = Path(__file__).resolve().parents[1] / "generate_full_exporter.py"


class GpuExporterTests(unittest.TestCase):
    def export(self, preview, benchmark=False):
        with tempfile.TemporaryDirectory() as temporary:
            core = Path(temporary) / "My DAW/DAW_Core"
            core.mkdir(parents=True)
            source = core / "DAW_Core.jucer"
            source.write_text('<JUCERPROJECT name="APEX" defines="JUCE_ASIO=1">'
                              '<MODULES><MODULE id="juce_gui_basics"/></MODULES>'
                              '<EXPORTFORMATS><VS2026/></EXPORTFORMATS></JUCERPROJECT>')
            before = source.read_bytes()
            env = os.environ.copy()
            env.update(GITHUB_REF_NAME="", GITHUB_HEAD_REF="",
                       APEX_ANDROID_PORTRAIT_PREVIEW="1" if preview else "0")
            args = [sys.executable, str(SCRIPT), "--repo-root", temporary]
            if benchmark:
                args.append("--benchmark-x86")
            subprocess.run(args, env=env, check=True, capture_output=True)
            self.assertEqual(before, source.read_bytes(), "Do not modify Windows source project")
            root = ET.parse(core / "DAW_Core_AndroidFull.jucer").getroot()
            modules = [m.get("id") for m in root.findall("./MODULES/MODULE")]
            paths = [m.get("id") for m in root.findall("./EXPORTFORMATS/ANDROIDSTUDIO/MODULEPATHS/MODULEPATH")]
            configs = root.findall("./EXPORTFORMATS/ANDROIDSTUDIO/CONFIGURATIONS/CONFIGURATION")
            self.assertIn(f"APEX_ANDROID_GPU_UI={int(preview)}", root.get("defines"))
            self.assertEqual("juce_opengl" in modules, preview)
            self.assertEqual("juce_opengl" in paths, preview)
            self.assertTrue(all(c.get("androidArchitectures") ==
                                ("x86_64" if benchmark else "arm64-v8a") for c in configs))

    def test_preview_adds_gpu_to_generated_android_only(self):
        self.export(True)

    def test_fulltest_does_not_change_backend(self):
        self.export(False)

    def test_runtime_benchmark_builds_same_app_for_x86(self):
        self.export(True, benchmark=True)


if __name__ == "__main__":
    unittest.main()
