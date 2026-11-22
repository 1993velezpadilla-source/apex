#pragma once

#include "ApexAndroidCanvasGraphics.h"
#include <android/bitmap.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <optional>
#include <utility>
#include <vector>

#if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
#include <android/log.h>
#include <map>
#include <typeinfo>
#endif

// Compiled in the Android JUCE GUI module with its JNI bindings available.
namespace juce::apexAndroidFrame
{
#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (setDensity, "setDensity", "(I)V")
DECLARE_JNI_CLASS (TileBitmap, "android/graphics/Bitmap")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (drawBitmap, "drawBitmap", "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V") \
    METHOD (getWidth, "getWidth", "()I") \
    METHOD (getHeight, "getHeight", "()I") \
    METHOD (isHardwareAccelerated, "isHardwareAccelerated", "()Z") \
    METHOD (save, "save", "()I") \
    METHOD (restoreToCount, "restoreToCount", "(I)V") \
    METHOD (clipRect, "clipRect", "(FFFF)Z") \
    METHOD (translate, "translate", "(FF)V") \
    METHOD (scale, "scale", "(FF)V")
DECLARE_JNI_CLASS (TileCanvas, "android/graphics/Canvas")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (construct, "<init>", "(I)V")
DECLARE_JNI_CLASS (TilePaint, "android/graphics/Paint")
#undef JNI_CLASS_MEMBERS

// Android's bitmap storage and JUCE's Android SoftwareImageType use the
// same premultiplied RGBA bytes. The old integer-array channel filter would
// swap red and blue a second time.
static_assert (PixelARGB::indexR == 0 && PixelARGB::indexG == 1
               && PixelARGB::indexB == 2 && PixelARGB::indexA == 3);

#if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
static bool reduceTimelineMotion = false;
static bool framePacingEnabled = true;
static bool horizontalBitmapReuse = false;
static bool preciseArrangerDamage = false;
static bool retainTranslatedTimeline = false;
static bool directTimelineCanvas = false;
static bool benchmarkLegacyFullTreeRaster = false;
static bool benchmarkSlowPanReplay = false;
static bool benchmarkLegacyHorizontalFilter = false;
static bool benchmarkViewportPreparation = false;
// Only the disposable Android UI benchmark may suppress the master preview.
static bool benchmarkSuppressMasterPreview = false;
#else
static constexpr bool framePacingEnabled = true;
static constexpr bool preciseArrangerDamage = true;
   #if defined(APEX_ANDROID_DIRECT_TIMELINE) && APEX_ANDROID_DIRECT_TIMELINE
    static constexpr bool directTimelineCanvas = true;
   #else
    static constexpr bool directTimelineCanvas = false;
   #endif
   #if defined(APEX_ANDROID_HORIZONTAL_REUSE_PREVIEW) && APEX_ANDROID_HORIZONTAL_REUSE_PREVIEW
    static constexpr bool horizontalBitmapReuse = !directTimelineCanvas;
   #else
    static constexpr bool horizontalBitmapReuse = false;
   #endif
static constexpr bool benchmarkSuppressMasterPreview = false;
static constexpr bool retainTranslatedTimeline = horizontalBitmapReuse;
// The matched emulator replay showed longer frame delays with this layer.
static constexpr bool reduceTimelineMotion = false;
#endif

// Set only around the two parent repaints from a horizontal bounds move.
// Normal invalidation still travels through JUCE's component caches.
static thread_local const Component* horizontalMoveRepaintSource = nullptr;

struct TimelineRasterOrigin
{
    const Component* content = nullptr;
    int deviceX = 0;
    Rectangle<int> body;
};

static thread_local TimelineRasterOrigin timelineRasterOrigin;

template <typename Paint>
static bool paintAlignedTimeline(const Component& component, Graphics& graphics, Paint&& paint)
{
    if (timelineRasterOrigin.content != &component)
        return false;
    auto* renderer = dynamic_cast<LowLevelGraphicsSoftwareRenderer*>(&graphics.getInternalContext());
    if (renderer == nullptr)
        return false;

    const auto body = timelineRasterOrigin.body;
    const auto clip = renderer->getApexDeviceClipBounds();
    if (!body.isEmpty())
    {
        if (!clip.intersects(body))
            return false;
        if (!body.contains(clip))
        {
            // Split only the viewed component. The peer, its siblings and
            // ancestor chrome traverse the original union once.
            {
                // paintWithinParentContext already flushed Graphics' pending
                // save via setOrigin. These saves are on the same renderer.
                renderer->saveState();
                const ScopeGuard restore { [renderer] { renderer->restoreState(); } };
                if (renderer->restrictApexDeviceClip(body, true))
                    paint();
            }
            if (!renderer->restrictApexDeviceClip(body, false))
                return true;
        }
    }

    renderer->setApexDeviceRasterOriginX(timelineRasterOrigin.deviceX);
    paint();
    return true;
}

struct HorizontalMoveDamage
{
    void add(Rectangle<int> area, int originX)
    {
        if (regions.isEmpty())
            minX = maxX = originX;
        else
        {
            minX = jmin(minX, originX);
            maxX = jmax(maxX, originX);
        }
        regions.add(area);
    }

    RectangleList<int> regions;
    int minX = 0, maxX = 0;
};

#if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
// Shared across the benchmark's input and paint instrumentation. The epoch
// resets samples at every measured gesture, not at APK startup.
static bool measuring = false;
static uint32_t measurementEpoch = 0;

struct TouchPanCadence
{
    Point<float> previousFinger;
    Point<int> previousOrigin;
    std::array<int, 3> x {}, y {};
    bool initialised = false;

    void observe(Point<float> finger, Point<int> origin) noexcept
    {
        if (initialised && finger == previousFinger)
            return;

        if (initialised)
        {
            // Compare viewport travel only against a new, one-logical-pixel
            // input step. Ignore the initial drag through the touch slop.
            const auto input = finger - previousFinger;
            const auto travel = origin - previousOrigin;
            if (std::abs(std::abs(input.x) - 1.0f) < 0.15f)
                ++x[static_cast<std::size_t>(jmin(2, std::abs(travel.x)))];
            if (std::abs(std::abs(input.y) - 1.0f) < 0.15f)
                ++y[static_cast<std::size_t>(jmin(2, std::abs(travel.y)))];
        }
        previousFinger = finger;
        previousOrigin = origin;
        initialised = true;
    }
};
static TouchPanCadence touchPanCadence;
#endif

struct Damage
{
    void add(Rectangle<int> area, std::optional<int> movingContentX = std::nullopt)
    {
        if (area.isEmpty())
            return;
       #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
        ++requests;
        const auto pixels = static_cast<int64_t>(area.getWidth()) * area.getHeight();
        requestedPixels += pixels;
        largestRequestPixels = jmax(largestRequestPixels, pixels);
        if (lastSurfacePixels > 0 && pixels * 100 >= lastSurfacePixels * 90)
            ++fullSurfaceRequests;
       #endif
        if (movingContentX)
        {
            horizontalMoves.add(area.expanded(1), *movingContentX);
            return;
        }
        pending.add(area.expanded(1));
        if (pending.getNumRectangles() > 64)
            pending = RectangleList<int>(pending.getBounds());
    }

