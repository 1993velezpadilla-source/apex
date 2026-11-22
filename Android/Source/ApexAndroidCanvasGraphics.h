// Native Canvas adapter for JUCE's visible arrangement. Included by the
// Android peer after the standard JUCE JNI declarations.
#pragma once

#include <android/bitmap.h>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

namespace juce::apexAndroidFrame
{
#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (save, "save", "()I") \
    METHOD (restore, "restoreToCount", "(I)V") \
    METHOD (layerAlpha, "saveLayerAlpha", "(FFFFI)I") \
    METHOD (translate, "translate", "(FF)V") \
    METHOD (scale, "scale", "(FF)V") \
    METHOD (concat, "concat", "(Landroid/graphics/Matrix;)V") \
    METHOD (clipRect, "clipRect", "(FFFF)Z") \
    METHOD (clipOutRect, "clipOutRect", "(FFFF)Z") \
    METHOD (clipPath, "clipPath", "(Landroid/graphics/Path;)Z") \
    METHOD (getClipBounds, "getClipBounds", "(Landroid/graphics/Rect;)Z") \
    METHOD (drawRect, "drawRect", "(FFFFLandroid/graphics/Paint;)V") \
    METHOD (drawLines, "drawLines", "([FIILandroid/graphics/Paint;)V") \
    METHOD (drawPath, "drawPath", "(Landroid/graphics/Path;Landroid/graphics/Paint;)V") \
    METHOD (drawBitmap, "drawBitmap", "(Landroid/graphics/Bitmap;Landroid/graphics/Matrix;Landroid/graphics/Paint;)V")
DECLARE_JNI_CLASS_WITH_MIN_SDK (DirectCanvas, "android/graphics/Canvas", 26)
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (constructor, "<init>", "()V") \
    METHOD (rewind, "rewind", "()V") \
    METHOD (setFillType, "setFillType", "(Landroid/graphics/Path$FillType;)V") \
    METHOD (moveTo, "moveTo", "(FF)V") \
    METHOD (lineTo, "lineTo", "(FF)V") \
    METHOD (quadTo, "quadTo", "(FFFF)V") \
    METHOD (cubicTo, "cubicTo", "(FFFFFF)V") \
    METHOD (close, "close", "()V") \
    METHOD (addRect, "addRect", "(FFFFLandroid/graphics/Path$Direction;)V")
DECLARE_JNI_CLASS (DirectPath, "android/graphics/Path")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    STATICFIELD (winding, "WINDING", "Landroid/graphics/Path$FillType;") \
    STATICFIELD (evenOdd, "EVEN_ODD", "Landroid/graphics/Path$FillType;")
DECLARE_JNI_CLASS (DirectFillType, "android/graphics/Path$FillType")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    STATICFIELD (clockwise, "CW", "Landroid/graphics/Path$Direction;")
DECLARE_JNI_CLASS (DirectPathDirection, "android/graphics/Path$Direction")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (strokeWidth, "setStrokeWidth", "(F)V") \
    METHOD (filterBitmap, "setFilterBitmap", "(Z)V") \
    METHOD (xfermode, "setXfermode", "(Landroid/graphics/Xfermode;)Landroid/graphics/Xfermode;")
DECLARE_JNI_CLASS (DirectPaint, "android/graphics/Paint")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    STATICFIELD (src, "SRC", "Landroid/graphics/PorterDuff$Mode;")
DECLARE_JNI_CLASS (DirectBlendMode, "android/graphics/PorterDuff$Mode")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (constructor, "<init>", "(Landroid/graphics/PorterDuff$Mode;)V")
DECLARE_JNI_CLASS (DirectSrcBlend, "android/graphics/PorterDuffXfermode")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    STATICFIELD (clamp, "CLAMP", "Landroid/graphics/Shader$TileMode;")
DECLARE_JNI_CLASS (DirectTileMode, "android/graphics/Shader$TileMode")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (setLocalMatrix, "setLocalMatrix", "(Landroid/graphics/Matrix;)V")
DECLARE_JNI_CLASS (DirectShader, "android/graphics/Shader")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (constructor, "<init>", "(FFFF[I[FLandroid/graphics/Shader$TileMode;)V")
DECLARE_JNI_CLASS (DirectLinearGradient, "android/graphics/LinearGradient")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (constructor, "<init>", "(FFF[I[FLandroid/graphics/Shader$TileMode;)V")
DECLARE_JNI_CLASS (DirectRadialGradient, "android/graphics/RadialGradient")
#undef JNI_CLASS_MEMBERS

class CanvasGraphics final : public LowLevelGraphicsContext
{
public:
    // One set of mutable Android drawing objects per peer. Only the Android
    // message thread uses them. Glyph paths are font assets, independent of
    // the project position and zoom.
    struct Resources
    {
        struct Glyph
        {
            Typeface::Ptr face;
            TypefaceMetricsKind metrics;
            uint16_t number;
            GlobalRef path;
        };

