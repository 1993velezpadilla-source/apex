#pragma once

#include <android/bitmap.h>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <vector>

// Included by the Android JUCE GUI module with JNI helpers enabled.
namespace juce::apexAndroidPeer
{
#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (setDensity, "setDensity", "(I)V")
DECLARE_JNI_CLASS (BitmapDensity, "android/graphics/Bitmap")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (drawBitmap, "drawBitmap", "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V")
DECLARE_JNI_CLASS (BitmapCanvas, "android/graphics/Canvas")
#undef JNI_CLASS_MEMBERS

// JUCE's Android software renderer and Android's ARGB_8888 Bitmap share
// premultiplied RGBA bytes. Keep that representation all the way to Canvas.
static_assert (PixelARGB::indexR == 0 && PixelARGB::indexG == 1
               && PixelARGB::indexB == 2 && PixelARGB::indexA == 3);

class BitmapPixels final : public ImagePixelData
{
public:
    BitmapPixels (int w, int h, int stride, uint8* pixels, bool clear)
        : ImagePixelData (Image::ARGB, w, h), bytes (pixels), lineStride (stride)
    {
        if (clear)
            for (int y = 0; y < h; ++y)
                zeromem (bytes + static_cast<size_t> (y) * lineStride,
                         static_cast<size_t> (w) * sizeof (PixelARGB));
    }

    std::unique_ptr<ImageType> createType() const override
    {
        return std::make_unique<SoftwareImageType>();
    }

    std::unique_ptr<LowLevelGraphicsContext> createLowLevelContext() override
    {
        return std::make_unique<LowLevelGraphicsSoftwareRenderer> (Image (this));
    }

    void initialiseBitmapData (Image::BitmapData& data, int x, int y,
                               Image::BitmapData::ReadWriteMode) override
    {
        const auto offset = static_cast<size_t> (y) * lineStride + static_cast<size_t> (x) * sizeof (PixelARGB);
        data.data = bytes + offset;
        data.lineStride = lineStride;
        data.pixelStride = sizeof (PixelARGB);
        data.pixelFormat = Image::ARGB;
        data.size = static_cast<size_t> (height) * lineStride - offset;
    }

    Ptr clone() override
    {
        auto copy = SoftwareImageType().create (Image::ARGB, width, height, false);
        Image image (copy);
        Image::BitmapData data (image, Image::BitmapData::writeOnly);
        for (int y = 0; y < height; ++y)
            memcpy (data.getLinePointer (y), bytes + static_cast<size_t> (y) * lineStride,
                    static_cast<size_t> (width) * sizeof (PixelARGB));
        return copy;
    }

private:
    uint8* const bytes;
    const int lineStride;
};

class LockedBitmap
{
public:
    LockedBitmap (JNIEnv* e, jobject b) : env (e), bitmap (b)
    {
        valid = AndroidBitmap_lockPixels (env, bitmap, &data) == ANDROID_BITMAP_RESULT_SUCCESS;
    }

    ~LockedBitmap()
    {
        if (valid)
            AndroidBitmap_unlockPixels (env, bitmap);
    }

    uint8* pixels() const { return valid ? static_cast<uint8*> (data) : nullptr; }

private:
    JNIEnv* const env;
    const jobject bitmap;
    void* data = nullptr;
    bool valid = false;

    JUCE_DECLARE_NON_COPYABLE (LockedBitmap)
};

class WindowBitmap
{
public:
    template <typename Raster>
    bool paint (JNIEnv* env, jobject canvas, jobject paint, Rectangle<int> clip, bool opaque, Raster&& raster)
    {
        if (! prepare (env, clip.getWidth(), clip.getHeight()))
            return false;

        {
            LockedBitmap lock (env, bitmap.get());
            if (auto* pixels = lock.pixels())
            {
                Image image (new BitmapPixels (clip.getWidth(), clip.getHeight(), stride, pixels, ! opaque));
                raster (image);
            }
            else
            {
                if (env->ExceptionCheck())
                    env->ExceptionClear();
                return false;
            }
        }

        // The Canvas is already clipped to the requested rectangle, including
        // when the reusable bitmap has extra columns or rows.
        env->CallVoidMethod (canvas, BitmapCanvas.drawBitmap, bitmap.get(),
                             static_cast<jfloat> (clip.getX()), static_cast<jfloat> (clip.getY()), paint);
        return true;
    }

private:
    bool prepare (JNIEnv* env, int w, int h)
    {
        constexpr int block = 64;
        const auto width = ((w + block - 1) / block) * block;
        const auto height = ((h + block - 1) / block) * block;
        const Point<int> size (width, height);
        if (bitmap.get() != nullptr && w <= allocatedSize.x && h <= allocatedSize.y)
            return true;
        if (size == allocatedSize)
            return false;

        allocatedSize = size;
        bitmap.clear (env);
        if (argb8888.get() == nullptr)
        {
            auto config = LocalRef<jobject> (env->CallStaticObjectMethod (AndroidBitmapConfig,
                AndroidBitmapConfig.valueOf, javaString ("ARGB_8888").get()));
            if (config != nullptr)
                argb8888 = GlobalRef (config, env);
        }

        if (argb8888.get() == nullptr)
        {
            if (env->ExceptionCheck())
                env->ExceptionClear();
            return false;
        }

        auto candidate = LocalRef<jobject> (env->CallStaticObjectMethod (AndroidBitmap,
            AndroidBitmap.createBitmap, width, height, argb8888.get()));
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            return false;
        }
        if (candidate == nullptr)
            return false;

