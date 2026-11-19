#!/usr/bin/env python3
"""Use a reusable NDK Bitmap as the render target of the pinned Android JUCE peer."""

import argparse
from pathlib import Path


def replace(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError(f"Unexpected JUCE Android peer layout near {old.splitlines()[0]!r}")
    return text.replace(old, new, 1)


def configure(juce_root: Path) -> None:
    gui = juce_root / "modules/juce_gui_basics"
    module = gui / "juce_gui_basics.cpp"
    peer = gui / "native/juce_Windowing_android.cpp"
    header = gui / "native/ApexAndroidBitmapPeer.h"
    original = peer.read_text(encoding="utf-8")
    module_text = module.read_text(encoding="utf-8")
    bitmap_source = Path(__file__).resolve().parents[1] / "Source/ApexAndroidBitmapPeer.h"
    bitmap_text = bitmap_source.read_text(encoding="utf-8")
    if "apexAndroidPeer::WindowBitmap apexWindowBitmap;" in original:
        if '#include "native/ApexAndroidBitmapPeer.h"' not in module_text:
            raise ValueError("The generated JUCE renderer patch is incomplete")
        header.write_text(bitmap_text, encoding="utf-8")
        return

    module_text = replace(module_text,
        '#include "native/juce_Windowing_android.cpp"',
        '#include "native/ApexAndroidBitmapPeer.h"\n #include "native/juce_Windowing_android.cpp"')
    peer_text = replace(original, "    int sizeAllocated = 0;",
        "    int sizeAllocated = 0;\n    apexAndroidPeer::WindowBitmap apexWindowBitmap;")
    paint_start = peer_text.index("    static void handlePaintCallback (")
    paint_end = peer_text.index("    void repaint (", paint_start)
    paint = peer_text[paint_start:paint_end]
    paint = replace(paint, "        auto sizeNeeded = clip.getWidth() * clip.getHeight();", """
       #if APEX_ANDROID_UI_BENCHMARK
        apexAndroidPeer::FrameProbe probe (clip);
       #endif
        const auto raster = [&] (const Image& image)
        {
           #if APEX_ANDROID_UI_BENCHMARK
            probe.startRaster();
           #endif
            {
                LowLevelGraphicsSoftwareRenderer g (image);
                g.setOrigin (-clip.getPosition());
                const auto scale = Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale
                                 / Desktop::getInstance().getGlobalScaleFactor();
                g.addTransform (AffineTransform::scale ((float) scale));
                t.handlePaint (g);
            }
           #if APEX_ANDROID_UI_BENCHMARK
            probe.endRaster();
           #endif
        };

        if (apexAndroidPeer::useBitmap
            && t.apexWindowBitmap.paint (env, canvas, paint, clip, t.component.isOpaque(), raster))
        {
           #if APEX_ANDROID_UI_BENCHMARK
            probe.usedBitmap();
           #endif
            t.buffer.clear();
            t.sizeAllocated = 0;
            return;
        }

        auto sizeNeeded = clip.getWidth() * clip.getHeight();""".lstrip("\n"))
    paint = replace(paint, """
                {
                    LowLevelGraphicsSoftwareRenderer g (temp);
                    g.setOrigin (-clip.getPosition());
                    const auto scale = Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale
                                     / Desktop::getInstance().getGlobalScaleFactor();
                    g.addTransform (AffineTransform::scale ((float) scale));
                    t.handlePaint (g);
                }""".lstrip("\n"), "                raster (temp);")

    peer.write_text(peer_text[:paint_start] + paint + peer_text[paint_end:], encoding="utf-8")
    header.write_text(bitmap_text, encoding="utf-8")
    module.write_text(module_text, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--juce", type=Path, default=Path("third_party/JUCE"))
    args = parser.parse_args()
    configure(args.juce)