        struct ColourGlyph
        {
            Typeface::Ptr face;
            TypefaceMetricsKind metrics;
            uint16_t number;
            Colour colour;
            Rectangle<int> bounds;
            float rasterHeight;
            GlobalRef bitmap;
        };

        bool initialise(JNIEnv* env)
        {
            if (ready)
                return true;
            brush = GlobalRef(LocalRef<jobject>(env->NewObject(AndroidPaint, AndroidPaint.constructor, 1)));
            imagePaint = GlobalRef(LocalRef<jobject>(env->NewObject(AndroidPaint, AndroidPaint.constructor, 2)));
            path = GlobalRef(LocalRef<jobject>(env->NewObject(DirectPath, DirectPath.constructor)));
            rect = GlobalRef(LocalRef<jobject>(env->NewObject(AndroidRect, AndroidRect.constructor, 0, 0, 0, 0)));
            matrix = GlobalRef(LocalRef<jobject>(env->NewObject(AndroidMatrix, AndroidMatrix.constructor)));
            matrixValues = GlobalRef(LocalRef<jobject>(env->NewFloatArray(9)));
            lines = GlobalRef(LocalRef<jobject>(env->NewFloatArray(lineCapacity)));
            clockwise = GlobalRef(LocalRef<jobject>(env->GetStaticObjectField(
                DirectPathDirection, DirectPathDirection.clockwise)));
            winding = GlobalRef(LocalRef<jobject>(env->GetStaticObjectField(DirectFillType, DirectFillType.winding)));
            evenOdd = GlobalRef(LocalRef<jobject>(env->GetStaticObjectField(DirectFillType, DirectFillType.evenOdd)));
            clamp = GlobalRef(LocalRef<jobject>(env->GetStaticObjectField(DirectTileMode, DirectTileMode.clamp)));
            LocalRef<jobject> src(env->GetStaticObjectField(DirectBlendMode, DirectBlendMode.src));
            srcBlend = GlobalRef(LocalRef<jobject>(env->NewObject(DirectSrcBlend, DirectSrcBlend.constructor, src.get())));
            ready = brush.get() && imagePaint.get() && path.get() && rect.get() && matrix.get()
                 && matrixValues.get() && lines.get() && clockwise.get() && winding.get()
                 && evenOdd.get() && clamp.get() && srcBlend.get() && !env->ExceptionCheck();
            return ready;
        }

        static constexpr int lineCapacity = 2048;
        GlobalRef brush, imagePaint, path, rect, matrix, matrixValues, lines;
        GlobalRef clockwise, winding, evenOdd, clamp, srcBlend;
        std::vector<Glyph> glyphs;
        std::vector<ColourGlyph> colourGlyphs;
        bool ready = false;
    };

    CanvasGraphics(JNIEnv* envToUse, jobject target, Rectangle<int> clip,
                   float scale, Resources& resources)
        : env(envToUse), canvas(target), native(resources)
    {
        if (!native.initialise(env))
        {
            failure = "canvas resources";
            return;
        }

        outerSave = env->CallIntMethod(canvas, DirectCanvas.save);
        clipRectangle(clip.toFloat(), DirectCanvas.clipRect);
        env->CallVoidMethod(canvas, DirectCanvas.scale, scale, scale);
        state.transform = AffineTransform::scale(scale);
    }

    ~CanvasGraphics() override
    {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (outerSave != 0)
            env->CallVoidMethod(canvas, DirectCanvas.restore, outerSave);
    }

    bool succeeded() const noexcept { return failure == nullptr && !env->ExceptionCheck(); }
    const char* failureReason() const noexcept { return failure != nullptr ? failure : "Canvas JNI call"; }

    bool isVectorDevice() const override { return false; }

