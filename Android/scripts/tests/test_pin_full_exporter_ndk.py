"""Regression tests: generated JUCE Android builds must use CI's installed NDK."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pin_full_exporter_ndk import pin_ndk_version, NDK_VERSION


class PinNdkTests(unittest.TestCase):
    def test_adds_pin_to_generated_groovy(self):
        source = "plugins { id 'com.android.application' }\nandroid {\n    compileSdkVersion 35\n}\n"
        pinned = pin_ndk_version(source)
        self.assertIn(f'ndkVersion "{NDK_VERSION}"', pinned)
        self.assertIn("compileSdkVersion 35", pinned)
        self.assertEqual(pinned, pin_ndk_version(pinned))

    def test_replaces_stale_ndk_without_duplicate(self):
        source = 'android {\n  ndkVersion "28.1.13356709"\n}\n'
        pinned = pin_ndk_version(source)
        self.assertEqual(pinned.count("ndkVersion"), 1)
        self.assertNotIn("28.1.13356709", pinned)
        self.assertIn(NDK_VERSION, pinned)

    def test_fails_closed_without_android_block(self):
        with self.assertRaises(ValueError):
            pin_ndk_version("plugins { id 'com.android.application' }\n")

    def test_actual_juce_8_0_12_symbol_assignment_cannot_override_pin(self):
        # Exact form emitted by jucer_ProjectExport_Android.h at JUCE 8.0.12.
        source = ('def ndkVersionString = "28.1.13356709"\n\n'
                  'android {\n    compileSdk 35\n'
                  '    ndkVersion ndkVersionString\n'
                  '    namespace "com.xziel.apex.mobileportrait"\n}\n')
        pinned = pin_ndk_version(source)
        self.assertIn(f'    ndkVersion "{NDK_VERSION}"\n', pinned)
        self.assertNotIn('ndkVersion ndkVersionString', pinned)
        self.assertEqual(pinned.count('    ndkVersion '), 1)
        self.assertEqual(pinned, pin_ndk_version(pinned))

    def test_assignment_expressions_comments_and_later_overrides_are_pinned(self):
        source = ('android {\n'
                  '  ndkVersion = "28.1.13356709" // generated default\n'
                  '  ndkVersion rootProject.ext.ndkVersion\n}\n')
        pinned = pin_ndk_version(source)
        self.assertNotIn('28.1.13356709', pinned)
        self.assertNotIn('rootProject.ext.ndkVersion', pinned)
        self.assertEqual(pinned.count(f'ndkVersion "{NDK_VERSION}"'), 2)
        self.assertEqual(pinned, pin_ndk_version(pinned))


if __name__ == "__main__":
    unittest.main()
