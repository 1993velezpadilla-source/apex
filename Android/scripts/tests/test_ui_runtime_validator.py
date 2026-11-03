import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_ui_runtime_benchmark import validate_result, validate_focus


def real_navigation_result(gpu=True, clip=True):
    def state(x, pps, frames):
        return {"scrollX": x, "pps": pps, "clips": int(clip),
                "backend": "opengl" if gpu else "software",
                "gpuReady": gpu, "renderCallbacks": frames if gpu else 0}
    return {"events": 400, "phases": [
        {"phase": "pan", "ui_callback_samples": 140,
         "before": state(0, 100, 10), "after": state(300, 100, 140)},
        {"phase": "pinch", "ui_callback_samples": 140,
         "before": state(300, 100, 140), "after": state(800, 250, 280)},
        {"phase": "idle", "ui_callback_samples": 140,
         "before": state(800, 250, 280), "after": state(800, 250, 280)},
        {"phase": "reverse", "ui_callback_samples": 140,
         "before": state(800, 250, 280), "after": state(200, 250, 400)},
    ]}


class RuntimeValidatorTests(unittest.TestCase):
    def test_rejects_system_dialogs_and_requires_actual_apex_window_focus(self):
        validate_focus("mCurrentFocus=Window{abc u0 com.xziel.apex.mobileportrait/.ApexImmersiveActivity}")
        for dump in ("mCurrentFocus=null", "mCurrentFocus=Window{abc Application Error: com.google.android.apps.nexuslauncher}",
                     "mCurrentFocus=Window{abc ImmersiveModeConfirmation}", "mFocusedApp=com.xziel.apex.mobileportrait"):
            with self.assertRaises(ValueError):
                validate_focus(dump)

    def test_accepts_real_movement_for_both_backends_and_scenes(self):
        for gpu in (False, True):
            for clip in (False, True):
                validate_result(real_navigation_result(gpu, clip),
                                "gpu" if gpu else "software", "clip" if clip else "empty")

    def test_rejects_gpu_silently_falling_back_to_software(self):
        result = real_navigation_result(False)
        with self.assertRaises(ValueError):
            validate_result(result, "gpu", "clip")

    def test_rejects_frame_callbacks_without_production_navigation(self):
        for phase in (0, 1, 3):
            result = real_navigation_result()
            result["phases"][phase]["after"] = copy.deepcopy(result["phases"][phase]["before"])
            with self.assertRaises(ValueError):
                validate_result(result, "gpu", "clip")

    def test_rejects_missing_audio_clip_or_inactive_gpu(self):
        result = real_navigation_result(clip=False)
        with self.assertRaises(ValueError):
            validate_result(result, "gpu", "clip")
        result = real_navigation_result()
        result["phases"][0]["before"]["gpuReady"] = False
        with self.assertRaises(ValueError):
            validate_result(result, "gpu", "clip")


if __name__ == "__main__":
    unittest.main()
