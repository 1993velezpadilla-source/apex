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


if __name__ == "__main__":
    unittest.main()
