#pragma once

#include <android/bitmap.h>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
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
#else
// The matched emulator replay showed longer frame delays with this layer.
static constexpr bool reduceTimelineMotion = false;
#endif

struct Damage
{
    void add(Rectangle<int> area)
    {
        if (!area.isEmpty())
            pending.add(area.expanded(1));
        if (pending.getNumRectangles() > 64)
            pending = RectangleList<int>(pending.getBounds());
    }

    RectangleList<int> pending;
};

class BitmapTiles
{
public:
    explicit BitmapTiles(int divisor) : pixelDivisor(divisor) {}

    void invalidate() noexcept { frameBounds = {}; }

    bool needsReset(Rectangle<int> clip, float scale, bool opaque) const noexcept
    {
        return frameBounds != clip || frameScale != scale || frameOpaque != opaque;
    }

    template <typename Raster>
    bool update(JNIEnv* env, Rectangle<int> clip, float scale, bool opaque,
                RectangleList<int> redraw, Raster&& raster)
    {
        if (needsReset(clip, scale, opaque) && !resetFrame(env, clip, scale, opaque))
            return false;

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
            for (auto& tile : tiles)
                if (redraw.intersectsRectangle(tile.bounds) && !upload(env, tile, source, redraw))
                    return false;
        }

        return true;
    }

    void draw(JNIEnv* env, jobject canvas, Rectangle<int> covered = {}) const
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
        for (const auto& tile : tiles)
        {
            if (covered.contains(tile.bounds + origin))
                continue;

            const auto position = origin + tile.bounds.getPosition();
            env->CallVoidMethod(canvas, TileCanvas.drawBitmap, tile.bitmap.get(),
                                (jfloat)position.x, (jfloat)position.y, filteredPaint.get());
        }
        if (pixelDivisor != 1)
            env->CallVoidMethod(canvas, TileCanvas.restoreToCount, saveCount);
    }

private:
    struct Tile
    {
        Rectangle<int> bounds;
        GlobalRef bitmap;
        std::size_t stride;
    };

    int smallerDimension(int size) const noexcept { return (size + pixelDivisor - 1) / pixelDivisor; }

    bool resetFrame(JNIEnv* env, Rectangle<int> clip, float scale, bool opaque)
    {
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
        next.reserve((std::size_t)((width + tileSize - 1) / tileSize) * ((height + tileSize - 1) / tileSize));
        for (int y = 0; y < height; y += tileSize)
            for (int x = 0; x < width; x += tileSize)
            {
                const int w = jmin(tileSize, width - x);
                const int h = jmin(tileSize, height - y);
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
                next.push_back({ { x, y, w, h }, std::move(ref), info.stride });
            }

        backing = Image(SoftwareImageType().create(Image::ARGB, width, height, false));
        tiles = std::move(next);
        frameBounds = clip;
        frameScale = scale;
        frameOpaque = opaque;
        return true;
    }

    static bool upload(JNIEnv* env, Tile& tile, const Image::BitmapData& source,
                       const RectangleList<int>& redraw)
    {
        void* pixels = nullptr;
        if (AndroidBitmap_lockPixels(env, tile.bitmap.get(), &pixels) != ANDROID_BITMAP_RESULT_SUCCESS)
            return clearFailure(env);

        auto* bytes = static_cast<uint8*>(pixels);
        for (const auto& area : redraw)
        {
            const auto overlap = area.getIntersection(tile.bounds);
            if (overlap.isEmpty())
                continue;
            for (int y = overlap.getY(); y < overlap.getBottom(); ++y)
            {
                const auto offset = (std::size_t)(y - tile.bounds.getY()) * tile.stride
                                   + (std::size_t)(overlap.getX() - tile.bounds.getX()) * sizeof(PixelARGB);
                std::memcpy(bytes + offset, source.getPixelPointer(overlap.getX(), y),
                            (std::size_t)overlap.getWidth() * sizeof(PixelARGB));
            }
        }
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
    Rectangle<int> frameBounds;
    float frameScale = 0.0f;
    bool frameOpaque = false;
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

    void invalidate() noexcept
    {
        nativeTiles.invalidate();
        motionTiles.invalidate();
        drawnMotionArea = {};
    }

    template <typename Raster>
    bool paint(JNIEnv* env, jobject canvas, Rectangle<int> clip, float scale,
               Component& root, Raster&& raster)
    {
        const bool opaque = root.isOpaque();
        const auto* viewport = motionViewport.getComponent();
        Rectangle<int> motionArea;
        if (opaque && scale >= 2.0f && viewport != nullptr && viewport->isShowing()
            && root.isParentOf(viewport))
            motionArea = (root.getLocalArea(viewport, viewport->getLocalBounds()).toFloat() * scale)
                .getSmallestIntegerContainer().getIntersection(clip);

        auto redraw = std::exchange(damage->pending, {});
        if (nativeTiles.needsReset(clip, scale, opaque))
            redraw.add(clip);
        if (motionArea != drawnMotionArea)
        {
            redraw.add(drawnMotionArea);
            redraw.add(motionArea);
        }
        if (!motionArea.isEmpty())
        {
            if (motionTiles.needsReset(motionArea, scale, true))
                redraw.add(motionArea);
            if (!motionTiles.update(env, motionArea, scale, true, redraw, raster))
                return false;
            redraw.subtract(motionArea);
        }
        if (!nativeTiles.update(env, clip, scale, opaque, std::move(redraw), raster))
            return false;

        nativeTiles.draw(env, canvas, motionArea);
        if (!motionArea.isEmpty())
            motionTiles.draw(env, canvas);
        drawnMotionArea = motionArea;
        return true;
    }

    bool drawsMotionLayer() const noexcept { return !drawnMotionArea.isEmpty(); }

private:
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
    uint32 lastMotionMs = 0;
    Rectangle<int> drawnMotionArea;
};