    void setOrigin(Point<int> origin) override
    {
        if (origin == Point<int>{} || !succeeded())
            return;
        env->CallVoidMethod(canvas, DirectCanvas.translate, (float)origin.x, (float)origin.y);
        state.transform = AffineTransform::translation(origin.toFloat()).followedBy(state.transform);
        if (state.clip)
            *state.clip -= origin;
    }

    void addTransform(const AffineTransform& transform) override
    {
        if (transform.isIdentity() || !succeeded())
            return;
        env->CallVoidMethod(canvas, DirectCanvas.concat, matrixFor(transform));
        state.transform = transform.followedBy(state.transform);
        state.clip.reset();
    }

    float getPhysicalPixelScaleFactor() const override
    {
        const auto& t = state.transform;
        return 0.5f * (std::hypot(t.mat00, t.mat10) + std::hypot(t.mat01, t.mat11));
    }

    bool clipToRectangle(const Rectangle<int>& rect) override
    {
        if (state.clip && !state.clip->isEmpty() && rect.contains(*state.clip))
            return true;
        return clipRectangle(rect.toFloat(), DirectCanvas.clipRect);
    }

    bool clipToRectangleList(const RectangleList<int>& rects) override
    {
        if (rects.getNumRectangles() <= 1)
            return clipToRectangle(rects.getBounds());
        if (!succeeded())
            return false;
        const auto path = resetPath();
        for (const auto& rect : rects)
            addRect(path, rect.toFloat());
        state.clip.reset();
        return env->CallBooleanMethod(canvas, DirectCanvas.clipPath, path) == JNI_TRUE;
    }

    void excludeClipRectangle(const Rectangle<int>& rect) override
    {
        if (!rect.isEmpty())
            clipRectangle(rect.toFloat(), DirectCanvas.clipOutRect);
    }

    void clipToPath(const Path& path, const AffineTransform& transform) override
    {
        if (succeeded())
        {
            const auto tmp = resetPath();
            writePath(tmp, path, transform);
            env->CallBooleanMethod(canvas, DirectCanvas.clipPath, tmp);
            state.clip.reset();
        }
    }

    // The peer can redraw this frame with JUCE's software context for
    // arbitrary alpha masks or tiled image brushes.
    void clipToImageAlpha(const Image&, const AffineTransform&) override { failure = "image alpha clip"; }

    bool clipRegionIntersects(const Rectangle<int>& rect) override
    {
        return getClipBounds().intersects(rect);
    }

    Rectangle<int> getClipBounds() const override
    {
        if (!succeeded())
            return {};
        if (!state.clip)
        {
            const auto rect = native.rect.get();
            state.clip = env->CallBooleanMethod(canvas, DirectCanvas.getClipBounds, rect) == JNI_TRUE
                ? Rectangle<int>::leftTopRightBottom(env->GetIntField(rect, AndroidRect.left),
                                                     env->GetIntField(rect, AndroidRect.top),
                                                     env->GetIntField(rect, AndroidRect.right),
                                                     env->GetIntField(rect, AndroidRect.bottom))
                : Rectangle<int>{};
        }
        return *state.clip;
    }

    bool isClipEmpty() const override { return getClipBounds().isEmpty(); }

    void saveState() override
    {
        if (succeeded())
            push(env->CallIntMethod(canvas, DirectCanvas.save));
    }

    void restoreState() override
    {
        if (saved.empty() || !succeeded())
            return;
        const auto count = saved.back().canvasSave;
        state = std::move(saved.back());
        saved.pop_back();
        env->CallVoidMethod(canvas, DirectCanvas.restore, count);
        brushDirty = true;
    }

    void beginTransparencyLayer(float opacity) override
    {
        if (!succeeded())
            return;
        const auto clip = getClipBounds().toFloat();
        push(env->CallIntMethod(canvas, DirectCanvas.layerAlpha,
            clip.getX(), clip.getY(), clip.getRight(), clip.getBottom(),
            jlimit(0, 255, roundToInt(opacity * 255.0f))));
    }

    void endTransparencyLayer() override { restoreState(); }

    void setFill(const FillType& fill) override
    {
        state.fill = fill;
        brushDirty = true;
    }

    void setOpacity(float opacity) override
    {
        state.fill.setOpacity(opacity);
        brushDirty = true;
    }

    void setInterpolationQuality(Graphics::ResamplingQuality quality) override
    {
        state.quality = quality;
    }