   #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
    void logAndReset(Rectangle<int> surface, Rectangle<int> canvasDirty,
                     const RectangleList<int>& accumulated)
    {
        lastSurfacePixels = static_cast<int64_t>(surface.getWidth()) * surface.getHeight();
        if (requests == 0)
            return;
        int64_t unionPixels = 0;
        for (const auto& rect : accumulated)
            unionPixels += static_cast<int64_t>(rect.getWidth()) * rect.getHeight();
        if (measuring)
        {
            if (lastMeasuredEpoch != measurementEpoch)
            {
                lastMeasuredEpoch = measurementEpoch;
                measuredFramesLogged = 0;
            }
            // The old first-24-frames trace was exhausted during startup,
            // before pan/REC. Log native requests DURING real gestures.
            if (measuredFramesLogged++ < 12)
                __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH",
                    "MEASURED_DAMAGE requests=%llu full90=%llu scheduled=%llu merged=%llu sumPixels=%lld "
                    "unionPixels=%lld surfacePixels=%lld canvasDirty=%d,%d,%d,%d",
                    static_cast<unsigned long long>(requests),
                    static_cast<unsigned long long>(fullSurfaceRequests),
                    static_cast<unsigned long long>(scheduledPresentations),
                    static_cast<unsigned long long>(mergedPresentations),
                    static_cast<long long>(requestedPixels),
                    static_cast<long long>(unionPixels),
                    static_cast<long long>(lastSurfacePixels),
                    canvasDirty.getX(), canvasDirty.getY(),
                    canvasDirty.getWidth(), canvasDirty.getHeight());
        }
        // Only sample a bounded number of frames; log the ORIGINAL damage
        // requests before the Android tile raster expands or merges them.
        // This distinguishes full-window JUCE invalidation from tile rebase.
        if (damageFramesLogged++ < 24)
            __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH",
                "DAMAGE_ORIGIN requests=%llu full90=%llu sumPixels=%lld "
                "maxPixels=%lld unionPixels=%lld surfacePixels=%lld canvasDirty=%d,%d,%d,%d",
                static_cast<unsigned long long>(requests),
                static_cast<unsigned long long>(fullSurfaceRequests),
                static_cast<long long>(requestedPixels),
                static_cast<long long>(largestRequestPixels),
                static_cast<long long>(unionPixels),
                static_cast<long long>(lastSurfacePixels),
                canvasDirty.getX(), canvasDirty.getY(),
                canvasDirty.getWidth(), canvasDirty.getHeight());
        requests = fullSurfaceRequests = 0;
        scheduledPresentations = mergedPresentations = 0;
        requestedPixels = largestRequestPixels = 0;
    }
    uint64_t requests = 0, fullSurfaceRequests = 0, damageFramesLogged = 0;
    uint64_t scheduledPresentations = 0, mergedPresentations = 0;
    uint32_t lastMeasuredEpoch = 0;
    int measuredFramesLogged = 0;
    int64_t requestedPixels = 0, largestRequestPixels = 0, lastSurfacePixels = 0;
   #endif

    RectangleList<int> pending;
    HorizontalMoveDamage horizontalMoves;
    bool hardwareCanvas = false;
    bool viewInvalidated = false;
};

#if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
// Controlled by the existing native benchmark phase boundary below.
static bool bitmapSubmitProfiling = false;
static int bitmapSubmitSamples = 0;
static int tileUploadSamples = 0;
#endif

struct HorizontalTileScroll
{
    Rectangle<int> area;
    int offset = 0;
    Point<int> contentOrigin;
    const Component* content = nullptr;
    bool unobstructed = false;
};

static int64_t snapDeviceX(int logicalX, float scale) noexcept
{
    return static_cast<int64_t>(std::floor(static_cast<double>(logicalX) * scale + 0.5));
}

static void addExposedScrollDamage(RectangleList<int>& redraw,
                                   const HorizontalMoveDamage& moves,
                                   const HorizontalTileScroll& scroll,
                                   int deltaX, float scale)
{
    const auto body = scroll.area;
    const auto origin = snapDeviceX(scroll.contentOrigin.x, scale);
    const auto left = origin - snapDeviceX(moves.maxX, scale);
    const auto right = origin - snapDeviceX(moves.minX, scale);

    // Pending content damage can predate one or more coalesced moves. Keep
    // its fixed location as well as every possible translated location.
    const auto pending = redraw;
    for (const auto& rect : pending)
    {
        const auto dirty = rect.getIntersection(body);
        if (!dirty.isEmpty())
            redraw.add(Rectangle<int>::leftTopRightBottom(
                static_cast<int>(jmax<int64_t>(body.getX(), dirty.getX() + jmin<int64_t>(0, left))),
                dirty.getY(),
                static_cast<int>(jmin<int64_t>(body.getRight(), dirty.getRight() + jmax<int64_t>(0, right))),
                dirty.getBottom()));
    }

    // The header and other stationary chrome still need the original
    // JUCE movement repaint. Only the timeline body can be translated.
    auto fixed = moves.regions;
    fixed.subtract(body);
    redraw.add(fixed);
    if (deltaX > 0)
        redraw.add(body.withWidth(jmin(body.getWidth(), deltaX + 2)));
    else if (deltaX < 0)
        redraw.add(body.withTrimmedLeft(jmax(0, body.getWidth() + deltaX - 2)));
}

class BitmapTiles
{
public:
    explicit BitmapTiles(int divisor) : pixelDivisor(divisor) {}

    void invalidate() noexcept { frameBounds = {}; }

    bool needsReset(Rectangle<int> clip, float scale, bool opaque,
                    Rectangle<int> scrollArea = {}) const noexcept
    {
        return frameBounds != clip || frameScale != scale || frameOpaque != opaque
            || scrollArea != frameScrollArea;
    }

