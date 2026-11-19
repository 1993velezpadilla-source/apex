#pragma once

#include <android/bitmap.h>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <utility>
#include <vector>

// Compiled in the Android JUCE GUI module with its JNI bindings available.
namespace juce::apexAndroidFrame
{
#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (setDensity, "setDensity", "(I)V")
DECLARE_JNI_CLASS (TileBitmap, "android/graphics/Bitmap")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (drawBitmap, "drawBitmap", "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V")
DECLARE_JNI_CLASS (TileCanvas, "android/graphics/Canvas")
#undef JNI_CLASS_MEMBERS

// Android's bitmap storage and JUCE's Android SoftwareImageType use the
// same premultiplied RGBA bytes. The old integer-array channel filter would
// swap red and blue a second time.
static_assert (PixelARGB::indexR == 0 && PixelARGB::indexG == 1
               && PixelARGB::indexB == 2 && PixelARGB::indexA == 3);

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

class FrameTiles
{
public:
    // The queued UI-thread invalidation may outlive the component peer.
    const std::shared_ptr<Damage> damage = std::make_shared<Damage>();

    void invalidate() noexcept
    {
        frameBounds = {};
    }

    template <typename Raster>
    bool paint(JNIEnv* env, jobject canvas, Rectangle<int> clip, float scale,
               bool opaque, Raster&& raster)
    {
        auto redraw = std::exchange(damage->pending, {});
        const bool reset = frameBounds != clip || frameScale != scale || frameOpaque != opaque;
        if (reset && !resetFrame(env, clip, scale, opaque))
            return false;

        if (reset)
            redraw.add(clip);
        redraw.clipTo(clip);
        redraw.offsetAll(-clip.getPosition());

        if (!redraw.isEmpty())
        {
            if (!opaque)
                for (const auto& area : redraw)
                    backing.clear(area);
            raster(backing, redraw, clip.getPosition());
            const Image::BitmapData source(backing, Image::BitmapData::readOnly);
            for (auto& tile : tiles)
                if (redraw.intersectsRectangle(tile.bounds) && !upload(env, tile, source, redraw))
                    return false;
        }

        for (const auto& tile : tiles)
        {
            const auto position = clip.getPosition() + tile.bounds.getPosition();
            env->CallVoidMethod(canvas, TileCanvas.drawBitmap, tile.bitmap.get(),
                                (jfloat)position.x, (jfloat)position.y, static_cast<jobject>(nullptr));
        }
        return true;
    }

private:
    struct Tile
    {
        Rectangle<int> bounds;
        GlobalRef bitmap;
        std::size_t stride;
    };

    bool resetFrame(JNIEnv* env, Rectangle<int> clip, float scale, bool opaque)
    {
        constexpr int tileSize = 384; // physical pixels
        const int width = clip.getWidth();
        const int height = clip.getHeight();
        if ((int64)width * height > 16'000'000)
            return false; // use the platform peer's fallback for unusually large surfaces

        auto config = LocalRef<jobject>(env->CallStaticObjectMethod(
            AndroidBitmapConfig, AndroidBitmapConfig.valueOf, javaString("ARGB_8888").get()));
        if (config == nullptr)
            return clearFailure(env);

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

    Image backing;
    std::vector<Tile> tiles;
    Rectangle<int> frameBounds;
    float frameScale = 0.0f;
    bool frameOpaque = false;
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
    void endRaster() { if (record) sample.rasterNs = ns(Clock::now() - beginRaster); }
    void presentedTiles() { if (record) sample.usedTiles = 1; }

private:
    using Clock = std::chrono::steady_clock;
    static jlong ns(Clock::duration duration)
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }
    static jlong threadCpuNs()
    {
        timespec now {};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
        return (jlong)now.tv_sec * 1'000'000'000 + now.tv_nsec;
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
Java_com_xziel_apex_mobileportrait_ApexUiBenchmark_nativeUseFrameTiles(JNIEnv*, jclass, jboolean enabled)
{
    juce::apexAndroidFrame::useTiles = enabled == JNI_TRUE;
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_xziel_apex_mobileportrait_ApexUiBenchmark_nativeWindowDrawSamples(JNIEnv* env, jclass, jboolean finish)
{
    using namespace juce::apexAndroidFrame;
    measuring = finish != JNI_TRUE;
    if (measuring)
    {
        frameCount = 0;
        return nullptr;
    }
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
