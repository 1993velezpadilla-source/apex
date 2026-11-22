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


def add_frame_pacing(peer: str) -> str:
    if "bool isApexAndroidFramePacingEnabled() noexcept" in peer:
        return peer

    peer = replace(peer, "        t.callVBlankListeners (timestampSec);", """\
        t.apexFrameTiles.damage->viewInvalidated = false;
        t.callVBlankListeners (timestampSec);""")
    peer = replace(peer, "        LocalRef<jobject> rect { env->CallObjectMethod (canvas, AndroidCanvas.getClipBounds) };",
                         "        t.apexFrameTiles.damage->viewInvalidated = false;\n"
                         "        LocalRef<jobject> rect { env->CallObjectMethod (canvas, AndroidCanvas.getClipBounds) };")
    peer = replace(peer, """\
            if (apexAndroidFrame::useTiles)
                damage->add (area);
            localView.callVoidMethod (AndroidView.invalidate,""", """\
            if (apexAndroidFrame::useTiles)
            {
                damage->add (area);
                if (apexAndroidFrame::framePacingEnabled && damage->hardwareCanvas)
                {
                    // Hardware Canvas ignores invalidate(rect) bounds since
                    // Android API 21. The internal RectangleList retains ALL
                    // dirty regions, so request one frame while its draw is
                    // pending rather than scheduling a new full Canvas pass
                    // for each clip/ruler/viewport repaint.
                    if (!damage->viewInvalidated)
                    {
                        damage->viewInvalidated = true;
                       #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
                        ++damage->scheduledPresentations;
                       #endif
                        localView.callVoidMethod (AndroidView.invalidate,
                                                  area.getX(), area.getY(),
                                                  area.getRight(), area.getBottom());
                    }
                   #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
                    else
                        ++damage->mergedPresentations;
                   #endif
                    return;
                }
            }
            localView.callVoidMethod (AndroidView.invalidate,""")
    return replace(peer, "void noteApexTimelineMotion (Component& viewport)\n{", """\
bool isApexAndroidFramePacingEnabled() noexcept
{
    return apexAndroidFrame::framePacingEnabled;
}

// The production build always keeps the Master waveform visible.
bool isApexAndroidMasterPreviewBenchSuppressed() noexcept
{
    return apexAndroidFrame::benchmarkSuppressMasterPreview;
}

void noteApexTimelineMotion (Component& viewport)
{""")


def add_horizontal_bitmap_reuse(peer: str) -> str:
    if "void setApexTimelineViewport (Viewport& viewport" in peer:
        return peer
    peer = replace(peer, "probe.presentedTiles (t.apexFrameTiles.drawsMotionLayer());",
                         "probe.presentedTiles (t.apexFrameTiles);")
    peer = replace(peer, "    void noteApexTimelineMotion (Component& viewport)", """\
    void setApexTimelineViewport (Viewport& viewport, int headerHeight)
    {
        apexFrameTiles.setTimelineViewport (viewport, headerHeight);
    }

    void noteApexTimelineMotion (Component& viewport)""")
    return replace(peer, "ComponentPeer* Component::createNewPeer", """\
void setApexTimelineViewport (Viewport& viewport, int headerHeight)
{
    if (auto* peer = dynamic_cast<AndroidComponentPeer*> (viewport.getPeer()))
        peer->setApexTimelineViewport (viewport, headerHeight);
}

ComponentPeer* Component::createNewPeer""")


def add_arranger_damage_query(peer: str) -> str:
    if "bool isApexAndroidArrangerDamageClippingEnabled()" in peer:
        return peer
    return replace(peer, "void setApexTimelineViewport (Viewport& viewport, int headerHeight)\n{", """\
bool isApexAndroidArrangerDamageClippingEnabled() noexcept
{
    return apexAndroidFrame::preciseArrangerDamage;
}

void setApexTimelineViewport (Viewport& viewport, int headerHeight)
{""")


def add_horizontal_move_damage(peer: str) -> str:
    if "apexFrameTiles.horizontalMoveOrigin (component)" in peer:
        return peer
    peer = replace(peer, """\
        auto damage = apexFrameTiles.damage;
        callOnMessageThread ([area, localView, damage = std::move (damage)]""", """\
        auto damage = apexFrameTiles.damage;
        const auto movingContentX = apexFrameTiles.horizontalMoveOrigin (component);
        callOnMessageThread ([area, localView, damage = std::move (damage), movingContentX]""")
    return replace(peer, "                damage->add (area);",
                         "                damage->add (area, movingContentX);")