    void fillRect(const Rectangle<int>& rect, bool replace) override
    {
        if (rect.isEmpty() || !applyBrush())
            return;
        const bool useSrc = replace && (!state.fill.isColour() || !state.fill.colour.isOpaque());
        if (useSrc)
            setSrcBlend(native.srcBlend.get());
        drawRectangle(rect.toFloat());
        if (useSrc)
            setSrcBlend(nullptr);
    }

    void fillRect(const Rectangle<float>& rect) override
    {
        if (!rect.isEmpty() && applyBrush())
            drawRectangle(rect);
    }

    void fillRectList(const RectangleList<float>& rects) override
    {
        if (rects.isEmpty() || !applyBrush())
            return;

        // Waveforms have one equal-width rectangle per visible column.
        // Android consumes the whole batch as a single drawLines operation
        // with butt caps, so each stroke covers its corresponding rectangle.
        const auto width = rects.begin()->getWidth();
        auto previousRight = rects.begin()->getX();
        if (rects.getNumRectangles() >= 12 && width > 0.0f && width <= 2.0f && state.fill.isColour()
            && std::all_of(rects.begin(), rects.end(), [&] (auto rect)
               {
                   const bool column = rect.getWidth() == width && rect.getX() >= previousRight;
                   previousRight = rect.getRight();
                   return column;
               }))
        {
            drawColumns(rects, width);
            return;
        }

        // Keep JUCE's per-rectangle blending when the caller uses alpha.
        for (const auto& rect : rects)
            drawRectangle(rect);
    }

    void fillPath(const Path& path, const AffineTransform& transform) override
    {
        if (path.isEmpty() || !applyBrush())
            return;
        const auto tmp = resetPath();
        writePath(tmp, path, transform);
        env->CallVoidMethod(canvas, DirectCanvas.drawPath, tmp, native.brush.get());
    }

    void drawLine(const Line<float>& line) override { drawLineWithThickness(line, 1.0f); }

    void drawImage(const Image& image, const AffineTransform& transform) override
    {
        if (!image.isValid() || !succeeded())
            return;
        const auto bitmap = createBitmap(image);
        if (bitmap != nullptr)
            drawBitmap(bitmap.get(), transform, state.fill.colour.getAlpha());
    }

    void setFont(const Font& font) override { state.font = font; }
    const Font& getFont() override { return state.font; }

    void drawGlyphs(Span<const uint16_t> glyphs, Span<const Point<float>> positions,
                    const AffineTransform& transform) override
    {
        if (glyphs.empty() || !applyBrush())
            return;
        auto face = state.font.getTypefacePtr();
        if (face == nullptr || !state.fill.isColour())
        {
            failure = "glyph brush";
            return;
        }
        const auto metrics = state.font.getMetricsKind();
        if (face->getColourGlyphFormats() != 0)
        {
            for (size_t i = 0; i < glyphs.size() && succeeded(); ++i)
                drawColourGlyph(face, metrics, glyphs[i], positions[i], transform);
            return;
        }

        const auto height = detail::FontRendering::getEffectiveHeight(state.font);
        const auto glyphScale = AffineTransform::scale(height * state.font.getHorizontalScale(), height);
        for (size_t i = 0; i < glyphs.size() && succeeded(); ++i)
        {
            const auto path = glyphPath(face, metrics, glyphs[i]);
            if (path == nullptr)
                continue;
            const auto pos = positions[i];
            const auto save = env->CallIntMethod(canvas, DirectCanvas.save);
            if (transform.isOnlyTranslation())
            {
                env->CallVoidMethod(canvas, DirectCanvas.translate, pos.x + transform.getTranslationX(),
                                    pos.y + transform.getTranslationY());
                env->CallVoidMethod(canvas, DirectCanvas.scale, glyphScale.mat00, height);
            }
            else
            {
                const auto placement = glyphScale.translated(pos).followedBy(transform);
                env->CallVoidMethod(canvas, DirectCanvas.concat, matrixFor(placement));
            }
            env->CallVoidMethod(canvas, DirectCanvas.drawPath, path, native.brush.get());
            env->CallVoidMethod(canvas, DirectCanvas.restore, save);
        }
    }

    std::unique_ptr<ImageType> getPreferredImageTypeForTemporaryImages() const override
    {
        return std::make_unique<SoftwareImageType>();
    }