#if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
struct FrameSample
{
    jlong wallNs = 0, cpuNs = 0, rasterNs = 0, pixels = 0, usedTiles = 0;
};
static std::array<FrameSample, 2048> recordedFrames {};
static std::size_t frameCount = 0;
static bool measuring = false;
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
        started = threadCpuNs();
    }

    ~ComponentPaintProbe()
    {
        if (type == nullptr)
            return;
        const auto duration = threadCpuNs() - started;
        current = parent;
        if (parent != nullptr)
            parent->childTime += duration;
        auto& sample = components[type];
        sample.selfNs += duration - childTime;
        ++sample.count;
    }

    static void reset() { components.clear(); }

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
        std::sort(sorted.begin(), sorted.end(), [](const Entry& left, const Entry& right)
        {
            return left.second.selfNs > right.second.selfNs;
        });
        __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH", "PEER_PROFILE frames=%zu", frames);
        for (std::size_t i = 0; i < jmin(sorted.size(), std::size_t{ 12 }); ++i)
        {
            const auto& [type, sample] = sorted[i];
            __android_log_print(ANDROID_LOG_INFO, "APEX_UI_BENCH", "PEER_PAINT %s calls=%zu self_ms=%.3f",
                                type->name(), sample.count, (double)sample.selfNs / 1'000'000.0);
        }
    }

private:
    struct Sample { jlong selfNs = 0; std::size_t count = 0; };
    static inline thread_local ComponentPaintProbe* current = nullptr;
    static inline std::map<const std::type_info*, Sample> components;

    const std::type_info* type;
    ComponentPaintProbe* parent = nullptr;
    jlong started = 0, childTime = 0;
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
            for (const auto& area : region)
                sample.pixels += (jlong)area.getWidth() * area.getHeight();
            beginRaster = Clock::now();
        }
    }
    void endRaster() { if (record) sample.rasterNs += ns(Clock::now() - beginRaster); }
    void presentedTiles(bool motion) { if (record) sample.usedTiles = motion ? 2 : 1; }

private:
    using Clock = std::chrono::steady_clock;
    static jlong ns(Clock::duration duration)
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }
    const bool record;
    FrameSample sample;
    Clock::time_point begin, beginRaster;
    jlong beginCpu = 0;
};
#else
static constexpr bool useTiles = true;
#endif
}

#if defined(APEX_ANDROID_UI_BENCHMARK) && APEX_ANDROID_UI_BENCHMARK
extern "C" JNIEXPORT void JNICALL
Java_com_xziel_apex_mobileportrait_ApexUiBenchmark_nativeUseFrameTiles(
    JNIEnv*, jclass, jboolean enabled, jboolean reduceMotion)
{
    juce::apexAndroidFrame::useTiles = enabled == JNI_TRUE;
    juce::apexAndroidFrame::reduceTimelineMotion = reduceMotion == JNI_TRUE;
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_xziel_apex_mobileportrait_ApexUiBenchmark_nativeWindowDrawSamples(JNIEnv* env, jclass, jboolean finish)
{
    using namespace juce::apexAndroidFrame;
    measuring = finish != JNI_TRUE;
    if (measuring)
    {
        frameCount = 0;
        ComponentPaintProbe::reset();
        return nullptr;
    }
    ComponentPaintProbe::logTop(frameCount);
    std::vector<jlong> values;
    values.reserve(frameCount * 5);
    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const auto& f = recordedFrames[i];
        values.insert(values.end(), { f.wallNs, f.cpuNs, f.rasterNs, f.pixels, f.usedTiles });
    }
    const auto count = (jsize)values.size();
    auto result = env->NewLongArray(count);
    if (result != nullptr && count > 0)
        env->SetLongArrayRegion(result, 0, count, values.data());
    return result;
}
#endif