    template <typename Raster>
    bool update(JNIEnv* env, Rectangle<int> clip, float scale, bool opaque,
                RectangleList<int> redraw, Raster&& raster, HorizontalTileScroll scroll = {},
                const HorizontalMoveDamage& moves = {})
    {
       #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
        updatedTilePixels = reusedTilePixels = retainedScrollPixels = 0;
       #endif
        const bool reset = needsReset(clip, scale, opaque, scroll.area);
        if (reset && !resetFrame(env, clip, scale, opaque, scroll.area))
            return false;

        const auto delta = snapDeviceX(scroll.contentOrigin.x, scale)
                         - snapDeviceX(lastScroll.contentOrigin.x, scale);
        const bool scrolled = !scroll.area.isEmpty() && delta != 0;
        // Timeline pixels in this layer use the same device-pixel origin
        // as the bitmap ring. A fractional motion in JUCE coordinates can
        // therefore translate the raster by the exact integer device delta.
        if (scroll.unobstructed != lastScroll.unobstructed || scroll.content != lastScroll.content)
            redraw.add(scroll.area);
        const auto [firstX, lastX] = std::minmax(lastScroll.contentOrigin.x, scroll.contentOrigin.x);
        const bool canTranslate = !reset && !moves.regions.isEmpty()
            && scroll.unobstructed && lastScroll.unobstructed
            && scroll.content == lastScroll.content
            && scroll.contentOrigin.y == lastScroll.contentOrigin.y
            // Pixels temporarily offscreen may have changed without a peer
            // repaint. Keep the full raster after an overshoot or round-trip.
            && moves.minX == firstX && moves.maxX == lastX
            && std::abs(delta) < scroll.area.getWidth();
       #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
        if (retainTranslatedTimeline && measuring && scrolled && !moves.regions.isEmpty())
        {
            if (scrollProbeEpoch != measurementEpoch)
            {
                scrollProbeEpoch = measurementEpoch;
                scrollProbeSamples = 0;
            }
            if (scrollProbeSamples++ < 6)
            {
                const auto logicalDelta = static_cast<double>(scroll.contentOrigin.x)
                                        - lastScroll.contentOrigin.x;
                __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH",
                    "TRANSLATED_TIMELINE eligible=%d unobstructed=%d/%d reset=%d "
                    "dx=%.5f aligned_dx=%lld origin=%d/%d range=%d/%d body=%d,%d,%d,%d",
                    canTranslate, lastScroll.unobstructed, scroll.unobstructed, reset,
                    logicalDelta * scale, static_cast<long long>(delta),
                    lastScroll.contentOrigin.x, scroll.contentOrigin.x,
                    moves.minX, moves.maxX, scroll.area.getX(), scroll.area.getY(),
                    scroll.area.getWidth(), scroll.area.getHeight());
            }
        }
       #endif
        if (canTranslate)
        {
            addExposedScrollDamage(redraw, moves, scroll, static_cast<int>(delta), scale);
           #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
            if (scrolled)
            {
                auto retained = RectangleList<int>(scroll.area);
                retained.subtract(redraw);
                for (const auto& area : retained)
                    retainedScrollPixels += area.getWidth() * area.getHeight();
            }
           #endif
        }
        else
        {
            redraw.add(moves.regions);
            if (scrolled)
                redraw.add(scroll.area);
        }
        lastScroll = scroll;
        frameScrollOffset = scroll.offset;
        redraw.clipTo(clip);
        redraw.offsetAll(-clip.getPosition());
        if (pixelDivisor != 1)
        {
            RectangleList<int> reduced;
            for (const auto& area : redraw)
                reduced.add(Rectangle<int>::leftTopRightBottom(
                    area.getX() / pixelDivisor, area.getY() / pixelDivisor,
                    smallerDimension(area.getRight()), smallerDimension(area.getBottom())));
            redraw = std::move(reduced);
        }

        if (!redraw.isEmpty())
        {
            if (!opaque)
                for (const auto& area : redraw)
                    backing.clear(area);
            raster(backing, redraw, clip.getPosition(), pixelDivisor);
            const Image::BitmapData source(backing, Image::BitmapData::readOnly);
            const auto localScrollArea = frameScrollArea - frameBounds.getPosition();
            const auto wrapShift = scroll.offset - localScrollArea.getWidth();
            RectangleList<int> first, wrapped;
            if (!localScrollArea.isEmpty())
            {
                first = redraw;
                first.clipTo(localScrollArea);
                wrapped = first;
                first.offsetAll({ scroll.offset, 0 });
                wrapped.offsetAll({ wrapShift, 0 });
                first.clipTo(localScrollArea);
                wrapped.clipTo(localScrollArea);
            }
            const std::array<UploadPart, 2> fixedParts {{ { &redraw, 0 }, {} }};
            const std::array<UploadPart, 2> scrollParts {{
                { &first, scroll.offset }, { &wrapped, wrapShift } }};
           #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
            // A hardware Canvas ignores the dirty invalidation bounds on
            // modern Android. Measure the bitmap-upload costs directly.
            const bool sampleUploads = bitmapSubmitProfiling && tileUploadSamples < 12;
            const auto uploadWallStart = sampleUploads ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point{};
            timespec uploadCpuStart {};
            if (sampleUploads) clock_gettime(CLOCK_THREAD_CPUTIME_ID, &uploadCpuStart);
            int uploadedTiles = 0;
            int64_t uploadedPixels = 0;
           #endif
            for (auto& tile : tiles)
            {
                const bool movable = !tile.shadow.empty();
                const auto& parts = movable ? scrollParts : fixedParts;
                const bool touched = parts[0].rects->intersectsRectangle(tile.bounds)
                    || (movable && wrapped.intersectsRectangle(tile.bounds));
                if (!touched)
                    continue;

                if (movable && !reset && scrolled && matchesShadow(tile, source, parts))
                {
                   #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
                    reusedTilePixels += tile.bounds.getWidth() * tile.bounds.getHeight();
                   #endif
                    continue;
                }
                if (!upload(env, tile, source, parts))
                    return false;
                   #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
                updatedTilePixels += tile.bounds.getWidth() * tile.bounds.getHeight();
                if (sampleUploads)
                {
                    ++uploadedTiles;
                    uploadedPixels += static_cast<int64_t>(tile.bounds.getWidth())
                                      * tile.bounds.getHeight();
                }
                   #endif
            }
           #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
            if (sampleUploads)
            {
                timespec uploadCpuEnd {};
                clock_gettime(CLOCK_THREAD_CPUTIME_ID, &uploadCpuEnd);
                const auto wallNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - uploadWallStart).count();
                const auto cpuNs = (static_cast<int64_t>(uploadCpuEnd.tv_sec - uploadCpuStart.tv_sec)
                                   * 1000000000LL) + uploadCpuEnd.tv_nsec - uploadCpuStart.tv_nsec;
                ++tileUploadSamples;
                __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH",
                    "TILE_UPLOAD tileSize=384 tiles=%d invalidatedPixels=%lld "
                    "wall_ms=%.3f cpu_ms=%.3f wait_ms=%.3f",
                    uploadedTiles, static_cast<long long>(uploadedPixels),
                    wallNs / 1.0e6, cpuNs / 1.0e6, (wallNs - cpuNs) / 1.0e6);
            }
           #endif
        }

        return true;
    }

   #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
    int updatedTilePixels = 0, reusedTilePixels = 0, retainedScrollPixels = 0;
    bool hasScrollTiles() const noexcept { return !frameScrollArea.isEmpty(); }
   #endif

    void draw(JNIEnv* env, jobject canvas, Rectangle<int> clip, Rectangle<int> covered = {}) const
    {
        jint saveCount = 0;
        if (pixelDivisor != 1)
        {
            const auto area = frameBounds.toFloat();
            saveCount = env->CallIntMethod(canvas, TileCanvas.save);
            env->CallBooleanMethod(canvas, TileCanvas.clipRect, area.getX(), area.getY(),
                                   area.getRight(), area.getBottom());
            env->CallVoidMethod(canvas, TileCanvas.translate, area.getX(), area.getY());
            env->CallVoidMethod(canvas, TileCanvas.scale, (jfloat)pixelDivisor, (jfloat)pixelDivisor);
        }

        const auto origin = pixelDivisor == 1 ? frameBounds.getPosition() : Point<int>{};
       #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
        // Native Canvas may supply a full-screen dirty rect despite small
        // JUCE damage. Measure JNI bitmap submission independently of raster
        // and component painting; production drawing is unchanged.
        const bool sampleDraw = bitmapSubmitProfiling && bitmapSubmitSamples < 12;
        const auto wallStart = sampleDraw ? std::chrono::steady_clock::now()
                                          : std::chrono::steady_clock::time_point{};
        timespec cpuStart {};
        if (sampleDraw) clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpuStart);
        int submittedTiles = 0;
        int64_t submittedPixels = 0;
       #endif
        const auto submit = [&] (const Tile& tile, Point<int> position)
        {
            env->CallVoidMethod(canvas, TileCanvas.drawBitmap, tile.bitmap.get(),
                                (jfloat)position.x, (jfloat)position.y, filteredPaint.get());
           #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
            if (sampleDraw)
            {
                ++submittedTiles;
                submittedPixels += static_cast<int64_t>(tile.bounds.getWidth())
                                   * tile.bounds.getHeight() * pixelDivisor * pixelDivisor;
            }
           #endif
        };
        for (const auto& tile : tiles)
        {
            if (!tile.shadow.empty())
                continue;
            const auto surfaceTile = tile.bounds * pixelDivisor + frameBounds.getPosition();
            if (clip.intersects(surfaceTile) && !covered.contains(surfaceTile))
                submit(tile, origin + tile.bounds.getPosition());
        }
        const auto scrollClip = clip.getIntersection(frameScrollArea);
        if (!scrollClip.isEmpty())
        {
            const auto restoreCount = env->CallIntMethod(canvas, TileCanvas.save);
            env->CallBooleanMethod(canvas, TileCanvas.clipRect,
                (jfloat)scrollClip.getX(), (jfloat)scrollClip.getY(),
                (jfloat)scrollClip.getRight(), (jfloat)scrollClip.getBottom());
            const int width = frameScrollArea.getWidth();
            for (const auto& tile : tiles)
            {
                if (tile.shadow.empty())
                    continue;
                const auto screen = tile.bounds + origin - Point<int>(frameScrollOffset, 0);
                for (const int shift : { 0, width })
                {
                    const auto position = screen.translated(shift, 0);
                    if (scrollClip.intersects(position))
                        submit(tile, position.getPosition());
                }
            }
            env->CallVoidMethod(canvas, TileCanvas.restoreToCount, restoreCount);
        }
        if (pixelDivisor != 1)
            env->CallVoidMethod(canvas, TileCanvas.restoreToCount, saveCount);
       #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
        if (sampleDraw)
        {
            timespec cpuEnd {};
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpuEnd);
            const auto wallNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - wallStart).count();
            const auto cpuNs = (static_cast<int64_t>(cpuEnd.tv_sec - cpuStart.tv_sec) * 1000000000LL)
                               + cpuEnd.tv_nsec - cpuStart.tv_nsec;
            ++bitmapSubmitSamples;
            __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH",
                "BITMAP_SUBMIT divisor=%d tiles=%d submittedPixels=%lld "
                "dirty=%d,%d,%d,%d wall_ms=%.3f cpu_ms=%.3f wait_ms=%.3f",
                pixelDivisor, submittedTiles, static_cast<long long>(submittedPixels),
                clip.getX(), clip.getY(), clip.getWidth(), clip.getHeight(),
                wallNs / 1.0e6, cpuNs / 1.0e6, (wallNs - cpuNs) / 1.0e6);
        }
       #endif
    }