    uint64_t getFrameId() const override { return frameId; }

private:
    struct State
    {
        AffineTransform transform;
        mutable std::optional<Rectangle<int>> clip;
        FillType fill { Colours::black };
        Font font { FontOptions{} };
        Graphics::ResamplingQuality quality = Graphics::mediumResamplingQuality;
        jint canvasSave = 0;
    };

    void push(jint count)
    {
        saved.push_back(state);
        saved.back().canvasSave = count;
    }

    bool clipRectangle(Rectangle<float> rect, jmethodID method)
    {
        if (!succeeded())
            return false;
        state.clip.reset();
        return env->CallBooleanMethod(canvas, method, rect.getX(), rect.getY(),
                                      rect.getRight(), rect.getBottom()) == JNI_TRUE;
    }

    jobject matrixFor(const AffineTransform& t)
    {
        const jfloat values[] { t.mat00, t.mat01, t.mat02, t.mat10, t.mat11, t.mat12, 0.0f, 0.0f, 1.0f };
        auto array = (jfloatArray)native.matrixValues.get();
        env->SetFloatArrayRegion(array, 0, 9, values);
        env->CallVoidMethod(native.matrix.get(), AndroidMatrix.setValues, array);
        return native.matrix.get();
    }

    jobject resetPath()
    {
        const auto path = native.path.get();
        env->CallVoidMethod(path, DirectPath.rewind);
        env->CallVoidMethod(path, DirectPath.setFillType, native.winding.get());
        return path;
    }

    void addRect(jobject path, Rectangle<float> rect)
    {
        env->CallVoidMethod(path, DirectPath.addRect, rect.getX(), rect.getY(), rect.getRight(),
                            rect.getBottom(), native.clockwise.get());
    }

    void writePath(jobject target, const Path& path, const AffineTransform& transform)
    {
        env->CallVoidMethod(target, DirectPath.setFillType,
                            path.isUsingNonZeroWinding() ? native.winding.get() : native.evenOdd.get());
        for (Path::Iterator i(path); i.next();)
        {
            const auto point = [&] (float x, float y) { return Point<float>(x, y).transformedBy(transform); };
            const auto a = point(i.x1, i.y1);
            switch (i.elementType)
            {
                case Path::Iterator::startNewSubPath:
                    env->CallVoidMethod(target, DirectPath.moveTo, a.x, a.y);
                    break;
                case Path::Iterator::lineTo:
                    env->CallVoidMethod(target, DirectPath.lineTo, a.x, a.y);
                    break;
                case Path::Iterator::quadraticTo:
                {
                    const auto b = point(i.x2, i.y2);
                    env->CallVoidMethod(target, DirectPath.quadTo, a.x, a.y, b.x, b.y);
                    break;
                }
                case Path::Iterator::cubicTo:
                {
                    const auto b = point(i.x2, i.y2), c = point(i.x3, i.y3);
                    env->CallVoidMethod(target, DirectPath.cubicTo, a.x, a.y, b.x, b.y, c.x, c.y);
                    break;
                }
                case Path::Iterator::closePath:
                    env->CallVoidMethod(target, DirectPath.close);
                    break;
            }
        }
    }

    jobject glyphPath(const Typeface::Ptr& face, TypefaceMetricsKind metrics, uint16_t number)
    {
        auto& glyphs = native.glyphs;
        const auto entry = std::find_if(glyphs.begin(), glyphs.end(), [&] (const Resources::Glyph& glyph)
        {
            return glyph.face == face && glyph.metrics == metrics && glyph.number == number;
        });
        if (entry != glyphs.end())
            return entry->path.get();

        Path outline;
        face->getOutlineForGlyph(metrics, number, outline);
        GlobalRef path;
        if (!outline.isEmpty())
        {
            LocalRef<jobject> tmp(env->NewObject(DirectPath, DirectPath.constructor));
            if (tmp == nullptr)
            {
                failure = "glyph path";
                return nullptr;
            }
            writePath(tmp.get(), outline, {});
            path = GlobalRef(tmp);
        }
        if (glyphs.size() == 384)
            glyphs.erase(glyphs.begin(), glyphs.begin() + 128);
        glyphs.push_back({ face, metrics, number, std::move(path) });
        return glyphs.back().path.get();
    }