def mark_horizontal_move_repaints(path: Path) -> None:
    contents = path.read_text(encoding="utf-8")
    if "apexAndroidFrame::horizontalMoveRepaintSource" in contents:
        return
    start = contents.index("void Component::setBounds (int x, int y, int w, int h)")
    end = contents.index("void Component::sendMovedResizedMessagesIfPending()", start)
    bounds = contents[start:end]
    if bounds.count("repaintParent();") != 2:
        raise ValueError("Unexpected JUCE Component move repaints")
    bounds = bounds.replace("repaintParent();", "repaintParentForBoundsChange();")
    bounds = replace(bounds, "        const bool showing = isShowing();", """\
        const bool showing = isShowing();
       #if JUCE_ANDROID
        const bool translated = wasMoved && ! wasResized && getY() == y;
       #endif
        const auto repaintParentForBoundsChange = [&]
        {
           #if JUCE_ANDROID
            const ScopedValueSetter<const Component*> origin (apexAndroidFrame::horizontalMoveRepaintSource,
                                                               translated ? this : nullptr);
           #endif
            repaintParent();
        };""")
    path.write_text(contents[:start] + bounds + contents[end:], encoding="utf-8")


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

    path = gui / "components/juce_Component.cpp"
    contents = path.read_text(encoding="utf-8")
    if "ComponentPaintProbe::logTimelineChild" not in contents:
        child = "        auto& child = *childComponentList.getUnchecked (i);\n\n        if (child.isVisible())"
        path.write_text(replace(contents, child, """\
        auto& child = *childComponentList.getUnchecked (i);
       #if JUCE_ANDROID && defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
        apexAndroidFrame::ComponentPaintProbe::logTimelineChild (*this, child, g, clipBounds);
       #endif

        if (child.isVisible())"""), encoding="utf-8")


    # Attribute native repaint requests to the original JUCE component, not
    # only the component that eventually paints. This is benchmark-only and
    # reveals full-viewport invalidations caused by moving the viewed content.
    path = gui / "components/juce_Component.cpp"
    contents = path.read_text(encoding="utf-8")
    if "ComponentPaintProbe::recordRepaint" not in contents:
        signature = "void Component::internalRepaintUnchecked (Rectangle<int> area, bool isEntireComponent)\n{"
        path.write_text(replace(contents, signature, signature + """
   #if JUCE_ANDROID && defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
    apexAndroidFrame::ComponentPaintProbe::recordRepaint (*this, area, isEntireComponent);
   #endif"""), encoding="utf-8")


def bound_wide_software_clips(path: Path) -> None:
    contents = path.read_text(encoding="utf-8")
    if "maxExactFloatSpan" in contents:
        return

    path.write_text(replace(contents,
        "                clip = clip->clipToRectangle (transform.transformed (r));", """\
                // At high display density a long timeline can have a device
                // width exceeding INT_MAX. Intersect it with the raster's
                // clip in double precision before forming an integer width.
                constexpr int maxExactFloatSpan = 1 << std::numeric_limits<float>::digits;
                if (r.getWidth() > maxExactFloatSpan || r.getHeight() > maxExactFloatSpan)
                {
                    const auto bounded = r.toDouble().transformedBy (transform.complexTransform)
                        .getIntersection (clip->getClipBounds().toDouble());
                    clip = clip->clipToRectangle (bounded.getSmallestIntegerContainer());
                }
                else
                {
                    clip = clip->clipToRectangle (transform.transformed (r));
                }"""), encoding="utf-8")


def add_opaque_image_blit(path: Path) -> None:
    contents = path.read_text(encoding="utf-8")
    if "JUCE_APEX_OPAQUE_IMAGE_BLIT" in contents:
        return
    contents = replace(contents, "namespace juce\n{", """\
#define JUCE_APEX_OPAQUE_IMAGE_BLIT 1

namespace juce
{""")
    contents = replace(contents, "private:\n    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR", """\
    // Copy a fully opaque memory-backed image under the current rectangular
    // clip. The caller owns source opacity and uses the normal paint path
    // if this target, clip, or transform cannot be copied directly.
    bool blitOpaqueImage (const Image& source, const AffineTransform& sourceToUser)
    {
        using RectangleClip = RenderingHelpers::SoftwareRendererSavedState::RectangleListRegionType;
        const auto* rectangularClip = dynamic_cast<const RectangleClip*> (stack->clip.get());
        const auto transform = stack->transform.getTransformWith (sourceToUser);
        if (rectangularClip == nullptr || source.getFormat() != Image::ARGB
            || stack->image.getFormat() != Image::ARGB || source == stack->image
            || ! RenderingHelpers::SoftwareRendererSavedState::isOnlyTranslationAllowingError (transform, 0.002f))
            return false;

        const auto snap = [] (float position) { return (int) std::floor (position + 0.5f); };
        const Point<int> origin { snap (transform.getTranslationX()), snap (transform.getTranslationY()) };
        const auto bounds = (source.getBounds() + origin).getIntersection (stack->image.getBounds());
        const Image::BitmapData src (source, Image::BitmapData::readOnly);
        const Image::BitmapData dst (stack->image, Image::BitmapData::readWrite);
        constexpr auto bytesPerPixel = static_cast<int> (sizeof (PixelARGB));
        if (src.pixelStride != bytesPerPixel || dst.pixelStride != bytesPerPixel)
            return false;

        for (const auto& rectangle : rectangularClip->clip)
        {
            const auto area = rectangle.getIntersection (bounds);
            if (area.isEmpty())
                continue;
            const auto bytes = (size_t) area.getWidth() * sizeof (PixelARGB);
            for (int y = area.getY(); y < area.getBottom(); ++y)
                std::memcpy (dst.getPixelPointer (area.getX(), y),
                             src.getPixelPointer (area.getX() - origin.x, y - origin.y), bytes);
        }
        return true;
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR""")
    path.write_text(contents, encoding="utf-8")