private:
    struct Tile
    {
        Rectangle<int> bounds;
        GlobalRef bitmap;
        std::size_t stride;
        std::vector<uint8> shadow;
    };

    struct UploadPart
    {
        const RectangleList<int>* rects = nullptr;
        int xShift = 0;
    };

    int smallerDimension(int size) const noexcept { return (size + pixelDivisor - 1) / pixelDivisor; }

    bool resetFrame(JNIEnv* env, Rectangle<int> clip, float scale, bool opaque,
                    Rectangle<int> scrollArea)
    {
        // The 256px experiment increased raster wall times and worsened
        // playback Choreographer p95 vs. the 384px baseline. Retain the
        // original tile size and the upload profiler for follow-up work.
        constexpr int tileSize = 384;
        const int width = smallerDimension(clip.getWidth());
        const int height = smallerDimension(clip.getHeight());
        if ((int64)width * height > 16'000'000)
            return false; // use the platform peer's fallback for unusually large surfaces

        auto config = LocalRef<jobject>(env->CallStaticObjectMethod(
            AndroidBitmapConfig, AndroidBitmapConfig.valueOf, javaString("ARGB_8888").get()));
        if (config == nullptr)
            return clearFailure(env);

        if (pixelDivisor != 1 && filteredPaint.get() == nullptr)
        {
            constexpr jint filterBitmapFlag = 2;
            auto paint = LocalRef<jobject>(env->NewObject(TilePaint, TilePaint.construct, filterBitmapFlag));
            if (paint == nullptr)
                return clearFailure(env);
            filteredPaint = GlobalRef(paint, env);
            if (filteredPaint.get() == nullptr)
                return clearFailure(env);
        }

        std::vector<Tile> next;
        const auto addTiles = [&] (Rectangle<int> region, bool movable)
        {
            for (int y = region.getY(); y < region.getBottom(); y += tileSize)
                for (int x = region.getX(); x < region.getRight(); x += tileSize)
                {
                    const int w = jmin(tileSize, region.getRight() - x);
                    const int h = jmin(tileSize, region.getBottom() - y);
                    auto bitmap = LocalRef<jobject>(env->CallStaticObjectMethod(
                        AndroidBitmap, AndroidBitmap.createBitmap, w, h, config.get()));
                    if (bitmap == nullptr)
                        return clearFailure(env);

                    AndroidBitmapInfo info {};
                    if (AndroidBitmap_getInfo(env, bitmap.get(), &info) != ANDROID_BITMAP_RESULT_SUCCESS
                        || info.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
                        return clearFailure(env);

                    env->CallVoidMethod(bitmap.get(), TileBitmap.setDensity, 0);
                    GlobalRef ref(bitmap, env);
                    if (ref.get() == nullptr)
                        return clearFailure(env);
                    Tile tile { { x, y, w, h }, std::move(ref), info.stride, {} };
                    if (movable)
                        tile.shadow.resize(static_cast<std::size_t>(w) * h * sizeof(PixelARGB));
                    next.push_back(std::move(tile));
                }
            return true;
        };
        const auto localScrollArea = scrollArea - clip.getPosition();
        RectangleList<int> fixed({ 0, 0, width, height });
        if (!localScrollArea.isEmpty())
            fixed.subtract(localScrollArea);
        for (const auto& rect : fixed)
            if (!addTiles(rect, false))
                return false;
        if (!addTiles(localScrollArea, true))
            return false;

        backing = Image(SoftwareImageType().create(Image::ARGB, width, height, false));
        tiles = std::move(next);
        frameBounds = clip;
        frameScrollArea = scrollArea;
        frameScale = scale;
        frameOpaque = opaque;
        return true;
    }

    template <typename Visit>
    static bool visitDirtyRows(const Tile& tile, const Image::BitmapData& source,
                               const std::array<UploadPart, 2>& parts, Visit&& visit)
    {
        for (const auto& part : parts)
        {
            if (part.rects == nullptr)
                continue;
            for (const auto& area : *part.rects)
            {
                const auto overlap = area.getIntersection(tile.bounds);
                if (overlap.isEmpty())
                    continue;
                const auto bytes = static_cast<std::size_t>(overlap.getWidth()) * sizeof(PixelARGB);
                for (int y = overlap.getY(); y < overlap.getBottom(); ++y)
                {
                    const auto row = static_cast<std::size_t>(y - tile.bounds.getY());
                    const auto x = static_cast<std::size_t>(overlap.getX() - tile.bounds.getX())
                                 * sizeof(PixelARGB);
                    if (!visit(source.getPixelPointer(overlap.getX() - part.xShift, y), row, x, bytes))
                        return false;
                }
            }
        }
        return true;
    }

    static bool matchesShadow(const Tile& tile, const Image::BitmapData& source,
                              const std::array<UploadPart, 2>& parts)
    {
        const auto stride = static_cast<std::size_t>(tile.bounds.getWidth()) * sizeof(PixelARGB);
        return visitDirtyRows(tile, source, parts, [&] (const uint8* row, std::size_t y,
                                                       std::size_t x, std::size_t bytes)
        {
            return std::memcmp(row, tile.shadow.data() + y * stride + x, bytes) == 0;
        });
    }

    static bool upload(JNIEnv* env, Tile& tile, const Image::BitmapData& source,
                       const std::array<UploadPart, 2>& parts)
    {
        void* pixels = nullptr;
        if (AndroidBitmap_lockPixels(env, tile.bitmap.get(), &pixels) != ANDROID_BITMAP_RESULT_SUCCESS)
            return clearFailure(env);

        auto* bytes = static_cast<uint8*>(pixels);
        const auto shadowStride = static_cast<std::size_t>(tile.bounds.getWidth()) * sizeof(PixelARGB);
        visitDirtyRows(tile, source, parts, [&] (const uint8* row, std::size_t y,
                                                std::size_t x, std::size_t length)
        {
            std::memcpy(bytes + y * tile.stride + x, row, length);
            if (!tile.shadow.empty())
                std::memcpy(tile.shadow.data() + y * shadowStride + x, row, length);
            return true;
        });
        return AndroidBitmap_unlockPixels(env, tile.bitmap.get()) == ANDROID_BITMAP_RESULT_SUCCESS
            || clearFailure(env);
    }

    static bool clearFailure(JNIEnv* env)
    {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }

    const int pixelDivisor;
    GlobalRef filteredPaint;
    Image backing;
    std::vector<Tile> tiles;
    Rectangle<int> frameBounds, frameScrollArea;
    HorizontalTileScroll lastScroll;
    int frameScrollOffset = 0;
    float frameScale = 0.0f;
    bool frameOpaque = false;
   #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
    uint32_t scrollProbeEpoch = 0;
    int scrollProbeSamples = 0;
   #endif
};

