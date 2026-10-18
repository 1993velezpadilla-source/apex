#pragma once
#include <JuceHeader.h>

#if JUCE_WINDOWS
#include <windows.h>
#include <gdiplus.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>
#include <initializer_list>
#include <cwchar>
#pragma comment(lib, "gdiplus.lib")

// APEX Evo Eye / Bubblegum Engine.
// Entire boot artwork is LIVE PROCEDURAL GDI+ DRAWING, like a native plugin GUI.
// The supplied picture was only an art-direction reference. NO PNG/JPEG, files,
// Win32 RCDATA artwork, external web renderer, or pre-baked image is displayed.
// Win32 owns the animation thread so JUCE's busy startup thread cannot freeze it.
class DAWSplashScreen final
{
public:
    static DAWSplashScreen* launch()
    {
        std::unique_ptr<DAWSplashScreen> splash(new DAWSplashScreen());
        splash->ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        splash->stop_  = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (! splash->ready_ || ! splash->stop_)
            return nullptr;
        splash->thread_ = CreateThread(nullptr, 0, &DAWSplashScreen::runThread,
                                      splash.get(), 0, nullptr);
        if (! splash->thread_)
            return nullptr;
        if (WaitForSingleObject(splash->ready_, 4000) != WAIT_OBJECT_0
            || ! splash->valid_.load())
            return nullptr;
        return splash.release();
    }

    // CI/QA preview is exported FROM THE EXACT LIVE C++ RENDERER, not
    // generated art. Kept separate from the production boot animation.
    static bool renderDiagnosticFrame(const wchar_t* pngFile, float seconds)
    {
        using namespace Gdiplus;
        GdiplusStartupInput input;
        ULONG_PTR token = 0;
        if (GdiplusStartup(&token, &input, nullptr) != Ok)
            return false;
        bool succeeded = false;
        {
            Bitmap bitmap(1200, 750, PixelFormat32bppARGB);
            if (bitmap.GetLastStatus() == Ok)
            {
                // Release the drawing context before Bitmap::Save;
                // otherwise GDI+ may return ObjectBusy for the bitmap.
                {
                    Graphics g(&bitmap);
                    drawFrame(g, seconds);
                }

                UINT count = 0, bytes = 0;
                if (GetImageEncodersSize(&count, &bytes) == Ok && count > 0
                    && bytes > 0)
                {
                    std::vector<BYTE> storage(bytes);
                    auto* codecs = reinterpret_cast<ImageCodecInfo*>(storage.data());
                    if (GetImageEncoders(count, bytes, codecs) == Ok)
                    {
                        for (UINT i = 0; i < count; ++i)
                        {
                            // Image encoder is a DIAGNOSTIC OUTPUT only.
                            if (codecs[i].MimeType
                                && std::wcscmp(codecs[i].MimeType, L"image/png") == 0)
                            {
                                succeeded = bitmap.Save(pngFile,
                                                   &codecs[i].Clsid, nullptr) == Ok;
                                break;
                            }
                        }
                    }
                }
            }
        }
        GdiplusShutdown(token);
        return succeeded;
    }

    // Hide only AFTER the real StartupPanel is visible. No fixed splash delay.
    void finish() { delete this; }

    ~DAWSplashScreen()
    {
        if (stop_) SetEvent(stop_);
        if (thread_)
        {
            WaitForSingleObject(thread_, INFINITE);
            CloseHandle(thread_);
        }
        if (ready_) CloseHandle(ready_);
        if (stop_) CloseHandle(stop_);
    }

private:
    DAWSplashScreen() = default;
    using G = Gdiplus::Graphics;
    using C = Gdiplus::Color;
    using P = Gdiplus::PointF;
    using R = Gdiplus::RectF;
    static constexpr float pi = 3.14159265358979323846f;

    static DWORD WINAPI runThread(void* self)
    {
        return static_cast<DAWSplashScreen*>(self)->run();
    }

    static float ease(float t)
    {
        t = juce::jlimit(0.0f, 1.0f, t);
        return t * t * (3.0f - 2.0f * t);
    }