    LocalRef<jobject> createBitmap(const Image& image)
    {
        const auto w = image.getWidth(), h = image.getHeight();
        if ((int64_t)w * h > 4'000'000)
        {
            failure = "large image";
            return {};
        }
        const auto pixels = image.convertedToFormat(Image::ARGB);
        LocalRef<jobject> config(env->CallStaticObjectMethod(AndroidBitmapConfig, AndroidBitmapConfig.valueOf,
                                                          javaString("ARGB_8888").get()));
        if (config == nullptr)
        {
            failure = "ARGB bitmap config";
            return {};
        }
        LocalRef<jobject> bitmap(env->CallStaticObjectMethod(AndroidBitmap, AndroidBitmap.createBitmap,
                                                           w, h, config.get()));
        AndroidBitmapInfo info {};
        void* dest = nullptr;
        if (bitmap == nullptr || AndroidBitmap_getInfo(env, bitmap.get(), &info) != ANDROID_BITMAP_RESULT_SUCCESS
            || info.format != ANDROID_BITMAP_FORMAT_RGBA_8888
            || AndroidBitmap_lockPixels(env, bitmap.get(), &dest) != ANDROID_BITMAP_RESULT_SUCCESS)
        {
            failure = "bitmap allocation";
            return {};
        }
        {
            Image::BitmapData src(pixels, Image::BitmapData::readOnly);
            for (int y = 0; y < h; ++y)
                std::memcpy(static_cast<uint8*>(dest) + y * info.stride, src.getLinePointer(y),
                            (size_t)w * sizeof(PixelARGB));
        }
        if (AndroidBitmap_unlockPixels(env, bitmap.get()) != ANDROID_BITMAP_RESULT_SUCCESS)
        {
            failure = "bitmap pixels";
            return {};
        }
        return bitmap;
    }

    void drawBitmap(jobject bitmap, const AffineTransform& transform, int alpha)
    {
        const auto paint = native.imagePaint.get();
        env->CallVoidMethod(paint, AndroidPaint.setAlpha, alpha);
        env->CallVoidMethod(paint, DirectPaint.filterBitmap,
                            (jboolean)(state.quality != Graphics::lowResamplingQuality));
        env->CallVoidMethod(canvas, DirectCanvas.drawBitmap, bitmap, matrixFor(transform), paint);
    }

    void drawColourGlyph(const Typeface::Ptr& face, TypefaceMetricsKind metrics, uint16_t number,
                         Point<float> position, const AffineTransform& transform)
    {
        auto& cache = native.colourGlyphs;
        const auto colour = state.fill.colour;
        auto entry = std::find_if(cache.begin(), cache.end(), [&] (const Resources::ColourGlyph& glyph)
        {
            return glyph.face == face && glyph.metrics == metrics && glyph.number == number
                && glyph.colour == colour;
        });
        if (entry == cache.end())
        {
            // Bitmap emoji and layered fonts share one small glyph image at
            // all timeline positions. JUCE paints their original glyph layers.
            auto font = state.font;
            font.setHeight(48.0f);
            font.setHorizontalScale(1.0f);
            const auto height = detail::FontRendering::getEffectiveHeight(font);
            const auto outline = face->getGlyphBounds(metrics, number);
            if (outline.isEmpty())
                return;
            const auto bounds = (outline * height).getSmallestIntegerContainer().expanded(2);
            if (bounds.getWidth() > 256 || bounds.getHeight() > 256)
            {
                failure = "large colour glyph";
                return;
            }
            Image image(Image::ARGB, bounds.getWidth(), bounds.getHeight(), true, SoftwareImageType());
            LowLevelGraphicsSoftwareRenderer raster(image);
            raster.setFont(font);
            raster.setFill(FillType(colour));
            const auto origin = -bounds.getPosition().toFloat();
            raster.drawGlyphs({ &number, 1 }, { &origin, 1 }, {});
            const auto bitmap = createBitmap(image);
            if (bitmap == nullptr)
                return;
            if (cache.size() == 96)
                cache.erase(cache.begin(), cache.begin() + 32);
            cache.push_back({ face, metrics, number, colour, bounds, height, GlobalRef(bitmap) });
            entry = std::prev(cache.end());
        }
        const auto scale = detail::FontRendering::getEffectiveHeight(state.font) / entry->rasterHeight;
        const auto placement = AffineTransform::translation(entry->bounds.getPosition())
            .scaled(scale * state.font.getHorizontalScale(), scale)
            .translated(position).followedBy(transform);
        drawBitmap(entry->bitmap.get(), placement, 255);
    }