class FrameTiles final : private Timer
{
public:
    // The queued UI-thread invalidation may outlive the component peer.
    const std::shared_ptr<Damage> damage = std::make_shared<Damage>();

    void noteTimelineMotion(Component& viewport)
    {
        if (!reduceTimelineMotion)
            return;

        motionViewport = &viewport;
        lastMotionMs = Time::getMillisecondCounter();
        if (!isTimerRunning())
            startTimer(50);
    }

    void setTimelineViewport(Viewport& viewport, int headerHeight)
    {
        if (horizontalViewport.getComponent() != &viewport)
            horizontalViewport = &viewport;
        timelineHeaderHeight = headerHeight;
    }

    std::optional<int> horizontalMoveOrigin(const Component& root) const
    {
        const auto* source = horizontalMoveRepaintSource;
        if (retainTranslatedTimeline && source != nullptr && damage->hardwareCanvas)
            if (const auto* viewport = horizontalViewport.getComponent())
                if (source == viewport->getViewedComponent())
                    return root.getLocalPoint(source, Point<int>{}).x;
        return std::nullopt;
    }

    void invalidate() noexcept
    {
        damage->hardwareCanvas = false;
        damage->viewInvalidated = false;
        nativeTiles.invalidate();
        motionTiles.invalidate();
        drawnMotionArea = {};
        drawnDirectArea = {};
    }

    template <typename Raster, typename PaintCanvas>
    bool paint(JNIEnv* env, jobject canvas, Rectangle<int> clip, float scale,
               Component& root, Raster&& raster, PaintCanvas&& paintCanvas)
    {
        const bool opaque = root.isOpaque();

        // The Android Canvas clip is a DIRTY RECT, not a stable frame size.
        // Binding tile allocations to that changing rect recycled every tile
        // when the playhead or touch gesture invalidated a different region.
        // Anchor our backing image to the actual Canvas surface dimensions.
        const auto surfaceWidth = env->CallIntMethod(canvas, TileCanvas.getWidth);
        const auto surfaceHeight = env->CallIntMethod(canvas, TileCanvas.getHeight);
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            return false;
        }
        if (surfaceWidth <= 0 || surfaceHeight <= 0)
            return false;
        const Rectangle<int> surface(0, 0, surfaceWidth, surfaceHeight);
        if (!surface.intersects(clip))
            return false;
        const bool hardwareCanvas = env->CallBooleanMethod(canvas, TileCanvas.isHardwareAccelerated) == JNI_TRUE;
        const auto scroll = hardwareCanvas && opaque && !reduceTimelineMotion && !directTimelineCanvas
            ? timelineScroll(root, surface, scale) : HorizontalTileScroll{};
        const bool initialOrResized = nativeTiles.needsReset(surface, scale, opaque, scroll.area);
       #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
        if (initialOrResized)
            __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH",
                "FRAME_CACHE_RESET surface=%dx%d dirty=%d,%d,%d,%d",
                surfaceWidth, surfaceHeight, clip.getX(), clip.getY(),
                clip.getWidth(), clip.getHeight());
       #endif

        const auto* viewport = motionViewport.getComponent();
        Rectangle<int> motionArea;
        if (opaque && scale >= 2.0f && viewport != nullptr && viewport->isShowing()
            && root.isParentOf(viewport))
            motionArea = (root.getLocalArea(viewport, viewport->getLocalBounds()).toFloat() * scale)
                .getSmallestIntegerContainer().getIntersection(clip);

        auto redraw = std::exchange(damage->pending, {});
        auto moves = std::exchange(damage->horizontalMoves, {});
       #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
        damage->logAndReset(surface, clip, redraw);
       #endif
        if (initialOrResized)
            redraw.add(surface);

        // The direct layer is the same on-screen rectangle at every world X.
        // Let the real component tree render into the hardware Canvas. Paint
        // the software chrome around it, outside the same rectangle.
        const auto candidate = hardwareCanvas && opaque && directTimelineCanvas
            ? directTimelineArea(root, surface, scale).getIntersection(clip) : Rectangle<int>{};
        auto directArea = candidate;
        if (!candidate.isEmpty() && !paintCanvas(canvasResources, candidate))
        {
            // A complex overlay may request an unsupported operation. The
            // opaque software draw below overwrites the incomplete recording
            // in full for this frame. Its backing is fixed in screen space.
            redraw.add(candidate);
            directArea = {};
        }
        if (directArea != drawnDirectArea)
            redraw.add(drawnDirectArea);
        if (!directArea.isEmpty())
            redraw.subtract(directArea);

        if (motionArea != drawnMotionArea)
        {
            redraw.add(drawnMotionArea);
            redraw.add(motionArea);
        }
        if (!motionArea.isEmpty())
        {
            redraw.add(moves.regions);
            moves = {};
            if (motionTiles.needsReset(motionArea, scale, true))
                redraw.add(motionArea);
            if (!motionTiles.update(env, motionArea, scale, true, redraw, raster))
                return false;
            redraw.subtract(motionArea);
        }
        const auto rasterTimeline = [&] (const Image& frame, const RectangleList<int>& regions,
                                         Point<int> origin, int divisor)
        {
            const auto retainedArea = scroll.area - origin;
            if (!scroll.unobstructed || !regions.intersectsRectangle(retainedArea))
            {
                raster(frame, regions, origin, divisor);
                return;
            }

            const auto deviceX = static_cast<int>(snapDeviceX(scroll.contentOrigin.x, scale) - origin.x);
           #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
            if (benchmarkLegacyFullTreeRaster)
            {
                // Same APK and bitmap reuse, with the 9.9.3 peer traversal.
                auto stationary = regions;
                stationary.subtract(retainedArea);
                if (!stationary.isEmpty())
                    raster(frame, stationary, origin, divisor);
                auto body = regions;
                body.clipTo(retainedArea);
                const TimelineRasterOrigin aligned { scroll.content, deviceX, {} };
                const ScopedValueSetter<TimelineRasterOrigin> useAlignedOrigin(timelineRasterOrigin, aligned);
                raster(frame, body, origin, divisor);
                return;
            }
           #endif
            const TimelineRasterOrigin aligned { scroll.content, deviceX, retainedArea };
            const ScopedValueSetter<TimelineRasterOrigin> useAlignedOrigin(timelineRasterOrigin, aligned);
            raster(frame, regions, origin, divisor);
        };
        if (!nativeTiles.update(env, surface, scale, opaque, std::move(redraw), rasterTimeline, scroll, moves))
            return false;

        damage->hardwareCanvas = framePacingEnabled && hardwareCanvas;
        if (!directArea.isEmpty())
        {
            const auto save = env->CallIntMethod(canvas, DirectCanvas.save);
            const auto r = directArea.toFloat();
            env->CallBooleanMethod(canvas, DirectCanvas.clipOutRect, r.getX(), r.getY(),
                                   r.getRight(), r.getBottom());
            nativeTiles.draw(env, canvas, clip, directArea);
            env->CallVoidMethod(canvas, DirectCanvas.restore, save);
        }
        else
            nativeTiles.draw(env, canvas, clip, motionArea);
        drawnDirectArea = directArea;
        if (!motionArea.isEmpty())
            motionTiles.draw(env, canvas, clip);
        drawnMotionArea = motionArea;
        return true;
    }

    bool drawsMotionLayer() const noexcept { return !drawnMotionArea.isEmpty(); }
    bool drawsDirectTimeline() const noexcept { return !drawnDirectArea.isEmpty(); }
   #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
    int getUpdatedTilePixels() const noexcept
    {
        return nativeTiles.updatedTilePixels + (drawsMotionLayer() ? motionTiles.updatedTilePixels : 0);
    }
    int getReusedTilePixels() const noexcept { return nativeTiles.reusedTilePixels; }
    int getRetainedScrollPixels() const noexcept { return nativeTiles.retainedScrollPixels; }
    bool drawsScrollTiles() const noexcept { return nativeTiles.hasScrollTiles(); }
   #endif