        AndroidBitmapInfo info {};
        if (AndroidBitmap_getInfo (env, candidate.get(), &info) != ANDROID_BITMAP_RESULT_SUCCESS
            || info.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
        {
            if (env->ExceptionCheck())
                env->ExceptionClear();
            return false;
        }

        // Pixel coordinates must never receive Android's Bitmap density scaling.
        env->CallVoidMethod (candidate.get(), BitmapDensity.setDensity, 0);
        bitmap = GlobalRef (candidate, env);
        stride = static_cast<int> (info.stride);
        return bitmap.get() != nullptr;
    }

    GlobalRef bitmap, argb8888;
    Point<int> allocatedSize;
    int stride = 0;
};

#if APEX_ANDROID_UI_BENCHMARK
struct FrameSample
{
    jlong wallNs = 0;
    jlong cpuNs = 0;
    jlong rasterNs = 0;
    jlong pixels = 0;
    jlong usedBitmap = 0;
};

static_assert (sizeof (FrameSample) == 5 * sizeof (jlong));
static std::array<FrameSample, 2048> frameSamples {};
static size_t sampleCount = 0;
static bool measureFrames = false;
static bool useBitmap = false;

class FrameProbe
{
public:
    FrameProbe (Rectangle<int> clip)
        : record (measureFrames && sampleCount < frameSamples.size())
    {
        if (record)
        {
            sample.pixels = static_cast<jlong> (clip.getWidth()) * clip.getHeight();
            started = Clock::now();
            cpuStarted = threadCpuNs();
        }
    }

    ~FrameProbe()
    {
        if (record)
        {
            sample.wallNs = elapsed (started);
            sample.cpuNs = threadCpuNs() - cpuStarted;
            frameSamples[sampleCount++] = sample;
        }
    }

    void startRaster() { if (record) rasterStarted = Clock::now(); }
    void endRaster() { if (record) sample.rasterNs = elapsed (rasterStarted); }
    void usedBitmap() { if (record) sample.usedBitmap = 1; }

private:
    using Clock = std::chrono::steady_clock;

    static jlong elapsed (Clock::time_point start)
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds> (Clock::now() - start).count();
    }

    static jlong threadCpuNs()
    {
        timespec value {};
        clock_gettime (CLOCK_THREAD_CPUTIME_ID, &value);
        return static_cast<jlong> (value.tv_sec) * 1000000000 + value.tv_nsec;
    }

    const bool record;
    FrameSample sample;
    Clock::time_point started, rasterStarted;
    jlong cpuStarted = 0;
};
#else
static constexpr bool useBitmap = true;
#endif
}

#if APEX_ANDROID_UI_BENCHMARK
extern "C" JNIEXPORT void JNICALL
Java_com_xziel_apex_mobileportrait_ApexUiBenchmark_nativeUseWindowBitmap (JNIEnv*, jclass, jboolean enabled)
{
    juce::apexAndroidPeer::useBitmap = enabled == JNI_TRUE;
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_xziel_apex_mobileportrait_ApexUiBenchmark_nativeWindowDrawSamples (JNIEnv* env, jclass, jboolean finish)
{
    using namespace juce::apexAndroidPeer;
    measureFrames = finish != JNI_TRUE;
    if (measureFrames)
    {
        sampleCount = 0;
        return nullptr;
    }
    std::vector<jlong> values;
    values.reserve (sampleCount * 5);
    for (size_t i = 0; i < sampleCount; ++i)
    {
        const auto& sample = frameSamples[i];
        values.insert (values.end(), { sample.wallNs, sample.cpuNs, sample.rasterNs, sample.pixels, sample.usedBitmap });
    }
    const auto count = static_cast<jsize> (values.size());
    auto result = env->NewLongArray (count);
    if (result != nullptr && count > 0)
        env->SetLongArrayRegion (result, 0, count, values.data());
    return result;
}
#endif