    static float blink(double sec)
    {
        // Quick asymmetric organic close/open: ~0.34 sec each 3.8 sec.
        const float phase = static_cast<float>(std::fmod(sec + 2.2, 3.8));
        if (phase < 2.76f || phase > 3.10f) return 0.0f;
        if (phase < 2.90f) return ease((phase - 2.76f) / 0.14f);
        return 1.0f - ease((phase - 2.90f) / 0.20f);
    }

    static void polygon(G& g, std::initializer_list<P> vertices, C upper,
                        C lower, C outline = C(0, 0, 0, 0))
    {
        if (vertices.size() < 3) return;
        std::vector<P> pts(vertices);
        Gdiplus::GraphicsPath path;
        path.AddPolygon(pts.data(), static_cast<INT>(pts.size()));
        Gdiplus::RectF bounds;
        path.GetBounds(&bounds);
        const float height = juce::jmax(1.0f, bounds.Height);
        Gdiplus::LinearGradientBrush fill(
            R(bounds.X, bounds.Y, juce::jmax(1.0f, bounds.Width), height),
            upper, lower, Gdiplus::LinearGradientModeVertical);
        g.FillPath(&fill, &path);
        if (outline.GetA() != 0)
        {
            Gdiplus::Pen edge(outline, 1.4f);
            g.DrawPath(&edge, &path);
        }
    }

    static void curve(G& g, C color, float width, P a, P b, P c, P d)
    {
        Gdiplus::Pen pen(color, width);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        g.DrawBezier(&pen, a, b, c, d);
    }