private:
    Rectangle<int> directTimelineArea(const Component& root, Rectangle<int> surface,
                                      float scale) const
    {
        const auto* viewport = horizontalViewport.getComponent();
        if (viewport == nullptr || !viewport->isShowing() || !root.isParentOf(viewport)
            || !std::isfinite(scale) || scale <= 0.0f)
            return {};
        const auto* content = viewport->getViewedComponent();
        if (content == nullptr || !content->isShowing())
            return {};
        const auto bounds = viewport->getLocalBounds().withWidth(viewport->getViewWidth())
            .withHeight(viewport->getViewHeight());
        return (root.getLocalArea(viewport, bounds).toDouble() * static_cast<double>(scale))
            .getSmallestIntegerContainer().getIntersection(surface);
    }

    static Rectangle<int> unobstructedTimelineBody(const Component& root, const Component& content,
                                                    Rectangle<int> bodyInRoot)
    {
        const auto reject = [] (const char* reason, const Component& blocker)
        {
           #if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
            static uint32 lastEpoch = 0;
            if (measuring && lastEpoch != measurementEpoch)
            {
                lastEpoch = measurementEpoch;
                const auto bounds = blocker.getBounds();
                __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH",
                    "TIMELINE_COVERAGE_BLOCK reason=%s type=%s bounds=%d,%d,%d,%d "
                    "opaque=%d transformed=%d alpha=%.3f buffered=%d",
                    reason, typeid(blocker).name(), bounds.getX(), bounds.getY(),
                    bounds.getWidth(), bounds.getHeight(), blocker.isOpaque(),
                    blocker.isTransformed(), blocker.getAlpha(),
                    blocker.getCachedComponentImage() != nullptr);
            }
           #else
            ignoreUnused(reason, blocker);
           #endif
            return Rectangle<int>{};
        };

        if (!content.isOpaque())
            return reject("content-not-opaque", content);
        if (!root.getLocalBounds().contains(bodyInRoot))
            return reject("outside-root", root);

        RectangleList<int> available(bodyInRoot);
        for (auto* child = &content; child != &root; )
        {
            const auto* parent = child->getParentComponent();
            if (parent == nullptr || child->isTransformed() || child->getAlpha() != 1.0f
                || child->getCachedComponentImage() != nullptr
                || !root.getLocalArea(child, child->getLocalBounds()).contains(bodyInRoot))
                return reject("ancestor-ineligible", *child);

            for (int index = parent->getIndexOfChildComponent(child) + 1;
                 index < parent->getNumChildComponents(); ++index)
            {
                const auto* sibling = parent->getChildComponent(index);
                if (!sibling->isVisible())
                    continue;

                const auto localBounds = sibling->getLocalBounds();
                auto bounds = root.getLocalArea(sibling, localBounds);
                if (bounds.isEmpty() || !bounds.expanded(1).intersects(bodyInRoot))
                    continue;

                // Some overlays use a full-screen drag envelope. Only an
                // explicit paint contract can narrow their occlusion.
                if (const auto* coverage = dynamic_cast<const ApexAndroidOverlayPaintBounds*>(sibling))
                    bounds = root.getLocalArea(sibling,
                        coverage->getApexAndroidPaintBounds().getIntersection(localBounds));
                if (!bounds.isEmpty())
                    available.subtract(bounds.expanded(1));
                if (available.isEmpty())
                    return reject("covered-by-sibling", *sibling);
            }
            child = parent;
        }

        // The existing bitmap ring presents one rectangular scroll layer.
        // Pick its largest unobstructed rectangle and paint the rest normally.
        Rectangle<int> best;
        for (const auto& candidate : available)
            if (static_cast<int64_t>(candidate.getWidth()) * candidate.getHeight()
                > static_cast<int64_t>(best.getWidth()) * best.getHeight())
                best = candidate;
        return best;
    }

    HorizontalTileScroll timelineScroll(const Component& root, Rectangle<int> surface, float scale) const
    {
        const auto* viewport = horizontalViewport.getComponent();
        if (!horizontalBitmapReuse || viewport == nullptr || !viewport->isShowing()
            || !root.isParentOf(viewport) || !std::isfinite(scale) || scale <= 0.0f)
            return {};
        const auto* content = viewport->getViewedComponent();
        if (content == nullptr)
            return {};

        const auto body = viewport->getLocalBounds().withWidth(viewport->getViewWidth())
            .withHeight(viewport->getViewHeight()).withTrimmedTop(timelineHeaderHeight);
        const auto bodyInRoot = root.getLocalArea(viewport, body);
        const auto contentOrigin = root.getLocalPoint(content, Point<int>{});
        const auto origin = snapDeviceX(contentOrigin.x, scale);
        const auto reusable = retainTranslatedTimeline && std::abs(origin) < (1 << 21)
            ? unobstructedTimelineBody(root, *content, bodyInRoot) : Rectangle<int>{};
        const auto area = ((reusable.isEmpty() ? bodyInRoot : reusable).toFloat() * scale)
            .getSmallestIntegerContainer().getIntersection(surface);
        if (area.isEmpty() || static_cast<int64_t>(area.getWidth()) * area.getHeight() > 4'000'000)
            return {};

        // Match the software blit's device-pixel origin. Rounding the scroll
        // distance alone introduces one-pixel mismatches at fractional DPI.
        const int width = area.getWidth();
        const int offset = static_cast<int>(-origin % width);
        return { area, offset < 0 ? offset + width : offset, contentOrigin, content, !reusable.isEmpty() };
    }

    void timerCallback() override
    {
        if (Time::getMillisecondCounter() - lastMotionMs < 150)
            return;

        stopTimer();
        auto viewport = motionViewport;
        motionViewport = nullptr;
        if (viewport != nullptr)
            viewport->repaint();
    }

    BitmapTiles nativeTiles { 1 }, motionTiles { 2 };
    Component::SafePointer<Component> motionViewport;
    Component::SafePointer<Viewport> horizontalViewport;
    int timelineHeaderHeight = 0;
    uint32 lastMotionMs = 0;
    Rectangle<int> drawnMotionArea, drawnDirectArea;
    CanvasGraphics::Resources canvasResources;
};

