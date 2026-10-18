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
        // Thin time-shifting smoke ribbons. These are cheap translucent Bezier
        // arcs, NOT a procedural noise bitmap or background photograph.
        for (int i = 0; i < 32; ++i)
        {
            const float j = i * 1.97f;
            const float turn = time * (0.22f + (i % 5) * 0.036f);
            const float side = i % 2 ? -1.0f : 1.0f;
            const float spread = 170.0f + (i % 13) * 18.0f;
            const float bx = 600.0f + side * spread;
            const float by = 260.0f + std::sin(j) * 155.0f + std::sin(turn + j) * 15.0f;
            curve(g,
                  i % 3 == 0 ? C(20, 212, 24, 95) : C(13, 165, 56, 111),
                  3.0f + i % 7 * 2.3f,
                  P(bx - 65.0f, by + 35.0f),
                  P(bx - 10.0f, by - 70.0f - std::sin(turn) * 23.0f),
                  P(bx + 75.0f, by + 48.0f),
                  P(bx + 170.0f, by - 52.0f));
        }

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

    // SHARED eyelid anatomy: both the aperture CLIP and metallic lip stroke
    // use exactly these four Beziers. The inner brow is pulled downward to a
    // pointed glabella while the outer third is raised -> ANGRY expression.
    // At a full blink both edges converge to one subtle chevron-shaped seam.
    static void appendUpperLid(Gdiplus::GraphicsPath& path, float open)
    {
        const float frownY = 317.0f - 91.0f * open;
        path.AddBezier(P(213,304), P(331,304 - 179*open),
                       P(468,304 - 143*open), P(600,frownY));
        path.AddBezier(P(600,frownY), P(732,304 - 143*open),
                       P(869,304 - 179*open), P(987,304));
    }

    static void appendLowerLid(Gdiplus::GraphicsPath& path, float open)
    {
        const float innerY = 317.0f + 125.0f * open;
        path.AddBezier(P(987,304), P(868,304 + 155*open),
                       P(734,304 + 142*open), P(600,innerY));
        path.AddBezier(P(600,innerY), P(466,304 + 142*open),
                       P(332,304 + 155*open), P(213,304));
    }

    static void makeEyeContour(Gdiplus::GraphicsPath& path, float openness)
    {
        path.Reset();
        path.StartFigure();
        appendUpperLid(path, openness);
        appendLowerLid(path, openness);
        path.CloseFigure();
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
        // Magenta-violet rim and scarlet plasma move at independent phases:
        // the reference is an electrical *eye*, not a circular LED display.
        glow(g, P(726, 283), 158, 125, 151, 10, 236, 12.0f * beat);
        glow(g, P(478, 336), 156, 101, 255, 31, 105, 10.0f * beat);
        glow(g, P(596, 428), 250, 53, 249, 36, 130, 8.0f * beat);

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

        // Fractal-looking arc discharges, authored as moving polylines from
        // deterministic oscillators rather than noise textures or sprites.
        // They are clipped to the physical curved eye aperture.
        for (int i = 0; i < 65; ++i)
        {
            const float angle = i * 2.39996323f;
            const float wobble = sec * (0.11f + (i % 6) * 0.013f);
            P filament[9];
            for (int node = 0; node < 9; ++node)
            {
                const float t = node / 8.0f;
                const float radius = 38.0f + t * (168.0f + 17.0f * (i % 5));
                const float crooked = std::sin(i * 5.37f + node * 2.74f + sec * 1.55f)
                                     * (2.7f + 8.3f * t);
                const float curl = angle + wobble + 0.19f * t;
                filament[node] = P(600.0f + std::cos(curl) * radius
                                            - std::sin(angle) * crooked,
                                   304.0f + std::sin(curl) * radius * 0.77f
                                            + std::cos(angle) * crooked * 0.77f);
            }
            Gdiplus::GraphicsPath electrical;
            electrical.AddLines(filament, 9);
            const BYTE alpha = static_cast<BYTE>(85 + (i % 7) * 20);
            Gdiplus::Pen vapor(C(55, 255, 25, 139), 5.0f);
            Gdiplus::Pen spark(
                (i % 4 == 0) ? C(alpha, 255, 146, 208)
                             : C(alpha, 255, 28, 116),
                (i % 6 == 0) ? 1.8f : 0.85f);
            g.DrawPath(&vapor, &electrical);
            g.DrawPath(&spark, &electrical);

            if (i % 3 == 0)
            {
                const P fork = filament[5];
                const P tip = filament[7];
                line(g, C(119, 255, 105, 186), 1.0f,
                     fork, P(tip.X + std::sin(i * 2.0f + sec) * 13.0f,
                             tip.Y - 8.0f + std::cos(i + sec) * 10.0f));
            }
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

    // APEX V4 angry brow: ONE continuous tapered forged blade per side.
    // Each surface descends monotonically toward the inner glabella.
    // In contrast to the older twin arches there is NO rounded mid-span
    // hump or McDonald's-M silhouette.
    static void drawScowlingBrow(G& g, bool right)
    {
        // Transform PATH vertices, not the Graphics world matrix: our
        // renderer is already HiDPI-scaled and an extra mirrored graphics
        // transform would also flip gradient / stroke calculations.
        const auto mirrored = [right](float x, float y) {
            return P(right ? 1200.0f - x : x, y);
        };
        Gdiplus::Matrix flip(-1.0f, 0.0f, 0.0f, 1.0f, 1200.0f, 0.0f);
        Gdiplus::GraphicsPath blade;
        blade.StartFigure();
        blade.AddBezier(P(91, 136), P(211, 139), P(464, 191), P(585, 237));
        blade.AddLine(P(585, 237), P(570, 257));
        blade.AddBezier(P(570, 257), P(439, 225), P(278, 190), P(172, 210));
        blade.AddBezier(P(172, 210), P(131, 208), P(106, 179), P(91, 136));
        blade.CloseFigure();
        if (right) blade.Transform(&flip);

        R bounds;
        blade.GetBounds(&bounds);
        Gdiplus::LinearGradientBrush gunmetal(
            R(bounds.X, bounds.Y, juce::jmax(1.0f, bounds.Width),
              juce::jmax(1.0f, bounds.Height)),
            C(255, 116, 125, 145), C(255, 12, 15, 28),
            Gdiplus::LinearGradientModeVertical);
        Gdiplus::Pen aura(C(58, 240, 22, 87), 10.0f);
        Gdiplus::Pen silhouette(C(247, 5, 6, 13), 5.4f);
        Gdiplus::Pen bright(C(206, 214, 217, 240), 1.7f);
        g.DrawPath(&aura, &blade);
        g.DrawPath(&silhouette, &blade);
        g.FillPath(&gunmetal, &blade);
        g.DrawPath(&bright, &blade);

        curve(g, C(167, 247, 246, 255), 1.9f,
              mirrored(98, 147), mirrored(238, 145),
              mirrored(467, 197), mirrored(580, 244));
        curve(g, C(195, 247, 27, 94), 1.8f,
              mirrored(147, 204), mirrored(279, 184),
              mirrored(457, 225), mirrored(570, 255));

        // A smaller under-ridge stays outside the actual open iris opening.
        Gdiplus::GraphicsPath underside;
        underside.StartFigure();
        underside.AddBezier(P(165, 219), P(323, 202),
                            P(472, 226), P(562, 265));
        underside.AddBezier(P(562, 265), P(475, 251),
                            P(306, 223), P(165, 240));
        underside.CloseFigure();
        if (right) underside.Transform(&flip);
        R underBounds;
        underside.GetBounds(&underBounds);
        Gdiplus::LinearGradientBrush secondary(
            R(underBounds.X, underBounds.Y,
              juce::jmax(1.0f, underBounds.Width),
              juce::jmax(1.0f, underBounds.Height)),
            C(255, 65, 71, 90),
            C(255, 9, 10, 20), Gdiplus::LinearGradientModeVertical);
        g.FillPath(&secondary, &underside);
        Gdiplus::Pen ridge(C(164, 126, 134, 161), 1.25f);
        g.DrawPath(&ridge, &underside);
    }

    // The full eyebrows have their own render layer. They MUST NOT be drawn
    // inside the eye cutout, nor before the iris/chamber covers that cutout.
    // This same function is called for open, mid-blink and fully shut frames.
    static void drawBrowPair(G& g)
    {
        drawScowlingBrow(g, false);
        drawScowlingBrow(g, true);
    }

    static void sculptedArmour(G& g)
    {
        // Secondary cheek/side armor only. Eyebrow blades are independent and
        // are composited after the ocular layers without any fullEye clip.
        for (bool right : { false, true })
        {
            // Crown and side cheek hardware remain fixed while blinking.
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

        // Chrome crest now sweeps DOWN toward the glabella, not UP
        // in a happy/cartoony arch, with a sharp metallic inward frown.
        for (int side = 0; side < 2; ++side)
        {
            auto m = [side](float x, float y) {
                return P(side ? 1200.0f - x : x, y);
            };
            // The chrome edge belongs to the blade itself above; do not
            // redraw duplicate swooping chrome arches over the eyeball.
            line(g, C(194, 255, 31, 104), 1.7f,
                 m(530,250), m(572,266));
            curve(g, C(177, 210, 203, 231), 1.25f,
                  m(186,350), m(291,423), m(390,463), m(518,450));
            curve(g, C(126, 255, 20, 89), 2.0f,
                  m(198,366), m(308,455), m(420,482), m(524,444));
        }
    }

    static void plating(G& g)
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

        // Lower crystal armor / fangs.
        polygon(g, {P(228,315), P(308,388), P(421,432), P(538,436),
                    P(493,467), P(352,449), P(288,417)},
                C(255, 46, 52, 65), C(255, 5, 7, 14), edge);
        polygon(g, {P(972,315), P(892,388), P(779,432), P(662,436),
                    P(707,467), P(848,449), P(912,417)},
                C(255, 46, 52, 65), C(255, 5, 7, 14), edge);
        polygon(g, {P(565,437), P(600,520), P(635,437), P(604,468)},
                C(255, 126, 131, 150), C(255, 11, 11, 21), hot);

        // Layer deep curved brushed steel ON TOP of the faceted subframe.
        // Distinct mirror-symmetric Bezier blade families with raised bevels.
        sculptedArmour(g);

        // Brushed-metal side strakes and facets.
        line(g, C(199, 211, 217, 230), 1.5f, P(114,192), P(241,319));
        line(g, C(199, 211, 217, 230), 1.5f, P(1086,192), P(959,319));
        line(g, C(175, 248, 36, 104), 2.2f, P(156,359), P(235,316));
        line(g, C(175, 248, 36, 104), 2.2f, P(1044,359), P(965,316));
        line(g, C(137, 255, 42, 102), 1.6f, P(396,446), P(559,457));
        line(g, C(137, 255, 42, 102), 1.6f, P(804,446), P(641,457));
    }

    // During closure we DO NOT paint a big gradient-filled eye oval.
    // Instead only the part hidden by the eyelid receives a thin, fading
    // graphite cover. A complete blink blends back into the black surrounding
    // socket, leaving two precise, inward-notched steel blade edges.
    // Closed-state ONLY: a slender, faceted, shadow-black guillotine visor.
    // Never fills the entire eye contour; the two plates meet in a narrow
    // anisotropic seam and remain visible against the crimson atmosphere.
    // The outer metal brow is fixed while the shutter becomes this slit.
    static void drawClosedVisor(G& g, float openness)
    {
        const float closed = 1.0f - ease(openness / 0.18f);
        if (closed <= 0.001f)
            return;
        const BYTE alpha = static_cast<BYTE>(250.0f * closed);

        Gdiplus::GraphicsPath top;
        top.StartFigure();
        top.AddBezier(P(210,301), P(344,275), P(472,293), P(600,313));
        top.AddBezier(P(600,313), P(728,293), P(856,275), P(990,301));
        top.AddBezier(P(990,301), P(831,313), P(716,328), P(600,326));
        top.AddBezier(P(600,326), P(484,328), P(369,313), P(210,301));
        top.CloseFigure();
        Gdiplus::LinearGradientBrush darkSteel(
            R(210,272,780,58), C(alpha, 50, 57, 75),
            C(alpha, 3, 5, 13), Gdiplus::LinearGradientModeVertical);
        g.FillPath(&darkSteel, &top);

        Gdiplus::GraphicsPath bottom;
        bottom.StartFigure();
        bottom.AddBezier(P(210,305), P(363,327), P(490,332), P(600,326));
        bottom.AddBezier(P(600,326), P(710,332), P(837,327), P(990,305));
        bottom.AddBezier(P(990,305), P(852,344), P(731,358), P(600,354));
        bottom.AddBezier(P(600,354), P(469,358), P(348,344), P(210,305));
        bottom.CloseFigure();
        Gdiplus::LinearGradientBrush bevel(
            R(210,305,780,53), C(alpha, 4, 5, 14),
            C(alpha, 35, 39, 55), Gdiplus::LinearGradientModeVertical);
        g.FillPath(&bevel, &bottom);

        const BYTE edgeAlpha = static_cast<BYTE>(180.0f * closed);
        Gdiplus::Pen gleam(C(edgeAlpha, 140, 154, 181), 1.65f);
        Gdiplus::Pen ink(C(static_cast<BYTE>(230.0f * closed), 2, 2, 8), 5.0f);
        g.DrawPath(&ink, &top);
        g.DrawPath(&gleam, &top);
        g.DrawPath(&gleam, &bottom);
        // Magenta laser incision is thin and localized, NOT a red slab.
        curve(g, C(static_cast<BYTE>(116.0f * closed), 246, 26, 98), 1.1f,
              P(251,310), P(441,324), P(525,334), P(600,326));
        curve(g, C(static_cast<BYTE>(116.0f * closed), 246, 26, 98), 1.1f,
              P(600,326), P(675,334), P(759,324), P(949,310));
    }

    static void drawShutters(G& g, float openness)
    {
        openness = juce::jlimit(0.0f, 1.0f, openness);
        const float strength = ease(juce::jmin(1.0f, openness * 3.0f));
        if (strength < 0.003f)
            return;

        Gdiplus::GraphicsPath fullEye, slit;
        makeEyeContour(fullEye, 1.0f);
        makeEyeContour(slit, openness);

        Gdiplus::Region aperture(&slit);

        // Separate top and bottom material using a subtle two-plane lighting
        // scheme. At full closure opacity tends to ZERO: no cheap steel oval.
        Gdiplus::Region upper(&fullEye);
        upper.Exclude(&aperture);
        upper.Intersect(R(205.0f, 110.0f, 790.0f, 208.0f));
        Gdiplus::Region lower(&fullEye);
        lower.Exclude(&aperture);
        lower.Intersect(R(205.0f, 318.0f, 790.0f, 166.0f));

        const auto a = static_cast<BYTE>(210.0f * strength);
        Gdiplus::LinearGradientBrush topSteel(
            R(205.0f, 133.0f, 790.0f, 180.0f),
            C(a, 62, 68, 87), C(a, 8, 9, 18),
            Gdiplus::LinearGradientModeVertical);
        Gdiplus::LinearGradientBrush bottomSteel(
            R(205.0f, 318.0f, 790.0f, 170.0f),
            C(a, 8, 9, 18), C(a, 43, 47, 63),
            Gdiplus::LinearGradientModeVertical);
        g.FillRegion(&topSteel, &upper);
        g.FillRegion(&bottomSteel, &lower);
    }

    static void drawLidRims(G& g, float openness)
    {
        openness = juce::jlimit(0.0f, 1.0f, openness);
        Gdiplus::GraphicsPath upper, lower;
        appendUpperLid(upper, openness);
        appendLowerLid(lower, openness);

        // At closed, both lids converge to ONE narrow angled seam.
        // Do not double-render bright red horizontal lines.
        if (openness < 0.035f)
        {
            Gdiplus::Pen under(C(235, 5, 6, 14), 7.0f);
            Gdiplus::Pen bevel(C(222, 72, 77, 99), 2.9f);
            Gdiplus::Pen incision(C(159, 221, 17, 77), 1.1f);
            g.DrawPath(&under, &upper);
            g.DrawPath(&bevel, &upper);
            g.DrawPath(&incision, &upper);
            // Angled inward scowl creases add tension to the closed posture.
            line(g, C(119, 244, 24, 76), 1.8f,
                 P(512,307), P(594,317));
            line(g, C(119, 244, 24, 76), 1.8f,
                 P(688,307), P(606,317));
            return;
        }

        const BYTE seamAlpha = static_cast<BYTE>(62.0f + 177.0f * openness);
        Gdiplus::Pen shadow(C(240, 7, 8, 17), 8.0f);
        Gdiplus::Pen chrome(C(220, 69, 76, 93), 4.0f);
        Gdiplus::Pen crimson(C(seamAlpha, 255, 30, 97), 1.75f);
        g.DrawPath(&shadow, &upper);
        g.DrawPath(&shadow, &lower);
        g.DrawPath(&chrome, &upper);
        g.DrawPath(&chrome, &lower);
        g.DrawPath(&crimson, &upper);
        g.DrawPath(&crimson, &lower);
    }

    // Slender, bespoke APEX letterforms designed as actual C++ glyph
    // strokes (not an ordinary heavy Segoe UI font and never a bitmap).
    static void identity(G& g, float sec)
    {
        using namespace Gdiplus;
        GraphicsPath brand;
        brand.StartFigure();
        brand.AddLine(P(435, 615), P(474, 559));  // A left diagonal
        brand.AddLine(P(474, 559), P(515, 615));  // A right diagonal
        brand.StartFigure();
        brand.AddLine(P(451, 599), P(498, 599));  // fine A crosspiece

        brand.StartFigure();
        brand.AddLine(P(540, 618), P(540, 561));  // P stem
        brand.AddLine(P(540, 561), P(576, 561));
        brand.AddBezier(P(576, 561), P(613, 561),
                        P(609, 591), P(579, 591));
        brand.AddLine(P(579, 591), P(541, 591));

        // E: horizontally cut bars like the original cybernetic wordmark.
        brand.StartFigure();
        brand.AddLine(P(624, 563), P(686, 563));
        brand.StartFigure();
        brand.AddLine(P(624, 590), P(677, 590));
        brand.StartFigure();
        brand.AddLine(P(624, 617), P(686, 617));

        brand.StartFigure();
        brand.AddLine(P(708, 560), P(785, 618));  // X top-left to bottom-right
        brand.StartFigure();
        brand.AddLine(P(785, 560), P(708, 618));  // X top-right to bottom-left

        // Five layered light passes create dark steel, beveled rim and
        // thin silver core; metallic gradients are rendered by GDI+ paths.
        Pen outerGlow(C(48, 255, 10, 74), 20.0f);
        Pen shadow(C(255, 1, 1, 9), 12.0f);
        Pen redBevel(C(222, 230, 15, 67), 9.0f);
        LinearGradientBrush silver(R(400, 546, 410, 89),
                                    C(255, 230, 234, 252),
                                    C(255, 46, 48, 65),
                                    LinearGradientModeVertical);
        Pen alloy(&silver, 6.1f);
        Pen specular(C(164, 252, 243, 255), 1.15f);
        outerGlow.SetLineJoin(LineJoinRound);
        shadow.SetLineJoin(LineJoinRound);
        redBevel.SetLineJoin(LineJoinRound);
        alloy.SetLineJoin(LineJoinRound);
        specular.SetLineJoin(LineJoinRound);
        g.DrawPath(&outerGlow, &brand);
        g.DrawPath(&shadow, &brand);
        g.DrawPath(&redBevel, &brand);
        g.DrawPath(&alloy, &brand);
        g.DrawPath(&specular, &brand);

        // Red laser seam joins the Eye's lower dagger to its brand mark.
        line(g, C(172, 255, 33, 117), 1.35f, P(600, 506), P(600, 545));
        glow(g, P(600, 544), 15, 15, 255, 44, 112, 6.1f);

        // Explicit letter tracking gives the thin, widely spaced lettering
        // shown in the reference, without baking any glyphs into a PNG.
        FontFamily family(L"Segoe UI");
        Font captionFont(&family, 17.0f, FontStyleRegular, UnitPixel);
        SolidBrush captionColor(C(239, 208, 208, 226));
        StringFormat centered;
        centered.SetAlignment(StringAlignmentCenter);
        centered.SetLineAlignment(StringAlignmentCenter);
        const wchar_t* title = L"AUDIO PRODUCTION ENGINEERING";
        constexpr int kLetterCount = 28;
        const float startX = 377.0f, spacing = 16.52f;
        for (int index = 0; index < kLetterCount; ++index)
        {
            wchar_t character[2] = { title[index], 0 };
            if (character[0] != L' ')
                g.DrawString(character, 1, &captionFont,
                             R(startX + index * spacing, 637, 28, 30),
                             &centered, &captionColor);
        }

        const float power = 0.5f + 0.5f * std::sin(sec * 2.8f);
        line(g, C(110, 168, 20, 60), 1.0f, P(365, 690), P(835, 690));
        line(g, C(static_cast<BYTE>(120 + power * 130.0f), 255, 29, 96),
             3.2f, P(467, 690), P(598.0f + 94.0f * power, 690));
        polygon(g, {P(600,679), P(608,690), P(600,701), P(592,690)},
                C(255, 255, 97, 141), C(255, 161, 3, 57));
    }

    static void drawFrame(G& g, float seconds)
    {
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        const auto oldTransform = g.Save();

        background(g, seconds);
        const float openness = 1.0f - blink(seconds);

        // Z ORDER CONTRACT:
        // 1. ONLY secondary metal/cheeks are clipped away from the eye.
        // 2. Eye socket, shutters and iris use the true animated aperture.
        // 3. Closed metal visor and eyelid seam follow the eye.
        // 4. Full fixed angry brow pair is drawn LAST, UNCLIPPED, so the
        //    same complete forged eyebrow is visible open AND closed.
        Gdiplus::GraphicsPath fullEye;
        makeEyeContour(fullEye, 1.0f);
        if (openness > 0.035f)
        {
            // During open and transitional frames armour is excluded from
            // the real ocular aperture; at closed the armour is allowed to
            // cover the (otherwise visible) entire hole.
            const auto guard = g.Save();
            g.SetClip(&fullEye, Gdiplus::CombineModeExclude);
            plating(g);
            g.Restore(guard);
        }
        else
        {
            // Full eyelid closure: NO static red oval-shaped socket is left.
            // Reveal the actual forged armor over the empty iris region.
            plating(g);
        }
        // Blend the deep iris socket away BEFORE total closure; otherwise
        // the static complete-eye shape remains as a huge black oval.
        const float material = ease(juce::jmin(1.0f, openness * 3.0f));
        Gdiplus::SolidBrush chamber(C(static_cast<BYTE>(242.0f * material),
                                      3, 2, 10));
        if (material > 0.003f)
            g.FillPath(&chamber, &fullEye);
        drawShutters(g, openness);
        iris(g, seconds, openness);
        // Draw the high-contrast black steel slit only while shut, above
        // the tiny final seam (not over the opened iris).
        drawClosedVisor(g, openness);
        drawLidRims(g, openness);
        // Always draw the complete eyebrow AFTER iris and lid masks.
        // No Graphics::SetClip active here: never trim the brow in OPEN.
        drawBrowPair(g);
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
