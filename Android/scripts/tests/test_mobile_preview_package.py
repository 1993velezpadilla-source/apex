"""Ensure the integrated APEX phone APK cannot overwrite the full-shell install."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from generate_full_exporter import is_portrait_preview


class AndroidPortraitPackageTests(unittest.TestCase):
    def test_unified_pr_uses_separate_package(self):
        self.assertTrue(is_portrait_preview("integrate/mobile-ui-clipperf-20261103"))
    def test_original_phone_branch_uses_separate_package(self):
        self.assertTrue(is_portrait_preview("feature/mobile-portrait-bubblegum-gauntlet-20261031"))
    def test_pr_merge_ref_uses_head_ref(self):
        self.assertTrue(is_portrait_preview("170/merge", "integrate/mobile-ui-clipperf-20261103"))
    def test_unrelated_android_uses_fullshell(self):
        self.assertFalse(is_portrait_preview("feature/android-port-original", "", ""))
    def test_explicit_preview(self):
        self.assertTrue(is_portrait_preview("main", "", "1"))


if __name__ == "__main__":
    unittest.main()