#if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
struct FrameSample
{
    jlong wallNs = 0, cpuNs = 0, rasterNs = 0, pixels = 0, usedTiles = 0;
    jlong rasterCpuNs = 0;
    jlong updatedTilePixels = 0, reusedTilePixels = 0, retainedScrollPixels = 0;
    jlong rasterPasses = 0;
};
static std::array<FrameSample, 2048> recordedFrames {};
static std::size_t frameCount = 0;
static bool useTiles = false;

static jlong threadCpuNs()
{
    timespec now {};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return (jlong)now.tv_sec * 1'000'000'000 + now.tv_nsec;
}

// Measure the work between child paint calls on the real Android UI thread.
// JUCE's top-level paint scope also includes clipping and window decoration.
class ComponentPaintProbe
{
public:
    explicit ComponentPaintProbe(const Component& component)
        : type(measuring ? &typeid(component) : nullptr)
    {
        if (type == nullptr)
            return;
        parent = current;
        current = this;
        startedCpu = threadCpuNs();
        startedWall = std::chrono::steady_clock::now();
    }

    ~ComponentPaintProbe()
    {
        if (type == nullptr)
            return;
        const auto durationWall = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - startedWall).count();
        const auto durationCpu = threadCpuNs() - startedCpu;
        current = parent;
        if (parent != nullptr)
        {
            parent->childWallTime += durationWall;
            parent->childCpuTime += durationCpu;
        }
        auto& sample = components[type];
        sample.selfCpuNs += durationCpu - childCpuTime;
        sample.selfWallNs += durationWall - childWallTime;
        ++sample.count;
    }

    static void recordRepaint(const Component& source, Rectangle<int> area, bool entire)
    {
        if (!measuring || area.isEmpty())
            return;
        auto& count = repaints[&typeid(source)];
        ++count.calls;
        if (entire)
            ++count.fullCalls;
        const auto pixels = static_cast<int64_t>(area.getWidth()) * area.getHeight();
        count.requestedPixels += pixels;
        count.largestPixels = jmax(count.largestPixels, pixels);
    }

    static void reset()
    {
        components.clear();
        repaints.clear();
    }

    static void logTimelineChild(const Component& parent, const Component& child,
                                 Graphics& graphics, Rectangle<int> parentClip)
    {
        if (!measuring || frameCount >= 2)
            return;

        const auto* parentType = typeid(parent).name();
        const auto* childType = typeid(child).name();
        if (std::strstr(parentType, "TimelineViewport") == nullptr
            && std::strstr(childType, "TimelineViewport") == nullptr
            && std::strstr(childType, "ArrangementView") == nullptr)
            return;

        const auto bounds = child.getBounds();
        Graphics::ScopedSaveState state(graphics);
        if (child.isTransformed())
            graphics.addTransform(child.getTransform());
        const auto intersects = parentClip.intersects(bounds);
        const auto clipped = graphics.reduceClipRegion(bounds);
        const auto reduced = graphics.getClipBounds();
        __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH",
                            "TIMELINE_CLIP %s/%s visible=%d transformed=%d cached=%d "
                            "bounds=[%d,%d,%d,%d] clip=[%d,%d,%d,%d] "
                            "intersects=%d clipped=%d remaining=[%d,%d,%d,%d]",
                            parentType, childType, child.isVisible(), child.isTransformed(),
                            child.getCachedComponentImage() != nullptr,
                            bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                            parentClip.getX(), parentClip.getY(), parentClip.getWidth(), parentClip.getHeight(),
                            intersects, clipped, reduced.getX(), reduced.getY(),
                            reduced.getWidth(), reduced.getHeight());
    }

    static void logTop(std::size_t frames)
    {
        using Entry = std::pair<const std::type_info*, Sample>;
        std::vector<Entry> sorted(components.begin(), components.end());
        // Rank by WAIT (wall minus thread CPU), not CPU alone: the 9.5
        // emulator profiler recorded ~8 ms CPU but ~38 ms off-CPU per
        // horizontal frame. Pinpoint which JUCE Component or inherited
        // painting phase blocks the Android message/render thread.
        std::sort(sorted.begin(), sorted.end(), [](const Entry& left, const Entry& right)
        {
            const auto leftWait = left.second.selfWallNs - left.second.selfCpuNs;
            const auto rightWait = right.second.selfWallNs - right.second.selfCpuNs;
            return leftWait > rightWait;
        });
        __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH", "PEER_PROFILE frames=%zu", frames);
        for (std::size_t i = 0; i < jmin(sorted.size(), std::size_t{ 12 }); ++i)
        {
            const auto& [type, sample] = sorted[i];
            const auto cpuMs = (double)sample.selfCpuNs / 1'000'000.0;
            const auto wallMs = (double)sample.selfWallNs / 1'000'000.0;
            __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH",
                                "PEER_PAINT_WAIT %s calls=%zu cpu_ms=%.3f wall_ms=%.3f wait_ms=%.3f",
                                type->name(), sample.count, cpuMs, wallMs, wallMs - cpuMs);
        }
        // Track where the invalidations originated, including the large
        // viewport requests that a paint-only profile cannot identify.
        using RepaintEntry = std::pair<const std::type_info*, RepaintSample>;
        std::vector<RepaintEntry> origins(repaints.begin(), repaints.end());
        std::sort(origins.begin(), origins.end(), [](const RepaintEntry& a, const RepaintEntry& b)
        {
            return a.second.requestedPixels > b.second.requestedPixels;
        });
        for (std::size_t i = 0; i < jmin(origins.size(), std::size_t{ 12 }); ++i)
        {
            const auto& [origin, item] = origins[i];
            __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH",
                "REPAINT_ORIGIN %s calls=%zu fullCalls=%zu requestedPixels=%lld maxPixels=%lld",
                origin->name(), item.calls, item.fullCalls,
                static_cast<long long>(item.requestedPixels),
                static_cast<long long>(item.largestPixels));
        }
    }