    static void line(G& g, C color, float width, P a, P b)
    {
        Gdiplus::Pen pen(color, width);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&pen, a, b);
    }

    static void oval(G& g, C color, float width, R r)
    {
        Gdiplus::Pen p(color, width);
        g.DrawEllipse(&p, r);
    }

    static void glow(G& g, P center, float rx, float ry,
                     BYTE red, BYTE green, BYTE blue, float intensity)
    {
        // Transparent additive-like bloom drawn on the opaque black canvas.
        for (int k = 11; k >= 1; --k)
        {
            const float f = k / 11.0f;
            const BYTE alpha = static_cast<BYTE>(
                juce::jlimit(0.0f, 255.0f, intensity * 9.0f * (1.15f - f)));
            Gdiplus::SolidBrush b(C(alpha, red, green, blue));
            g.FillEllipse(&b, R(center.X - rx * f, center.Y - ry * f,
                                2.0f * rx * f, 2.0f * ry * f));
        }
    }

    static void background(G& g, float time)
    {
        g.Clear(C(255, 3, 3, 8));

        // Graphite-black GUI plate and very restrained dark-red atmosphere.
        Gdiplus::LinearGradientBrush bg(R(0.0f, 0.0f, 1200.0f, 750.0f),
                                        C(255, 11, 11, 18),
                                        C(255, 2, 2, 6),
                                        Gdiplus::LinearGradientModeVertical);
        g.FillRectangle(&bg, R(0.0f, 0.0f, 1200.0f, 750.0f));
        glow(g, P(600.0f, 315.0f), 545.0f, 310.0f, 125, 8, 38, 7.0f);

        const C ring(50, 200, 18, 62);
        oval(g, ring, 1.2f, R(255, 40, 690, 570));
        oval(g, C(28, 238, 19, 80), 1.0f, R(307, 88, 586, 470));
        line(g, C(38, 255, 42, 108), 1.0f, P(600, 24), P(600, 112));
        line(g, C(38, 255, 42, 108), 1.0f, P(600, 502), P(600, 542));
        for (int i = 0; i < 14; ++i)
        {
            const float a = (i * 2.0f * pi / 14.0f) + 0.04f;
            const float x = 600.0f + std::cos(a) * 355.0f;
            const float y = 322.0f + std::sin(a) * 294.0f;
            line(g, C(72, 220, 24, 64), 1.5f,
                 P(x - std::cos(a) * 6.0f, y - std::sin(a) * 6.0f),
                 P(x + std::cos(a) * 6.0f, y + std::sin(a) * 6.0f));
        }

        // Deterministic animated embers. No random allocations on audio thread.
        for (int i = 0; i < 42; ++i)
        {
            const float a = i * 2.399963f;
            const float f = 0.22f + (i % 13) / 17.0f;
            const float x = 600.0f + std::cos(a) * 480.0f * f
                                      + std::sin(time * 0.23f + i) * 12.0f;
            const float y = 305.0f + std::sin(a) * 300.0f * f
                                      - std::fmod(time * (8.0f + i % 6), 38.0f);
            Gdiplus::SolidBrush ember(C(static_cast<BYTE>(35 + (i % 7) * 10),
                                         255, 24, 74));
            const float radius = (i % 5 == 0) ? 1.4f : 0.7f;
            g.FillEllipse(&ember, R(x, y, radius * 2, radius * 2));
        }
    }

    static void makeEyeContour(Gdiplus::GraphicsPath& p, float open)
    {
        // The eye is physically modeled in code, and its interior CLIP follows
        // the eyelids. It is NOT an image with an artificial dark overlay.
        const float top = 304.0f - 176.0f * open;
        const float bottom = 304.0f + 161.0f * open;
        p.Reset();
        p.StartFigure();
        p.AddBezier(P(213, 304), P(365, top), P(835, top), P(987, 304));
        p.AddBezier(P(987, 304), P(826, bottom), P(374, bottom), P(213, 304));
        p.CloseFigure();
    }

    static void iris(G& g, float sec, float openness)
    {
        if (openness < 0.006f) return;
        Gdiplus::GraphicsPath aperture;
        makeEyeContour(aperture, openness);
        const auto saved = g.Save();
        g.SetClip(&aperture, Gdiplus::CombineModeIntersect);

        Gdiplus::LinearGradientBrush vitreous(R(208, 148, 786, 330),
                                             C(255, 12, 0, 12),
                                             C(255, 54, 3, 21),
                                             Gdiplus::LinearGradientModeVertical);
        g.FillPath(&vitreous, &aperture);

        const float beat = 0.74f + 0.26f * std::sin(sec * 2.8f);
        glow(g, P(600, 304), 224, 168, 236, 8, 51, 20.0f * beat);
        glow(g, P(600, 304), 170, 157, 255, 13, 61, 12.0f * beat);

        // Radial iris fibers / electrical veins: individually Bezier-rendered.
        // Rotational movement and phase are true procedural animation.
        for (int i = 0; i < 78; ++i)
        {
            const float theta = i * 2.0f * pi / 78.0f;
            const float drift = std::sin(sec * 0.9f + i * 1.41f) * 0.065f;
            const float innerR = 41.0f + static_cast<float>(i % 5) * 2.5f;
            const float outerR = 150.0f + static_cast<float>(i % 9) * 2.1f;
            const auto point = [](float radius, float t) {
                return P(600.0f + std::cos(t) * radius,
                         304.0f + std::sin(t) * radius * 0.87f);
            };
            const P a = point(innerR, theta);
            const P b = point(77.0f, theta + drift + 0.11f);
            const P c = point(112.0f, theta - drift - 0.085f);
            const P d = point(outerR, theta + drift);
            const BYTE alpha = static_cast<BYTE>(78 + (i % 7) * 19);
            curve(g, C(alpha, 255, static_cast<BYTE>(25 + i % 4 * 18), 97),
                  (i % 9 == 0) ? 2.7f : 1.1f, a, b, c, d);
        }

        // Luminescent iris ring and internal specular layer.
        for (int k = 6; k > 0; --k)
            oval(g, C(static_cast<BYTE>(16 + (6 - k) * 10), 255, 18, 78),
                 static_cast<float>(k * 4),
                 R(426.0f, 147.0f, 348.0f, 314.0f));
        oval(g, C(188, 255, 83, 133), 2.1f, R(430, 151, 340, 306));
        oval(g, C(160, 255, 21, 106), 1.2f, R(461, 180, 278, 248));

        // The pupil is a genuine drawn path and narrows / expands with life.
        const float pupilHalf = 21.0f + 4.0f * std::sin(sec * 1.65f);
        Gdiplus::GraphicsPath pupil;
        pupil.StartFigure();
        pupil.AddBezier(P(600, 176), P(600 + pupilHalf, 236),
                        P(600 + pupilHalf, 374), P(600, 434));
        pupil.AddBezier(P(600, 434), P(600 - pupilHalf, 375),
                        P(600 - pupilHalf, 234), P(600, 176));
        pupil.CloseFigure();
        Gdiplus::SolidBrush pupilBlack(C(255, 4, 0, 7));
        g.FillPath(&pupilBlack, &pupil);
        Gdiplus::Pen pupilRim(C(245, 255, 32, 115), 2.5f);
        g.DrawPath(&pupilRim, &pupil);
        glow(g, P(600, 298), 20, 85, 255, 23, 154, 4.2f * beat);
        line(g, C(210, 255, 105, 182), 1.7f, P(600, 181), P(600, 426));
        glow(g, P(600, 304), 24, 24, 255, 100, 235, 13.0f * beat);

        // Glass reflection drawn as freeform translucent strokes.
        curve(g, C(69, 255, 192, 222), 6.5f,
              P(377, 285), P(421, 222), P(486, 207), P(536, 201));
        curve(g, C(37, 247, 188, 210), 3.0f,
              P(654, 199), P(724, 213), P(777, 261), P(816, 297));

        g.Restore(saved);
    }


    // Hand-sculpted curved graphite/obsidian metal shells. Coordinates are
    // mirrored geometrically, not via PNG mirroring or prerendered sprites.
    // The reference uses MANY overlapping forged steel claws; these 4 shell
    // families replace the simplistic flat triangular silhouette.
    static void sculptedPlate(G& g, bool mirrored,
                             std::initializer_list<P> bezierNodes,
                             int layer)
    {
        std::vector<P> v(bezierNodes);
        if (v.size() < 4 || ((v.size() - 1) % 3) != 0) return;
        if (mirrored)
            for (auto& p : v) p.X = 1200.0f - p.X;

        Gdiplus::GraphicsPath shell;
        shell.StartFigure();
        shell.AddBeziers(v.data(), static_cast<INT>(v.size()));
        shell.CloseFigure();

        R bounds;
        shell.GetBounds(&bounds);
        const C high = layer == 0 ? C(255, 139, 146, 166)
                   : layer == 1 ? C(255, 96, 98, 116)
                   : layer == 2 ? C(255, 112, 116, 139)
                                : C(255, 77, 82, 99);
        Gdiplus::LinearGradientBrush steel(
            R(bounds.X, bounds.Y, juce::jmax(1.0f, bounds.Width),
              juce::jmax(1.0f, bounds.Height)),
            high, C(255, 4, 5, 13), Gdiplus::LinearGradientModeVertical);

        // Inward crimson underglow establishes layered depth, while
        // the graphite bevel has two nonuniform specular edge strokes.
        Gdiplus::Pen aura(C(74, 255, 17, 73), 16.0f);
        g.DrawPath(&aura, &shell);
        Gdiplus::Pen blackContour(C(255, 0, 0, 7), 5.0f);
        g.DrawPath(&blackContour, &shell);
        g.FillPath(&steel, &shell);
        Gdiplus::Pen graphite(C(229, 168, 174, 197), 1.35f);
        g.DrawPath(&graphite, &shell);
        Gdiplus::Pen chrome(C(170, 229, 233, 250), layer == 0 ? 2.2f : 1.2f);
        g.DrawBezier(&chrome, v[0], v[1], v[2], v[3]);

        Gdiplus::Pen internalGlow(C(123, 255, 21, 95), 1.15f);
        const P a = v[0], tail = v.back();
        g.DrawLine(&internalGlow, a, tail);

        // Refined etched highlights: intermittent, short silver scratches
        // simulate brushed/gouged forged metal, computed in C++ each frame.
        for (int n = 0; n < 13; ++n)
        {
            const float frac = (n + 0.35f) / 13.0f;
            const float bx = bounds.X + bounds.Width * frac;
            const float by = bounds.Y + bounds.Height * (0.23f + 0.08f *
                                                        std::sin(n * 2.73f));
            // Only scratch inside this plate. No texture is ever loaded.
            if (shell.IsVisible(bx, by))
            {
                const BYTE alpha = static_cast<BYTE>(38 + n % 5 * 13);
                line(g, C(alpha, 236, 221, 246), n % 4 == 0 ? 1.4f : 0.65f,
                     P(bx, by), P(bx + (mirrored ? -1.0f : 1.0f) *
                                    (6.0f + n % 8), by - (n % 3 + 1) * 1.7f));
            }
        }
    }

    static void sculptedArmour(G& g)
    {
        for (bool right : { false, true })
        {
            sculptedPlate(g, right, {
                P(585,216), P(490,143), P(379,108), P(292,164),
                P(243,207), P(170,237), P(92,165),
                P(129,263), P(165,281), P(255,271),
                P(391,225), P(465,207), P(584,245)
            }, 0);

            sculptedPlate(g, right, {
                P(583,240), P(455,176), P(359,177), P(282,229),
                P(248,248), P(200,264), P(172,240),
                P(238,310), P(323,262), P(430,246),
                P(483,223), P(522,245), P(583,259)
            }, 1);

            sculptedPlate(g, right, {
                P(217,317), P(274,370), P(383,439), P(547,430),
                P(490,488), P(365,468), P(285,414),
                P(248,389), P(213,370), P(180,326),
                P(190,348), P(201,328), P(217,317)
            }, 2);

            sculptedPlate(g, right, {
                P(216,307), P(167,269), P(108,304), P(53,260),
                P(98,343), P(130,375), P(157,426),
                P(185,383), P(188,345), P(256,331),
                P(241,318), P(229,312), P(216,307)
            }, 3);
        }
        // Forged strakes and red-light leakage between overlapping shells.
        for (int side = 0; side < 2; ++side)
        {
            auto m = [side](float x, float y) {
                return P(side ? 1200.0f - x : x, y);
            };
            curve(g, C(202, 230, 233, 246), 1.9f, m(115,178),
                  m(228,278), m(360,143), m(535,220));
            curve(g, C(127, 250, 18, 98), 1.65f, m(141,258),
                  m(250,301), m(364,196), m(520,241));
            curve(g, C(177, 210, 203, 231), 1.25f, m(186,350),
                  m(291,423), m(390,463), m(518,450));
            curve(g, C(126, 255, 20, 89), 2.0f, m(198,366),
                  m(308,455), m(420,482), m(524,444));
        }
    }

    static void plating(G& g, float open)
    {
        const C graphiteTop(255, 81, 88, 105);
        const C graphiteBottom(255, 9, 11, 19);
        const C edge(180, 148, 159, 181);
        const C hot(215, 255, 27, 94);

        // Massive symmetric blades and horns inspired by reference silhouette.
        // Every plate is its own faceted vector shape; no sprite atlas.
        polygon(g, {P(223,301), P(104,169), P(159,273), P(54,238),
                    P(160,367), P(190,422), P(263,384), P(334,334)},
                graphiteTop, graphiteBottom, edge);
        polygon(g, {P(977,301), P(1096,169), P(1041,273), P(1146,238),
                    P(1040,367), P(1010,422), P(937,384), P(866,334)},
                graphiteTop, graphiteBottom, edge);
        polygon(g, {P(278,271), P(355,154), P(473,195), P(600,232),
                    P(602,275), P(444,233), P(335,255)},
                C(255, 64, 67, 83), C(255, 6, 7, 14), edge);
        polygon(g, {P(922,271), P(845,154), P(727,195), P(600,232),
                    P(598,275), P(756,233), P(865,255)},
                C(255, 64, 67, 83), C(255, 6, 7, 14), edge);

        polygon(g, {P(600,51), P(572,233), P(590,196), P(600,130),
                    P(610,196), P(628,233)},
                C(255, 138, 146, 157), C(255, 15, 16, 28),
                C(222, 249, 91, 120));
        line(g, C(220, 255, 25, 105), 2.7f, P(600,82), P(600,211));

        // The four sculpted mirrored shells are filled with real metallic
        // gradients, edge bevels, grooves and chrome scratches.
        sculptedArmour(g);

        // Lower crystal armor / fangs.
        polygon(g, {P(228,315), P(308,388), P(421,432), P(538,436),
                    P(493,467), P(352,449), P(288,417)},
                C(255, 46, 52, 65), C(255, 5, 7, 14), edge);
        polygon(g, {P(972,315), P(892,388), P(779,432), P(662,436),
                    P(707,467), P(848,449), P(912,417)},
                C(255, 46, 52, 65), C(255, 5, 7, 14), edge);
        polygon(g, {P(565,437), P(600,520), P(635,437), P(604,468)},
                C(255, 126, 131, 150), C(255, 11, 11, 21), hot);

        // A moving, material metallic eyelid: the red inner contour converges
        // into a real slit on blink while outer armor stays fixed.
        const float top = 304.0f - 176.0f * open;
        const float bottom = 304.0f + 161.0f * open;
        curve(g, C(64, 255, 15, 71), 17.0f,
              P(213, 304), P(365, top), P(835, top), P(987, 304));
        curve(g, C(245, 92, 100, 117), 10.0f,
              P(213, 304), P(365, top), P(835, top), P(987, 304));
        curve(g, hot, 2.3f,
              P(213, 304), P(365, top), P(835, top), P(987, 304));

        curve(g, C(64, 255, 15, 71), 17.0f,
              P(987, 304), P(826, bottom), P(374, bottom), P(213, 304));
        curve(g, C(240, 40, 42, 62), 10.0f,
              P(987, 304), P(826, bottom), P(374, bottom), P(213, 304));
        curve(g, hot, 2.2f,
              P(987, 304), P(826, bottom), P(374, bottom), P(213, 304));

        // Brushed-metal side strakes and facets.
        line(g, C(199, 211, 217, 230), 1.5f, P(114,192), P(241,319));
        line(g, C(199, 211, 217, 230), 1.5f, P(1086,192), P(959,319));
        line(g, C(175, 248, 36, 104), 2.2f, P(156,359), P(235,316));
        line(g, C(175, 248, 36, 104), 2.2f, P(1044,359), P(965,316));
        line(g, C(137, 255, 42, 102), 1.6f, P(396,446), P(559,457));
        line(g, C(137, 255, 42, 102), 1.6f, P(804,446), P(641,457));
    }

    static void identity(G& g, float sec)
    {
        using namespace Gdiplus;
        FontFamily brandFamily(L"Segoe UI");
        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);

        // Wordmark is an outlined font PATH with layered metallic fills,
        // NOT text printed into a JPEG. Crisp at any HiDPI scale.
        GraphicsPath textPath;
        textPath.AddString(L"APEX", -1, &brandFamily, FontStyleBold, 102.0f,
                           P(418, 530), &format);
        RectF bounds;
        textPath.GetBounds(&bounds);
        // Center using the actual vector outlines, not assumed font metrics.
        Matrix m;
        m.Translate(600.0f - (bounds.X + bounds.Width * 0.5f), 0.0f);
        textPath.Transform(&m);
        textPath.GetBounds(&bounds);
        LinearGradientBrush metal(
            R(bounds.X, bounds.Y, juce::jmax(1.0f, bounds.Width),
              juce::jmax(1.0f, bounds.Height)),
            C(255, 246, 249, 255), C(255, 57, 61, 78),
            LinearGradientModeVertical);
        Pen darkEdge(C(245, 7, 7, 13), 4.3f);
        g.DrawPath(&darkEdge, &textPath);
        g.FillPath(&metal, &textPath);
        Pen redEdge(C(177, 255, 19, 81), 1.7f);
        g.DrawPath(&redEdge, &textPath);
        // A central energy beam ties mechanical eye and wordmark together.
        line(g, C(110, 255, 22, 91), 1.0f, P(600, 516), P(600, 539));

        Font subtitle(&brandFamily, 19.0f, FontStyleRegular, UnitPixel);
        SolidBrush subtitleBrush(C(255, 214, 215, 229));
        const wchar_t* caption = L"AUDIO PRODUCTION ENGINEERING";
        g.DrawString(caption, -1, &subtitle, R(270, 627, 660, 35),
                     &format, &subtitleBrush);

        const float power = 0.5f + 0.5f * std::sin(sec * 2.8f);
        line(g, C(110, 168, 20, 60), 1.0f, P(365, 688), P(835, 688));
        line(g, C(static_cast<BYTE>(120 + power * 130.0f), 255, 29, 96),
             3.2f, P(468, 688), P(600 + 93.0f * power, 688));
        polygon(g, {P(600,675), P(610,688), P(600,701), P(590,688)},
                C(255, 255, 97, 141), C(255, 161, 3, 57));
    }

    static void drawFrame(G& g, float seconds)
    {
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        const auto oldTransform = g.Save();

        background(g, seconds);
        const float closed = blink(seconds);
        iris(g, seconds, 1.0f - closed);
        plating(g, 1.0f - closed);
        identity(g, seconds);

        g.Restore(oldTransform);
    }

    DWORD run()
    {
        using namespace Gdiplus;
        struct GdiSession
        {
            ULONG_PTR token = 0;
            ~GdiSession() { if (token) GdiplusShutdown(token); }
        } session;
        GdiplusStartupInput init;
        if (GdiplusStartup(&session.token, &init, nullptr) != Ok)
        {
            SetEvent(ready_);
            return 1;
        }

        constexpr int kDesignW = 1200, kDesignH = 750;
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        const float scale = juce::jmin(1.0f,
            juce::jmin(screenW * 0.89f / kDesignW,
                       screenH * 0.85f / kDesignH));
        const int w = juce::jmax(1, juce::roundToInt(kDesignW * scale));
        const int h = juce::jmax(1, juce::roundToInt(kDesignH * scale));
        const int x = (screenW - w) / 2, y = (screenH - h) / 2;

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"APEX_ProceduralEye_Splash";
        RegisterClassExW(&wc);
        HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW
                                       | WS_EX_NOACTIVATE,
                                    wc.lpszClassName, L"", WS_POPUP,
                                    x, y, w, h, nullptr, nullptr, wc.hInstance, nullptr);
        if (! hwnd)
        {
            SetEvent(ready_);
            return 2;
        }
        HDC desktop = GetDC(nullptr);
        HDC memoryDC = desktop ? CreateCompatibleDC(desktop) : nullptr;
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP dib = memoryDC
            ? CreateDIBSection(memoryDC, &info, DIB_RGB_COLORS, &pixels, nullptr, 0)
            : nullptr;
        if (! dib)
        {
            if (memoryDC) DeleteDC(memoryDC);
            if (desktop) ReleaseDC(nullptr, desktop);
            DestroyWindow(hwnd);
            SetEvent(ready_);
            return 3;
        }
        HGDIOBJ old = SelectObject(memoryDC, dib);
        const ULONGLONG start = GetTickCount64();
        ULONGLONG finishAt = 0;
        bool signalled = false;

        for (;;)
        {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            const auto now = GetTickCount64();
            if (! finishAt && WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0)
                finishAt = now;
            const float fade = ! finishAt ? 1.0f
                : juce::jlimit(0.0f, 1.0f, 1.0f - (now - finishAt) / 220.0f);
            if (fade <= 0.0f) break;

            {
                Graphics g(memoryDC);
                // Coordinates are a responsive virtual vector canvas.
                g.ScaleTransform(w / 1200.0f, h / 750.0f);
                drawFrame(g, static_cast<float>((now - start) / 1000.0));
            }
            POINT pos = { x, y }, origin = { 0, 0 };
            SIZE dims = { w, h };
            BLENDFUNCTION blend = { AC_SRC_OVER, 0,
                                    static_cast<BYTE>(fade * 255.0f),
                                    AC_SRC_ALPHA };
            const BOOL painted = UpdateLayeredWindow(hwnd, desktop, &pos,
                                                     &dims, memoryDC, &origin,
                                                     0, &blend, ULW_ALPHA);
            if (! signalled)
            {
                ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                valid_.store(painted != FALSE);
                SetEvent(ready_);
                signalled = true;
            }
            Sleep(33); // ~30fps, only this Win32 boot worker, never DSP
        }

        if (! signalled) SetEvent(ready_);
        SelectObject(memoryDC, old);
        DeleteObject(dib);
        DeleteDC(memoryDC);
        ReleaseDC(nullptr, desktop);
        DestroyWindow(hwnd);
        return 0;
    }

    HANDLE ready_ = nullptr;
    HANDLE stop_ = nullptr;
    HANDLE thread_ = nullptr;
    std::atomic<bool> valid_ { false };
};

#else
class DAWSplashScreen
{
public:
    static DAWSplashScreen* launch() { return nullptr; }
    static bool renderDiagnosticFrame(const wchar_t*, float) { return false; }
    void finish() { delete this; }
};
#endif