    LocalRef<jobject> makeGradient(const ColourGradient& gradient)
    {
        const auto n = gradient.getNumColours();
        std::vector<jint> colours((size_t)n);
        std::vector<jfloat> positions((size_t)n);
        for (int i = 0; i < n; ++i)
        {
            colours[(size_t)i] = (jint)gradient.getColour(i).getARGB();
            positions[(size_t)i] = (float)gradient.getColourPosition(i);
        }
        LocalRef<jintArray> args(env->NewIntArray(n));
        LocalRef<jfloatArray> stops(env->NewFloatArray(n));
        if (args == nullptr || stops == nullptr)
            return {};
        env->SetIntArrayRegion(args.get(), 0, n, colours.data());
        env->SetFloatArrayRegion(stops.get(), 0, n, positions.data());
        const auto p = gradient.point1, q = gradient.point2;
        return LocalRef<jobject>(gradient.isRadial
            ? env->NewObject(DirectRadialGradient, DirectRadialGradient.constructor, p.x, p.y,
                             p.getDistanceFrom(q), args.get(), stops.get(), native.clamp.get())
            : env->NewObject(DirectLinearGradient, DirectLinearGradient.constructor, p.x, p.y, q.x, q.y,
                             args.get(), stops.get(), native.clamp.get()));
    }

    bool applyBrush()
    {
        if (!succeeded())
            return false;
        if (!brushDirty)
            return true;

        if (state.fill.isTiledImage())
        {
            failure = "tiled image brush";
            return false;
        }

        const auto brush = native.brush.get();
        auto colour = state.fill.colour;
        LocalRef<jobject> shader;
        if (const auto* gradient = state.fill.gradient.get())
        {
            const auto last = gradient->getNumColours() - 1;
            if (last >= 1 && gradient->point1 != gradient->point2)
            {
                shader = makeGradient(*gradient);
                if (shader == nullptr)
                {
                    failure = "gradient shader";
                    return false;
                }
                if (!state.fill.transform.isIdentity())
                    env->CallVoidMethod(shader.get(), DirectShader.setLocalMatrix, matrixFor(state.fill.transform));
                colour = Colours::white.withAlpha(colour.getFloatAlpha());
            }
            else
                colour = last < 0 ? Colours::transparentBlack
                                  : gradient->getColour(last).withMultipliedAlpha(colour.getFloatAlpha());
        }
        LocalRef<jobject> oldShader(env->CallObjectMethod(brush, AndroidPaint.setShader, shader.get()));
        env->CallVoidMethod(brush, AndroidPaint.setColor, (jint)colour.getARGB());
        brushDirty = false;
        return succeeded();
    }

    void setSrcBlend(jobject mode)
    {
        LocalRef<jobject> old(env->CallObjectMethod(native.brush.get(), DirectPaint.xfermode, mode));
    }

    void drawRectangle(Rectangle<float> r)
    {
        env->CallVoidMethod(canvas, DirectCanvas.drawRect, r.getX(), r.getY(), r.getRight(),
                            r.getBottom(), native.brush.get());
    }

    void drawColumns(const RectangleList<float>& rects, float width)
    {
        auto* array = (jfloatArray)native.lines.get();
        std::array<jfloat, Resources::lineCapacity> values;
        jint count = 0;
        env->CallVoidMethod(native.brush.get(), DirectPaint.strokeWidth, width);
        const auto flush = [&]
        {
            if (count != 0)
            {
                env->SetFloatArrayRegion(array, 0, count, values.data());
                env->CallVoidMethod(canvas, DirectCanvas.drawLines, array, 0, count, native.brush.get());
                count = 0;
            }
        };
        for (const auto& rect : rects)
        {
            const auto x = rect.getCentreX();
            values[(size_t)count++] = x;
            values[(size_t)count++] = rect.getY();
            values[(size_t)count++] = x;
            values[(size_t)count++] = rect.getBottom();
            if (count == Resources::lineCapacity)
                flush();
        }
        flush();
    }

    JNIEnv* env;
    jobject canvas;
    Resources& native;
    State state;
    std::vector<State> saved;
    const char* failure = nullptr;
    bool brushDirty = true;
    jint outerSave = 0;
    const uint64_t frameId = nextFrame++;
    static inline uint64_t nextFrame = 1; // Compiled and used on the Android message thread.
};
}