private:
    struct Sample { jlong selfCpuNs = 0, selfWallNs = 0; std::size_t count = 0; };
    struct RepaintSample { std::size_t calls = 0, fullCalls = 0;
                           int64_t requestedPixels = 0, largestPixels = 0; };
    static inline thread_local ComponentPaintProbe* current = nullptr;
    static inline std::map<const std::type_info*, Sample> components;
    static inline std::map<const std::type_info*, RepaintSample> repaints;

    const std::type_info* type;
    ComponentPaintProbe* parent = nullptr;
    std::chrono::steady_clock::time_point startedWall {};
    jlong startedCpu = 0, childCpuTime = 0, childWallTime = 0;
};

class FrameProbe
{
public:
    FrameProbe() : record(measuring && frameCount < recordedFrames.size())
    {
        if (record)
        {
            begin = Clock::now();
            beginCpu = threadCpuNs();
        }
    }

    ~FrameProbe()
    {
        if (record)
        {
            sample.wallNs = ns(Clock::now() - begin);
            sample.cpuNs = threadCpuNs() - beginCpu;
            recordedFrames[frameCount++] = sample;
        }
    }

    void startRaster(const RectangleList<int>& region)
    {
        if (record)
        {
            ++sample.rasterPasses;
            for (const auto& area : region)
                sample.pixels += (jlong)area.getWidth() * area.getHeight();
            beginRaster = Clock::now();
            beginRasterCpu = threadCpuNs();
        }
    }
    void endRaster()
    {
        if (record)
        {
            sample.rasterNs += ns(Clock::now() - beginRaster);
            sample.rasterCpuNs += threadCpuNs() - beginRasterCpu;
        }
    }
    void presentedTiles(const FrameTiles& tiles)
    {
        if (record)
        {
            sample.usedTiles = tiles.drawsDirectTimeline() ? 4
                : (tiles.drawsMotionLayer() ? 2 : (tiles.drawsScrollTiles() ? 3 : 1));
            sample.updatedTilePixels = tiles.getUpdatedTilePixels();
            sample.reusedTilePixels = tiles.getReusedTilePixels();
            sample.retainedScrollPixels = tiles.getRetainedScrollPixels();
        }
    }

private:
    using Clock = std::chrono::steady_clock;
    static jlong ns(Clock::duration duration)
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }
    const bool record;
    FrameSample sample;
    Clock::time_point begin, beginRaster;
    jlong beginCpu = 0, beginRasterCpu = 0;
};
#else
static constexpr bool useTiles = true;
#endif
}

#if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
namespace juce
{
bool isApexLegacyHorizontalFilterEnabled() noexcept
{
    return apexAndroidFrame::benchmarkLegacyHorizontalFilter;
}

bool isApexSlowPanReplayEnabled() noexcept
{
    return apexAndroidFrame::benchmarkSlowPanReplay;
}

bool isApexViewportPreparationEnabled() noexcept
{
    return apexAndroidFrame::benchmarkViewportPreparation;
}

void recordApexTouchPanForBenchmark(Point<float> finger, Point<int> origin) noexcept
{
    if (apexAndroidFrame::measuring)
        apexAndroidFrame::touchPanCadence.observe(finger, origin);
}
}

extern "C" JNIEXPORT void JNICALL
Java_com_xziel_apex_mobileportrait_ApexUiBenchmark_nativeUseFrameTiles(
    JNIEnv*, jclass, jboolean enabled, jboolean reduceMotion, jboolean framePaced,
    jboolean suppressMasterPreview, jboolean reuseHorizontalBitmaps, jboolean preciseDamage,
    jboolean retainTranslation, jboolean legacyFullTreeRaster,
    jboolean slowPanReplay, jboolean legacyHorizontalFilter, jboolean prepareViewport,
    jboolean directCanvas)
{
    juce::apexAndroidFrame::useTiles = enabled == JNI_TRUE;
    juce::apexAndroidFrame::reduceTimelineMotion = reduceMotion == JNI_TRUE;
    juce::apexAndroidFrame::framePacingEnabled = framePaced == JNI_TRUE;
    juce::apexAndroidFrame::benchmarkSuppressMasterPreview = suppressMasterPreview == JNI_TRUE;
    juce::apexAndroidFrame::horizontalBitmapReuse = reuseHorizontalBitmaps == JNI_TRUE;
    juce::apexAndroidFrame::preciseArrangerDamage = preciseDamage == JNI_TRUE;
    juce::apexAndroidFrame::retainTranslatedTimeline = retainTranslation == JNI_TRUE;
    juce::apexAndroidFrame::benchmarkLegacyFullTreeRaster = legacyFullTreeRaster == JNI_TRUE;
    juce::apexAndroidFrame::benchmarkSlowPanReplay = slowPanReplay == JNI_TRUE;
    juce::apexAndroidFrame::benchmarkLegacyHorizontalFilter = legacyHorizontalFilter == JNI_TRUE;
    juce::apexAndroidFrame::benchmarkViewportPreparation = prepareViewport == JNI_TRUE;
    juce::apexAndroidFrame::directTimelineCanvas = directCanvas == JNI_TRUE;
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_xziel_apex_mobileportrait_ApexUiBenchmark_nativeWindowDrawSamples(JNIEnv* env, jclass, jboolean finish)
{
    using namespace juce::apexAndroidFrame;
    measuring = finish != JNI_TRUE;
    bitmapSubmitProfiling = measuring;
    if (measuring)
    {
        bitmapSubmitSamples = 0;
        tileUploadSamples = 0;
        frameCount = 0;
        touchPanCadence = {};
        ++measurementEpoch;
        ComponentPaintProbe::reset();
        return nullptr;
    }
    if (benchmarkSlowPanReplay)
        __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH",
            "PAN_CADENCE x=%d,%d,%d y=%d,%d,%d",
            touchPanCadence.x[0], touchPanCadence.x[1], touchPanCadence.x[2],
            touchPanCadence.y[0], touchPanCadence.y[1], touchPanCadence.y[2]);
    ComponentPaintProbe::logTop(frameCount);
    std::vector<jlong> values;
    values.reserve(frameCount * 10);
    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const auto& f = recordedFrames[i];
        values.insert(values.end(), { f.wallNs, f.cpuNs, f.rasterNs, f.pixels,
                                      f.usedTiles, f.rasterCpuNs, f.updatedTilePixels,
                                      f.reusedTilePixels, f.retainedScrollPixels, f.rasterPasses });
    }
    const auto count = (jsize)values.size();
    auto result = env->NewLongArray(count);
    if (result != nullptr && count > 0)
        env->SetLongArrayRegion(result, 0, count, values.data());
    return result;
}
#endif
