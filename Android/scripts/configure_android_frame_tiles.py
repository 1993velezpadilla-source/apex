#!/usr/bin/env python3
"""Install the APEX dirty-region renderer into the pinned Android JUCE peer."""
import argparse
from pathlib import Path


def replace(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError(f"Unexpected JUCE source near {old.splitlines()[0]!r}")
    return text.replace(old, new, 1)


def add_timeline_motion(peer: str) -> str:
    peer = replace(peer,
        "const auto raster = [&] (const Image& frame, const RectangleList<int>& regions, Point<int> origin)",
        "const auto raster = [&] (const Image& frame, const RectangleList<int>& regions,\n"
        "                                 Point<int> origin, int pixelDivisor)")
    peer = replace(peer, """\
                g.setOrigin (-origin);
                g.addTransform (AffineTransform::scale (scale));""", """\
                const auto factor = 1.0f / (float) pixelDivisor;
                g.addTransform (AffineTransform::scale (scale * factor)
                    .translated ((float) -origin.x * factor, (float) -origin.y * factor));""")
    peer = replace(peer, "t.apexFrameTiles.paint (env, canvas, clip, scale, t.component.isOpaque(), raster)",
                         "t.apexFrameTiles.paint (env, canvas, clip, scale, t.component, raster)")
    peer = replace(peer, "probe.presentedTiles();",
                         "probe.presentedTiles (t.apexFrameTiles.drawsMotionLayer());")
    peer = replace(peer, "raster (temp, RectangleList<int> (clip.withZeroOrigin()), clip.getPosition());",
                         "raster (temp, RectangleList<int> (clip.withZeroOrigin()), clip.getPosition(), 1);")
    peer = replace(peer, "    void repaint (const Rectangle<int>& userArea) override", """\
    void noteApexTimelineMotion (Component& viewport)
    {
        if (apexAndroidFrame::useTiles && component.isOpaque())
            apexFrameTiles.noteTimelineMotion (viewport);
    }

    void repaint (const Rectangle<int>& userArea) override""")
    return replace(peer, "ComponentPeer* Component::createNewPeer", """\
void noteApexTimelineMotion (Component& viewport)
{
    if (auto* peer = dynamic_cast<AndroidComponentPeer*> (viewport.getPeer()))
        peer->noteApexTimelineMotion (viewport);
}

ComponentPeer* Component::createNewPeer""")


def configure_opaque_viewport_blit(root: Path) -> None:
    """Copy fully opaque arranger snapshots through JUCE's integer image path."""
    path = root / "modules/juce_graphics/native/juce_RenderingHelpers.h"
    original = path.read_text(encoding="utf-8")
    if 'opaqueViewport ("apexOpaqueHorizontalViewport")' in original:
        return
    text = replace(original, """\
    private:
        const Image::BitmapData& destData;
        const Image::BitmapData& srcData;
        const int extraAlpha, xOffset, yOffset;""", """\
        bool opaqueSource = false;

    private:
        const Image::BitmapData& destData;
        const Image::BitmapData& srcData;
        const int extraAlpha, xOffset, yOffset;""")
    text = replace(text, """\
            if (destStride == srcStride
                 && srcData.pixelFormat  == Image::RGB
                 && destData.pixelFormat == Image::RGB)""", """\
            if (destStride == srcStride
                 && ((srcData.pixelFormat == Image::RGB && destData.pixelFormat == Image::RGB)
                     || (opaqueSource && srcData.pixelFormat == Image::ARGB && destData.pixelFormat == Image::ARGB)))""")
    text = replace(text, """\
const Image::BitmapData& srcData, int alpha, int x, int y, bool tiledFill)
    {
        switch (destData.pixelFormat)""", """\
const Image::BitmapData& srcData, int alpha, int x, int y, bool tiledFill, bool opaqueSource)
    {
        if (opaqueSource)
        {
            ImageFill<PixelARGB, PixelARGB, false> renderer (destData, srcData, alpha, x, y);
            renderer.opaqueSource = true;
            iter.iterate (renderer);
            return;
        }
        switch (destData.pixelFormat)""")
    text = replace(text,
        "        EdgeTableFillers::renderImageUntransformed (iter, destData, srcData, alpha, x, y, tiledFill);", """\
        static const Identifier opaqueViewport ("apexOpaqueHorizontalViewport");
        const auto* properties = src.getProperties();
        const bool copyOpaqueViewport = !tiledFill && alpha == 255 && src != image
            && srcData.pixelFormat == Image::ARGB && destData.pixelFormat == Image::ARGB
            && properties != nullptr && (bool) properties->getWithDefault (opaqueViewport, false);
        EdgeTableFillers::renderImageUntransformed (iter, destData, srcData, alpha, x, y, tiledFill, copyOpaqueViewport);""")
    path.write_text(text, encoding="utf-8")


def configure_component_paint_probe(gui: Path) -> None:
    for path, signature, component in (
        (gui / "components/juce_Component.cpp", "void Component::paintWithinParentContext (Graphics& g)", "*this"),
        (gui / "windows/juce_ComponentPeer.cpp", "void ComponentPeer::handlePaint (LowLevelGraphicsContext& contextToPaintTo)", "component"),
    ):
        contents = path.read_text(encoding="utf-8")
        if "apexAndroidFrame::ComponentPaintProbe" in contents:
            continue
        start = signature + "\n{"
        path.write_text(replace(contents, start, start + f"""
   #if JUCE_ANDROID && defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
    apexAndroidFrame::ComponentPaintProbe apexPaintProbe ({component});
   #endif"""), encoding="utf-8")


def configure(root: Path) -> None:
    configure_opaque_viewport_blit(root)
    gui = root / "modules/juce_gui_basics"
    configure_component_paint_probe(gui)
    module = gui / "juce_gui_basics.cpp"
    peer = gui / "native/juce_Windowing_android.cpp"
    source = Path(__file__).resolve().parents[1] / "Source/ApexAndroidFrameTiles.h"
    header = gui / "native/ApexAndroidFrameTiles.h"
    module_text = module.read_text(encoding="utf-8")
    peer_text = peer.read_text(encoding="utf-8")
    include = '#include "native/ApexAndroidFrameTiles.h"'
    if "apexAndroidFrame::FrameTiles apexFrameTiles;" in peer_text:
        if include not in module_text:
            raise ValueError("Incomplete APEX Android renderer")
        if "void noteApexTimelineMotion (Component& viewport)" not in peer_text:
            peer.write_text(add_timeline_motion(peer_text), encoding="utf-8")
        header.write_bytes(source.read_bytes())
        return

    module_text = replace(module_text, '#include "native/juce_Windowing_android.cpp"',
                          include + '\n #include "native/juce_Windowing_android.cpp"')
    peer_text = replace(peer_text, "    int sizeAllocated = 0;",
        "    int sizeAllocated = 0;\n    apexAndroidFrame::FrameTiles apexFrameTiles;")
    first = peer_text.index("    static void handlePaintCallback (")
    last = peer_text.index("    void repaint (", first)
    paint = peer_text[first:last]
    paint = replace(paint, "        auto sizeNeeded = clip.getWidth() * clip.getHeight();", """\
       #if APEX_ANDROID_UI_BENCHMARK
        apexAndroidFrame::FrameProbe probe;
       #endif
        const auto scale = (float) (Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale
                                    / Desktop::getInstance().getGlobalScaleFactor());
        const auto raster = [&] (const Image& frame, const RectangleList<int>& regions, Point<int> origin)
        {
           #if APEX_ANDROID_UI_BENCHMARK
            probe.startRaster (regions);
           #endif
            {
                LowLevelGraphicsSoftwareRenderer g (frame);
                g.clipToRectangleList (regions);
                g.setOrigin (-origin);
                g.addTransform (AffineTransform::scale (scale));
                t.handlePaint (g);
            }
           #if APEX_ANDROID_UI_BENCHMARK
            probe.endRaster();
           #endif
        };

        if (apexAndroidFrame::useTiles
            && t.apexFrameTiles.paint (env, canvas, clip, scale, t.component.isOpaque(), raster))
        {
           #if APEX_ANDROID_UI_BENCHMARK
            probe.presentedTiles();
           #endif
            t.buffer.clear();
            t.sizeAllocated = 0;
            return;
        }
        t.apexFrameTiles.invalidate();
        auto sizeNeeded = clip.getWidth() * clip.getHeight();""")
    paint = replace(paint, """\
                {
                    LowLevelGraphicsSoftwareRenderer g (temp);
                    g.setOrigin (-clip.getPosition());
                    const auto scale = Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale
                                     / Desktop::getInstance().getGlobalScaleFactor();
                    g.addTransform (AffineTransform::scale ((float) scale));
                    t.handlePaint (g);
                }""",
        """                raster (temp, RectangleList<int> (clip.withZeroOrigin()), clip.getPosition());""")
    peer_text = peer_text[:first] + paint + peer_text[last:]
    peer_text = replace(peer_text, """\
        callOnMessageThread ([area, localView]
        {
            localView.callVoidMethod (AndroidView.invalidate,""", """\
        auto damage = apexFrameTiles.damage;
        callOnMessageThread ([area, localView, damage = std::move (damage)]
        {
            if (apexAndroidFrame::useTiles)
                damage->add (area);
            localView.callVoidMethod (AndroidView.invalidate,""")
    header.write_bytes(source.read_bytes())
    peer.write_text(add_timeline_motion(peer_text), encoding="utf-8")
    module.write_text(module_text, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--juce", type=Path, default=Path("third_party/JUCE"))
    configure(parser.parse_args().juce)