def add_device_clip_query(path: Path) -> None:
    contents = path.read_text(encoding="utf-8")
    if "bool getApexDeviceClip (" in contents:
        return
    contents = replace(contents, "    // Copy a fully opaque memory-backed image", """\
    // Return the actual raster damage in local device pixels. A user-space
    // bounding box would fill the gaps between independent dirty regions.
    bool getApexDeviceClip (float localScale, RectangleList<int>& result) const
    {
        using RectangleClip = RenderingHelpers::SoftwareRendererSavedState::RectangleListRegionType;
        const auto* rectangularClip = dynamic_cast<const RectangleClip*> (stack->clip.get());
        const auto toDevice = stack->transform.getTransformWith (AffineTransform::scale (1.0f / localScale));
        if (rectangularClip == nullptr
            || ! RenderingHelpers::SoftwareRendererSavedState::isOnlyTranslationAllowingError (toDevice, 0.002f))
            return false;

        // Match blitOpaqueImage's device-pixel snapping, including negative
        // origins at fractional Android densities.
        const auto snap = [] (float position) { return (int) std::floor (position + 0.5f); };
        const Point<int> origin { snap (toDevice.getTranslationX()), snap (toDevice.getTranslationY()) };
        result = rectangularClip->clip;
        result.offsetAll (-origin);
        return true;
    }

    // Copy a fully opaque memory-backed image""")
    path.write_text(contents, encoding="utf-8")


def query_scaled_clip_coverage(path: Path) -> None:
    contents = path.read_text(encoding="utf-8")
    if "APEX exact coverage of scaled dirty rectangles" in contents:
        return
    contents = replace(contents, "            return getClipBounds().intersects (r);", """\
            // APEX exact coverage of scaled dirty rectangles. At Android's
            // display scale, a bounding-box query also hits all the gaps in
            // the dirty region between the arranger and stationary controls.
            if (! transform.isRotated)
            {
                const auto device = r.toDouble().transformedBy (transform.complexTransform)
                    .getIntersection (clip->getClipBounds().toDouble());
                return ! device.isEmpty() && clip->clipRegionIntersects (device.getSmallestIntegerContainer());
            }

            return getClipBounds().intersects (r);""")
    path.write_text(contents, encoding="utf-8")


def add_waveform_column_fill(path: Path) -> None:
    contents = path.read_text(encoding="utf-8")
    if "bool fillApexWaveformColumns (" in contents:
        return
    contents = replace(contents, "    // Return the actual raster damage in local device pixels.", """\
    // Keep solid waveform columns on the software rectangle renderer. The
    // generic float list constructs a combined EdgeTable at every DPI scale.
    bool fillApexWaveformColumns (const RectangleList<float>& columns)
    {
        if (stack->transform.isRotated || ! stack->fillType.isColour())
            return false;

        for (const auto& column : columns)
            stack->fillRect (column);

        return true;
    }

    // Return the actual raster damage in local device pixels.""")
    path.write_text(contents, encoding="utf-8")


def configure(root: Path) -> None:
    gui = root / "modules/juce_gui_basics"
    configure_component_paint_probe(gui)
    mark_horizontal_move_repaints(gui / "components/juce_Component.cpp")
    graphics = root / "modules/juce_graphics"
    bound_wide_software_clips(graphics / "native/juce_RenderingHelpers.h")
    query_scaled_clip_coverage(graphics / "native/juce_RenderingHelpers.h")
    software_renderer = graphics / "contexts/juce_LowLevelGraphicsSoftwareRenderer.h"
    add_opaque_image_blit(software_renderer)
    add_device_clip_query(software_renderer)
    add_waveform_column_fill(software_renderer)
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
            peer_text = add_timeline_motion(peer_text)
        peer_text = add_arranger_damage_query(add_horizontal_bitmap_reuse(add_frame_pacing(peer_text)))
        peer.write_text(add_horizontal_move_damage(peer_text), encoding="utf-8")
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
    peer_text = add_horizontal_bitmap_reuse(add_frame_pacing(add_timeline_motion(peer_text)))
    peer.write_text(add_horizontal_move_damage(add_arranger_damage_query(peer_text)), encoding="utf-8")
    module.write_text(module_text, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--juce", type=Path, default=Path("third_party/JUCE"))
    configure(parser.parse_args().juce)
